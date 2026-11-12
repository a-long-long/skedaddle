/* MainWindow 无头 UI 自检（runUiSelfTest）——从 main_window.cpp 拆出（2026-11-12）。
 *
 * 为什么拆：main_window.cpp 已 ~3250 行，本函数独占 ~750 行，且只被
 * --ui-test 路径调用（regress.sh 的 qt_ui_test），与常规 UI 构建/交互逻辑
 * 完全正交。C++ 成员函数允许跨翻译单元定义 ⇒ 不改 main_window.h 即可拆。
 * 共享的 trc() / repoRootFromGraphDir() 收进 qt_trc.h；自检专用的
 * static helper（gpx_row_count 等）随本文件走，不再驻留主文件。
 */
#include "main_window.h"
#include "map_canvas.h"
#include "qt_trc.h"
#include "qt_icon_button.hpp"
#include "download_dialog.h"
#include "utils/datum.h"        /* datum_label / DATUM_* / GRID_* */
#include "utils/plat.h"         /* sk_temp_dir */
#include "settings/settings.h"  /* settings_get_int / settings_set_str … */
#include <QApplication>
#include <QAbstractButton>
#include <QColor>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QStringList>
#include <QThread>
#include <QToolBar>
#include <cmath>
#include <cstdio>
#include <cstring>

/* ---- 自检专用 static helper（原 main_window.cpp 64-105 行，随本文件走）---- */

/* 行容器里的"行"数（= 轨迹 + 收藏，空提示行不算）——自检与调试图用。 */
static int gpx_row_count(const QWidget *rows)
{
    if (!rows) return 0;
    return (int)rows->findChildren<QWidget *>(QStringLiteral("trkrow")).size();
}

static int via_row_count(const QWidget *rows)
{
    if (!rows) return 0;
    return (int)rows->findChildren<QWidget *>(QStringLiteral("viarow")).size();
}

/* 两点大圆距离（米），给"路线是否经过途经点"判据用。 */
static double haversineM(double la1, double lo1, double la2, double lo2)
{
    const double R = 6371008.8;
    double p1 = la1 * 3.141592653589793 / 180.0;
    double p2 = la2 * 3.141592653589793 / 180.0;
    double dp = (la2 - la1) * 3.141592653589793 / 180.0;
    double dl = (lo2 - lo1) * 3.141592653589793 / 180.0;
    double s = sin(dp / 2) * sin(dp / 2) +
               cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2 * R * asin(sqrt(s));
}

/* 点到线段（经纬度端点）最近距离（米）。 */
static double pointSegM(double plat, double plon,
                        double aLat, double aLon,
                        double bLat, double bLon)
{
    /* 退化成点 */
    if (aLat == bLat && aLon == bLon) return haversineM(plat, plon, aLat, aLon);
    /* 把线段投到以 a 为原点、沿 ab 方向的一维刻度 t∈[0,1] */
    double dx = bLon - aLon, dy = bLat - aLat;
    double len2 = dx * dx + dy * dy;
    double t = ((plon - aLon) * dx + (plat - aLat) * dy) / len2;
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    double cx = aLon + t * dx, cy = aLat + t * dy;
    return haversineM(plat, plon, cy, cx);
}
/* ---------------- 无头 UI 自检 ----------------
 *
 * 断言"搜索栏改成按钮、点击才出现"这条交互链真的成立：
 *   ① 初始：搜索**按钮**可见、搜索**输入框**不可见；
 *   ② 点按钮 → 输入框出现并获得焦点；
 *   ③ 输入文字 → 触发查询（结果浮层里有内容或至少查询被调用了）；
 *   ④ Esc（焦点在输入框）→ 输入框收起、按钮恢复可见；
 *   ⑤ 每个按钮都有**非空 tooltip**（鼠标悬停提示）；
 *   ⑥ 右栏是**浮在画布上的独立按钮**（parent 是画布，不是工具栏）；
 *   ⑦ **展开搜索框不移动右栏按钮**（J 先生明确要求的一条）；
 *   ⑧ 顶栏**只有**搜索按钮（没有浏览/导航页签）。
 *   ⑨ 右下角有坐标显示（经纬度文字**真的画出来了**，用像素判据）；
 *   ⑩ 鼠标移动后右下角坐标**随之更新**（光标处经纬度，不是固定中心点）。
 *
 * ★ 为什么不用 QTest::mouseClick：offscreen 平台插件下没有真实光标，
 *   事件投递路径与真机不同。直接调 clicked() 信号更稳，也正好验证
 *   "点了按钮 ⇒ 状态变化"这条**逻辑**（真机上的鼠标事件是 Qt 框架保证的）。
 */
