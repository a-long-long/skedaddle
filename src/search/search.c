#include "search/search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#include "utils/geo.h"

/* ---------------------------------------------------------------------------
 * 实现要点见 search.h 顶部。这里只写三件容易写错的事：
 *   1. bigram 必须按**字符**切，不是按字节（UTF-8 汉字 3 字节，按字节切
 *      会把一个字劈成两半，和建库时的 gram 对不上，搜索永远 0 结果）
 *   2. gram 只负责召回，必须再用子串复核
 *   3. LIKE 的 % 和 _ 要转义，否则用户敲个 "%" 就等于全表匹配
 * ------------------------------------------------------------------------- */

#define MAX_GRAMS   32
#define GRAM_BYTES  16
#define CAND_MAX    3000      /* 候选上限：再多也进不了 top-K，别白算 */
#define LIMIT_DEF   20
#define LIMIT_MAX   64

struct SearchDb {
    sqlite3      *db;
    sqlite3_stmt *st_one;     /* SELECT ... FROM place WHERE id=? */
};

/* ---- UTF-8 ---- */

/* 返回下一个字符起点。不校验合法性：非法字节按 1 字节推进，不至于死循环。 */
static const char *u8_next(const char *p, const char *end)
{
    unsigned char c = (unsigned char)*p;
    int n = 1;
    if      (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    const char *q = p + n;
    return q > end ? end : q;
}

/* 地址串规范化（见 search.h）。UTF-8 逐"字符"处理，多字节整段复制，
 * 绝不劈开汉字；规范化只会变短或等长，不会变长。 */
int search_normalize_addr(const char *in, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!in) return 0;
    const unsigned char *p = (const unsigned char *)in;
    size_t o = 0;
    while (*p && o + 4 < cap) {
        if (p[0] == 0xE3 && p[1] == 0x80 && p[2] == 0x80) { p += 3; continue; } /* U+3000 */
        if (p[0] == 0xEF && p[1] == 0xBC && p[2] >= 0x90 && p[2] <= 0x99) {    /* U+FF10..19 */
            out[o++] = (char)('0' + (p[2] - 0x90)); p += 3; continue;
        }
        if (p[0] == 0xEF && p[1] == 0xBC && p[2] >= 0xA1 && p[2] <= 0xBA) {    /* U+FF21..3A */
            out[o++] = (char)('a' + (p[2] - 0xA1)); p += 3; continue;
        }
        if (p[0] == 0xEF && p[1] == 0xBD && p[2] >= 0x81 && p[2] <= 0x9A) {    /* U+FF41..5A */
            out[o++] = (char)('a' + (p[2] - 0x81)); p += 3; continue;
        }
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { p++; continue; }
        if (*p >= 'A' && *p <= 'Z') { out[o++] = (char)(*p + 32); p++; continue; }
        if (*p < 0x80) { out[o++] = (char)*p; p++; continue; }
        size_t len = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
        size_t k = 0;
        for (; k < len && p[k]; k++) out[o + k] = (char)p[k];
        o += k; p += k;
    }
    out[o] = '\0';
    if (o >= 3) {
        unsigned char a = (unsigned char)out[o-3], b = (unsigned char)out[o-2],
                      c = (unsigned char)out[o-1];
        if ((a == 0xE5 && b == 0x8F && c == 0xB7) ||   /* 号 */
            (a == 0xE8 && b == 0x99 && c == 0x9F)) {   /* 號 */
            o -= 3; out[o] = '\0';
        }
    }
    return (int)o;
}

