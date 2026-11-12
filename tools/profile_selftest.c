/* skedaddle_profile_selftest —— 出行方式 profile 的确定性判据 + 负向对照
 *
 * 正常模式：
 *   A) 纯函数（clamp / name / speed / allowed / cruise）逐值比对；
 *   B) 用一条**手工构造的边**直接调 cost.c:edge_cost / edge_speed_kmh，
 *      验证「搜索代价 g 真的随 profile 变、meters 不变、RP_CAR 逐位不变」。
 *      ★ 不依赖任何地图产物，跨机器可重跑。
 *   ⇒ PROFILE-OK，rc=0。
 *
 * --inject=nowiring：把三个 profile 的成本计算统统按 RP_CAR 跑
 *   （模拟"接线被删掉、profile 字段没传到 cost.c"这个真实失效模式），
 *   合规的判据必须抓到差异 ⇒ PROFILE-INJECT-CAUGHT / rc=0；
 *   没抓到则 rc=1 让门禁变红。
 *
 * 注意：日志里不要出现 "ASSERT-FAIL" 字样（regress.sh 的 run 会当失败）。
 */

#include "route/route_priv.h"
#include "route/cost.h"
#include "route/profile.h"
#include "map/binfmt.h"
#include "map/road_tags.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fails;

static void eqi(const char *tag, int got, int want)
{
    if (got == want) return;
    printf("MISMATCH %-26s 得到[%d] 期望[%d]\n", tag, got, want);
    g_fails++;
}

static void eqs(const char *tag, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) return;
    printf("MISMATCH %-26s 得到[%s] 期望[%s]\n", tag, got, want);
    g_fails++;
}

static void eqd(const char *tag, double got, double want, double tol)
{
    if (fabs(got - want) <= tol) return;
    printf("MISMATCH %-26s 得到[%.6f] 期望[%.6f]\n", tag, got, want);
    g_fails++;
}

/* 手工构造一条 1000 m 的主干道（EF_SPEED=70，无特殊标志）。
   sk_get_u32 是小端读，这里按字节写，避免依赖任何产物。 */
static void make_edge(uint8_t er[SK_EDGE_BYTES], uint8_t cls, uint8_t spd,
                      uint16_t flags, uint32_t dcm)
{
    memset(er, 0, SK_EDGE_BYTES);
    er[EF_TO + 0] = 0; er[EF_TO + 1] = 0; er[EF_TO + 2] = 0; er[EF_TO + 3] = 0;
    er[EF_DIST_CM + 0] = (uint8_t)(dcm & 0xFF);
    er[EF_DIST_CM + 1] = (uint8_t)((dcm >> 8) & 0xFF);
    er[EF_DIST_CM + 2] = (uint8_t)((dcm >> 16) & 0xFF);
    er[EF_DIST_CM + 3] = (uint8_t)((dcm >> 24) & 0xFF);
    er[EF_FLAGS + 0] = (uint8_t)(flags & 0xFF);
    er[EF_FLAGS + 1] = (uint8_t)((flags >> 8) & 0xFF);
    er[EF_CLASS] = cls;
    er[EF_SPEED] = spd;
}

static Router *make_router(int mode_time, int profile)
{
    Router *r = (Router *)calloc(1, sizeof(Router));
    if (!r) return NULL;
    r->opt.mode_time = mode_time;
    r->opt.profile = profile;
    return r;
}

