#ifndef SKEDADDLE_QT_SKYPLOT_DRAW_H
#define SKEDADDLE_QT_SKYPLOT_DRAW_H

#include <QPainter>
#include <QColor>

/* 天顶图（sky plot）绘制：把 GNSS 卫星按方位角/仰角投影到一个圆里。
 *
 * ★ 纯绘制，不依赖任何 widget 或模型——只认一个 SkSatellite 数组，
 *   因此可以直接渲染到 QImage 做单元验证（像素级），也方便将来接
 *   任意数据源（模拟 / Qt Positioning / NMEA）。 */

enum SkSatSystem {
    SK_SAT_GPS = 0,   /* GPS      （L1） */
    SK_SAT_BDS = 1,   /* 北斗 BDS  */
    SK_SAT_GLO = 2,   /* 格洛纳斯  */
    SK_SAT_GAL = 3,   /* 伽利略    */
    SK_SAT_SYS_N
};

struct SkSatellite {
    int    prn;        /* 卫星编号（系统内唯一；北斗用 201+ 等真实风格） */
    int    system;     /* SkSatSystem */
    double azimuth;    /* 方位角 0..360，0=北，顺时针（东=90） */
    double elevation;  /* 仰角 0..90，0=地平线，90=天顶 */
    double snr;        /* 信号强度 0..50 dBHz（0=不可见/无信号） */
    bool   in_use;     /* 是否用于定位（高亮实心） */
};

/* 系统主题色：卫星点 + 图例用。 */
QColor sk_sat_system_color(int system);

/* 信号强度 → 点半径（像素）：snr 0..50 映射到 2..7 px。 */
double sk_sat_radius(double snr);

/* 主绘制：在 rect 指定的（建议正方形）区域里画天顶图。
 * sats 可为 nullptr（count==0 时画空图 + "无卫星"提示）；
 * hasFix=true 时外圈描绿（定位成功）。 */
void sk_draw_skyplot(QPainter *p, const QRect &rect,
                     const SkSatellite *sats, int count, bool hasFix);

#endif