int search_bigrams(const char *q, char grams[][GRAM_BYTES], int max_grams,
                   int *chars)
{
    if (!q) { if (chars) *chars = 0; return 0; }
    const char *end = q + strlen(q);
    const char *st[MAX_GRAMS + 2];
    int nc = 0;
    for (const char *p = q; p < end && nc < MAX_GRAMS + 2;) {
        st[nc++] = p;
        p = u8_next(p, end);
    }
    if (chars) *chars = nc;

    int ng = 0;
    for (int i = 0; i + 1 < nc && ng < max_grams; i++) {
        const char *s = st[i];
        const char *e = (i + 2 < nc) ? st[i + 2] : end;
        size_t len = (size_t)(e - s);
        if (len == 0) continue;
        if (len > GRAM_BYTES - 1) len = GRAM_BYTES - 1;
        int dup = 0;
        for (int k = 0; k < ng; k++)
            if (strlen(grams[k]) == len && memcmp(grams[k], s, len) == 0) { dup = 1; break; }
        if (dup) continue;                     /* "呵呵" 只算一个 gram */
        memcpy(grams[ng], s, len);
        grams[ng][len] = '\0';
        ng++;
    }
    return ng;
}

/* LIKE 转义：\ % _ 都是通配符，用户输入的必须当字面量 */
static void like_literal(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; s[i] && o + 3 < n; i++) {
        if (s[i] == '\\' || s[i] == '%' || s[i] == '_') {
            out[o++] = '\\';
            out[o++] = s[i];
        } else {
            out[o++] = s[i];
        }
    }
    out[o] = '\0';
}

/* ---- 候选 id 收集 ---- */

typedef struct { uint32_t *v; int n, cap; } IdList;

static int ids_push(IdList *l, uint32_t id)
{
    if (l->n >= l->cap) {
        int nc = l->cap ? l->cap * 2 : 512;
        if (nc > CAND_MAX) nc = CAND_MAX;
        if (l->n >= nc) return 0;              /* 到上限就丢弃（后面还有更好的？没有，但保住了耗时） */
        uint32_t *nv = realloc(l->v, (size_t)nc * sizeof(uint32_t));
        if (!nv) return -1;
        l->v = nv; l->cap = nc;
    }
    l->v[l->n++] = id;
    return 0;
}

