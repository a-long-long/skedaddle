#include "skyplot_draw.h"

#include <cmath>

static const double kPi = 3.14159265358979323846;   /* 本地 π（不依赖 M_PI，C++ 标准未定义） */

/* 系统主题色（卫星点 + 图例）。 */
QColor sk_sat_system_color(int system)
{
    switch (system) {
    case SK_SAT_BDS: return QColor(214, 48, 49);    /* 北斗：红 */
    case SK_SAT_GLO: return QColor(28, 160, 90);    /* 格洛纳斯：绿 */
    case SK_SAT_GAL: return QColor(150, 70, 200);   /* 伽利略：紫 */
    default:         return QColor(38, 115, 220);   /* GPS：蓝 */
    }
}

/* 信号强度 → 点半径（像素）。 */
double sk_sat_radius(double snr)
{
    double t = snr / 50.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return 2.0 + 5.0 * t;
}

void sk_draw_skyplot(QPainter *p, const QRect &rect,
                     const SkSatellite *sats, int count, bool hasFix)
{
    if (!p) return;

    /* 统计可见 / 在用（供顶部文字）。 */
    int vis = 0, use = 0;
    if (sats && count > 0) {
        for (int i = 0; i < count; i++) {
            if (sats[i].elevation > 0 && sats[i].snr > 0) vis++;
            if (sats[i].in_use) use++;
        }
    }

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    const double cx = rect.center().x();
    const double cy = rect.center().y() + 6.0;            /* 下移给顶部文字留位 */
    const double R = std::min(rect.width(), rect.height()) / 2.0 - 12.0;
    if (R <= 4.0) { p->restore(); return; }

    /* --- 顶栏文字：可见 / 在用 --- */
    QFont f = p->font();
    f.setPointSize(9);
    p->setFont(f);
    p->setPen(QColor(40, 50, 65));
    p->drawText(rect.adjusted(6, 1, 0, 0),
                Qt::AlignLeft | Qt::AlignTop,
                QString::asprintf("卫星  可见 %d / 在用 %d", vis, use));

    /* --- 底盘：半透明白圆 --- */
    p->setBrush(QColor(248, 249, 251, 220));
    p->setPen(QPen(QColor(150, 160, 175, 200), 1.0));
    p->drawEllipse(QRectF(cx - R, cy - R, 2.0 * R, 2.0 * R));

    /* --- 仰角同心圆 + 标签（90/60/30 度圈）--- */
    p->setPen(QPen(QColor(205, 212, 222, 220), 1.0));
    static const double ring[3] = { 60.0, 30.0, 0.0 };   /* 圈对应的"仰角下限" */
    for (int k = 0; k < 3; k++) {
        double rr = R * (90.0 - ring[k]) / 90.0;
        p->drawEllipse(QRectF(cx - rr, cy - rr, 2.0 * rr, 2.0 * rr));
    }
    p->setPen(QColor(150, 160, 175));
    f.setPointSize(7);
    p->setFont(f);
    p->drawText(QRectF(cx - R, cy - R, 2.0 * R, 2.0 * R),
                Qt::AlignLeft | Qt::AlignTop, "90");
    p->drawText(QRectF(cx - R, cy - R, 2.0 * R, 2.0 * R),
                Qt::AlignLeft | Qt::AlignBottom, "30");
    p->drawText(QRectF(cx - R, cy - R, 2.0 * R, 2.0 * R),
                Qt::AlignRight | Qt::AlignTop, "60");

    /* --- 方位角辐射线 + N/E/S/W 标签（0=北=上，顺时针）--- */
    p->setPen(QPen(QColor(210, 216, 226, 200), 1.0));
    for (int a = 0; a < 360; a += 30) {
        double th = (a - 90.0) * kPi / 180.0;
        double x = cx + R * std::cos(th), y = cy + R * std::sin(th);
        p->drawLine(cx, cy, x, y);
    }
    p->setPen(QColor(90, 100, 115));
    f.setPointSize(8);
    p->setFont(f);
    const double lr = R + 9.0;
    p->drawText(QRectF(cx - 8, cy - lr - 8, 16, 14), Qt::AlignCenter, "N");
    p->drawText(QRectF(cx + lr - 8, cy - 8, 16, 14), Qt::AlignCenter, "E");
    p->drawText(QRectF(cx - 8, cy + lr - 6, 16, 14), Qt::AlignCenter, "S");
    p->drawText(QRectF(cx - lr - 8, cy - 8, 16, 14), Qt::AlignCenter, "W");

    /* --- 卫星点 --- */
    f.setPointSize(7);
    p->setFont(f);
    if (sats && count > 0) {
        for (int i = 0; i < count; i++) {
            const SkSatellite &s = sats[i];
            if (s.elevation <= 0 || s.snr <= 0) continue;     /* 地平线下：不画 */
            double r = R * (90.0 - s.elevation) / 90.0;
            double th = (s.azimuth - 90.0) * kPi / 180.0;
            double x = cx + r * std::cos(th);
            double y = cy + r * std::sin(th);
            QColor c = sk_sat_system_color(s.system);
            double rad = sk_sat_radius(s.snr);
            if (s.in_use) {
                p->setBrush(c);
                p->setPen(QPen(QColor(255, 255, 255), 1.2));
                p->drawEllipse(QRectF(x - rad, y - rad, 2.0 * rad, 2.0 * rad));
            } else {
                p->setBrush(Qt::NoBrush);
                p->setPen(QPen(c, 1.2));
                p->drawEllipse(QRectF(x - rad, y - rad, 2.0 * rad, 2.0 * rad));
            }
            /* PRN 编号（小幅偏移，避免压在点上） */
            p->setPen(QColor(70, 78, 90));
            p->drawText(QRectF(x + rad + 1, y - 5, 22, 10),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        QString::number(s.prn));
        }
    } else {
        p->setPen(QColor(150, 160, 175));
        p->drawText(QRectF(cx - R, cy - 8, 2.0 * R, 16),
                    Qt::AlignCenter, "无卫星信号");
    }

    /* --- hasFix：外圈描绿 --- */
    if (hasFix) {
        p->setPen(QPen(QColor(40, 170, 90, 230), 2.0));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(QRectF(cx - R - 2.5, cy - R - 2.5,
                              2.0 * (R + 2.5), 2.0 * (R + 2.5)));
    }

    p->restore();
}
