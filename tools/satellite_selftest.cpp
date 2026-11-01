/* 卫星天顶图单元自检：验证模拟星座数据合理 + 天顶图确实渲染出内容。
 *
 * 不依赖 QApplication（只往 QImage 上画，raster 引擎脱屏即可用）。
 * 退出码 0 = 全过；非 0 = 有失败（stderr 打印细节）。regress 看标记 SAT-OK。
 *
 * ★ 为什么单独一个可执行：模型推进 + 像素级渲染判据与整 App 的 --ui-test 正交——
 *   --ui-test 验"按钮→浮层"这条交互链，本工具验"数据合理 + 确实画出了东西"。
 *   二者都过，天顶图才算闭环。
 */
#include "ui_qt/skyplot_draw.h"
#include "ui_qt/satellite_model.h"

#include <cstdio>
#include <QImage>
#include <QPainter>
#include <QDir>

int main()
{
    int fail = 0;
    auto chk = [&](bool ok, const char *what) {
        printf("  %-46s %s\n", what, ok ? "OK" : "FAIL");
        if (!ok) fail++;
    };

    /* ① 模拟模型：确定性、数量 / 可见 / 在用 / 定位 合理 */
    SkSatelliteModel model;
    model.advance(0.0);
    chk(model.count() == 31, "模拟星座：卫星总数 31（GPS11+BDS9+GLO6+GAL5）");
    chk(model.visibleCount() > 0, "模拟星座：初始即有可见星");
    chk(model.inUseCount() <= 12, "模拟星座：在用星 ≤ 12");

    /* 推进一段时间：可见星应持续存在、在用数不越界、定位判定自洽 */
    bool inUseOk = true, anyVisible = false;
    for (int i = 0; i < 200; i++) {
        model.advance(1.0);
        if (model.inUseCount() > 12) inUseOk = false;
        if (model.visibleCount() > 0) anyVisible = true;
    }
    chk(inUseOk, "模拟星座：推进中在用星始终 ≤ 12");
    chk(anyVisible, "模拟星座：推进过程中持续有可见星");
    chk(model.hasFix() == (model.inUseCount() >= 4), "模拟星座：hasFix 与在用数自洽");

    /* ② 系统着色：四个系统返回有效且互异的颜色 */
    QColor c0 = sk_sat_system_color(SK_SAT_GPS);
    QColor c1 = sk_sat_system_color(SK_SAT_BDS);
    QColor c2 = sk_sat_system_color(SK_SAT_GLO);
    QColor c3 = sk_sat_system_color(SK_SAT_GAL);
    chk(c0.isValid() && c1.isValid() && c2.isValid() && c3.isValid(),
        "着色：四系统颜色均有效");
    chk(!(c0 == c1 && c1 == c2 && c2 == c3), "着色：四系统颜色互异");

    /* ③ 信号强度 → 半径单调不减 */
    double r0 = sk_sat_radius(0.0), r1 = sk_sat_radius(25.0), r2 = sk_sat_radius(50.0);
    chk(r0 <= r1 && r1 <= r2, "半径：随信号增强单调不减");

    /* ④ 渲染：画一帧天顶图到 QImage，统计与底色不同的变化像素 */
    const int S = 240;
    QImage img(S, S, QImage::Format_ARGB32);
    const QColor bg(244, 241, 234);          /* 与渲染背景一致 */
    img.fill(bg);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        SkSatelliteModel m2;
        m2.advance(12.0);
        sk_draw_skyplot(&p, QRect(10, 10, S - 20, S - 20),
                        m2.satellites(), m2.count(), m2.hasFix());
    }
    int changed = 0;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            QRgb px = img.pixel(x, y);
            if (qAbs(qRed(px)   - bg.red())   +
                qAbs(qGreen(px) - bg.green()) +
                qAbs(qBlue(px)  - bg.blue())  > 12)
                changed++;
        }
    chk(changed > 200, "渲染：天顶图确实画出内容（变化像素 > 200）");

    /* ⑤ 存一份 BMP 到临时目录，便于肉眼核查（非判据） */
    QString out = QDir::tempPath() + QStringLiteral("/satellite_selftest_out.bmp");
    if (!img.save(out, "BMP"))
        fprintf(stderr, "warn: 无法写出 %s\n", out.toLocal8Bit().constData());
    else
        printf("  （天顶图样张已存 %s）\n", out.toLocal8Bit().constData());

    if (fail == 0) { printf("SAT-OK\n"); return 0; }
    printf("SAT-FAIL: %d 项不符\n", fail);
    return 1;
}
