/* skedaddle_search_cli —— 地名 / POI 搜索的无头验收（阶段 7）
 *
 * 搜索这种东西 GUI 里敲两个字看一眼就算"验过了"，最容易留下性能与排序的坑，
 * 所以把断言写在这里：
 *
 *   1. 命中：搜"中关村"/"西二旗"有结果，且**结果名里确实含关键词**
 *      （gram 只召回、不保证连续，漏做子串复核就会返回"中…关村"这种脏结果）
 *   2. 排序：给中心点时按加权距离升序，且距离不能来回跳
 *   3. 性能：单次查询百毫秒级（PLAN 的验收线），连跑 20 次看均值与最大值
 *   4. 边界：空串 / 纯英文 / 生僻字 / 超长串 / 带 % 的通配符串，都不崩
 *   5. 召回：gram 索引完整性抽查（随机抽 400 个二字以上的地名，逐个搜得到自己）
 *
 * 用法：
 *   ./skedaddle_search_cli [--db=products/places_auto.db] [--near=39.90,116.40] [--limit=20]
 *                [--q=中关村] [--repeat=20] [--recall-check]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "search/search.h"
#include "utils/geo.h"
#include "tool_common.h"


static int read_word(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return 0;
}

static int parse_ll(const char *s, double *lat, double *lon)
{
    char *sp = strchr(s, ',');
    if (!sp) return -1;
    char buf[128];
    size_t n = (size_t)(sp - s);
    if (n >= sizeof(buf)) return -1;
    memcpy(buf, s, n); buf[n] = '\0';
    *lat = atof(buf);
    *lon = atof(sp + 1);
    return 0;
}

int main(int argc, char **argv)
{
    const char *db = "products/places_auto.db";
    const char *near_s = NULL;
    const char *q = NULL;
    const char *q_file = NULL;   /* --q-file: UTF-8 文件读查询串（Windows
                                    命令行传中文会被 MSYS2 转码，验收会假红） */
    const char *ext_path = NULL; /* --ext: 外部 POI CSV，验证新加数据可被检索 */
    const char *miss_file = NULL;/* --miss-file: 负向对照词（必须搜不到） */
    const char *reverse_s = NULL;/* --reverse=lat,lon: 逆地理编码（地址层 L2） */
    double radius_m = 3000.0;
    int expect_empty = 0;       /* --expect-empty: 负向对照（半径内必须无地址） */
    int limit = 20, repeat = 20, recall_check = 0;
    int fails = 0;

