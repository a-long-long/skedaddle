#include "map/autobuild.h"
#include "utils/plat.h"

#include <sqlite3.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 产物状态。前四种都需要重建。 */
enum { PV_OK = 0, PV_MISSING, PV_INCOMPLETE, PV_CORRUPT, PV_STALE };

#define PV_MISSING_TXT     "产物不存在"
#define PV_INCOMPLETE_TXT  "产物不完整"

/* 与 scripts/sync_maps.sh 的 LM_NLM / --cell / LM_WANT_VER 对齐。
 * ★ 改那边必须同步改这里；scripts/check_autobuild_sidecars.py 会核对，
 *   否则 autobuild 建出来的表与脚本建的不是同一套配置，w 默认值会漂移。 */
#define AB_LM_NLM    30
#define AB_LM_CELL   3000
#define AB_LM_VER    2

/* 旁路产物文件名。注意 C 块注释里不要写通配形式，避免提前闭合注释。 */
#define AB_PLACES_DB   "places_auto.db"
#define AB_LM_BIN      "lm.bin"
#define AB_CHUNKS_JSON "chunks.json"
#define AB_CHUNKS_GZ   "chunks.json.gz"

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static double now_ms(void)
{
    return (double)sk_now_ms();       /* 平台层：clock_gettime / QPC */
}

/* 省名白名单：会被拼进 --out= 路径，必须挡住 '/' 与 ./.. */
static int name_ok(const char *n)
{
    if (!n || !*n) return 0;
    if (strchr(n, '/')) return 0;
    if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) return 0;
    for (const char *p = n; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}

/* "beijing.osm.pbf" / "beijing.pbf" → "beijing" */
static int prov_name(const char *file, char *out, size_t cap)
{
    static const char *sfx[] = { ".osm.pbf", ".pbf" };
    size_t n = strlen(file);
    for (size_t k = 0; k < sizeof(sfx) / sizeof(sfx[0]); k++) {
        size_t sl = strlen(sfx[k]);
        if (n > sl && strcmp(file + n - sl, sfx[k]) == 0) {
            if (n - sl >= cap) return -1;
            memcpy(out, file, n - sl);
            out[n - sl] = '\0';
            return name_ok(out) ? 0 : -1;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* 探测：转换器、源目录                                                 */
/* ------------------------------------------------------------------ */

/* 任何一个项目自带的可执行文件在哪：与本程序同目录 → 同目录的 build/ →
 * 上一级 → PATH。（第三档是 build/RelWithDebInfo/skedaddle 这种多发生器成本身
 * 的场景，顺带也覆盖仓库根下的 <根>/build/。）
 * 泛化成按名字查找，places / lm 两个构建器共用同一份探测顺序。 */
static int find_exe(const char *name, char *out, size_t cap)
{
    char exe[4096];
    if (sk_exe_path(exe, sizeof(exe)) == 0) {
        char dir[4096];
        size_t n = strlen(exe);
        if (n < sizeof(dir)) {
            memcpy(dir, exe, n + 1);
            char *slash = (char *)sk_last_sep(dir);
            if (slash) {
                *slash = '\0';
                if (sk_which(dir, name, out, cap) == 0) return 0;
                char sub[4096];
                if (sk_path_join(sub, sizeof(sub), dir, "build") == 0 &&
                    sk_which(sub, name, out, cap) == 0) return 0;
                char up[4096];
                if (sk_path_join(up, sizeof(up), dir, "..") == 0 &&
                    sk_which(up, name, out, cap) == 0) return 0;
            }
        }
    }
    return sk_find_in_path(name, out, cap);
}

static int find_tool(char *out, size_t cap)
{
    return find_exe("skedaddle_graph_extract", out, cap);
}

/* python3：块表生成器是 Python 权威源，桌面发布包通常不带解释器。
 * prefer 非空时直接用（测试注入坏路径 / 假解释器用），但要求文件确实在。 */
static int find_python(const char *prefer, char *out, size_t cap)
{
    if (prefer && *prefer) {
        snprintf(out, cap, "%s", prefer);
        return sk_file_exists(out) ? 0 : -1;
    }
    if (find_exe("python3", out, cap) == 0) return 0;
    return find_exe("python", out, cap);
}

/* gen_chunks.py：src_dir 的父目录就是仓库根（src_dir 探测自 graph_dir 逐级
 * 往上找 sources/）。相对路径 "sources" 的父目录记作 "."，正好能拼出
 * ./scripts/gen_chunks.py。prefer 非空时直接用于测试隔离。 */
static int find_chunks_script(const char *prefer, const char *src_dir,
                              char *out, size_t cap)
{
    if (prefer && *prefer) {
        snprintf(out, cap, "%s", prefer);
        return sk_file_exists(out) ? 0 : -1;
    }
    char root[4096];
    snprintf(root, sizeof(root), "%s", src_dir);
    size_t n = strlen(root);
    while (n > 1 && (root[n - 1] == '/' || root[n - 1] == '\\')) root[--n] = '\0';
    char *slash = (char *)sk_last_sep(root);
    if (slash && slash != root) *slash = '\0';
    else snprintf(root, sizeof(root), ".");
    if (sk_path_join(out, cap, root, "scripts/gen_chunks.py") == 0 &&
        sk_file_exists(out)) return 0;
    return -1;
}

/* sources/ 在哪：从 graph_dir 逐级往上找，最多 6 级 */
static int find_src_dir(char *out, size_t cap, const char *graph_dir)
{
    char cur[4096];
    snprintf(cur, sizeof(cur), "%s", graph_dir);
    size_t n = strlen(cur);
    while (n > 1 && (cur[n - 1] == '/' || cur[n - 1] == '\\')) cur[--n] = '\0';

    for (int i = 0; i < 6; i++) {
        if (sk_path_join(out, cap, cur, "sources") == 0 && sk_is_dir(out)) return 0;
        char *slash = (char *)sk_last_sep(cur);
        if (!slash || slash == cur) break;
        *slash = '\0';
    }
    snprintf(out, cap, "sources");
    return sk_is_dir(out) ? 0 : -1;
}

/* 该省对应的源 pbf（两种后缀都认）。找到返回 0 并填 pbf 路径。 */
static int find_pbf(const char *src_dir, const char *name, char *out, size_t cap)
{
    char base[512];
    int n = snprintf(base, sizeof(base), "%s.osm.pbf", name);
    if (n >= 0 && (size_t)n < sizeof(base) &&
        sk_path_join(out, cap, src_dir, base) == 0 && sk_file_exists(out)) return 0;

    n = snprintf(base, sizeof(base), "%s.pbf", name);
    if (n >= 0 && (size_t)n < sizeof(base) &&
        sk_path_join(out, cap, src_dir, base) == 0 && sk_file_exists(out)) return 0;
    return -1;
}

/* ------------------------------------------------------------------ */
/* manifest 读取（与 sync_maps.sh 的 mani_get 同语义）                    */
/* ------------------------------------------------------------------ */

static int mani_get(const char *dir, const char *key, char *out, size_t cap)
{
    char p[4096];
    if (sk_path_join(p, sizeof(p), dir, "manifest.txt") != 0) return -1;
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    char line[512];
    size_t klen = strlen(key);
    int got = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) != 0) continue;
        if (line[klen] != ' ' && line[klen] != '\t') continue;
        char *v = line + klen;
        while (*v == ' ' || *v == '\t') v++;
        char *hash = strchr(v, '#');            /* 行内注释（areas 那行有） */
        if (hash) *hash = '\0';
        char *nl = strpbrk(v, "\r\n");
        if (nl) *nl = '\0';
        size_t n = strlen(v);
        while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) v[--n] = '\0';
        snprintf(out, cap, "%s", v);
        got = 0;
        break;
    }
    fclose(f);
    return got;
}

