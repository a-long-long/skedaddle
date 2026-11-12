#include "route/cost.h"
#include "route/profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 代价 / 启发式 / 地标表辅助：原 route.c 的"代价与启发式"段与生命周期辅助，
 * 整段迁来，行为未改。
 * 注意 fallback_speed 除本书件的 edge_cost 外，还被 route.c 的结果回填
 * （逐段耗时 seg_ds）与 route_check 校验调用，因此必须是导出的而非 static。 */

/* ------------------------ 代价与启发式 ------------------------ */

uint8_t fallback_speed(uint8_t cls)
{
    switch (cls) {
    case RC_MOTORWAY:    return 100;
    case RC_PRIMARY:     return 60;
    case RC_TERTIARY:    return 40;
    case RC_RESIDENTIAL: return 30;
    case RC_OTHER:       return 20;
    default:             return 20;
    }
}

double edge_speed_kmh(const Router *r, const uint8_t *er)
{
    double stored = (double)er[EF_SPEED];
    if (stored <= 0.0) stored = (double)fallback_speed(er[EF_CLASS]);
    double eff = route_profile_speed_kmh(r->opt.profile, er[EF_CLASS], stored);
    /* 不可通行的边本应在边展开处被硬排除；真漏到这里也绝不返回 0（除零）。
       给 1 km/h 让它的代价大到不可能被选中，同时保持搜索不会静默算错。 */
    return eff > 0.0 ? eff : 1.0;
}

double edge_cost(const Router *r, const uint8_t *er, double *meters)
{
    double m = (double)sk_get_u32(er + EF_DIST_CM) / 100.0;
    if (meters) *meters = m;
    if (!r->opt.mode_time) return m;
    /* ★ 2026-11-12：口径从「uint8_t sp」换成 profile 的有效速度。
       RP_CAR 时 route_profile_speed_kmh 原样返回 stored ⇒ (double)sp 完全相同，
       这条除法与改动前逐字节一致（regress 的全部算路判据都是负向对照锚点）。 */
    return m * 3.6 / edge_speed_kmh(r, er);
}

/* 搜索专用代价：在物理代价上叠加"路线偏好"软惩罚。
 *
 * ★★ 只允许 route.c 的**边展开**调用（正向 + 反向）。三个绝对不能用的地方：
 *   1) lmdist.c 建地标表 —— lm.bin 是全图共享的物理距离表，掺进偏好会让
 *      h 随用户设置漂移，表与当前设置不匹配；
 *   2) route.c 结果回填 —— 它取 meters 后按 EF_SPEED 重算 seg_ds，
 *      total_s 必须保持"这条路实际要开多久"（result.c 有 Σseg_ds==total_s 自检）；
 *   3) navsim / ETA 显示 —— 同理。
 *   反过来，h 是物理时间下界，而 g 被放大 ⇒ admissible 依旧成立。
 * ★ 三个偏好全不是 1 档时直接返回 edge_cost 的**原值**（不乘 1.0），
 *   保证缺省配置下搜索展开数、路线与逐字节出图与改动前完全相同。 */
double edge_cost_pref(const Router *r, const uint8_t *er, double *meters)
{
    double w = edge_cost(r, er, meters);
    const RouteOpts *o = &r->opt;
    if (o->pref_toll != 1 && o->pref_ferry != 1 && o->pref_unpaved != 1)
        return w;
    uint16_t fl = sk_get_u16(er + EF_FLAGS);
    double pen = (o->pref_add > 0.0) ? o->pref_add : ROUTE_PREF_ADD;
    double add = 0.0;
    if (o->pref_toll == 1 && (fl & EF_TOLL))         add += pen;
    if (o->pref_ferry == 1 && (fl & EF_FERRY))       add += pen;
    if (o->pref_unpaved == 1 && (fl & EF_UNPAVED))   add += pen;
    return add > 0.0 ? w * (1.0 + add) : w;
}

/* 大圆距离：sk_fast_dist_m 是等距投影近似，跨纬度大时会失真，
   一旦高估就破坏 admissible（A* 会给出次优解）。
   正向搜索朝终点估，反向搜索朝起点估（两边必须各自 admissible）。

   再与**地标下界**取 max（PLAN.md §48/§49）：
       d(u→t) ≥ d(L→t) − d(L→u) ⇒ 表给出的是米，按 H_SPEED_KMH 换成秒后
   与直线同口径比较。表是可选的：没加载/该侧不在网格内就当它不存在。
   ★ 单位必须一致：mode_time=0 时下面两条都是米，mode_time=1 时都是秒。 */
double heur(const Router *r, int dir, int32_t lat_e7, int32_t lon_e7)
{
    double d = sk_haversine_m((double)lat_e7 / 1e7, (double)lon_e7 / 1e7,
                              (double)r->tgt_lat[dir] / 1e7,
                              (double)r->tgt_lon[dir] / 1e7);
    if (r->lm.loaded && r->lm_tc[dir] != (size_t)-1) {
        double b = lm_bound_m(&r->lm, r->lm_tc[dir], lat_e7, lon_e7);
        if (b > d) d = b;
    }
    return r->opt.mode_time ? d * 3.6 / H_SPEED_KMH : d;
}

uint64_t node_coord(const MapSrc *s, uint32_t node)
{
    const uint8_t *nr = s->nodes.data + (uint64_t)node * SK_NODE_BYTES;
    return coord_key_e7(sk_get_i32(nr + NF_LAT), sk_get_i32(nr + NF_LON));
}

/* 地标表开关：默认开；`SKEDADDLE_LM=0` 关掉（A/B 用，也用于现场退回纯直线）。
   ★ 写成"只查一次"的函数：environ 在本机有 178 项，热路径上一次 getenv 135 ns。 */
int lm_enabled(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("SKEDADDLE_LM");
        v = !(e && e[0] == '0' && e[1] == '\0');
    }
    return v;
}

/* 【诊断用】在表自带的 penalty 上再加 N 米。默认 0 = 不改行为。
 * ★ 为什么留这个旋钮：§52.8 发现下界会轻微越界，判据是"开表 w=1.0 的预计分钟
 *   必须等于无表 w=1.0"。要定位"到底需要多大余量"就得能现场扫 penalty，
 *   否则每试一个值都要重建一次 30 地标的表（249 秒）。
 * ★ 只读一次（getenv 在热路径上一次 135 ns，见 §29）。 */
uint32_t lm_penalty_add_m(void)
{
    static long v = -1;
    if (v < 0) {
        const char *e = getenv("SKEDADDLE_LM_PENALTY_ADD_M");
        v = e ? atol(e) : 0;
        if (v < 0) v = 0;
        if (v > 1000000) v = 1000000;
    }
    return (uint32_t)v;
}

/* 图目录 = 省目录的上一级（products/graph/beijing → products/graph）。
   lm.bin 与 rev.bin 不同：**全图只有一份**，写在父目录里。 */
int graph_root_dir(const MapSet *ms, char *out, size_t cap)
{
    if (!ms || ms->n == 0 || !ms->srcs[0].dir[0]) return -1;
    const char *d = ms->srcs[0].dir;
    const char *slash = strrchr(d, '/');
    size_t n;
    if (!slash) {                        /* 相对的单层目录 ⇒ 父目录是 "." */
        if (cap < 2) return -1;
        out[0] = '.'; out[1] = '\0';
        return 0;
    }
    n = (size_t)(slash - d);
    if (n == 0) n = 1;                   /* "/beijing" ⇒ 父目录是 "/" */
    if (n >= cap) return -1;
    memcpy(out, d, n);
    out[n] = '\0';
    return 0;
}
