#include "route/profile.h"

#include "map/road_tags.h"   /* RC_*, EF_FERRY */

/* 出行方式 profile（2026-11-12，L2 本地能力）
 *
 * ★★ 范围声明（别把它写成"完整步行导航"）：
 *   当前 graph 只收录 road_tags.c 里 drivable=1 的道路；footway / cycleway /
 *   steps / pedestrian 是 drivable=0，只进 lines.bin 渲染、**不进路网**。
 *   让它们参与算路需要改 road_tags.c 并重抽 8 省 PBF（构建期数据变更），
 *   本轮不动数据 ⇒ 步行/骑行 profile 是在「现有机动车路网」上换速度口径与
 *   硬排除规则。这是明确的 L2 边界，不是遗漏。
 *
 * ★★ 缺省档 RP_CAR 必须与原行为逐字节一致：
 *   route_profile_speed_kmh(RP_CAR, ..., stored) 原样返回 stored，
 *   route_profile_allowed(RP_CAR, ...) 恒 1。所有新增分支都从 clamp 之后
 *   的 if 里走，CAR 不经过任何浮点运算 ⇒ 负向对照锚点成立。 */

/* 下表按 RC_* 下标：0 高速/国道，1 主干，2 次干，3 支路，4 其他，
 * 5 步行道（当前不在路网里，留档位），6/7 铁路/水系（永不进路网）。 */
static const double k_bike_cap_kmh[8]   = { 0.0, 18.0, 17.0, 15.0, 12.0, 14.0, 0.0, 0.0 };
static const double k_foot_speed_kmh[8] = { 0.0,  5.0,  5.0,  5.0,  4.5,  4.0, 0.0, 0.0 };
static const double k_cruise_kmh[RP_COUNT] = { 60.0, 16.0, 5.0 };

static int cls_index(uint8_t cls)
{
    return cls < 8 ? (int)cls : (int)RC_OTHER;
}

const char *route_profile_name(int profile)
{
    switch (route_profile_clamp(profile)) {
    case RP_BIKE: return "骑行";
    case RP_FOOT: return "步行";
    default:      return "驾车";
    }
}

int route_profile_clamp(int v)
{
    return (v >= 0 && v < RP_COUNT) ? v : RP_CAR;
}

double route_profile_cruise_kmh(int profile)
{
    return k_cruise_kmh[route_profile_clamp(profile)];
}

/* 有效速度（km/h）。stored 是调用方已做完 fallback_speed 的 EF_SPEED。
 * 返回 0 = 该等级在此 profile 下不可通行；可达性的正式判据在
 * route_profile_allowed（边展开处硬排除），这里只是不返回非法速度。 */
double route_profile_speed_kmh(int profile, uint8_t cls, double stored)
{
    int p = route_profile_clamp(profile);
    if (p == RP_CAR) return stored;

    int i = cls_index(cls);
    if (p == RP_BIKE) {
        double cap = k_bike_cap_kmh[i];
        if (cap <= 0.0) return 0.0;
        /* 慢路仍按自己的限速（如 living_street 10 km/h），快路钳到骑行上限。
           ★ 用 min 而不是恒定值：否则"走小路"和"走主干"对骑行毫无差别，
             算路会失去区分度（这也是负向对照能抓到的点）。 */
        return (stored > 0.0 && stored < cap) ? stored : cap;
    }
    return k_foot_speed_kmh[i];
}

/* 可达性硬排除。★ 只在 route.c 的**边展开**（正向 + 反向）调用：
 * 不碰 lmdist 建表、不碰 rev.bin —— 那两个是构建期共享产物。 */
int route_profile_allowed(int profile, uint8_t cls, uint16_t flags)
{
    int p = route_profile_clamp(profile);
    if (p == RP_CAR) return 1;                 /* 缺省档逐位不变 */

    if (cls == RC_MOTORWAY) return 0;          /* 高速/国道：非机动车禁行 */
    if (p == RP_FOOT && (flags & EF_FERRY)) return 0;  /* 步行不坐轮渡 */
    return 1;
}