/* ------------------------------------------------------------------ */
/* 体检：这个省的产物还能不能用                                          */
/* ------------------------------------------------------------------ */

static int bin_ok(const char *dir, const char *file, char *why, size_t cap)
{
    char p[4096], err[128];
    if (sk_path_join(p, sizeof(p), dir, file) != 0) {
        snprintf(why, cap, "%s 路径过长", file);
        return -1;
    }
    NMBinFile f;
    if (nmbin_open(p, &f) != 0) {
        snprintf(why, cap, "%s 打不开", file);
        return -1;
    }
    int rc = nmbin_check(&f, err, sizeof(err));
    nmbin_close(&f);
    if (rc != 0) {
        snprintf(why, cap, "%s 校验失败（%s）", file, err);
        return -1;
    }
    return 0;
}

static int prov_check(const char *dir, const char *pbf, char *why, size_t cap)
{
    char p[4096];

    /* 1) 产物目录在不在 */
    if (!sk_is_dir(dir)) {
        snprintf(why, cap, "%s", PV_MISSING_TXT);
        return PV_MISSING;
    }
    /* 2) 必需文件齐不齐（与 sync_maps 同一份清单） */
    if (sk_path_join(p, sizeof(p), dir, "manifest.txt") != 0 ||
        !sk_file_exists(p)) {
        snprintf(why, cap, "%s（缺 manifest.txt，上次转换没走完）", PV_INCOMPLETE_TXT);
        return PV_INCOMPLETE;
    }
    static const char *need[] = { "nodes.bin", "edges.bin" };
    for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); i++) {
        if (sk_path_join(p, sizeof(p), dir, need[i]) != 0) {
            snprintf(why, cap, "%s（路径过长）", PV_INCOMPLETE_TXT);
            return PV_INCOMPLETE;
        }
        if (!sk_file_exists(p)) {
            snprintf(why, cap, "%s（缺 %s）", PV_INCOMPLETE_TXT, need[i]);
            return PV_INCOMPLETE;
        }
    }
    /* 3) 二进制自洽性（截断/半截文件在这里现形） */
    if (bin_ok(dir, "nodes.bin", why, cap) != 0) return PV_CORRUPT;
    if (bin_ok(dir, "edges.bin", why, cap) != 0) return PV_CORRUPT;

    /* 4) 格式版本 */
    char v[64];
    if (mani_get(dir, "binfmt_version", v, sizeof(v)) == 0) {
        int ver = atoi(v);
        if (ver > 0 && ver != NMBIN_VERSION) {
            snprintf(why, cap, "二进制格式升级 v%d → v%d", ver, NMBIN_VERSION);
            return PV_STALE;
        }
    }
    /* 5) 源指纹：源 pbf 换了就该重转 */
    uint64_t sb = 0;
    int64_t  sm = 0;
    if (sk_stat_file(pbf, &sb, &sm) != 0) { sb = 0; sm = 0; }
    if (mani_get(dir, "src_bytes", v, sizeof(v)) == 0) {
        uint64_t mb = strtoull(v, NULL, 10);
        if (sb && mb && mb != sb) {
            snprintf(why, cap, "源文件大小变了（%llu → %llu 字节）",
                     (unsigned long long)mb, (unsigned long long)sb);
            return PV_STALE;
        }
    }
    if (mani_get(dir, "src_mtime", v, sizeof(v)) == 0) {
        int64_t mm = strtoll(v, NULL, 10);
        if (sm && mm && mm != sm) {
            snprintf(why, cap, "源文件被换过了（mtime 不同）");
            return PV_STALE;
        }
    }
    why[0] = '\0';
    return PV_OK;
}

