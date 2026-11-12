#ifndef SKEDADDLE_ROUTE_PROFILE_H
#define SKEDADDLE_ROUTE_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 出行方式 profile（2026-11-12，L2 本地能力）
 *
 * 项目此前只有一份车行代价（cost.c:edge_cost 读 EF_SPEED）。这里补骑行 / 步行：
 *   RP_CAR  = 0  驾车：完全沿用 stored speed，行为与改动前逐字节一致（负向对照锚点）
 *   RP_BIKE = 1  骑行：按道路等级给速度，高速禁行
 *   RP_FOOT = 2  步行：恒速 5 km/h，高速禁行，允许人行道 / 台阶
 *
 * ★★ 三条不可破的约束（违反会静默算错，且现有判据抓不到）：
 *   1) h 仍是物理时间下界：heur() 用 H_SPEED_KMH=130 换算直线/地标距离。
 *      降低 profile 速度只会让 g 变大、h 不变 ⇒ admissible 依旧成立，
 *      **lm.bin 是全图共享的物理距离表，绝不重算、绝不受 profile 影响**。
 *      代价是 h 相对变松，展开数会上升（这是正确性的价格，不是性能回归）。
 *   2) ETA 必须同步：seg_ds / total_s 在 route.c 结果回填里按 EF_SPEED 算。
 *      若只改搜索代价而不改回填，步行会显示驾车 ETA ⇒ 必须共用
 *      cost.c:edge_speed_kmh() 这一个速度入口（result.c 的 route_check
 *      也用它，否则 Σseg_ds==total_s 自检会炸）。
 *   3) 可达性过滤只在「边展开」硬排除，且与旧 avoid_* / pref_*=2 并列，
 *      不碰 lmdist 建表（同上，表是构建期产物）。 */

typedef enum {
    RP_CAR  = 0,
    RP_BIKE = 1,
    RP_FOOT = 2,
    RP_COUNT
} RouteProfile;

/* 名称（中文，给设置面板 / 日志）。越界返回 "驾车"。 */
const char *route_profile_name(int profile);

/* 该 profile 下这条边的有效速度（km/h）。
 * stored 是 EF_SPEED（0 时由调用方先填 fallback_speed）。
 * 返回 0 表示这条边**不可通行**（由 route_profile_allowed 统一判定，这里只管速度）。 */
double route_profile_speed_kmh(int profile, uint8_t cls, double stored);

/* 可达性：1 = 允许走，0 = 硬排除。
 * cls 是 EF_CLASS（RC_*），flags 是 EF_FLAGS（u16）。
 * RP_CAR 恒返回 1 —— 缺省档必须与原行为逐位一致。 */
int route_profile_allowed(int profile, uint8_t cls, uint16_t flags);

/* 规范化：越界值钳到 RP_CAR（防止 ini 里写脏值让路由静默走错档）。 */
int route_profile_clamp(int v);

/* 巡航速度：给 UI 显示「预计 X 分钟」之外的参考，也给自检做数值断言。 */
double route_profile_cruise_kmh(int profile);

#ifdef __cplusplus
}
#endif
#endif /* SKEDADDLE_ROUTE_PROFILE_H */
