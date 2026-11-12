#include "utils/units.h"

#include <stdio.h>

static int g_system = UNITS_DEFAULT;

void units_set_system(int system)
{
    g_system = (system == UNITS_IMPERIAL) ? UNITS_IMPERIAL : UNITS_METRIC;
}

int units_get_system(void)
{
    return g_system;
}

/* ★★ 公制分支是**原封不动**照抄改动前的两个函数：
 *   spoken=0 ← maneuver.c:fmt_distance（用 meters 判断，负数语义保留）
 *   spoken=1 ← voice.c:voice_spoken_distance（用 |meters| 判断，照抄 OsmAnd）
 * 连格式串与分段阈值都不动 —— 缺省公制必须与改动前逐字节一致，这是负向对照
 * 的锚点，任何"顺手美化"都会让既有语音/文案判据失真。英制走下面的独立分支。 */
void units_fmt_distance(double meters, int system, int spoken, char *buf, size_t n)
{
    if (!buf || n == 0) return;

    if (system != UNITS_IMPERIAL) {
        if (spoken) {
            double a = meters < 0.0 ? -meters : meters;
            if (a < 1e-6) {
                snprintf(buf, n, "0 公里");        /* 官方 dist() 的零值分支 */
            } else if (a < 10.0) {
                snprintf(buf, n, "%.1f 米", meters);
            } else if (a < 1000.0) {
                /* 官方：((int)(Math.round(signed / 10.0) * 10)) + " 米" */
                snprintf(buf, n, "%.0f 米", (double)((long)((meters / 10.0) + 0.5) * 10));
            } else if (a < 100000.0) {
                snprintf(buf, n, "%.1f 公里", meters / 1000.0);
            } else {
                snprintf(buf, n, "%.0f 公里", meters / 1000.0);
            }
        } else {
            if (meters < 1000.0) snprintf(buf, n, "%.0f 米", meters);
            else if (meters < 10000.0) snprintf(buf, n, "%.2f 公里", meters / 1000.0);
            else snprintf(buf, n, "%.0f 公里", meters / 1000.0);
        }
        return;
    }

    /* 英制：先换算成 ft / mi；分段阈值用**等价物理距离**，不按数值硬套公制档位。
       1 ft = 0.3048 m（国际英尺），1 mi = 1609.344 m。 */
    double a = meters < 0.0 ? -meters : meters;
    if (spoken) {
        if (a < 1e-6) {
            snprintf(buf, n, "0 英里");
        } else if (a < 0.3048 * 10.0) {              /* < 10 ft */
            snprintf(buf, n, "%.1f 英尺", meters * UNITS_FT_PER_M);
        } else if (a < 0.3048 * 1000.0) {            /* < 1000 ft：整十英尺 */
            double ft = meters * UNITS_FT_PER_M;
            snprintf(buf, n, "%.0f 英尺", (double)((long)((ft / 10.0) + 0.5) * 10));
        } else if (a < UNITS_M_PER_MI * 100.0) {     /* < 100 mi：一位小数英里 */
            snprintf(buf, n, "%.1f 英里", meters / UNITS_M_PER_MI);
        } else {
            snprintf(buf, n, "%.0f 英里", meters / UNITS_M_PER_MI);
        }
        return;
    }
    if (a < 0.3048 * 1000.0)
        snprintf(buf, n, "%.0f 英尺", meters * UNITS_FT_PER_M);
    else if (a < UNITS_M_PER_MI * 10.0)
        snprintf(buf, n, "%.2f 英里", meters / UNITS_M_PER_MI);
    else
        snprintf(buf, n, "%.0f 英里", meters / UNITS_M_PER_MI);
}

double units_distance_parts(double meters, int system, int spoken, const char **unit)
{
    int imp = (system == UNITS_IMPERIAL);
    double a = meters < 0.0 ? -meters : meters;
    (void)spoken;   /* 两种口径目前共用单位词；保留参数给未来细分 */
    if (a < (imp ? 0.3048 * 1000.0 : 1000.0)) {
        if (unit) *unit = imp ? "英尺" : "米";
        return imp ? meters * UNITS_FT_PER_M : meters;
    }
    if (unit) *unit = imp ? "英里" : "公里";
    return imp ? meters / UNITS_M_PER_MI : meters / 1000.0;
}

double units_speed_parts(double kmh, int system, const char **unit)
{
    if (system == UNITS_IMPERIAL) {
        if (unit) *unit = "mph";
        return kmh / 1.609344;
    }
    if (unit) *unit = "km/h";
    return kmh;
}

void units_fmt_short(double meters, int system, int chinese, char *buf, size_t n)
{
    if (!buf || n == 0) return;
    if (system != UNITS_IMPERIAL) {
        /* ★ 原样照抄 map_canvas.cpp 的两行，别"顺手"改格式串。 */
        if (meters >= 1000.0) snprintf(buf, n, chinese ? "%.1f 公里" : "%.1f km",
                                       meters / 1000.0);
        else                  snprintf(buf, n, chinese ? "%.0f 米"   : "%.0f m", meters);
        return;
    }
    double a = meters < 0.0 ? -meters : meters;
    if (a >= UNITS_M_PER_MI)
        snprintf(buf, n, chinese ? "%.1f 英里" : "%.1f mi", meters / UNITS_M_PER_MI);
    else
        snprintf(buf, n, chinese ? "%.0f 英尺" : "%.0f ft", meters * UNITS_FT_PER_M);
}

double units_big_parts(double meters, int system, const char **unit)
{
    if (system == UNITS_IMPERIAL) {
        if (unit) *unit = "mi";
        return meters / UNITS_M_PER_MI;
    }
    if (unit) *unit = "km";
    return meters / 1000.0;
}