/* ------------------------------------------------------------------ */
/* 并发锁：两个实例同时发现缺失时，只有一个去转                            */
/* ------------------------------------------------------------------ */

static void lock_path(char *out, size_t cap, const char *parent, const char *name)
{
    char nm[512];
    int n = snprintf(nm, sizeof(nm), "%s.building", name);
    if (n < 0 || (size_t)n >= sizeof(nm) || sk_path_join(out, cap, parent, nm) != 0)
        out[0] = '\0';
}

static int lock_pid_alive(const char *lp)
{
    FILE *f = fopen(lp, "r");
    if (!f) return 0;
    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1) pid = 0;
    fclose(f);
    return sk_pid_alive(pid);
}

/* 0 = 拿到锁；-1 = 拿不到（超时）。锁里写自己的 pid，便于识别过期锁。 */
static int lock_acquire(const char *parent, const char *name)
{
    char lp[4096];
    lock_path(lp, sizeof(lp), parent, name);
    sk_mkdir_p(parent);

    for (int try = 0; try < 360; try++) {          /* 最多等约 3 分钟 */
        char b[32];
        snprintf(b, sizeof(b), "%d\n", sk_get_pid());
        if (sk_create_exclusive(lp, b) == 0) return 0;
        if (!lock_pid_alive(lp)) {                 /* 上一个人崩了，锁是孤儿 */
            sk_unlink(lp);
            continue;
        }
        if (try == 0)
            fprintf(stderr, "[自动重建] %s 有另一个进程正在转换，等待中…\n", name);
        sk_sleep_ms(500);
    }
    return -1;
}

static void lock_release(const char *parent, const char *name)
{
    char lp[4096];
    lock_path(lp, sizeof(lp), parent, name);
    sk_unlink(lp);
}

/* ------------------------------------------------------------------ */
/* 重建一个省                                                           */
/* ------------------------------------------------------------------ */

/* 统一的"起一个子进程并等它结束"。av[0] 必须是 exe 本身：Windows 分支
 * 会把整个 av 拼成命令行，argv[0] 缺位会让脚本收到错位的参数。 */
static int run_argv(const char *exe, char *const av[])
{
    /* fork 会把 stdio 缓冲区复制一份，先冲干净（平台层内部也做一次） */
    fflush(NULL);
    int code = 0;
    if (sk_spawn_wait(exe, av, &code) != 0) return -1;
    return code == 0 ? 0 : -1;
}

static int run_extract(const char *tool, const char *pbf, const char *dir)
{
    char outarg[4224];      /* 路径 buffer(4096) + "--out=" 还有富余 */
    if (snprintf(outarg, sizeof(outarg), "--out=%s", dir) >= (int)sizeof(outarg))
        return -1;
    char *const av[] = { (char *)tool, outarg, (char *)pbf, NULL };
    return run_argv(tool, av);
}

/* ------------------------------------------------------------------ */
/* 图集级旁路产物：可用性判据                                          */
/* ------------------------------------------------------------------ */

static int file_nonempty(const char *p)
{
    uint64_t b = 0;
    int64_t  m = 0;
    return sk_stat_file(p, &b, &m) == 0 && b > 0;
}

/* 地名库：不是只看文件在不在 —— 半截 SQLite 文件也能被打开，必须真的查一次
 * place 表。只读打开，绝不改写运行时正在用的库。 */
static int places_ok(const char *db_path)
{
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return 0;
    }
    sqlite3_stmt *st = NULL;
    int ok = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM place", -1, &st, NULL) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW &&
        sqlite3_column_int64(st, 0) > 0)
        ok = 1;
    if (st) sqlite3_finalize(st);
    sqlite3_close(db);
    return ok;
}

/* 地标表：header 有 magic 与规模，太小的文件一定是半截的。真正的图指纹
 * 校验在 router_create() 里（省数与总节点数），这里只挡"缺/空"。 */
static int lm_ok(const char *p)
{
    uint64_t b = 0;
    int64_t  m = 0;
    return sk_stat_file(p, &b, &m) == 0 && b > 64;
}