static void check_pure(void)
{
    eqi("clamp -1", route_profile_clamp(-1), RP_CAR);
    eqi("clamp 0",  route_profile_clamp(0),  RP_CAR);
    eqi("clamp 1",  route_profile_clamp(1),  RP_BIKE);
    eqi("clamp 2",  route_profile_clamp(2),  RP_FOOT);
    eqi("clamp 3",  route_profile_clamp(3),  RP_CAR);
    eqi("clamp 99", route_profile_clamp(99), RP_CAR);

    eqs("name car",  route_profile_name(RP_CAR),  "驾车");
    eqs("name bike", route_profile_name(RP_BIKE), "骑行");
    eqs("name foot", route_profile_name(RP_FOOT), "步行");
    eqs("name bad",  route_profile_name(77),      "驾车");

    eqd("cruise car",  route_profile_cruise_kmh(RP_CAR),  60.0, 1e-9);
    eqd("cruise bike", route_profile_cruise_kmh(RP_BIKE), 16.0, 1e-9);
    eqd("cruise foot", route_profile_cruise_kmh(RP_FOOT),  5.0, 1e-9);

    /* RP_CAR：原样返回 stored，一个浮点运算都不许做。 */
    eqd("car speed primary",  route_profile_speed_kmh(RP_CAR, RC_PRIMARY, 70.0), 70.0, 1e-9);
    eqd("car speed resident", route_profile_speed_kmh(RP_CAR, RC_RESIDENTIAL, 30.0), 30.0, 1e-9);
    /* 骑行：快路钳到上限、慢路保留自身限速。 */
    eqd("bike primary",  route_profile_speed_kmh(RP_BIKE, RC_PRIMARY, 70.0), 18.0, 1e-9);
    eqd("bike resident", route_profile_speed_kmh(RP_BIKE, RC_RESIDENTIAL, 30.0), 15.0, 1e-9);
    eqd("bike slow",     route_profile_speed_kmh(RP_BIKE, RC_LIVING, 10.0), 10.0, 1e-9);
    eqd("bike motorway", route_profile_speed_kmh(RP_BIKE, RC_MOTORWAY, 100.0), 0.0, 1e-9);
    /* 步行：与 stored 无关。 */
    eqd("foot primary",  route_profile_speed_kmh(RP_FOOT, RC_PRIMARY, 70.0), 5.0, 1e-9);
    eqd("foot other",    route_profile_speed_kmh(RP_FOOT, RC_OTHER, 20.0), 4.5, 1e-9);
    eqd("foot motorway", route_profile_speed_kmh(RP_FOOT, RC_MOTORWAY, 100.0), 0.0, 1e-9);

    eqi("car allow motorway", route_profile_allowed(RP_CAR, RC_MOTORWAY, EF_FERRY), 1);
    eqi("bike allow primary", route_profile_allowed(RP_BIKE, RC_PRIMARY, 0), 1);
    eqi("bike deny motorway", route_profile_allowed(RP_BIKE, RC_MOTORWAY, 0), 0);
    eqi("bike allow ferry",   route_profile_allowed(RP_BIKE, RC_PRIMARY, EF_FERRY), 1);
    eqi("foot deny motorway", route_profile_allowed(RP_FOOT, RC_MOTORWAY, 0), 0);
    eqi("foot deny ferry",    route_profile_allowed(RP_FOOT, RC_PRIMARY, EF_FERRY), 0);
    eqi("foot allow primary", route_profile_allowed(RP_FOOT, RC_PRIMARY, 0), 1);
}

/* B 段：真调 cost.c。frozen=1 时故意把 profile 抹成 RP_CAR（负向注入）。 */
static void check_cost(int frozen)
{
    uint8_t er[SK_EDGE_BYTES];
    make_edge(er, RC_PRIMARY, 70, 0, 100000);   /* 1000 m */
    const int prof[3] = { RP_CAR, RP_BIKE, RP_FOOT };
    const double want_speed[3] = { 70.0, 18.0, 5.0 };
    const double want_cost[3] = {
        1000.0 * 3.6 / 70.0,
        1000.0 * 3.6 / 18.0,
        1000.0 * 3.6 /  5.0
    };
    for (int i = 0; i < 3; i++) {
        Router *r = make_router(1, frozen ? RP_CAR : prof[i]);
        if (!r) { printf("MISMATCH router 内存分配失败\n"); g_fails++; return; }
        double meters = -1.0;
        double w = edge_cost(r, er, &meters);
        char tag[64];
        snprintf(tag, sizeof(tag), "p%d effective speed", i);
        eqd(tag, edge_speed_kmh(r, er), want_speed[i], 1e-9);
        snprintf(tag, sizeof(tag), "p%d edge cost", i);
        eqd(tag, w, want_cost[i], 1e-9);
        snprintf(tag, sizeof(tag), "p%d meters", i);
        eqd(tag, meters, 1000.0, 1e-9);
        free(r);
    }
    /* 距离模式：profile 不参与，三档必须完全一样（lm 建表就走这条）。 */
    for (int i = 0; i < 3; i++) {
        Router *r = make_router(0, prof[i]);
        if (!r) { printf("MISMATCH router 内存分配失败\n"); g_fails++; return; }
        eqd("dist-mode meters", edge_cost(r, er, NULL), 1000.0, 1e-9);
        free(r);
    }
}

int main(int argc, char **argv)
{
    int inject = (argc > 1 && strcmp(argv[1], "--inject=nowiring") == 0);
    check_pure();
    int pure_fails = g_fails;
    check_cost(inject);
    int cost_fails = g_fails - pure_fails;

    if (inject) {
        if (pure_fails == 0 && cost_fails > 0) {
            printf("PROFILE-INJECT-CAUGHT 抹掉 profile 接线被 g 代价判据抓到（%d 处）\n",
                   cost_fails);
            return 0;
        }
        printf("PROFILE-INJECT-MISSED 判据没有分辨力（pure=%d cost=%d）\n",
               pure_fails, cost_fails);
        return 1;
    }
    if (g_fails > 0) {
        printf("PROFILE-FAIL %d 处不一致\n", g_fails);
        return 1;
    }
    printf("PROFILE-OK 三档速度/可达性 + g 代价 + 距离模式不变\n");
    return 0;
}