/* 交集：含有全部 ng 个 gram 的 place_id */
static int ids_by_grams(SearchDb *d, char grams[][GRAM_BYTES], int ng, IdList *out)
{
    char sql[1536];
    int pos = snprintf(sql, sizeof(sql),
                       "SELECT place_id FROM gram WHERE gram IN (");
    for (int i = 0; i < ng; i++)
        pos += snprintf(sql + pos, sizeof(sql) - (size_t)pos, "%s?", i ? "," : "");
    pos += snprintf(sql + pos, sizeof(sql) - (size_t)pos,
                    ") GROUP BY place_id HAVING COUNT(DISTINCT gram)=%d", ng);
    if (pos < 0 || (size_t)pos >= sizeof(sql)) return -1;

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(d->db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    for (int i = 0; i < ng; i++)
        sqlite3_bind_text(st, i + 1, grams[i], -1, SQLITE_TRANSIENT);

    while (sqlite3_step(st) == SQLITE_ROW) {
        if (ids_push(out, (uint32_t)sqlite3_column_int64(st, 0)) != 0) break;
        if (out->n >= CAND_MAX) break;
    }
    sqlite3_finalize(st);
    return 0;
}

/* 单字：gram 前缀（仍走 (gram,place_id) 索引的范围扫描） */
static int ids_by_prefix(SearchDb *d, const char *q, IdList *out)
{
    char pat[GRAM_BYTES + 8];
    char lit[GRAM_BYTES];
    like_literal(q, lit, sizeof(lit));
    snprintf(pat, sizeof(pat), "%s%%", lit);

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(d->db,
            "SELECT place_id FROM gram WHERE gram LIKE ?1 ESCAPE '\\' LIMIT 4000",
            -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, pat, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        uint32_t id = (uint32_t)sqlite3_column_int64(st, 0);
        /* 同一个 place 可能有多个以该字开头的 gram，去重 */
        int dup = 0;
        for (int i = 0; i < out->n; i++) if (out->v[i] == id) { dup = 1; break; }
        if (dup) continue;
        if (ids_push(out, id) != 0) break;
        if (out->n >= CAND_MAX) break;
    }
    sqlite3_finalize(st);
    return 0;
}

/* 兜底：全表扫 name（place 表没有 name 索引，只在前面都找不到时才用） */
static int ids_by_scan(SearchDb *d, const char *q, IdList *out)
{
    char pat[160];
    char lit[128];
    like_literal(q, lit, sizeof(lit));
    snprintf(pat, sizeof(pat), "%%%s%%", lit);

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(d->db,
            "SELECT id FROM place WHERE name LIKE ?1 ESCAPE '\\' LIMIT 2000",
            -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, pat, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (ids_push(out, (uint32_t)sqlite3_column_int64(st, 0)) != 0) break;
        if (out->n >= CAND_MAX) break;
    }
    sqlite3_finalize(st);
    return 0;
}

/* ---- 结果 ---- */

typedef struct { SearchHit h; double score; } Cand;

/* 排序键（越小越靠前）：
 *   有中心：真实距离 / (1 + weight*0.05)
 *     纯按距离排会把"XX村公厕"顶到"中关村"前面；纯按权重排又会把远处的大城
 *     拽到眼前。给权重一个温和折扣，距离仍主导。
 *   无中心：1000 / (1 + weight)（同样是"越小越靠前"）
 *   ★ 两者都必须是正数，下面还要除以"匹配质量"，负数会翻向。
 * 再乘长度惩罚、除匹配质量：全等 > 前缀 > 包含，名字比关键词长太多也要降权
 * （搜"中关村"时"中关村软件园24号楼"不该压过"中关村"本体）。 */
static int cmp_cand(const void *pa, const void *pb)
{
    const Cand *a = (const Cand *)pa, *b = (const Cand *)pb;
    if (a->score < b->score) return -1;
    if (a->score > b->score) return 1;
    if (a->h.weight != b->h.weight) return b->h.weight - a->h.weight;
    return strcmp(a->h.name, b->h.name);
}

/* strstr 的大小写不敏感版（只对 ASCII 生效；UTF-8 的多字节字节值都 >=0x80，
   tolower 不动它们，所以安全） */
static int contains_ci(const char *hay, const char *needle)
{
    if (strstr(hay, needle)) return 1;
    size_t n = strlen(hay), m = strlen(needle);
    if (m == 0 || m > n) return 0;
    for (size_t i = 0; i + m <= n; i++) {
        size_t k = 0;
        for (; k < m; k++) {
            int ca = (unsigned char)hay[i + k], cb = (unsigned char)needle[k];
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
        }
        if (k == m) return 1;
    }
    return 0;
}

SearchDb *search_open(const char *db_path)
{
    if (!db_path) return NULL;
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return NULL;
    }
    SearchDb *d = calloc(1, sizeof(*d));
    if (!d) { sqlite3_close(db); return NULL; }
    d->db = db;
    sqlite3_prepare_v2(db, "SELECT name,lat,lon,kind,weight FROM place WHERE id=?",
                       -1, &d->st_one, NULL);
    return d;
}

void search_close(SearchDb *d)
{
    if (!d) return;
    if (d->st_one) sqlite3_finalize(d->st_one);
    if (d->db) sqlite3_close(d->db);
    free(d);
}