/* 块表：每个省的 JSON 与 gzip 伴生都要在。返回 0 = 全齐，-1 = 有缺失。 */
static int chunks_ok(const char *parent, const char *graph_dir, int single,
                     char **names, int nn, char *why, size_t cap)
{
    static const char *fs[] = { AB_CHUNKS_JSON, AB_CHUNKS_GZ };
    char p[4096], prov[4096];
    for (int i = 0; i < nn; i++) {
        if (single) {
            snprintf(prov, sizeof(prov), "%s", graph_dir);
        } else if (sk_path_join(prov, sizeof(prov), parent, names[i]) != 0) {
            snprintf(why, cap, "%s 路径过长", names[i]);
            return -1;
        }
        for (size_t k = 0; k < sizeof(fs) / sizeof(fs[0]); k++) {
            if (sk_path_join(p, sizeof(p), prov, fs[k]) != 0) {
                snprintf(why, cap, "%s 路径过长", names[i]);
                return -1;
            }
            if (!file_nonempty(p)) {
                snprintf(why, cap, "%s 缺 %s", names[i], fs[k]);
                return -1;
            }
        }
    }
    why[0] = '\0';
    return 0;
}

/* 让位目录：prov_dir 同级 + ".old.<pid>"。带 .old. 是刻意的——
   mapset_load 会跳过 .old.* / .tmp.*，不会被当成一个省加载进来。 */
static int aside_path(char *out, size_t cap, const char *prov_dir)
{
    int n = snprintf(out, cap, "%s.old.%d", prov_dir, sk_get_pid());
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

/* 返回 0 = 重建成功 */
static int rebuild_one(const char *graph_parent, const char *prov_dir,
                       const char *name, const char *pbf, const char *tool)
{
    if (lock_acquire(graph_parent, name) != 0) {
        fprintf(stderr, "[自动重建] %s 等不到转换锁，放弃重建\n", name);
        return -1;
    }

    /* 旧产物先清掉：留着会和转换器新写的文件混在一起（比如更大的 edges.bin
       配上新的 nodes.bin），出来的图是自相矛盾的。删不掉就改名让位 ——
       名字带 .old. 是因为 mapset_load 会跳过 .old.* / .tmp.*，不会被当成
       一个省加载进来（同一份数据装两遍）。 */
    if (sk_is_dir(prov_dir)) {
        char aside[4224];
        if (aside_path(aside, sizeof(aside), prov_dir) != 0) {
            fprintf(stderr, "[自动重建] %s 路径过长，放弃重建\n", name);
            lock_release(graph_parent, name);
            return -1;
        }
        if (sk_rmtree(prov_dir) != 0 && sk_rename(prov_dir, aside) != 0) {
            fprintf(stderr, "[自动重建] %s 旧产物清不掉（%s），放弃重建\n",
                    name, strerror(errno));
            lock_release(graph_parent, name);
            return -1;
        }
    }

    fprintf(stderr, "[自动重建] %s 转换中（%s）…\n", name, pbf);
    double t0 = now_ms();
    int rc = run_extract(tool, pbf, prov_dir);
    double ms = now_ms() - t0;

    if (rc != 0) {
        fprintf(stderr,
                "[自动重建] %s 转换失败（%.1f s）。源 pbf 可能已损坏，建议挪到 "
                "sources/_bad/ 后重试；\n"
                "           也可手工执行：%s --out=%s %s\n",
                name, ms / 1000.0, tool, prov_dir, pbf);
        lock_release(graph_parent, name);
        return -1;
    }

    char why[256] = "";
    int pv = prov_check(prov_dir, pbf, why, sizeof(why));
    if (pv != PV_OK) {
        fprintf(stderr, "[自动重建] %s 转换结束但产物仍不可用：%s\n", name, why);
        lock_release(graph_parent, name);
        return -1;
    }

    char nodes[64] = "-", edges[64] = "-";
    mani_get(prov_dir, "nodes", nodes, sizeof(nodes));
    mani_get(prov_dir, "edges", edges, sizeof(edges));
    fprintf(stderr, "[自动重建] %s 重建完成（%.1f s，节点 %s / 边 %s）\n",
            name, ms / 1000.0, nodes, edges);

    /* 让位的旧目录这时可以清了 */
    char aside[4224];
    if (aside_path(aside, sizeof(aside), prov_dir) == 0 && sk_is_dir(aside))
        sk_rmtree(aside);

    lock_release(graph_parent, name);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 图集级旁路产物：重建                                                */
/* ------------------------------------------------------------------ */

/* places_build 把 --db 的值原样写进 meta 的 db 行；我们建在 .tmp.<pid> 上，
 * 就位前把那一行改成正式路径，免得下次同步看到一个临时文件名。 */
static int fix_meta_db_line(const char *meta_path, const char *db_path)
{
    FILE *f = fopen(meta_path, "r");
    if (!f) return -1;
    char tmp[4224];
    int n = snprintf(tmp, sizeof(tmp), "%s.abfix.%d", meta_path, sk_get_pid());
    if (n < 0 || (size_t)n >= sizeof(tmp)) { fclose(f); return -1; }
    FILE *g = fopen(tmp, "w");
    if (!g) { fclose(f); return -1; }
    char line[4096];
    int wrote = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "db", 2) == 0 &&
            (line[2] == ' ' || line[2] == '\t' || line[2] == '=')) {
            fprintf(g, "db           = %s\n", db_path);
            wrote = 1;
        } else {
            fputs(line, g);
        }
    }
    fclose(f);
    if (fclose(g) != 0) { sk_unlink(tmp); return -1; }
    if (!wrote) { sk_unlink(tmp); return -1; }   /* meta 格式变了，宁可失败 */
    if (sk_rename(tmp, meta_path) != 0) { sk_unlink(tmp); return -1; }
    return 0;
}