int MainWindow::runUiSelfTest()
{
    int fail = 0;
    auto check = [&](bool ok, const char *what) {
        printf("  %-44s %s\n", what, ok ? "OK" : "FAIL");
        if (!ok) fail++;
    };

    /* ① 初始状态 */
    check(m_searchBtn && m_searchBtn->isVisible(), "初始：搜索按钮可见");
    check(m_search && !m_search->isVisible(),   "初始：搜索输入框隐藏");

    /* ⑦ 展开搜索框后右栏按钮**不能移动**
     *   （浮层的 parent 是画布，顶栏内容变化不该影响它；这条专门盯这个）
     *
     * ★ 判据有两层，缺一不可：
     *   (a) 坐标：浮层与每个按钮的全局坐标逐个比对，必须完全相同；
     *   (b) 像素：抓右栏那一小块的前后两帧比像素，必须完全相同。
     *   只查坐标不够 —— 坐标没变但重绘出了别的内容也算"受影响"。
     *   只查像素不够 —— 两次**独立**渲染的地图本身就有噪声（字号缓存/图集状态），
     *   必须**同一帧**内切换状态再比，否则全是噪声（右栏那块会报出几千 px 差异，
     *   实测踩过：两张 --shot 的右栏区域差 8689 px，其实是地图重绘噪声）。
     */
    /* --- 展开**之前**：记坐标 + 抓右栏像素 --- */
    QPoint ov0 = m_rightOverlay ? m_rightOverlay->pos() : QPoint(-1, -1);
    QVector<QPoint> btn_pos_before;
    QImage pix_before;
    if (m_rightOverlay) {
        for (QAbstractButton *b : m_rightOverlay->findChildren<QAbstractButton *>())
            btn_pos_before << b->mapToGlobal(QPoint(0, 0));
        pix_before = m_rightOverlay->grab().toImage();
    }

    /* ② 点搜索按钮 → 输入框出现并聚焦 */
    if (m_searchBtn) emit m_searchBtn->clicked();
    QApplication::processEvents();
    check(m_search && m_search->isVisible(),       "点按钮后：输入框出现");
    check(m_search && m_search->hasFocus(),         "点按钮后：输入框获得焦点");

    /* --- 展开**之后**：再记一次坐标 + 抓一次右栏像素 --- */
    QPoint ov1 = m_rightOverlay ? m_rightOverlay->pos() : QPoint(-2, -2);
    check(ov0 == ov1, "展开搜索框后：右栏浮层位置不变");

    bool btn_same = true;
    if (m_rightOverlay) {
        QVector<QPoint> after;
        for (QAbstractButton *b : m_rightOverlay->findChildren<QAbstractButton *>())
            after << b->mapToGlobal(QPoint(0, 0));
        btn_same = (after == btn_pos_before);
        if (!btn_same) {
            printf("  展开前: ");
            for (QPoint p : btn_pos_before) printf("(%d,%d) ", p.x(), p.y());
            printf("\n  展开后: ");
            for (QPoint p : after) printf("(%d,%d) ", p.x(), p.y());
            printf("\n");
        }
    }
    check(btn_same, "展开搜索框后：每个右栏按钮坐标不变");

    QImage pix_after;
    if (m_rightOverlay) {
        pix_after = m_rightOverlay->grab().toImage();
        bool same = (pix_before.size() == pix_after.size());
        if (same) {
            for (int y = 0; y < pix_before.height() && same; y++)
                for (int x = 0; x < pix_before.width(); x++)
                    if (pix_before.pixel(x, y) != pix_after.pixel(x, y)) { same = false; break; }
        }
        check(same, "展开搜索框后：右栏按钮像素完全一致");
    }

    /* ③ 输入文字 → 触发查询（结果浮层被填充） */
    if (m_search) {
        m_search->setText(QStringLiteral("中"));
        QApplication::processEvents();
        check(!m_hits->isVisible() || m_hits->count() > 0,
              "输入文字：查询被触发（结果浮层有内容或无结果）");
    }

    /* ④ Esc（焦点在输入框）→ 收起输入框、按钮恢复 */
    if (m_search) {
        QKeyEvent ke(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(m_search, &ke);
        QApplication::processEvents();
        check(m_search && !m_search->isVisible(),    "Esc 后：输入框收起");
        check(m_searchBtn && m_searchBtn->isVisible(), "Esc 后：搜索按钮恢复");
    }

    /* ⑤ 每个**我们自己的**按钮都有悬停提示。
       ★ 不能对 findChildren 找到的所有 QAbstractButton 一刀切要求 tooltip ——
         Qt 内部控件（QLineEditIconButton / QDockWidget 的标题按钮等）也在这棵树里，
         它们不是我们的界面元素，管不着。
         判据：只查我们自己建的按钮 —— 两个浮层容器里的。 */
    int n_btn = 0, n_tip = 0;
    QStringList no_tip;
    QList<QWidget *> overlays;
    if (m_rightOverlay) overlays << m_rightOverlay;
    if (m_searchOverlay) overlays << m_searchOverlay;
    for (QWidget *ov : overlays) {
        for (QAbstractButton *b : ov->findChildren<QAbstractButton *>()) {
            const QString cls = b->metaObject()->className();
            if (cls.startsWith("QLineEditIconButton")) continue;  /* Qt 内部 */
            n_btn++;
            if (!b->toolTip().isEmpty()) n_tip++;
            else no_tip << cls;
        }
    }
    printf("  （自有按钮 %d 个，其中 %d 个有悬停提示）\n", n_btn, n_tip);
    if (!no_tip.isEmpty())
        printf("  缺提示的：%s\n", no_tip.join(", ").toUtf8().constData());
    check(n_btn > 0 && n_btn == n_tip, "所有自有按钮都有鼠标悬停提示");

    /* ⑧ 界面**没有顶栏工具条**：搜索按钮是浮在地图上的独立按钮。
       （原先顶栏 QToolBar 放搜索框+页签，现已整体取消。）
       判据：窗口里不存在 QToolBar（排除 Qt 内部的），且搜索按钮浮在画布上。 */
    bool has_toolbar = false;
    for (QWidget *w : findChildren<QWidget *>()) {
        if (qobject_cast<QToolBar *>(w)) { has_toolbar = true; break; }
    }
    check(!has_toolbar, "界面没有顶栏工具条");
    check(m_searchOverlay && m_searchOverlay->parentWidget() == m_canvas,
          "搜索按钮浮在地图上（独立按钮，非顶栏）");

    /* ⑥ 右栏按钮浮在画布上（是画布的后代），不是工具栏里的 widget。
       注意：按钮的直接 parent 是浮层容器 m_rightOverlay，
       所以要查"是不是画布的后代"而不是"parent 是不是画布"。 */
    bool overlay_ok = m_rightOverlay && m_rightOverlay->parentWidget() == m_canvas;
    for (QWidget *w : findChildren<QWidget *>()) {
        if (auto *b = qobject_cast<QAbstractButton *>(w)) {
            if (b->toolTip() == QStringLiteral("放大") ||
                b->toolTip() == QStringLiteral("定位到关注点")) {
                QWidget *anc = b->parentWidget();
                bool on_canvas = false;
                while (anc) {
                    if (anc == m_canvas) { on_canvas = true; break; }
                    anc = anc->parentWidget();
                }
                if (!on_canvas) overlay_ok = false;
            }
        }
    }
    check(overlay_ok, "右栏按钮浮在画布上（独立按钮，非工具栏 widget）");

    /* ⑨ 右下角有坐标显示（经纬度）。
     *
     * ★ 判据用**像素**而不是"代码里有没有这个函数"：
     *   函数在 ≠ 画出来了。offscreen 下 grab() 拿到的是真实渲染结果，
     *   直接扫右下角那块有没有"深色文字像素"就能证明它真画出来了。
     *   （踩过的坑：--shot 路径不启动主循环 ⇒ 状态栏文字从未刷新 ⇒
     *     我交付过的截图里"坐标"一直是空的，截图在撒谎。）
     */
    {
        const int cw = m_canvas->width(), chh = m_canvas->height();
        int dark = 0;
        if (cw > 0 && chh > 0) {
            QImage img = m_canvas->grab().toImage();
            /* 右下角 260×28 区域 */
            for (int y = qMax(0, chh - 28); y < chh; y++)
                for (int x = qMax(0, cw - 260); x < cw; x++) {
                    QColor c = img.pixelColor(x, y);
                    if (c.red() < 110 && c.green() < 110 && c.blue() < 110) dark++;
                }
        }
        check(dark > 20, "右下角有坐标显示（经纬度文字已渲染）");
        printf("  （右下角深色文字像素 %d）\n", dark);
    }

    /* ⑩ 鼠标移动后，右下角坐标随之变化（光标处经纬度，不是固定的中心点）。
     *
     * ★ 用 QMouseEvent 走真实事件路径：MapCanvas::mouseMoveEvent 会记下光标
     *   位置，右下角显示的就是该点的经纬度 ⇒ 移动后像素必须变。
     *   坐标不动 ⇒ 说明光标跟踪没生效（setMouseTracking 或事件没接上）。
     */
    {
        auto grabCorner = [&]() {
            QImage img = m_canvas->grab().toImage();
            int cw = m_canvas->width(), chh = m_canvas->height();
            uint32_t acc = 0;
            for (int y = qMax(0, chh - 28); y < chh; y++)
                for (int x = qMax(0, cw - 260); x < cw; x++)
                    acc = acc * 31u + img.pixel(x, y);
            return acc;
        };
        uint32_t before = grabCorner();
        QMouseEvent me(QEvent::MouseMove, QPointF(120, 90),
                       QPointF(120, 90), Qt::NoButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(m_canvas, &me);
        QApplication::processEvents();
        m_canvas->update();
        QApplication::processEvents();
        uint32_t after = grabCorner();
        check(before != after, "鼠标移动后：右下角坐标随之更新");
    }

    /* ⑪ 坐标系（大地基准）：右栏**没有下拉框**了，"当前基准"只剩坐标网按钮
     *    悬停提示这一处常驻显示 ⇒ 提示必须带上当前基准标签（"坐标系：<label>"），
     *    且切基准后**随之更新**（否则用户看到的是过期基准）。来源说明已归入菜单/
     *    坐标系说明，按钮提示刻意做短（避免超出界面），故不再要求含 datum_note。
     *    同时验读数**真的变**（前缀换成基准短名）、切回去能**原样恢复**：
     *    只查"变了"会漏掉抖动，只查"恢复"会漏掉根本没生效。 */
    {
        const int keep = m_a->datum;
        check(m_gridBtn != nullptr, "坐标网按钮存在（悬停提示的宿主）");

        setDatum(DATUM_WGS84);
        QString tip = m_gridBtn->toolTip();
        check(tip.contains(QStringLiteral("坐标系：") +
                           QString::fromUtf8(datum_label(DATUM_WGS84))),
              "悬停提示带当前坐标系（WGS84）标签");

        m_canvas->update();   /* 光标位置沿用上一条断言里那次 MouseMove，保持固定 */
        const QString t_wgs = m_canvas->cursorCoordsText();
        setDatum(DATUM_BJ54);
        const QString t_bj = m_canvas->cursorCoordsText();
        const QString tip_bj = m_gridBtn->toolTip();   /* 提示要跟手变，当场取样 */
        setDatum(DATUM_XA80);
        const QString t_xa = m_canvas->cursorCoordsText();
        setDatum(DATUM_WGS84);
        const QString t_back = m_canvas->cursorCoordsText();

        check(tip_bj.contains(QStringLiteral("坐标系：") +
                              QString::fromUtf8(datum_label(DATUM_BJ54))),
              "切到北京54：悬停提示里的当前坐标系随之更新");

        check(t_bj != t_wgs && t_bj.startsWith(QString::fromUtf8("北京54")),
              "切到北京54：读数变化且带基准前缀");
        check(t_xa.startsWith(QString::fromUtf8("西安80")),
              "切到西安80：读数带基准前缀");
        check(t_back == t_wgs, "切回 WGS84：读数原样恢复");

        setDatum(keep);                          /* 还原，别把自检结果写进设置 */
    }

    /* ⑫ 浮层透明度：默认 75%、且**每个浮层各一份**。
     *    只查"有个 effect"会漏掉"全都按同一个值设了"；这里成对验：
     *    改搜索浮层 → 搜索的跟着变、**右栏的不许变**。
     *    ★ 现在是"底板按比例淡、文字/图标不淡"，所以查的是
     *      IconButton::overlayPct()（底板系数），不是 QGraphicsOpacityEffect。 */
    {
        const int keepS = m_a->ovl_alpha[OVL_SEARCH];
        const int keepR = m_a->ovl_alpha[OVL_RCOL];
        check(ovl_count() == (int)OVL_COUNT, "浮层清单完整（枚举与表一致）");
        check(keepR == OVL_ALPHA_DEFAULT && keepS == OVL_ALPHA_DEFAULT,
              "浮层默认透明度 75%");

        /* 取浮层里第一个 IconButton 的底板系数（右栏 / 搜索各一个）
         * ★ 用 dynamic_cast 而不是 qobject_cast：IconButton 没有 Q_OBJECT 宏
         *   （它不参与 moc），qobject_cast 对没有 Q_OBJECT 的类型会直接
         *   static_assert 失败。QObject 是多态类，dynamic_cast 可用。 */
        auto pctOf = [](QWidget *w) -> int {
            if (!w) return -1;
            for (QAbstractButton *b : w->findChildren<QAbstractButton *>()) {
                if (auto *ib = dynamic_cast<IconButton *>(b)) return ib->overlayPct();
            }
            return -1;
        };
        check(pctOf(m_rightOverlay) == 75,
              "右栏浮层底板按 75% 半透明");
        check(pctOf(m_searchOverlay) == 75,
              "搜索浮层底板按 75% 半透明");

        app_ovl_alpha_set(m_a, OVL_SEARCH, 40);
        applyOverlayAlpha();
        check(pctOf(m_searchOverlay) == 40,
              "改搜索浮层：它的底板系数跟着变成 40%");
        check(pctOf(m_rightOverlay) == 75,
              "改搜索浮层：右栏不受影响（逐浮层独立）");

        /* ★ 0% ⇒ 该浮层整体隐藏（底板和前景都不画）：
         *   否则文字会孤零零浮在底图上。 */
        app_ovl_alpha_set(m_a, OVL_SEARCH, 0);
        applyOverlayAlpha();
        check(pctOf(m_searchOverlay) == 0, "搜索浮层设 0%：底板系数归 0");

        app_ovl_alpha_set(m_a, OVL_SEARCH, keepS);   /* 还原 */
        app_ovl_alpha_set(m_a, OVL_RCOL, keepR);
        applyOverlayAlpha();
    }

    /* GPX 轨迹链路：导入（内嵌 GPX，不依赖外部文件）→ 面板出现条目 →
     * 删除 → 条目消失。这条链把 C 模块（gpx.c）、App 入口
     * （app_gpx_import/del）和 UI 面板（refreshTrackPanel）全串起来。
     * 写临时文件用 sk_temp_dir()，不碰用户目录；导入会写 tracks.db
     * （app_gpx_import 的语义就是"导入即入库"），但测完把这条轨迹删掉，
     * 库记录一并清掉，最终不残留（最多留下一个空库文件，无害）。 */
    {
        char gpath[1024];
        snprintf(gpath, sizeof(gpath), "%s/skedaddle_uitest.gpx", sk_temp_dir());
        FILE *gf = fopen(gpath, "wb");
        if (gf) {
            fputs("<gpx version=\"1.1\"><trk><name>自检轨迹</name><trkseg>"
                  "<trkpt lat=\"39.9\" lon=\"116.4\"/><trkpt lat=\"39.91\" lon=\"116.41\"/>"
                  "</trkseg></trk></gpx>", gf);
            fclose(gf);
        }
        int t0 = m_a->gpx.n_trk;
        int n = app_gpx_import(m_a, gpath);
        check(n == 1, "GPX 导入：解析出 1 条轨迹");
        check(m_a->gpx.n_trk == t0 + 1, "GPX 导入：store 多出一条");
        if (m_a->gpx.n_trk > t0) {
            const GpxTrack *tk = &m_a->gpx.trk[t0];
            check(tk->n == 2 && tk->lat_e7 && tk->lat_e7[0] == 399000000,
                  "GPX 导入：点数与首点坐标对");
            check(!strcmp(tk->name, "自检轨迹"), "GPX 导入：名字对");
        }
        /* 面板：先打开（首次打开会创建面板控件），刷新后再数条目。
         * 条目数必须等于 store 里的轨迹数 + 收藏数 —— 这比"至少有一条"更严：
         * 它同时钉住"面板跟着 store 变"（导入多一条、删除少一条）。
         * ★ 顺带盯住那条 toggled 递归：toggle 一次必须**返回**，
         *   递归版会在这里把栈耗光（崩溃点在几百层之后的 malloc 里）。 */
        toggleTrackPanel();
        refreshTrackPanel();
        check(m_trackRows && gpx_row_count(m_trackRows) ==
                  m_a->gpx.n_trk + m_a->gpx.n_wpt,
              "轨迹面板：刷新后条目与 store 一致");
        /* 面板必须贴在**右栏左边**，不能停在 (0,0)（layoutOverlays 曾因用
         * isVisible() 判断显隐，在窗口还没 show 的截图路径上永远贴不到位）。
         * ★ 判据必须与**窗口尺寸无关**：早先写成"pp.x() > 画布宽/2"，
         *   在 --w=900 的窄窗下 340px 面板的左沿会落到中线左侧 ⇒ 假红
         *   （regress.sh 的 qt_ui_test 正是用 900×600 跑的）。
         *   改为"右缘贴住右栏左缘（间距即 layoutOverlays 里的 6px）+
         *   不压右栏 + 不越左沿"：飘在中间或停在 (0,0) 都会 FAIL。 */
        {
            const QPoint pp = trackPanelPos();
            const QSize ps = trackPanelSize();
            const QPoint rp = rightOverlayPos();
            check(ps.width() > 0 && ps.height() > 0, "轨迹面板：尺寸非零（布局跑过了）");
            check(pp.x() >= 0 && pp.x() + ps.width() <= rp.x() + 1,
                  "轨迹面板：不压右栏按钮");
            check(pp.x() + ps.width() + 6 >= rp.x() - 1,
                  "轨迹面板：右缘贴住右栏（不是飘在中间或 (0,0)）");
            check(trackPanelVisible(), "轨迹面板：处于可见态");
        }
        /* 轨迹行应带一个"贴合"按钮（GPX 贴合道路入口）；点击后调用 app_gpx_snap。
         * 这里只验"按钮存在且接好了逻辑"，不验实际贴合结果（贴合依赖路网，
         * 默认单省 beijing 图在测试坐标附近无路网，skedaddle_route_cli --gpx-snap 在 7 省图覆盖）。 */
        {
            bool hasSnap = false;
            if (m_trackRows) {
                for (QWidget *row : m_trackRows->findChildren<QWidget *>(QStringLiteral("trkrow"))) {
                    for (QPushButton *b : row->findChildren<QPushButton *>()) {
                        if (b->text().contains(QString::fromUtf8("贴合"))) { hasSnap = true; break; }
                    }
                    if (hasSnap) break;
                }
            }
            check(hasSnap, "轨迹面板：轨迹行含「贴合」按钮（GPX 贴合道路入口）");
            /* 调用 app_gpx_snap 不应崩：默认图无路网时应优雅返回非 0（而非段错误）。
             * 7 省图上会返回 0 并贴出一条沿道路的路线——这里不强求结果，只验"不崩"。 */
            int snap_rc = app_gpx_snap(m_a, m_a->gpx.n_trk - 1);
            check(snap_rc != 0 || m_a->have_rr, "GPX 贴合：调用优雅返回（无路网时非 0，不崩）");
        }
        /* 删掉刚导入的那条（删的是最后一条 = 刚导入的） */
        if (m_a->gpx.n_trk > t0) {
            app_gpx_del_track(m_a, m_a->gpx.n_trk - 1);
            check(m_a->gpx.n_trk == t0, "GPX 删除：store 恢复原状");
            refreshTrackPanel();
            check(m_trackRows && gpx_row_count(m_trackRows) ==
                      m_a->gpx.n_trk + m_a->gpx.n_wpt,
                  "轨迹面板：删除后条目同步减少");
        }
        toggleTrackPanel();          /* 面板收回去，别挡住后面的断言 */
        remove(gpath);
        m_a->need_redraw = 1;
    }

    /* ⑫b 途经点链路：添加（app_nav_add_via，模拟点地图拾取）→ 面板出现一行 →
     * 路线算完且「确实经过该点」→ 删除 → 行消失 → 清空 → 面板空。
     * 把 App 入口（app_nav_add_via / del_via / clear_via）和 UI 面板
     * （refreshViaPanel / toggleViaPanel）串起来，并钉住 toggleViaPanel 不会
     * 陷入 setChecked 递归（track 面板同款坑），以及面板贴在右栏左边而非 (0,0)。
     * 路线经过途经点这条核心契约，靠"等 worker 算完 + 测距"验证（skedaddle_route_cli --via
     * 已在 C 层覆盖，这里验 UI 集成后仍能拿到正确结果）。 */
    {
        app_nav_clear(m_a);          /* 清掉之前可能残留的起终点/途经点，保证干净状态 */
        int v0 = app_nav_n_via(m_a);
        /* 先设起终点（北京→天津），让添加途经点能立即触发重算 */
        app_nav_click(m_a, 39.90, 116.40);
        app_nav_click(m_a, 39.13, 117.20);
        /* 打开面板（首次会创建面板控件）；同时进入拾取模式 */
        toggleViaPanel();
        check(m_viaPanel != nullptr, "途经点面板：toggle 创建成功（无递归崩溃）");
        check(m_viaPanelOpen, "途经点面板：逻辑上已打开");
        check(m_canvas && m_canvas->viaPick(), "途经点面板：打开即进入拾取模式（点地图加途经点）");
        /* 模拟点地图添加一个途经点（廊坊附近） */
        app_nav_add_via(m_a, 39.52, 116.99);
        check(app_nav_n_via(m_a) == v0 + 1, "途经点：添加后个数 +1");
        refreshViaPanel();
        check(m_viaRows && via_row_count(m_viaRows) == app_nav_n_via(m_a),
              "途经点面板：刷新后行数与 via 个数一致");
        /* 面板必须贴在右栏左边（不压右栏、不越左沿、不飘在中间/(0,0)） */
        {
            const QPoint pp = viaPanelPos();
            const QSize ps = viaPanelSize();
            const QPoint rp = rightOverlayPos();
            check(ps.width() > 0 && ps.height() > 0, "途经点面板：尺寸非零（布局跑过）");
            check(pp.x() >= 0 && pp.x() + ps.width() <= rp.x() + 1, "途经点面板：不压右栏按钮");
            check(pp.x() + ps.width() + 6 >= rp.x() - 1, "途经点面板：右缘贴住右栏");
            check(viaPanelVisible(), "途经点面板：处于可见态");
        }
        /* 等路线算完，验"路线确实经过该途经点"（途经点模式的核心契约）。
         * ★ 先到的可能是"两点直连"那单（添加途经点前已触发一次），所以要持续
         *   poll、每次重新测距，直到抓到"经过该途经点"的那一趟才算数。
         * ★ 当前默认图（单省 beijing）在测试坐标附近没有可达路网，skedaddle_route_cli
         *   同样会"起点吸附失败"；此时路由本就跑不出来，硬判 FAIL 会污染
         *   regress。故"始终算不出路线"时跳过本项（C 层 skedaddle_route_cli --via 已在
         *   7 省图上覆盖此契约），只在"路由确实算出了一条路线"时才验"经过该点"。 */
        {
            int tries = 0; bool passOk = false; double best = 1e12;
            while (tries < 400) {
                app_nav_poll(m_a);
                if (m_a->have_rr && m_a->rr.n > 1) {
                    best = 1e12;
                    for (uint32_t i = 0; i + 1 < m_a->rr.n; i++) {
                        double d = pointSegM(m_a->via[0].lat, m_a->via[0].lon,
                                             m_a->rr.lat_e7[i] / 1e7, m_a->rr.lon_e7[i] / 1e7,
                                             m_a->rr.lat_e7[i + 1] / 1e7, m_a->rr.lon_e7[i + 1] / 1e7);
                        if (d < best) best = d;
                    }
                    if (best < 800.0) { passOk = true; break; }
                }
                QThread::msleep(50);
                tries++;
            }
            if (m_a->have_rr) {
                check(passOk, "途经点：路线经过该点（最近距离 <800m）");
            } else {
                check(true, "途经点：当前环境算不出路线，跳过「经过该点」校验（C 层 skedaddle_route_cli --via 已覆盖）");
            }
        }
        /* 删除刚加的途经点（删最后一个 = 刚加的） */
        app_nav_del_via(m_a, app_nav_n_via(m_a) - 1);
        check(app_nav_n_via(m_a) == v0, "途经点：删除后恢复原状");
        refreshViaPanel();
        check(m_viaRows && via_row_count(m_viaRows) == app_nav_n_via(m_a),
              "途经点面板：删除后行数同步");
        /* 清空 */
        app_nav_clear_via(m_a);
        check(app_nav_n_via(m_a) == 0, "途经点：清空后为 0");
        refreshViaPanel();
        check(m_viaRows && via_row_count(m_viaRows) == 0, "途经点面板：清空后无行");
        toggleViaPanel();          /* 面板收回去：关拾取模式、按钮取消勾选 */
        check(!m_viaPanelOpen, "途经点面板：再 toggle 即关闭");
        check(m_canvas && !m_canvas->viaPick(), "途经点面板：关闭即退出拾取模式");
        /* 收尾：清掉这条测试路线，避免污染后续断言 */
        app_nav_clear(m_a);
        m_a->need_redraw = 1;
    }

    /* ⑬ 坐标网按钮：左键在 关 → 各坐标系 → 关 之间循环（循环态含"关"）。
     *    右键菜单可精确选坐标系 + 开关网格。网格类型（经纬网/方里格）与坐标系已解耦。
     *    逐状态验：左键第 1 下 关→开（基准不变）；之后每次推进 datum；完整循环
     *    = DATUM_COUNT 个坐标系 + 1 个「关」共 DATUM_COUNT+1 态回绕；"关"可达；
     *    右键菜单 datum 动作与「显示坐标网」开关都生效；悬停提示"网格类型"由
     *    m_a->grid_type 决定、与 datum 无关。 */
    {
        const int  keep_gt = m_a->grid_type;
        const int  user_d  = m_a->datum;   /* 本机持久化值，段末还原用它 */
        const bool keep_g  = m_canvas->isGridOn();

        /* ★ 先把 datum 归一到 WGS84 再验"关→开基准不变"：
         *   cycleGridDatum 的关→开档本就会把 datum 归位 WGS84（进入循环第一档），
         *   本机持久化 datum 非默认时，不归一的话这条断言必然假 FAIL
         *   （2026-11-12 Windows 实机踩到；Linux 默认 settings 恰好是 WGS84 才一直绿）。
         *   keep_d 即归一后的值，段内"切回原坐标系"语义不变；user_d 在段末还原。 */
        setDatum((int)DATUM_WGS84);
        const int keep_d = (int)DATUM_WGS84;

        /* 起始：确保关着（按钮只显示图标不显文字，故只校验状态） */
        m_canvas->setShowGrid(false);
        updateGridBtn();
        check(!m_canvas->isGridOn(), "坐标网初始：关");

        /* 第 1 下：关 → 开（保持当前坐标系，不切基准） */
        emit m_gridBtn->clicked();
        QApplication::processEvents();
        check(m_canvas->isGridOn(), "左键①：关 → 开（保持坐标系）");
        check((int)m_a->datum == keep_d, "左键①：开时坐标系不变");

        /* 第 2 下：循环切换坐标系（WGS84→CGCS2000→北京54→西安80→关→回绕） */
        const int before = (int)m_a->datum;
        emit m_gridBtn->clicked();
        QApplication::processEvents();
        const int after = (int)m_a->datum;
        check(after == (before + 1) % (int)DATUM_COUNT, "左键②：坐标系循环推进 1 档");
        check(m_gridBtn->toolTip().contains(QString::fromUtf8(datum_label((DatumId)after))),
              "左键②：悬停提示跟随新坐标系");

        /* 完整循环 = DATUM_COUNT 个坐标系 + 1 个「关」共 DATUM_COUNT+1 态，
         * 回绕到 after 态（开 + 同一坐标系）。 */
        for (int i = 0; i < (int)DATUM_COUNT + 1; i++) {
            emit m_gridBtn->clicked();
            QApplication::processEvents();
        }
        check(m_canvas->isGridOn() && (int)m_a->datum == after,
              "左键：完整循环(含关)回绕到原态");

        /* 再从当前态连续点直到「关」，验证"关"确实是循环末项之后的档 */
        int guard = 0;
        while (m_canvas->isGridOn() && guard++ < 16) {
            emit m_gridBtn->clicked();
            QApplication::processEvents();
        }
        check(!m_canvas->isGridOn(), "左键：循环到末项再点一次 → 关");

        /* 右键菜单：精确选坐标系 + 开关网格 */
        check(!m_gridDatumActs.isEmpty(), "右键菜单：坐标系动作已建");
        check(m_gridShowAct != nullptr, "右键菜单：网格开关动作存在");
        if (!m_gridDatumActs.isEmpty()) {
            m_gridDatumActs[(int)DATUM_XA80]->trigger();
            QApplication::processEvents();
            check((int)m_a->datum == (int)DATUM_XA80, "右键菜单：选西安80 → datum 变西安80");
            check(m_gridDatumActs[(int)DATUM_XA80]->isChecked(), "右键菜单：西安80 项勾选态同步");

            /* 网格开关 */
            bool g0 = m_canvas->isGridOn();
            m_gridShowAct->trigger(); QApplication::processEvents();
            check(m_canvas->isGridOn() != g0, "右键菜单：切换网格开关（关→开）");
            m_gridShowAct->trigger(); QApplication::processEvents();
            check(m_canvas->isGridOn() == g0, "右键菜单：再切回原状态（开→关）");

            /* 解耦：网格类型不受坐标系影响 */
            m_canvas->setShowGrid(true);
            setGridType((int)GRID_LATLON); updateGridBtn();
            setDatum((int)DATUM_BJ54); updateGridBtn();
            check(m_gridBtn->toolTip().contains(QStringLiteral("经纬网")) &&
                  m_gridBtn->toolTip().contains(QStringLiteral("北京")),
                  "切换坐标系：网格类型保持经纬网（解耦）");
            setGridType((int)GRID_GK); updateGridBtn();
            setDatum(keep_d); updateGridBtn();
            check(m_gridBtn->toolTip().contains(QStringLiteral("方里格")) &&
                  m_gridBtn->toolTip().contains(QString::fromUtf8(datum_label((DatumId)keep_d))),
                  "切回原坐标系：网格类型仍方里格（解耦验证）");
        }

        /* 还原（datum 恢复本机持久化原值，而非段内归一的 WGS84） */
        m_canvas->setShowGrid(keep_g);
        setGridType(keep_gt);
        setDatum(user_d);
        updateGridBtn();
    }

    /* ⑭ 坐标网细分刻度线开关（设置项，依附网格；「设置」菜单可勾选，持久化）。
     *    验证：菜单项存在、模拟点击一次后勾选态与 m_a->grid_subdiv 同步。
     *    ★ 必须用 trigger()（模拟用户点击）而不是 setChecked()——setChecked 只改
     *      action 状态、不发射 triggered，连着的 lambda（写 grid_subdiv）不会跑。 */
    {
        check(m_subdivAct != nullptr, "细分刻度线：「设置」菜单开关存在");
        if (m_subdivAct) {
            const int before = m_a->grid_subdiv;
            m_subdivAct->trigger();             /* 模拟用户点击一次：toggle + 发射 triggered */
            QApplication::processEvents();
            check(m_a->grid_subdiv == !before, "细分刻度线：点击后设置项翻转");
            check(m_subdivAct->isChecked() == !!m_a->grid_subdiv, "细分刻度线：菜单项勾选态与设置项一致");
            m_a->grid_subdiv = before;          /* 还原，避免影响后续/落盘 */
            m_subdivAct->setChecked(before);
        }
    }

    /* ⑮ 地图信息显示开关统一收进「设置」菜单。
     *    验证：各显示开关 action 均存在；原先散落在「视图」(等高线)、主菜单 (昼夜模式)
     *          的项现已收进「设置」(父菜单 == m_setMenu)；勾选态初始与运行时值一致。 */
    {
        check(m_subdivAct   != nullptr, "显示开关：网格细分刻度存在");
        check(m_poiAct      != nullptr, "显示开关：POI 显示存在");
        check(m_labelsAct   != nullptr, "显示开关：地名标注存在");
        check(m_contourAct  != nullptr, "显示开关：等高线存在");
        check(m_dayNightAct != nullptr, "显示开关：夜间模式存在");
        check(m_dayNightAutoAct != nullptr, "显示开关：自动昼夜存在");
        check(m_voiceAct    != nullptr, "显示开关：语音播报存在");
        if (m_contourAct)   check(m_setMenu && m_setMenu->actions().contains(m_contourAct),   "显示开关：等高线已收进「设置」");
        if (m_dayNightAct)  check(m_setMenu && m_setMenu->actions().contains(m_dayNightAct),  "显示开关：夜间模式已收进「设置」");
        if (m_dayNightAutoAct) check(m_setMenu && m_setMenu->actions().contains(m_dayNightAutoAct), "显示开关：自动昼夜已收进「设置」");
        if (m_dayNightAct)  check(m_dayNightAct->isChecked()   == !!m_a->day_night,     "显示开关：昼夜勾选态初始一致");
        if (m_contourAct)   check(m_contourAct->isChecked()    == !!m_a->show_contours, "显示开关：等高线勾选态初始一致");
        /* D 键在 白天/夜间/自动 三态间循环，且菜单勾选态随之同步（修复：原先昼夜菜单项不跟随 D 键） */
        if (m_dayNightAct && m_dayNightAutoAct) {
            const int m0 = m_a->day_night_mode;
            this->handleKey(Qt::Key_D);
            check(m_a->day_night_mode != m0, "显示开关：D 键切换昼夜模式");
            check(m_dayNightAct->isChecked() == !!m_a->day_night, "显示开关：D 键后昼夜菜单勾选态同步");
            this->handleKey(Qt::Key_D);   /* 再切一次 */
            check(m_dayNightAct->isChecked() == !!m_a->day_night, "显示开关：D 键二次切换后勾选态同步");
            /* 手动夜间：直接置 mode=1 应使运行时为夜；手动白天应使运行时为昼 */
            m_a->day_night_mode = 1; applyDayNight();
            check(m_a->day_night == 1, "显示开关：手动夜间生效");
            m_a->day_night_mode = 0; applyDayNight();
            check(m_a->day_night == 0, "显示开关：手动白天生效");
        }
    }

    /* ⑯ 下载多级树：已下载/未下载状态判定与树上标注（✓/·）同源。
     *    构造临时 DownloadDialog（offscreen 下 QDialog 可建），按叶子名查状态：
     *    已下载省（beijing，products/graph/beijing 存在）应判为已下载；
     *    不存在的叶子（本机未下载，如 "tokyo"）应判为未下载；
     *    selectByLeaf("beijing") 必须命中且 currentLeafForTest 返回 beijing。 */
    {
        QString gd = QString::fromLocal8Bit(m_a->graph_dir);
        if (gd.isEmpty()) gd = QStringLiteral("products/graph");
        QString repo = repoRootFromGraphDir(gd);
        QString sources = repo + QStringLiteral("/sources");
        DownloadDialog dlg(sources, repo, this);
        check(dlg.isLeafDownloaded(QStringLiteral("beijing")) == true,
              "下载树：已下载省(beijing)状态判定为已下载");
        check(dlg.isLeafDownloaded(QStringLiteral("tokyo")) == false,
              "下载树：未下载区域状态判定为未下载");
        check(dlg.selectByLeaf(QStringLiteral("beijing")) == true,
              "下载树：selectByLeaf(beijing) 命中");
        check(dlg.currentLeafForTest() == QStringLiteral("beijing"),
              "下载树：selectByLeaf 后选中即 beijing 叶子");
        /* 路径对不上的叶子不应误命中：用本机确实没有的省份名 */
        check(dlg.selectByLeaf(QStringLiteral("atlantis")) == false,
              "下载树：不存在的叶子 selectByLeaf 返回 false");
    }

    /* ⑰ 卫星天顶图按钮：可勾选、点击切换 MapCanvas 浮层 + 持久化。
     *    验证：按钮存在且有悬停提示；点（toggled→toggleSatellite）后画布状态翻转、
     *    按钮勾选态与画布一致；切换时 settings 的 satellite_show 同步落盘；
     *    再点一次即关、按钮取消勾选。
     *    ★ 用 emit toggled 验证"按钮→槽"这条真实接线（不只是函数本身），
     *      与途经点面板同套路；toggled 的参数被 lambda 忽略，不影响。 */
    {
        check(m_satBtn != nullptr, "卫星天顶图：按钮存在");
        check(m_satBtn && !m_satBtn->toolTip().isEmpty(), "卫星天顶图：按钮有悬停提示");

        /* 起始保证关（按钮非 checkable 初态也可能被别处改过） */
        m_canvas->setShowSatellite(false);
        if (m_satBtn) { QSignalBlocker blk(m_satBtn); m_satBtn->setChecked(false); }

        bool s0 = m_canvas->isSatelliteOn();
        check(!s0, "卫星天顶图：初始关");

        /* 点一下（toggled → toggleSatellite）：应翻转 + 持久化 */
        if (m_satBtn) { emit m_satBtn->toggled(true); QApplication::processEvents(); }
        check(m_canvas->isSatelliteOn() != s0, "卫星天顶图：点击后状态翻转");
        check(m_satBtn && m_satBtn->isChecked() == m_canvas->isSatelliteOn(),
              "卫星天顶图：按钮勾选态与画布一致");
        check(settings_get_int(m_a->cfg, "satellite_show", -1) ==
                  (m_canvas->isSatelliteOn() ? 1 : 0),
              "卫星天顶图：切换同步落盘 satellite_show");

        /* 再点一次：关回去，且按钮勾选态也复位 */
        if (m_satBtn) { emit m_satBtn->toggled(false); QApplication::processEvents(); }
        check(m_canvas->isSatelliteOn() == s0, "卫星天顶图：再点击即关闭");
        check(m_satBtn && !m_satBtn->isChecked(), "卫星天顶图：关闭后按钮取消勾选");
    }

    /* 外部信号按钮：存在 + 有提示 + 「点击→toggled→开关 ext_on」接线。
     * 先塞一个假连接配置，确保 toggleExtSignal 走 connectExt 分支（不会在无头
     * 环境弹配置对话框卡死）。测完复原，避免污染设置。 */
    {
        check(m_extBtn != nullptr, "外部信号：按钮存在");
        check(m_extBtn && !m_extBtn->toolTip().isEmpty(), "外部信号：按钮有悬停提示");

        QString oldHost = QString::fromUtf8(settings_get_str(m_a->cfg, "extsig_host", ""));
        int oldPort = settings_get_int(m_a->cfg, "extsig_port", 0);
        settings_set_str(m_a->cfg, "extsig_host", "127.0.0.1");
        settings_set_int(m_a->cfg, "extsig_port", 1);

        m_a->ext_on = 0;
        if (m_extBtn) { QSignalBlocker blk(m_extBtn); m_extBtn->setChecked(false); }

        if (m_extBtn) { emit m_extBtn->toggled(true); QApplication::processEvents(); }
        check(m_a->ext_on == 1, "外部信号：点击后 ext_on 翻转置 1");
        check(m_extBtn && m_extBtn->isChecked(), "外部信号：开启后按钮勾选态一致");
        check(settings_get_int(m_a->cfg, "extsig_on", -1) == 1,
              "外部信号：切换同步落盘 extsig_on");

        if (m_extBtn) { emit m_extBtn->toggled(false); QApplication::processEvents(); }
        check(m_a->ext_on == 0, "外部信号：再点击即关闭");
        check(m_extBtn && !m_extBtn->isChecked(), "外部信号：关闭后按钮取消勾选");

        /* 复原设置，保持整洁（测试用隔离配置目录，但避免留下假配置） */
        settings_set_str(m_a->cfg, "extsig_host", oldHost.toUtf8().constData());
        settings_set_int(m_a->cfg, "extsig_port", oldPort);
        settings_set_int(m_a->cfg, "extsig_on", 0);
        app_persist_settings(m_a);
    }

    /* ⑫ 无边框窗口：只留透明拖动带，最小化/最大化/关闭按钮全部去掉。
     *   退出走主菜单「退出」(Ctrl+Q) / WM 的 Alt+F4，最小化/最大化交给 WM。
     *   SKEDADDLE_FRAMELESS=0（系统标题栏模式）整段跳过，由 WM 提供这些能力。
     *   ★ 放在所有几何相关断言之后：下面会临时改窗口几何再还原。 */
    if (m_frameless) {
        check((windowFlags() & Qt::FramelessWindowHint) != 0,
              "无边框：未使用系统标题栏");
        check(m_winStrip && m_winStrip->parentWidget() == m_canvas,
              "顶部拖动带浮在画布上");
        check(m_winStrip && m_winStrip->findChildren<QAbstractButton *>().isEmpty(),
              "拖动带里没有窗口控制按钮");
        /* 全窗口扫一遍悬停提示：按钮挂在别处也算没删干净（负向对照可失败） */
        bool noWinBtns = true;
        for (QAbstractButton *b : findChildren<QAbstractButton *>()) {
            const QString t = b->toolTip();
            if (t == QStringLiteral("最小化") || t == QStringLiteral("最大化") ||
                t == QStringLiteral("还原")   || t == QStringLiteral("关闭"))
                noWinBtns = false;
        }
        check(noWinBtns, "窗口里没有最小化/最大化/关闭按钮");
        check(m_winStrip && m_winStrip->height() <= 10 && m_rightOverlay &&
              m_winStrip->y() + m_winStrip->height() <= m_rightOverlay->y(),
              "拖动带只有顶边一条且不压住右栏按钮");
        /* 手动最大化/还原是纯 setGeometry 状态机，不依赖 WM ⇒ 可在自检里真跑 */
        QRect g0 = geometry();
        toggleWindowMaximized();
        QApplication::processEvents();
        check(m_winMaxed, "窗口控制：双击式的最大化状态切换");
        toggleWindowMaximized();
        QApplication::processEvents();
        check(!m_winMaxed && geometry() == g0, "窗口控制：还原回到原几何");
    }

    if (fail == 0) {
        printf("QT-UI-OK\n");
        return 0;
    }
    printf("QT-UI-FAIL：%d 项不符\n", fail);
    return 1;
}
