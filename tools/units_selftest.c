/* skedaddle_units_selftest —— 单位制的确定性判据 + 负向对照
 *
 * 正常模式：把全局单位制设成公制，走**真正的产品出口**
 *   maneuver.c:fmt_distance / voice.c:voice_spoken_distance
 * 逐字节比对改动前的期望串；再单独验英制换算、速度、短格式、全局量。
 *   ⇒ 打印 UNITS-OK，rc=0。
 *
 * --inject=imperial：故意把全局单位制切成英制，但公制期望串保持不变。
 *   合规的实现必须让产品出口的字符串随之改变 ⇒ 判据应当**抓到**差异。
 *   抓到了打印 UNITS-INJECT-CAUGHT 且 rc=0；没抓到 rc=1（门禁变红）。
 *   ★ 这才是「负向对照必须能失败」：它验的是判据本身有分辨力，而不是
 *     "跑一遍没报错"。若有人把 units_set_system/包装层删掉，注入模式会红。
 *
 * 注意：日志里不要出现 "ASSERT-FAIL" 字样（regress.sh 的 run 会当失败）。
 */

#include "utils/units.h"
#include "route/maneuver.h"
#include "route/voice.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_fails;

static void eq(const char *tag, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) return;
    printf("MISMATCH %-22s 得到[%s] 期望[%s]\n", tag, got, want);
    g_fails++;
}

static void eqd(const char *tag, double got, double want, double tol)
{
    if (fabs(got - want) <= tol) return;
    printf("MISMATCH %-22s 得到[%.6f] 期望[%.6f]\n", tag, got, want);
    g_fails++;
}

/* A 段：公制逐字节兼容。这些期望串是改动前两个函数的输出，一个字都不许改。 */
static void check_metric_product_path(void)
{
    char b[64];
    fmt_distance(0,       b, sizeof(b)); eq("metric 0",        b, "0 米");
    fmt_distance(999,     b, sizeof(b)); eq("metric 999",      b, "999 米");
    fmt_distance(1234,    b, sizeof(b)); eq("metric 1234",     b, "1.23 公里");
    fmt_distance(12345,   b, sizeof(b)); eq("metric 12345",    b, "12 公里");
    voice_spoken_distance(0,      b, sizeof(b)); eq("spoken 0",        b, "0 公里");
    voice_spoken_distance(5.5,    b, sizeof(b)); eq("spoken 5.5",      b, "5.5 米");
    voice_spoken_distance(234,    b, sizeof(b)); eq("spoken 234",      b, "230 米");
    voice_spoken_distance(2500,   b, sizeof(b)); eq("spoken 2500",     b, "2.5 公里");
    voice_spoken_distance(200000, b, sizeof(b)); eq("spoken 200000",   b, "200 公里");
}

/* B 段：与全局量无关的显式算术（注入模式下也必须全绿）。 */
static void check_explicit(void)
{
    char b[64];
    units_fmt_distance(1609.344, UNITS_IMPERIAL, 0, b, sizeof(b));
    eq("imp 1mi panel",   b, "1.00 英里");
    units_fmt_distance(100.0, UNITS_IMPERIAL, 0, b, sizeof(b));
    eq("imp 100m panel",  b, "328 英尺");
    units_fmt_distance(1609.344, UNITS_IMPERIAL, 1, b, sizeof(b));
    eq("imp 1mi spoken",  b, "1.0 英里");
    units_fmt_distance(0, UNITS_IMPERIAL, 1, b, sizeof(b));
    eq("imp zero spoken", b, "0 英里");
    /* 越界 system 一律按公制钳制，不许读出垃圾单位词 */
    units_fmt_distance(1234, 99, 0, b, sizeof(b));
    eq("clamp bad system", b, "1.23 公里");

    const char *u = "";
    eqd("distance parts m",  units_distance_parts(500, UNITS_METRIC, 0, &u), 500.0, 1e-9);
    eq("distance parts unit", u, "米");
    eqd("distance parts mi", units_distance_parts(1609.344, UNITS_IMPERIAL, 0, &u), 1.0, 1e-9);
    eq("distance parts unit2", u, "英里");

    eqd("speed km/h", units_speed_parts(100, UNITS_METRIC, &u), 100.0, 1e-9);
    eq("speed unit",  u, "km/h");
    eqd("speed mph",  units_speed_parts(160.9344, UNITS_IMPERIAL, &u), 100.0, 1e-6);
    eq("speed unit2", u, "mph");

    eqd("big parts km", units_big_parts(2500, UNITS_METRIC, &u), 2.5, 1e-9);
    eq("big parts unit", u, "km");
    eqd("big parts mi", units_big_parts(1609.344, UNITS_IMPERIAL, &u), 1.0, 1e-9);
    eq("big parts unit2", u, "mi");

    units_fmt_short(2500, UNITS_METRIC,   1, b, sizeof(b)); eq("short 2.5km zh", b, "2.5 公里");
    units_fmt_short(500,  UNITS_METRIC,   0, b, sizeof(b)); eq("short 500m en",  b, "500 m");
    units_fmt_short(1609.344, UNITS_IMPERIAL, 0, b, sizeof(b)); eq("short 1mi en", b, "1.0 mi");
    units_fmt_short(100,  UNITS_IMPERIAL, 1, b, sizeof(b)); eq("short 100m zh",  b, "328 英尺");
}

int main(int argc, char **argv)
{
    int inject = (argc > 1 && strcmp(argv[1], "--inject=imperial") == 0);
    units_set_system(inject ? UNITS_IMPERIAL : UNITS_METRIC);
    if (units_get_system() != (inject ? UNITS_IMPERIAL : UNITS_METRIC)) {
        printf("MISMATCH global 单位制没生效\n");
        return 1;
    }

    int before = g_fails;
    check_metric_product_path();
    int a_fails = g_fails - before;
    check_explicit();
    int b_fails = g_fails - before - a_fails;

    if (inject) {
        if (a_fails > 0 && b_fails == 0) {
            printf("UNITS-INJECT-CAUGHT 英制注入被公制逐字节判据抓到（%d 处）\n", a_fails);
            return 0;
        }
        printf("UNITS-INJECT-MISSED 判据没有分辨力（A=%d B=%d）\n", a_fails, b_fails);
        return 1;
    }
    if (g_fails > 0) {
        printf("UNITS-FAIL %d 处不一致\n", g_fails);
        return 1;
    }
    printf("UNITS-OK 公制逐字节兼容 + 英制换算 + 全局单位制接线\n");
    return 0;
}