/* 不复刻 sync_maps.sh 的 md5 指纹（两份实现必然漂移）。写一个明确的哨兵：
 * 脚本读到它必然与 want_fp 不等 ⇒ 下次重算并写回正确指纹。既不假装
 * "已验证"，也不让旧指纹留着误导下一次同步。 */
static int write_lm_meta(const char *meta_path)
{
    FILE *f = fopen(meta_path, "w");
    if (!f) return -1;
    fprintf(f, "# lm.bin 由 autobuild 重建；指纹由 scripts/sync_maps.sh 重算\n"
               "fingerprint autobuild-unverified\n"
               "lmver %d\ncell_m %d\nnlm %d\n",
            AB_LM_VER, AB_LM_CELL, AB_LM_NLM);
    return fclose(f) == 0 ? 0 : -1;
}

/* 地名库：sources 全量重抽 → 临时库 → 自检 → 两个文件一起原子就位。
 * 与 sync_maps.sh 同一套参数，只是不套 --only 过滤（地名库是全国共用）。 */
static int rebuild_places(const char *places_db, const char *src_dir)
{
    char tool[4096];
    if (find_exe("skedaddle_places_build", tool, sizeof(tool)) != 0) {
        fprintf(stderr, "[自动重建] 找不到 skedaddle_places_build，地名库无法补齐\n");
        return -1;
    }
    char db_tmp[4224], meta_tmp[4224], meta_final[4224];
    char sarg[4224], darg[4224], marg[4224];
    int pid = sk_get_pid();
    if (snprintf(db_tmp, sizeof(db_tmp), "%s.tmp.%d", places_db, pid) >= (int)sizeof(db_tmp) ||
        snprintf(meta_tmp, sizeof(meta_tmp), "%s.meta.tmp.%d", places_db, pid) >= (int)sizeof(meta_tmp) ||
        snprintf(meta_final, sizeof(meta_final), "%s.meta", places_db) >= (int)sizeof(meta_final) ||
        snprintf(sarg, sizeof(sarg), "--sources=%s", src_dir) >= (int)sizeof(sarg) ||
        snprintf(darg, sizeof(darg), "--db=%s", db_tmp) >= (int)sizeof(darg) ||
        snprintf(marg, sizeof(marg), "--meta=%s", meta_tmp) >= (int)sizeof(marg))
        return -1;

    char *const av1[] = { tool, sarg, darg, marg, NULL };
    char *const av2[] = { tool, darg, (char *)"--selftest", NULL };
    int rc = run_argv(tool, av1);
    if (rc == 0) rc = run_argv(tool, av2);
    if (rc == 0 && !file_nonempty(db_tmp)) rc = -1;
    if (rc == 0) rc = fix_meta_db_line(meta_tmp, places_db);
    if (rc == 0) {
        if (sk_rename(db_tmp, places_db) != 0 ||
            sk_rename(meta_tmp, meta_final) != 0) rc = -1;
    }
    if (rc != 0) { sk_unlink(db_tmp); sk_unlink(meta_tmp); }
    return rc;
}

/* 地标表：全图共享，写在图集根。参数与 sync_maps.sh 的 LM 段一致，
 * 常量由 check_autobuild_sidecars.py 守同步。 */
static int rebuild_lm(const char *graph_root, const AutoBuildOpt *o)
{
    char tool[4096];
    if (o->lm_tool && *o->lm_tool) {
        snprintf(tool, sizeof(tool), "%s", o->lm_tool);
        if (!sk_file_exists(tool)) {
            fprintf(stderr, "[自动重建] 注入的地标表构建器不存在：%s\n", tool);
            return -1;
        }
    } else if (find_exe("skedaddle_lmbuild_cli", tool, sizeof(tool)) != 0) {
        fprintf(stderr, "[自动重建] 找不到 skedaddle_lmbuild_cli，地标表无法补齐\n");
        return -1;
    }
    char lm_bin[4224], lm_meta[4224], lm_tmp[4224];
    char garg[4224], oarg[4224], carg[64], narg[64];
    int pid = sk_get_pid();
    if (sk_path_join(lm_bin, sizeof(lm_bin), graph_root, AB_LM_BIN) != 0 ||
        snprintf(lm_meta, sizeof(lm_meta), "%s.meta", lm_bin) >= (int)sizeof(lm_meta) ||
        snprintf(lm_tmp, sizeof(lm_tmp), "%s.tmp.%d", lm_bin, pid) >= (int)sizeof(lm_tmp) ||
        snprintf(garg, sizeof(garg), "--graph=%s", graph_root) >= (int)sizeof(garg) ||
        snprintf(oarg, sizeof(oarg), "--out=%s", lm_tmp) >= (int)sizeof(oarg) ||
        snprintf(carg, sizeof(carg), "--cell=%d", AB_LM_CELL) >= (int)sizeof(carg) ||
        snprintf(narg, sizeof(narg), "--nlm=%d", AB_LM_NLM) >= (int)sizeof(narg))
        return -1;

    char *const av[] = { tool, garg, oarg, carg, narg, NULL };
    int rc = run_argv(tool, av);
    if (rc == 0 && !file_nonempty(lm_tmp)) rc = -1;
    if (rc == 0 && sk_rename(lm_tmp, lm_bin) != 0) rc = -1;
    if (rc == 0 && write_lm_meta(lm_meta) != 0) rc = -1;
    if (rc != 0) sk_unlink(lm_tmp);
    return rc;
}

