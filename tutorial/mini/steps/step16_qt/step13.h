/* tutorial/mini · 第 13 课新增：大地基准（datum）——只换椭球 ≠ 基准转换
 *
 * 对照 src/utils/datum.c。结论与项目一致：CGCS2000→WGS84 平移 0；西安 1980
 * 用全国三参数 (−104.57, −45.94, +5.30)；北京 1954 用 EPSG:4214 的 TOWGS84
 * （本项目不自行落具体数值，只登记「有参数」）。
 */
#ifndef MINI_STEP13_H
#define MINI_STEP13_H

#include <string.h>

typedef struct {
    const char *name;
    double a, invf;
    double dx, dy, dz;
    int has_towgs84;
} MiniDatum;

static const MiniDatum g_mini_datums[] = {
    { "CGCS2000",    6378137.0, 298.257222101,   0.0,    0.0,   0.0, 0 },
    { "WGS84",       6378137.0, 298.257223563,   0.0,    0.0,   0.0, 0 },
    { "Beijing1954", 6378245.0, 298.3,           0.0,    0.0,   0.0, 1 },
    { "Xian1980",    6378140.0, 298.257,      -104.57, -45.94,  5.30, 0 }
};
#define MINI_DATUMS ((int)(sizeof g_mini_datums / sizeof g_mini_datums[0]))

static inline const MiniDatum *mini_datum_find(const char *name) {
    for (int i = 0; i < MINI_DATUMS; i++)
        if (strcmp(g_mini_datums[i].name, name) == 0) return &g_mini_datums[i];
    return NULL;
}

static inline void mini_datum_shift(const MiniDatum *d,
                                    double x, double y, double z,
                                    double *ox, double *oy, double *oz) {
    *ox = x + d->dx; *oy = y + d->dy; *oz = z + d->dz;
}

static void mini_step_13(MiniCtx *c) {
    const MiniDatum *cg = mini_datum_find("CGCS2000");
    const MiniDatum *wg = mini_datum_find("WGS84");
    const MiniDatum *bj = mini_datum_find("Beijing1954");
    const MiniDatum *xa = mini_datum_find("Xian1980");
    double ox = 0, oy = 0, oz = 0;
    mini_datum_shift(cg, 100.0, 200.0, 30.0, &ox, &oy, &oz);
    MINI_ASSERT(c, cg && wg && bj && xa, "step13: 四个基准都登记在册");
    MINI_ASSERT(c, ox == 100.0 && oy == 200.0 && oz == 30.0,
                "step13: CGCS2000 平移为 0");
    MINI_ASSERT(c, cg->a == wg->a && cg->invf != wg->invf,
                "step13: 长半轴相同但扁率不同 ⇒ 只换椭球不等于基准转换");
    MINI_ASSERT(c, xa->dx == -104.57 && xa->dy == -45.94 && xa->dz == 5.30
                && bj->has_towgs84 == 1,
                "step13: 西安80 三参数 + 北京54 有 TOWGS84");
    /* 追加：把「平移前 / 平移后」画成两个点（偏移量按像素缩放示意） */
    mini_fill_rect(c, 270, 200, 5, 5, 0x004080C0u);
    mini_fill_rect(c, (int)(270 + xa->dx / 8), (int)(200 - xa->dy / 8),
                   5, 5, 0x00D04040u);
    printf("MINI-STEP-13-OK  datums=%d ink=%ld\n", MINI_DATUMS, mini_ink(c));
    c->step_count++;
}

#endif /* MINI_STEP13_H */
