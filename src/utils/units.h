#ifndef SKEDADDLE_UNITS_H
#define SKEDADDLE_UNITS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 单位制（2026-11-12，L2 本地能力）
 *
 * 只有两个取值：0 = 公制（米 / 公里，项目自诞生起的唯一口径）、1 = 英制（英尺 / 英里）。
 * ★ 缺省 0 时所有格式化结果必须与改动前的 fmt_distance / voice_spoken_distance
 *   **逐字节一致** —— 这是负向对照的锚点，任何"顺手美化"都会让对拍失真。
 *
 * 为什么集中到 utils：DAG 里 utils 是最底层，route / render / ui / ui_qt 都能
 * include 它而不会形成反向依赖（check_layers.sh 会拦）。以前距离字符串散在
 * maneuver.c / voice.c / navdraw.c / chrome.c / map_canvas.cpp 五处，各写各的。 */

typedef enum {
    UNITS_METRIC   = 0,   /* 米 / 公里 */
    UNITS_IMPERIAL = 1    /* 英尺 / 英里 */
} UnitSystem;

#define UNITS_DEFAULT UNITS_METRIC

/* 距离格式化。spoken=0 给面板/表格（精确），spoken=1 给语音播报（口语化整十）。
 * system 不合法时按公制处理（钳制，避免越界读出垃圾单位词）。
 * buf 至少 32 字节；NULL / n==0 时静默返回。 */
void units_fmt_distance(double meters, int system, int spoken, char *buf, size_t n);

/* 只做换算、不格式化：返回显示数值，unit 回填单位词（"米"/"公里"/"英尺"/"英里"）。
 * 给已经自带 snprintf 的调用点用（例如 UI 里带前缀的固定句式）。 */
double units_distance_parts(double meters, int system, int spoken, const char **unit);

/* 速度换算：输入 km/h，返回显示数值；unit 回填 "km/h" 或 "mph"。 */
double units_speed_parts(double kmh, int system, const char **unit);

/* 比例尺 / 短距离：内部按阈值切大/小单位，chinese=1 用「公里/米」，
 * 否则不用（Qt 的 "%.1f km" 句式走下面 units_big_parts）。
 * ★ 公制分支与改动前 map_canvas.cpp 的 "%.1f 公里" / "%.0f 米" 逐字节一致。 */
void units_fmt_short(double meters, int system, int chinese, char *buf, size_t n);

/* 大单位数值：路线/搜索结果里固定用 "%.1f km" 的句式。返回 km 或 mi 的数值，
 * unit 回填 "km" / "mi"。★ 公制时数值 = meters/1000，字符串与改动前完全相同，
 * 只是单位词从调用点的字面量搬到这里 —— 像素判据不失真。 */
double units_big_parts(double meters, int system, const char **unit);

/* 当前生效的全局单位制。进程内一次设置（app_init 读配置 / 自检里显式设），
 * 逐帧零开销；fmt_distance / voice_spoken_distance 的兼容包装读它。 */
void  units_set_system(int system);
int   units_get_system(void);

/* 科学依据：1 m = 1/0.3048 ft（国际英尺，1959 定义）；1 mile = 1609.344 m。 */
#define UNITS_FT_PER_M  3.280839895013123
#define UNITS_M_PER_MI  1609.344

#ifdef __cplusplus
}
#endif
#endif /* SKEDADDLE_UNITS_H */
