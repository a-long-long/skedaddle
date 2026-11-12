#include "search/history.h"

#include "utils/plat.h"   /* sk_config_dir / sk_mkdir_p */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 落盘格式（每行一条，制表符分隔，字段数固定）：
 *     ts \t lat \t lon \t kind \t name
 * ★ 用文本而不是二进制：用户能直接看/删，跨机器无字节序问题，坏行可跳过。
 * ★ name 里的 \t / \r / \n 在 history_add 时就剔掉，落盘不会被切错列。 */

void history_init(History *h, const char *path)
{
    if (!h) return;
    memset(h, 0, sizeof(*h));
    if (path && *path) {
        snprintf(h->path, sizeof(h->path), "%s", path);
    } else {
        const char *d = sk_config_dir();
        snprintf(h->path, sizeof(h->path), "%s/history.txt", d ? d : ".");
    }
}

void history_clear(History *h)
{
    if (!h) return;
    memset(h->v, 0, sizeof(h->v));
    h->n = 0;
}

static int same_place(const HistoryItem *a, const char *name, double lat, double lon)
{
    return strcmp(a->name, name) == 0 &&
           fabs(a->lat - lat) < 1e-5 && fabs(a->lon - lon) < 1e-5;
}

int history_add(History *h, const char *name, double lat, double lon,
                int kind, uint64_t ts)
{
    if (!h || !name || !*name) return -1;
    char clean[HISTORY_NAME];
    size_t o = 0;
    for (const char *p = name; *p && o + 1 < sizeof(clean); p++) {
        if (*p == '\t' || *p == '\r' || *p == '\n') continue;
        clean[o++] = *p;
    }
    clean[o] = '\0';
    if (!o) return -1;

    int hit = -1;
    for (int i = 0; i < h->n; i++)
        if (same_place(&h->v[i], clean, lat, lon)) { hit = i; break; }

    int from = (hit >= 0) ? hit : h->n;
    if (hit < 0) {
        if (h->n < HISTORY_MAX) h->n++;
        from = h->n - 1;
    }
    /* 把 from 之前的整体后移一格，再把新值塞到队首（MRU）。 */
    for (int i = from; i > 0; i--) h->v[i] = h->v[i - 1];
    snprintf(h->v[0].name, sizeof(h->v[0].name), "%s", clean);
    h->v[0].lat = lat;
    h->v[0].lon = lon;
    h->v[0].kind = kind;
    h->v[0].ts = ts;
    return 0;
}

int history_load(History *h)
{
    if (!h || !*h->path) return -1;
    FILE *f = fopen(h->path, "rb");
    if (!f) { h->n = 0; return -1; }
    char line[512];
    int bad = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long ts_ll = 0;
        double lat = 0, lon = 0;
        int kind = 0;
        char name[HISTORY_NAME] = { 0 };
        /* 用扫描集收尾（name 里没有 \t，前面 4 列用 \t 切开）。
           ★ 先读进 unsigned long long 再赋给 uint64_t：直接把 &uint64_t
           强转成 ULL* 在 uint64_t 是 unsigned long 的平台上属于类型双关。 */
        if (sscanf(line, "%llu\t%lf\t%lf\t%d\t%95[^\r\n]",
                   &ts_ll, &lat, &lon, &kind, name) != 5 ||
            !*name || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
            bad++;
            continue;
        }
        uint64_t ts = (uint64_t)ts_ll;
        /* 直接写数组而不是走 history_add：文件已经是 MRU 顺序，
           再 add 会按 ts 之外的规则重排，且同名去重语义不同。 */
        if (h->n >= HISTORY_MAX) break;
        snprintf(h->v[h->n].name, sizeof(h->v[h->n].name), "%s", name);
        h->v[h->n].lat = lat;
        h->v[h->n].lon = lon;
        h->v[h->n].kind = kind;
        h->v[h->n].ts = ts;
        h->n++;
    }
    fclose(f);
    return bad == 0 ? 0 : -1;
}

int history_save(const History *h)
{
    if (!h || !*h->path) return -1;
    /* path 可能是用户给的带目录路径；建目录失败不致命（OPEN 会再报错）。 */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", h->path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) { *slash = '\0'; sk_mkdir_p(dir); }

    FILE *f = fopen(h->path, "wb");
    if (!f) return -1;
    for (int i = 0; i < h->n; i++)
        fprintf(f, "%llu\t%.7f\t%.7f\t%d\t%s\n",
                (unsigned long long)h->v[i].ts,
                h->v[i].lat, h->v[i].lon, h->v[i].kind, h->v[i].name);
    fclose(f);
    return 0;
}

int history_format(const History *h, int i, char *out, size_t cap)
{
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    if (!h || i < 0 || i >= h->n) return -1;
    snprintf(out, cap, "%s · %.5f,%.5f", h->v[i].name, h->v[i].lat, h->v[i].lon);
    return 0;
}