int search_query(SearchDb *d, const char *q,
                 int have_center, double clat, double clon,
                 double max_dist_m, int kind_max, int limit,
                 SearchHit *out, int cap, int *n_out, uint64_t *elapsed_ms)
{
    if (n_out) *n_out = 0;
    if (elapsed_ms) *elapsed_ms = 0;
    if (!d || !out || cap <= 0) return -1;
    if (!q || !*q) return 0;                 /* 空串：0 条，不算错 */

    clock_t t0 = clock();

    /* ★ 查询与建库共用一份地址规范化：用户输入「建国路 88 号」与库里
       「建国路88」的 bigram / 子串复核才能对上。普通地名是恒等变换，
       旧查询结果逐条不变（负向对照）。 */
    char qn[256];
    search_normalize_addr(q, qn, sizeof(qn));
    if (!*qn) return 0;

    /* ★ 门牌（kind 14）只在查询串含数字时参与召回：用户搜「中关村」要的是
       地名，不是「中关村大街27」；门牌行进的是同一张 gram 表，不加门控会
       挤占候选并把精确同名地名压到后面（11-11 实测 search_cli 断言变红）。
       门牌查询必然带号码数字 ⇒ 这条门控不影响任何门牌搜索，且让不含数字的
       普通查询与改动前逐条一致（负向对照）。14 与 places_build.c 的
       PLK_ADDRESS 双处锚定（search 层不能 include 建库端头）。 */
    int q_has_digit = 0;
    for (const char *p = qn; *p; p++)
        if (*p >= '0' && *p <= '9') { q_has_digit = 1; break; }

    char grams[MAX_GRAMS][GRAM_BYTES];
    int nchars = 0;
    int ng = search_bigrams(qn, grams, MAX_GRAMS, &nchars);

    IdList ids = { 0 };
    if (ng > 0) {
        ids_by_grams(d, grams, ng, &ids);
    } else {
        /* 单字（或空）：先走 gram 前缀，没结果再全表兜底 */
        ids_by_prefix(d, qn, &ids);
        if (ids.n == 0) {
            free(ids.v);
            ids.v = NULL; ids.n = ids.cap = 0;
            ids_by_scan(d, qn, &ids);
        }
    }

    int lim = limit > 0 ? limit : LIMIT_DEF;
    if (lim > LIMIT_MAX) lim = LIMIT_MAX;
    if (lim > cap) lim = cap;

    Cand *cand = malloc((size_t)(ids.n > 0 ? ids.n : 1) * sizeof(Cand));
    int nc = 0;
    if (cand && d->st_one) {
        for (int i = 0; i < ids.n; i++) {
            sqlite3_reset(d->st_one);
            sqlite3_bind_int64(d->st_one, 1, (sqlite3_int64)ids.v[i]);
            if (sqlite3_step(d->st_one) != SQLITE_ROW) continue;
            const unsigned char *nm = sqlite3_column_text(d->st_one, 0);
            if (!nm) continue;

            SearchHit h;
            snprintf(h.name, sizeof(h.name), "%s", (const char *)nm);
            h.lat    = sqlite3_column_double(d->st_one, 1);
            h.lon    = sqlite3_column_double(d->st_one, 2);
            h.kind   = sqlite3_column_int(d->st_one, 3);
            h.weight = sqlite3_column_int(d->st_one, 4);
            h.dist_m = -1;

            /* ★ 子串复核：gram 全中不等于连续 */
            if (!contains_ci(h.name, qn)) continue;
            if (kind_max > 0 && h.kind > kind_max) continue;
            if (h.kind >= 14 && !q_has_digit) continue;

            double dist = -1;
            if (have_center) {
                dist = sk_haversine_m(clat, clon, h.lat, h.lon);
                if (max_dist_m > 0 && dist > max_dist_m) continue;
                h.dist_m = dist;
            }
            double base = have_center ? dist / (1.0 + h.weight * 0.05)
                                      : 1000.0 / (1.0 + (double)h.weight);
            int exact  = (strcmp(h.name, qn) == 0);
            int prefix = !exact && (strncmp(h.name, qn, strlen(qn)) == 0);
            double qm   = exact ? 3.0 : (prefix ? 2.0 : 1.0);
            int extra   = (int)strlen(h.name) - (int)strlen(qn);
            if (extra < 0) extra = 0;
            h.score = base * (1.0 + extra * 0.03) / qm;
            cand[nc].h = h;
            cand[nc].score = h.score;
            nc++;
        }
    }
    free(ids.v);

    if (nc > 1) qsort(cand, (size_t)nc, sizeof(Cand), cmp_cand);
    int n = nc < lim ? nc : lim;
    for (int i = 0; i < n; i++) out[i] = cand[i].h;
    free(cand);

    if (n_out) *n_out = n;
    if (elapsed_ms)
        *elapsed_ms = (uint64_t)((double)(clock() - t0) / CLOCKS_PER_SEC * 1000);
    return 0;
}
