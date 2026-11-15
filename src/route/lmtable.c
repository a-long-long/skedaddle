#include "route/lmtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "utils/plat.h"

size_t lm_cell_of(const LmTable *t, int32_t lat_e7, int32_t lon_e7)
{
    if (!t->loaded) return (size_t)-1;
    int64_t r = ((int64_t)lat_e7 - (int64_t)t->h.lat0_e7) / (int64_t)t->h.cell_e7;
    int64_t c = ((int64_t)lon_e7 - (int64_t)t->h.lon0_e7) / (int64_t)t->h.cell_e7;
    if (r < 0 || c < 0) return (size_t)-1;
    if ((uint64_t)r >= t->h.nrow || (uint64_t)c >= t->h.ncol) return (size_t)-1;
    return (size_t)r * t->h.ncol + (size_t)c;
}

double lm_bound_m(const LmTable *t, size_t tc, int32_t lat_e7, int32_t lon_e7)
{
    if (!t->loaded || tc == (size_t)-1) return -1.0;
    size_t uc = lm_cell_of(t, lat_e7, lon_e7);
    if (uc == (size_t)-1) return -1.0;

    uint32_t nlm = t->h.nlm;
    const uint16_t *tb = t->v + tc * nlm;
    const uint16_t *ub = t->v + uc * nlm;
    int best = -1;
    for (uint32_t k = 0; k < nlm; k++) {
        int a = tb[k], b = ub[k];
        if (a == (int)LM_NONE || b == (int)LM_NONE) continue;
        int d = a - b;
        if (d > best) best = d;
    }
    if (best < 0) return -1.0;
    double m = (double)best * (double)t->h.unit_m - (double)t->h.penalty_m;
    return m > 0.0 ? m : 0.0;
}

int lm_table_init(LmTable *t, int32_t lat0_e7, int32_t lon0_e7, uint32_t cell_e7,
                  uint32_t nrow, uint32_t ncol, uint32_t nlm, uint32_t penalty_m,
                  uint32_t nprov, uint64_t total_nodes)
{
    if (!t || cell_e7 == 0 || nrow == 0 || ncol == 0 || nlm == 0) return -1;
    /* 先做尺寸/乘法检查：nrow*ncol*nlm 在 32 位宿主或恶意参数下会溢出，
     * 溢出后的 malloc 很小，但后续 lm_cell_of/lm_bound_m 仍按原维度索引。 */
    if (nrow > SIZE_MAX / ncol) return -1;
    size_t ncell = (size_t)nrow * (size_t)ncol;
    if (nlm > SIZE_MAX / ncell) return -1;
    size_t nvals = ncell * (size_t)nlm;
    if (nvals > SIZE_MAX / sizeof(uint16_t)) return -1;
    memset(t, 0, sizeof(*t));
    t->h.magic = LM_MAGIC;
    t->h.version = LM_VERSION;
    t->h.lat0_e7 = lat0_e7;
    t->h.lon0_e7 = lon0_e7;
    t->h.cell_e7 = cell_e7;
    t->h.nrow = nrow;
    t->h.ncol = ncol;
    t->h.nlm = nlm;
    t->h.unit_m = LM_UNIT_M;
    t->h.penalty_m = penalty_m;
    t->h.nprov = nprov;
    t->h.total_nodes = total_nodes;
    t->ncell = ncell;
    t->v = malloc(nvals * sizeof(uint16_t));
    if (!t->v) return -1;
    for (size_t i = 0; i < nvals; i++) t->v[i] = LM_NONE;
    t->loaded = 1;
    return 0;
}

void lm_table_free(LmTable *t)
{
    if (!t) return;
    free(t->v);
    memset(t, 0, sizeof(*t));
}

static int wr(const void *p, size_t n, FILE *f)
{
    return fwrite(p, 1, n, f) == n ? 0 : -1;
}

int lm_table_save(const LmTable *t, const char *path)
{
    if (!t || !t->loaded || !t->v || !path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (wr(&t->h, sizeof(t->h), f) != 0) { fclose(f); return -1; }
    if (wr(t->v, t->ncell * (size_t)t->h.nlm * sizeof(uint16_t), f) != 0) {
        fclose(f);
        return -1;
    }
    /* 关闭失败同样算写坏：数据可能还在 stdio 缓冲里没落盘。 */
    if (fclose(f) != 0) return -1;
    return 0;
}

int lm_table_match(const LmTable *t, uint32_t nprov, uint64_t total_nodes)
{
    if (!t || !t->loaded) return 0;
    return (t->h.nprov == nprov && t->h.total_nodes == total_nodes) ? 1 : 0;
}

int lm_table_load(LmTable *t, const char *dir)
{
    if (!t || !dir) return -1;
    memset(t, 0, sizeof(*t));
    char path[4096 + 32];
    int n = snprintf(path, sizeof(path), "%s/lm.bin", dir);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    uint64_t fsize = 0;
    if (sk_stat_file(path, &fsize, NULL) != 0 || fsize < sizeof(LmHeader)) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    LmHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) { fclose(f); return -1; }
    if (h.magic != LM_MAGIC || h.version != LM_VERSION || h.cell_e7 == 0 ||
        h.nrow == 0 || h.ncol == 0 || h.nlm == 0 || h.unit_m != LM_UNIT_M) {
        fclose(f);
        return -1;
    }
    /* 维度乘法全部先检查；文件长度必须精确等于“头 + 表体”。否则 malloc 可能
     * 因溢出变小，而 lm_cell_of/lm_bound_m 仍按头里的维度索引。 */
    if (h.nrow > SIZE_MAX / h.ncol) { fclose(f); return -1; }
    size_t ncell = (size_t)h.nrow * (size_t)h.ncol;
    if (h.nlm > SIZE_MAX / ncell) { fclose(f); return -1; }
    size_t nvals = ncell * (size_t)h.nlm;
    if (nvals > SIZE_MAX / sizeof(uint16_t)) { fclose(f); return -1; }
    size_t need = nvals * sizeof(uint16_t);
    if (fsize - sizeof(LmHeader) != need) { fclose(f); return -1; }
    uint16_t *v = malloc(need);
    if (!v) { fclose(f); return -1; }
    if (fread(v, 1, need, f) != need) { free(v); fclose(f); return -1; }
    if (fclose(f) != 0) { free(v); return -1; }
    t->h = h;
    t->v = v;
    t->ncell = ncell;
    t->loaded = 1;
    return 0;
}