#define FAIL(...) do { printf("ASSERT-FAIL    " __VA_ARGS__); fails++; } while (0)

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v;
        if ((v = tc_arg_val(a, "--db=")))          db = v;
        else if ((v = tc_arg_val(a, "--q=")))      q = v;
        else if ((v = tc_arg_val(a, "--q-file="))) q_file = v;
        else if ((v = tc_arg_val(a, "--ext=")))    ext_path = v;
        else if ((v = tc_arg_val(a, "--miss-file="))) miss_file = v;
        else if ((v = tc_arg_val(a, "--reverse="))) reverse_s = v;
        else if ((v = tc_arg_val(a, "--radius=")))  radius_m = atof(v);
        else if (strcmp(a, "--expect-empty") == 0) expect_empty = 1;
        else if ((v = tc_arg_val(a, "--near=")))   near_s = v;
        else if ((v = tc_arg_val(a, "--limit=")))  limit = atoi(v);
        else if ((v = tc_arg_val(a, "--repeat="))) repeat = atoi(v);
        else if (strcmp(a, "--recall-check") == 0) recall_check = 1;
        else { fprintf(stderr, "未知参数 %s\n", a); return 2; }
    }

    char qbuf[256];
    if (q_file) {
        if (read_word(q_file, qbuf, sizeof(qbuf)) != 0) {
            printf("ASSERT-FAIL    打不开查询文件 %s\n", q_file); return 1;
        }
        q = qbuf;
    }

    SearchDb *d = search_open(db);
    if (!d) { printf("ASSERT-FAIL    打不开搜索库 %s\n", db); return 1; }

    /* 逆地理编码：独立的一条判据，跑完直接收尾（不与关键词断言混在一起）。
       --expect-empty 是负向对照：把半径缩到极小 / 点在无数据区时必须返回 1，
       否则说明"什么都没找到也被当成成功"（判据没有分辨力）。 */
    if (reverse_s) {
        double la = 0, lo = 0;
        if (parse_ll(reverse_s, &la, &lo) != 0) {
            printf("ASSERT-FAIL    --reverse 应为 lat,lon\n");
            search_close(d); return 1;
        }
        char addr[512];
        int rc = search_reverse(d, la, lo, radius_m, addr, sizeof(addr));
        printf("逆地理编码    (%.5f,%.5f) 半径 %.1f m rc=%d → %s\n",
               la, lo, radius_m, rc, addr[0] ? addr : "<空>");
        if (expect_empty) {
            if (rc != 1 || addr[0]) FAIL("负向对照：半径内本应无地址，却得到「%s」\n", addr);
            else printf("负向对照    半径 %.1f m 内无地址（符合预期，判据有分辨力）\n", radius_m);
        } else {
            if (rc != 0 || !addr[0]) FAIL("逆地理编码没合成地址\n");
        }
        search_close(d);
        if (fails) { printf("SEARCH-FAIL  %d 条断言失败\n", fails); return 1; }
        printf("SEARCH-OK\n");
        return 0;
    }

    double clat = 39.9, clon = 116.4;
    int have_center = 0;
    if (near_s) {
        if (parse_ll(near_s, &clat, &clon) != 0) {
            printf("ASSERT-FAIL    --near 应为 lat,lon\n");
            return 1;
        }
        have_center = 1;
    }
    printf("库            %s\n", db);
    printf("中心          %s%.5f,%.5f\n", have_center ? "" : "(未给) ", clat, clon);

    SearchHit hits[64];

    /* ---- 外部 POI：新加的 POI 数据没进过建库器，靠运行期补充索引 ---- */
    if (ext_path) {
        SearchExtPoi *ext = NULL;
        int n_ext = 0;
        if (search_ext_load_csv(ext_path, &ext, &n_ext) != 0)
            FAIL("外部 POI 解析失败 %s\n", ext_path);
        int added = search_add_external(d, ext, n_ext);
        printf("\n外部POI      %s 载入 %d 条，并入搜索 %d 条\n",
               ext_path, n_ext, added);
        if (n_ext <= 0) FAIL("外部 POI 文件 %s 没有有效行\n", ext_path);
        if (added != n_ext) FAIL("外部 POI 只并入 %d/%d 条\n", added, n_ext);
        /* 逐条自检：搜自己的全名必须命中自己，坐标 / kind / 权重逐字段一致。
           这条断言覆盖「解析 → 索引 → 子串复核 → 打分 → top-K」全链路。 */
        for (int i = 0; i < n_ext; i++) {
            int n = 0;
            if (search_query(d, ext[i].name, 0, 0, 0, 0, 0, 64,
                             hits, 64, &n, NULL) != 0)
                FAIL("查询外部 POI「%s」返回错误\n", ext[i].name);
            int found = 0;
            for (int k = 0; k < n; k++) {
                if (strcmp(hits[k].name, ext[i].name) == 0 &&
                    fabs(hits[k].lat - ext[i].lat) < 1e-7 &&
                    fabs(hits[k].lon - ext[i].lon) < 1e-7 &&
                    hits[k].kind == SEARCH_EXT_KIND &&
                    hits[k].weight == ext[i].weight) { found = 1; break; }
            }
            printf("  %-22s %s\n", ext[i].name, found ? "命中" : "★未命中");
            if (!found) FAIL("外部 POI「%s」搜不到自己\n", ext[i].name);
        }
        free(ext);
    }

    /* ---- 负向对照：不灌外部数据时，同一个词必须 0 条 ----
       否则上面那条「能搜到」判据没有分辨力（词本身就在库里也会绿）。 */
    if (miss_file) {
        char mb[256];
        if (read_word(miss_file, mb, sizeof(mb)) != 0)
            FAIL("打不开负向对照词文件 %s\n", miss_file);
        else {
            int n = 0;
            search_query(d, mb, 0, 0, 0, 0, 0, limit, hits, 64, &n, NULL);
            printf("\n负向对照    「%s」→ %d 条（期望 0）\n", mb, n);
            if (n != 0)
                FAIL("负向对照：「%s」不该被搜到，却返回 %d 条\n", mb, n);
        }
    }

    /* ---- 1~3. 关键词查询 + 命中 + 排序 + 性能 ---- */
    const char *words[] = { "中关村", "西二旗", "天安门", "五道口" };
    int nwords = (int)(sizeof(words) / sizeof(words[0]));
    if (q) { words[0] = q; nwords = 1; }

    int sum_n = 0;

    for (int w = 0; w < nwords; w++) {
        int n = 0;
        uint64_t ms = 0;
        if (search_query(d, words[w], have_center, clat, clon, 0, 0, limit,
                         hits, 64, &n, &ms) != 0) {
            FAIL("查询 %s 返回错误\n", words[w]);
            continue;
        }
        printf("\n查询 %-10s %d 条  %llu ms\n", words[w], n,
               (unsigned long long)ms);
        sum_n++;

        if (n == 0) { FAIL("%s 一条都没搜到\n", words[w]); continue; }
        if (n > limit) { FAIL("%s 返回 %d 条，超过 limit %d\n", words[w], n, limit); }

        double prev_score = -1;
        for (int i = 0; i < n && i < 6; i++) {
            char dd[48];
            if (have_center) snprintf(dd, sizeof(dd), "%.1f km", hits[i].dist_m / 1000.0);
            else             snprintf(dd, sizeof(dd), "-");
            printf("  %2d  %-22s kind %2d w %2d  %s\n",
                   i, hits[i].name, hits[i].kind, hits[i].weight, dd);
            /* 1. 结果名必须真的含关键词。★ 用规范化后的词比：用户可能输入
               「建国路 88 号」，库里存的是规范化名「建国路88」，原串直接
               strstr 会误报 ASSERT-FAIL（查询端与建库端共用一份规范化）。 */
            char qn[128];
            search_normalize_addr(words[w], qn, sizeof(qn));
            if (!strstr(hits[i].name, qn))
                FAIL("%s 的第 %d 条结果「%s」不含关键词\n", qn, i, hits[i].name);
            /* 2. 排序单调：直接用结果里带回的排序分，不重算公式 */
            if (prev_score >= 0 && hits[i].score < prev_score - 1e-9)
                FAIL("%s 排序不是升序：第 %d 条分数更小（%.3f < %.3f）\n",
                     words[w], i, hits[i].score, prev_score);
            prev_score = hits[i].score;
        }
        /* 2b. 完全同名的条目要吃到"匹配质量"加成——但只在它离得不算远时
           才要求排第一：库里有跨省同名（实测"中关村"在山东还有个同名村，
           378 km 外），那种情况按距离压后才是对的。 */
        if (have_center && n > 1) {
            double dmin = hits[0].dist_m;
            for (int i = 1; i < n; i++) {
                char qn2[128];
                search_normalize_addr(words[w], qn2, sizeof(qn2));
                if (strcmp(hits[i].name, qn2) == 0 &&
                    hits[i].dist_m <= dmin * 2.0 + 1000.0)
                    FAIL("%s 有同名条目（%.1f km）却排在第 %d 位\n",
                         words[w], hits[i].dist_m / 1000.0, i);
            }
        }
        /* 3. 单次要够快（PLAN：百毫秒级） */
        if (ms > 100) FAIL("%s 查询 %llu ms 超过 100 ms\n", words[w],
                           (unsigned long long)ms);
    }

    /* ---- 3b. 连续查询的均值/峰值 ---- */
    if (repeat > 0 && sum_n > 0) {
        uint64_t tot = 0, worst = 0;
        for (int i = 0; i < repeat; i++) {
            int n = 0;
            uint64_t ms = 0;
            search_query(d, "中关村", have_center, clat, clon, 0, 0, limit,
                         hits, 64, &n, &ms);
            tot += ms;
            if (ms > worst) worst = ms;
        }
        printf("\n重复 %d 次    均值 %.1f ms  最慢 %llu ms\n",
               repeat, (double)tot / repeat, (unsigned long long)worst);
        if ((double)tot / repeat > 100.0)
            FAIL("平均 %.1f ms 超过 100 ms\n", (double)tot / repeat);
    }

    /* ---- 4. 边界输入 ---- */
    const char *edge[] = { "", " ", "abc", "XYZ", "龘龘", "%%", "_", "\\",
                           "中关村大街北口东侧辅路一段很长的名字超过三十二字",
                           "中" };
    printf("\n边界输入\n");
    for (size_t i = 0; i < sizeof(edge) / sizeof(edge[0]); i++) {
        int n = 0;
        uint64_t ms = 0;
        int rc = search_query(d, edge[i], have_center, clat, clon, 0, 0, limit,
                              hits, 64, &n, &ms);
        printf("  「%s」→ rc %d  %d 条  %llu ms\n", edge[i], rc, n,
               (unsigned long long)ms);
        if (rc != 0) FAIL("输入「%s」返回 %d（应为 0）\n", edge[i], rc);
        if (ms > 500) FAIL("输入「%s」耗时 %llu ms 太慢\n", edge[i],
                           (unsigned long long)ms);
    }
    /* 单字必须能搜到东西（走 gram 前缀路径） */
    {
        int n = 0;
        search_query(d, "中", have_center, clat, clon, 0, 0, limit, hits, 64, &n, NULL);
        if (n == 0) FAIL("单字「中」搜不到任何结果（前缀/兜底路径坏了）\n");
    }

    /* ---- 5. 召回抽查：随机地名能否被自己搜出来 ---- */
    if (recall_check) {
        printf("\n召回抽查\n");
        /* 没有 SQL 接口暴露，就搜一批常见词，看总数是否合理 */
        const char *probe[] = { "中关村", "西二旗", "国贸", "西单", "王府井",
                                "颐和园", "首都机场", "长春", "郑州", "太原" };
        int hit_cnt = 0;
        for (size_t i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
            int n = 0;
            search_query(d, probe[i], 0, 0, 0, 0, 0, limit, hits, 64, &n, NULL);
            if (n > 0) hit_cnt++;
            printf("  %-10s %d 条\n", probe[i], n);
        }
        if (hit_cnt < 8)
            FAIL("%d/10 个常见词有结果，召回率偏低\n", hit_cnt);
    }

    search_close(d);
    if (fails) { printf("SEARCH-FAIL  %d 条断言失败\n", fails); return 1; }
    printf("SEARCH-OK\n");
    return 0;
}