/* 块表：一次把全部省目录交给 Python 权威源生成，再同源复核 gzip 伴生。
 * 找不到 python3 或脚本时降级为警告（桌面运行时零读取，不算失败）。 */
static int rebuild_chunks(const char *parent, const char *graph_dir, int single,
                          char **names, int nn, const char *src_dir,
                          const AutoBuildOpt *o, int *degraded)
{
    *degraded = 0;
    char py[4096], script[4096];
    if (find_python(o->py, py, sizeof(py)) != 0 ||
        find_chunks_script(o->chunks_script, src_dir, script, sizeof(script)) != 0) {
        fprintf(stderr,
                "[自动重建] 块表需要 python3 与 scripts/gen_chunks.py，本机未找到；\n"
                "           桌面运行时不读块表，Android 差分需另行生成"
                "（跑 scripts/sync_maps.sh 可补齐）。\n");
        *degraded = 1;
        return 0;
    }

    char **provs = calloc((size_t)(nn ? nn : 1), sizeof(char *));
    char **av    = calloc((size_t)(nn + 4), sizeof(char *));
    if (!provs || !av) { free(provs); free(av); return -1; }
    int rc = 0;
    for (int i = 0; i < nn; i++) {
        provs[i] = malloc(4096);
        if (!provs[i]) { rc = -1; break; }
        if (single) snprintf(provs[i], 4096, "%s", graph_dir);
        else if (sk_path_join(provs[i], 4096, parent, names[i]) != 0) { rc = -1; break; }
    }
    if (rc == 0) {
        av[0] = py; av[1] = script;
        for (int i = 0; i < nn; i++) av[2 + i] = provs[i];
        av[2 + nn] = NULL;
        rc = run_argv(py, av);

        if (rc == 0) {
            av[0] = py; av[1] = script; av[2] = (char *)"--sidecar-check";
            for (int i = 0; i < nn; i++) av[3 + i] = provs[i];
            av[3 + nn] = NULL;
            rc = run_argv(py, av);
        }
    }
    for (int i = 0; i < nn; i++) free(provs[i]);
    free(provs);
    free(av);
    return rc;
}

/* 旁路产物总入口。parent 在单省/图集两种模式下都等于图集根；products 根是
 * 它的上一级。SKEDADDLE_PLACES 若设了就尊重它（发布包 start.bat 会设）。 */
static void repair_sidecars(const char *graph_dir, const char *parent,
                            const char *src_dir, int single,
                            char **names, int nn, const AutoBuildOpt *o,
                            AutoBuildStat *st)
{
    char places_db[4224];
    const char *env = getenv("SKEDADDLE_PLACES");
    if (env && *env) {
        snprintf(places_db, sizeof(places_db), "%s", env);
    } else {
        char products[4096];
        snprintf(products, sizeof(products), "%s", parent);
        size_t n = strlen(products);
        while (n > 1 && (products[n - 1] == '/' || products[n - 1] == '\\'))
            products[--n] = '\0';
        char *slash = (char *)sk_last_sep(products);
        if (slash && slash != products) *slash = '\0';
        else snprintf(products, sizeof(products), ".");
        if (sk_path_join(places_db, sizeof(places_db), products, AB_PLACES_DB) != 0)
            return;
    }

    char lm_bin[4224];
    if (sk_path_join(lm_bin, sizeof(lm_bin), parent, AB_LM_BIN) != 0) return;

    char why[256] = "";
    int need_places = !places_ok(places_db);
    int need_lm     = !lm_ok(lm_bin);
    int need_chunks = chunks_ok(parent, graph_dir, single, names, nn,
                                why, sizeof(why)) != 0;
    /* 有省被重建 ⇒ 三样必然过期，无条件全建；这是 sync_maps.sh 注释里反复
     * 强调的"静默过期"坑，autobuild 重抽省后必须自己兜住。 */
    int force = (st->rebuilt > 0);

    if (!(force || need_places || need_lm || need_chunks)) return;  /* 正常态零动作 */
    st->sidecars_checked = 1;

    int do_places = force || need_places;
    int do_lm     = force || need_lm;
    int do_chunks = force || need_chunks;
    int todo = do_places + do_lm + do_chunks;

    if (!o->enable) {
        fprintf(stderr,
                "[自动重建] 图集旁路产物需要补齐（地名库%s、地标表%s、块表%s），"
                "已用 --no-autobuild 关闭；手工执行：./scripts/sync_maps.sh\n",
                do_places ? "待建" : "齐", do_lm ? "待建" : "齐",
                do_chunks ? "待建" : "齐");
        st->sidecars_skipped = todo;
        return;
    }

    if (force)
        fprintf(stderr, "[自动重建] 本次重建了 %d 个省，旁路产物无条件全部重算\n",
                st->rebuilt);
    if (need_chunks && why[0])
        fprintf(stderr, "[自动重建] 块表缺失：%s\n", why);

    if (lock_acquire(parent, "sidecars") != 0) {
        fprintf(stderr, "[自动重建] 旁路产物等不到转换锁，放弃补齐\n");
        st->sidecars_failed = todo;
        return;
    }

    if (do_places) {
        fprintf(stderr, "[自动重建] 补齐地名库 %s …\n", places_db);
        if (rebuild_places(places_db, src_dir) == 0) st->sidecars_rebuilt++;
        else {
            st->sidecars_failed++;
            fprintf(stderr, "[自动重建] 地名库补齐失败（搜索将为空）\n");
        }
    }
    if (do_lm) {
        fprintf(stderr, "[自动重建] 补齐地标表 %s …\n", lm_bin);
        if (rebuild_lm(parent, o) == 0) st->sidecars_rebuilt++;
        else {
            st->sidecars_failed++;
            fprintf(stderr, "[自动重建] 地标表补齐失败（算路退回直线启发式）\n");
        }
    }
    if (do_chunks) {
        fprintf(stderr, "[自动重建] 补齐块表与 gzip 伴生 …\n");
        int degraded = 0;
        if (rebuild_chunks(parent, graph_dir, single, names, nn,
                           src_dir, o, &degraded) == 0) {
            if (degraded) st->sidecars_degraded = 1;
            else st->sidecars_rebuilt++;
        } else {
            st->sidecars_failed++;
            fprintf(stderr, "[自动重建] 块表生成或伴生复核失败\n");
        }
    }
    lock_release(parent, "sidecars");
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */

/* 单省目录（products/graph/beijing）还是图集目录（products/graph）？
 * 目录被整个删掉时没法靠内容判断，这时看 basename 有没有同名 pbf ——
 * 有就说明它本该是个省目录。判错后果很不一样：把图集当单省会漏建 7 个省，
 * 把单省当图集会把 8 个省全建一遍。 */
static int is_single_prov(const char *graph_dir, const char *src_dir,
                          char *name_out, size_t cap)
{
    char p[4096];
    if (sk_path_join(p, sizeof(p), graph_dir, "manifest.txt") == 0 &&
        sk_file_exists(p))
        goto single;
    if (sk_path_join(p, sizeof(p), graph_dir, "nodes.bin") == 0 &&
        sk_file_exists(p))
        goto single;

    {
        const char *base = sk_base_name(graph_dir);
        char pbf[4096];
        if (find_pbf(src_dir, base, pbf, sizeof(pbf)) == 0 && name_ok(base)) {
            snprintf(name_out, cap, "%s", base);
            return 1;
        }
        return 0;
    }
single:
    {
        const char *base = sk_base_name(graph_dir);
        snprintf(name_out, cap, "%s", base);
        return 1;
    }
}

typedef struct { char **v; int n, cap; } NameList;

static void nl_add(NameList *l, const char *nm)
{
    for (int i = 0; i < l->n; i++)
        if (strcmp(l->v[i], nm) == 0) return;      /* 去重 */
    if (l->n == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 16;
        char **t = realloc(l->v, (size_t)ncap * sizeof(char *));
        if (!t) return;
        l->v = t; l->cap = ncap;
    }
    l->v[l->n++] = sk_strdup(nm);
}

/* 图集模式：sources/ 下所有 pbf 对应的省名 */
static void list_provs(const char *src_dir, NameList *l)
{
    SkDir *d = sk_dir_open(src_dir);
    if (!d) return;
    const char *de;
    char nm[256];
    while ((de = sk_dir_next(d)) != NULL) {
        if (de[0] == '.') continue;
        if (prov_name(de, nm, sizeof(nm)) != 0) continue;
        nl_add(l, nm);
    }
    sk_dir_close(d);
}

/* 反向补一遍：products/graph 下已经存在的省目录。
 * 只扫 sources/ 的话，"pbf 和产物都没了"会完全静默 —— 省份悄无声息少一个。
 * 扫进来的这些省即使坏了也重建不了（没有原料），但至少要说出来。
 * .tmp.* / .old.* 是过渡目录，必须跳过（与 mapset_load 同一份过滤名单）。 */
static void list_existing_provs(const char *graph_dir, NameList *l)
{
    SkDir *d = sk_dir_open(graph_dir);
    if (!d) return;
    const char *de;
    char p[4096];
    while ((de = sk_dir_next(d)) != NULL) {
        if (de[0] == '.') continue;
        if (strstr(de, ".tmp.") || strstr(de, ".old.")) continue;
        if (!name_ok(de)) continue;
        if (sk_path_join(p, sizeof(p), graph_dir, de) != 0) continue;
        if (!sk_is_dir(p)) continue;
        nl_add(l, de);
    }
    sk_dir_close(d);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int autobuild_repair(const char *graph_dir, const AutoBuildOpt *opt,
                     AutoBuildStat *st_out)
{
    AutoBuildStat st;
    memset(&st, 0, sizeof(st));
    double t0 = now_ms();

    AutoBuildOpt o;
    memset(&o, 0, sizeof(o));   /* opt==NULL 时 py/chunks_script/lm_tool 必须为 0(NULL) */
    o.enable  = 1;
    if (opt) o = *opt;

    char src_dir[4096], tool[4096];
    if (o.src_dir && *o.src_dir) {
        snprintf(src_dir, sizeof(src_dir), "%s", o.src_dir);
    } else if (find_src_dir(src_dir, sizeof(src_dir), graph_dir) != 0) {
        fprintf(stderr, "[自动重建] 探测不到源地图目录（sources/），跳过自检；"
                        "可用 --sources=<目录> 指定。\n");
        goto done;
    }

    char single_name[256] = "";
    int single = is_single_prov(graph_dir, src_dir, single_name, sizeof(single_name));

    /* 省目录的父目录（= 图集根）。单省模式下产物放 graph_dir 本身，
       它的父目录才是放锁文件的地方。 */
    char parent[4096];
    if (single) {
        snprintf(parent, sizeof(parent), "%s", graph_dir);
        char *slash = (char *)sk_last_sep(parent);
        if (slash && slash != parent) *slash = '\0';
        else snprintf(parent, sizeof(parent), ".");
    } else {
        snprintf(parent, sizeof(parent), "%s", graph_dir);
    }

    NameList nl = { NULL, 0, 0 };
    if (single) {
        nl_add(&nl, single_name);
    } else {
        list_provs(src_dir, &nl);
        list_existing_provs(graph_dir, &nl);   /* 补上"没有 pbf 的省目录" */
    }
    char **names = nl.v;
    int nn = nl.n;
    if (nn > 1) qsort(names, (size_t)nn, sizeof(char *), cmp_str);

    /* 转换器只在真的要干活时才找，别让正常启动多一次探测开销 */
    int have_tool = 0;
    for (int i = 0; i < nn; i++) {
        const char *name = names[i];
        char pbf[4096], prov_dir[4096], why[256];
        st.checked++;          /* 扫到的每个目录都算检查过，包括"没有原料"的 */

        if (single) {
            size_t gl = strlen(graph_dir);
            if (gl >= sizeof(prov_dir)) continue;
            memcpy(prov_dir, graph_dir, gl + 1);
        }
        else if (sk_path_join(prov_dir, sizeof(prov_dir), graph_dir, name) != 0)
            continue;                        /* 路径过长，跳过这个省 */

        if (find_pbf(src_dir, name, pbf, sizeof(pbf)) != 0) {
            /* 源都没有：只有在产物也不可用时才值得喊一声 */
            if (prov_check(prov_dir, "(无)", why, sizeof(why)) != PV_OK) {
                fprintf(stderr, "[自动重建] %s 产物不可用（%s），但 sources/ 下没有 "
                                "%s.osm.pbf —— 没有原料，无法重建。\n",
                        name, why, name);
                st.nosrc++;
            }
            continue;
        }

        int pv = prov_check(prov_dir, pbf, why, sizeof(why));
        if (pv == PV_OK) { st.ok++; continue; }

        if (!o.enable) {
            fprintf(stderr, "[自动重建] %s 需要重建（%s）。已用 --no-autobuild 关闭"
                            "自动重建，手工执行：\n"
                            "           ./scripts/sync_maps.sh --only=%s\n",
                    name, why, name);
            st.skipped++;
            continue;
        }

        if (!have_tool) {
            if (o.tool && *o.tool) snprintf(tool, sizeof(tool), "%s", o.tool);
            else if (find_tool(tool, sizeof(tool)) != 0) {
                fprintf(stderr, "[自动重建] 找不到 skedaddle_graph_extract，无法自动重建。"
                                "手工执行：./scripts/sync_maps.sh\n");
                st.failed += (nn - i);
                break;
            }
            have_tool = 1;
        }

        fprintf(stderr, "[自动重建] %s 需要重建：%s\n", name, why);
        if (rebuild_one(parent, prov_dir, name, pbf, tool) == 0) st.rebuilt++;
        else st.failed++;
    }

    /* 省循环之后再补图集级旁路产物：必须在 names 释放前调用，且必须能看到
     * st.rebuilt（有省被重建 ⇒ 三样全部重算）。 */
    repair_sidecars(graph_dir, parent, src_dir, single, names, nn, &o, &st);

    for (int i = 0; i < nn; i++) free(names[i]);
    free(names);

done:
    st.elapsed_ms = now_ms() - t0;
    if (st.checked && (st.rebuilt || st.failed || st.nosrc || st.skipped))
        fprintf(stderr,
                "[自动重建] 检查 %d 省：完好 %d、重建 %d、失败 %d、无源 %d、跳过 %d"
                "（%.0f ms）\n",
                st.checked, st.ok, st.rebuilt, st.failed, st.nosrc, st.skipped,
                st.elapsed_ms);
    if (st.sidecars_checked)
        fprintf(stderr,
                "[自动重建] 旁路产物：补齐 %d、失败 %d、跳过 %d、块表降级 %d\n",
                st.sidecars_rebuilt, st.sidecars_failed, st.sidecars_skipped,
                st.sidecars_degraded);
    if (st_out) *st_out = st;
    return 0;
}

int mapset_load_repair(MapSet *ms, const char *graph_dir,
                       const AutoBuildOpt *opt, AutoBuildStat *st_out)
{
    autobuild_repair(graph_dir, opt, st_out);
    return mapset_load(ms, graph_dir);
}
