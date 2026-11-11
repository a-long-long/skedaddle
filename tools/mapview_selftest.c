/* skedaddle_mapview_selftest —— 旋转视图（车头朝上）的坐标链验收
 *
 * 为什么单独开一个工具：「车头朝上」把 MapView.rot 从「用户手动 Ctrl+拖动
 * 的装饰性角度」升级成「每帧都在变的导航视角」。原本投影层只在
 * mv_proj / mv_unproject 里支持 rot，其余四个接口仍假设「屏幕 y ↔ 纬度
 * 单调」：
 *   · mv_visible_bbox 只反投影对角两角 ⇒ 旋转后 bbox 偏小 ⇒ 渲染裁掉要素；
 *   · mv_pan_pixels / mv_zoom_at 把屏幕位移当世界位移 ⇒ 拖拽/缩放锚点漂；
 *   · mv_fit_bbox 用未旋转的世界矩形算缩放 ⇒ 45° 时差 √2 倍，路线出框；
 *   · paint_nav_car 没减 rot ⇒ 车标跟着地图一起转，反而永远指不对。
 * 这些都是**数值**问题，靠看 BMP 判不出来，必须用像素/坐标做判据。
 *
 * ★ 负向对照：所有 rot==0 的分支都保留改造前的算式，本工具同时验证
 *   「rot=0 时与旧公式逐位一致」；旋转用例期望值都由
 *   R(-rot) / R(+rot) 手工推出来，不用被测函数的输出当真值。
 *
 * 用法：./skedaddle_mapview_selftest   （全过打印 MAPVIEW-OK）
 */
#include "map/map_view.h"
#include "render/navdraw.h"
#include "render/raster.h"
#include "utils/geo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do {                                            \
    if (!(cond)) {                                                       \
        fprintf(stderr, "ASSERT-FAIL %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        g_fail++;                                                       \
    }                                                                   \
} while (0)

#define W 1280
#define H 800
#define LAT0 39.90
#define LON0 116.40
#define ZOOM 15.0

static MapView mk(double rot)
{
    MapView v;
    mv_init(&v, LAT0, LON0, ZOOM, W, H);
    v.rot = rot;
    return v;
}

/* 旧版（rot=0）投影公式的独立复刻：不调用被测函数，专门当逐位对照真值。 */
static void old_project(const MapView *v, double lat, double lon, int *x, int *y)
{
    double cx, cy, s;
    sk_mercator(v->lat, v->lon, &cx, &cy);
    s = 256.0 * pow(2.0, v->zoom);
    double wx = (lon + 180.0) / 360.0;
    double wy = sk_mercator_y_fast(lat);
    *x = (int)lround((wx - cx) * s + v->w / 2.0);
    *y = (int)lround((wy - cy) * s + v->h / 2.0);
}

/* --------------------------------------------------------------------------
 * 1) rot=0 逐位对照：不能因为加了旋转支持就把默认视角改掉半像素。
 * ------------------------------------------------------------------------ */
static void case_rot0_identical(void)
{
    MapView v = mk(0.0);
    const double pts[][2] = {
        { LAT0, LON0 }, { 39.95, 116.35 }, { 39.80, 116.55 },
        { 40.10, 116.20 }, { 39.70, 116.80 }, { 31.20, 121.50 },
    };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        int x, y, ox, oy;
        mv_project(&v, pts[i][0], pts[i][1], &x, &y);
        old_project(&v, pts[i][0], pts[i][1], &ox, &oy);
        CHECK(x == ox && y == oy, "rot=0 的 mv_project 必须与旧公式逐位一致");
    }

    /* 旧 mv_visible_bbox = 左下 + 右上两角 */
    double bbox[4], la0, lo0, la1, lo1;
    mv_visible_bbox(&v, bbox);
    mv_unproject(&v, 0, H, &la0, &lo0);
    mv_unproject(&v, W, 0, &la1, &lo1);
    CHECK(bbox[0] == la0 && bbox[1] == lo0 && bbox[2] == la1 && bbox[3] == lo1,
          "rot=0 的可见 bbox 必须仍是原来的对角两角口径");

    /* mv_init 复用语义：带 rot 的旧结构重新 init 后必须回正北、缓存失效 */
    MapView r = mk(73.0);
    int dummyx, dummyy;
    mv_project(&r, LAT0, LON0, &dummyx, &dummyy);   /* 填热缓存 */
    (void)dummyx; (void)dummyy;
    mv_init(&r, 31.2, 121.5, 10.0, 640, 480);
    CHECK(r.rot == 0.0 && r.c_valid == 0,
          "mv_init 必须显式清 rot 与投影缓存（复用 MapView 时不残留）");
}

/* --------------------------------------------------------------------------
 * 2) 投影 / 反投影在各种旋转角下互逆。
 * ------------------------------------------------------------------------ */
static void case_roundtrip(void)
{
    const double rots[] = { 0.0, 30.0, 90.0, -45.0, 179.0, -170.0 };
    const double la[] = { 39.90, 39.93, 39.87, 40.02 };
    const double lo[] = { 116.40, 116.44, 116.36, 116.20 };
    for (size_t r = 0; r < sizeof rots / sizeof rots[0]; r++) {
        MapView v = mk(rots[r]);
        for (int i = 0; i < 4; i++) {
            int x, y;
            mv_project(&v, la[i], lo[i], &x, &y);
            double bla, blo;
            mv_unproject(&v, x, y, &bla, &blo);
            CHECK(fabs(bla - la[i]) < 2e-4 && fabs(blo - lo[i]) < 2e-4,
                  "各 rot 下 project/unproject 必须互逆（1px 量化容差内）");
        }
    }
    /* ★ 语义判据：rot=90 时「东」应当出现在屏幕正上方（车头朝东 = 东朝上） */
    MapView e = mk(90.0);
    int x0, y0, xe, ye, xn, yn;
    mv_project(&e, LAT0, LON0, &x0, &y0);
    mv_project(&e, LAT0, LON0 + 0.002, &xe, &ye);   /* 正东 */
    mv_project(&e, LAT0 + 0.002, LON0, &xn, &yn);   /* 正北 */
    CHECK(ye < y0 - 10 && abs(xe - x0) < 3,
          "rot=90 时正东应指向屏幕上方（车头朝上的含义）");
    CHECK(xn < x0 - 10 && abs(yn - y0) < 3,
          "rot=90 时正北应指向屏幕左方（指北针据此修正符号）");
}

/* --------------------------------------------------------------------------
 * 3) 旋转下的可见 bbox 必须是四角超集，且明显大于旧的「对角两角」估计。
 * ------------------------------------------------------------------------ */
static void case_bbox_superset(void)
{
    const double rots[] = { 30.0, 45.0, 90.0, 135.0, -60.0 };
    for (size_t r = 0; r < sizeof rots / sizeof rots[0]; r++) {
        MapView v = mk(rots[r]);
        double bbox[4];
        mv_visible_bbox(&v, bbox);

        const int xs[4] = { 0, W, 0, W }, ys[4] = { 0, 0, H, H };
        double la0 = 90, lo0 = 180, la1 = -90, lo1 = -180;
        for (int k = 0; k < 4; k++) {
            double la, lo;
            mv_unproject(&v, xs[k], ys[k], &la, &lo);
            if (la < la0) la0 = la;
            if (la > la1) la1 = la;
            if (lo < lo0) lo0 = lo;
            if (lo > lo1) lo1 = lo;
        }
        CHECK(bbox[0] <= la0 + 1e-9 && bbox[2] >= la1 - 1e-9 &&
              bbox[1] <= lo0 + 1e-9 && bbox[3] >= lo1 - 1e-9,
              "旋转 bbox 必须包住四个屏幕角（否则地理裁剪会漏画）");

        /* 旧的「左下 + 右上」口径 */
        double ola0, olo0, ola1, olo1;
        mv_unproject(&v, 0, H, &ola0, &olo0);
        mv_unproject(&v, W, 0, &ola1, &olo1);
        double old_span = (ola1 - ola0) + (olo1 - olo0);
        double new_span = (bbox[2] - bbox[0]) + (bbox[3] - bbox[1]);
        CHECK(new_span > old_span + 1e-6,
              "旋转下四角 bbox 必须严格大于旧的两角 bbox（负向对照要能失败）");
    }
}

/* --------------------------------------------------------------------------
 * 4) 锚点缩放 / 平移 / 路线适配，在旋转下都要守住「屏幕点不动」。
 * ------------------------------------------------------------------------ */
static void case_zoom_pan_fit(void)
{
    const double rots[] = { 0.0, 30.0, 90.0, -45.0 };
    for (size_t r = 0; r < sizeof rots / sizeof rots[0]; r++) {
        /* 4a) 滚轮缩放：px,py 处的经纬度不变 */
        MapView v = mk(rots[r]);
        int px = 913, py = 217;
        double la_before, lo_before, la_after, lo_after;
        mv_unproject(&v, px, py, &la_before, &lo_before);
        mv_zoom_at(&v, 1.0, px, py);
        mv_unproject(&v, px, py, &la_after, &lo_after);
        CHECK(fabs(la_after - la_before) < 2e-4 &&
              fabs(lo_after - lo_before) < 2e-4,
              "旋转视图下滚轮锚点必须不动");

        /* 4b) 拖动：原本在屏幕中心的固定地理点，应正好移动 (dx,dy) */
        MapView p = mk(rots[r]);
        int dx = 40, dy = -25;
        mv_pan_pixels(&p, dx, dy);
        int cx, cy;
        mv_project(&p, LAT0, LON0, &cx, &cy);
        CHECK(abs(cx - (W / 2 + dx)) <= 1 && abs(cy - (H / 2 + dy)) <= 1,
              "旋转视图下拖动方向必须与屏幕位移一致");

        /* 4c) 缩放到路线：旋转后的四个角都应落在画布内（margin=0） */
        MapView f = mk(rots[r]);
        mv_fit_bbox(&f, 39.88, 116.36, 39.93, 116.45, 0.0);
        int ok = 1;
        const double la[2] = { 39.88, 39.93 }, lo[2] = { 116.36, 116.45 };
        for (int i = 0; i < 2; i++) for (int k = 0; k < 2; k++) {
            int qx, qy;
            mv_project(&f, la[i], lo[k], &qx, &qy);
            if (qx < -2 || qy < -2 || qx > W + 2 || qy > H + 2) ok = 0;
        }
        CHECK(ok, "mv_fit_bbox 必须按旋转后的屏幕跨度算缩放，四角不出框");
    }
}

/* --------------------------------------------------------------------------
 * 5) 车标：rot == heading 时恒指屏幕上方；rot=0 时与旧行为一致。
 *
 * 判据用**像素统计**（蓝色三角形的包围盒），不用「看着像不像」。
 * ------------------------------------------------------------------------ */
#define CARW 80
#define CARH 80

static void car_pixels(double heading, double rot, uint32_t *buf,
                       int *minx, int *maxx, int *miny, int *maxy, int *nblue)
{
    Raster rs;
    memset(buf, 0, sizeof(uint32_t) * CARW * CARH);
    raster_bind_mem(&rs, buf, CARW, CARH, CARW,
                    0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0xff000000u);
    MapView v = mk(rot);
    v.w = CARW; v.h = CARH;
    paint_nav_car(&rs, &v, LAT0, LON0, heading);
    uint32_t blue = raster_rgb(&rs, 20, 90, 210);
    *minx = *miny = 1 << 30;
    *maxx = *maxy = -1;
    *nblue = 0;
    for (int y = 0; y < CARH; y++) for (int x = 0; x < CARW; x++) {
        if (rs.px[(size_t)y * CARW + x] != blue) continue;
        (*nblue)++;
        if (x < *minx) *minx = x;
        if (x > *maxx) *maxx = x;
        if (y < *miny) *miny = y;
        if (y > *maxy) *maxy = y;
    }
}

static void case_nav_car(void)
{
    static uint32_t a[CARW * CARH], b[CARW * CARH], c[CARW * CARH];
    int ax0, ax1, ay0, ay1, an;
    int bx0, bx1, by0, by1, bn;
    int cx0, cx1, cy0, cy1, cn;

    /* ① 车头朝东(90) + 地图转 90 ⇒ 车标应指屏幕上方 */
    car_pixels(90.0, 90.0, a, &ax0, &ax1, &ay0, &ay1, &an);
    CHECK(an > 50, "车标蓝色三角形应被画出来");
    CHECK(ay0 <= 26 && ay1 >= 46 && abs((ax0 + ax1) / 2 - CARW / 2) <= 2,
          "rot==heading 时车标必须恒指屏幕正上方（地图在脚下转）");

    /* ② 正北朝上 + 车头朝东 ⇒ 车标指右（旧行为） */
    car_pixels(90.0, 0.0, b, &bx0, &bx1, &by0, &by1, &bn);
    CHECK(bx1 >= 54 && bx0 <= 34 && abs((by0 + by1) / 2 - CARH / 2) <= 2,
          "rot=0 时车头朝东应指屏幕右方（旧行为不变）");

    /* ③ 逐像素恒等：heading=0/rot=0 与 heading=90/rot=90 画出的车标相同，
     *    这正是「车标屏幕角 = heading - rot」的可证伪推论。 */
    car_pixels(0.0, 0.0, c, &cx0, &cx1, &cy0, &cy1, &cn);
    CHECK(memcmp(a, c, sizeof a) == 0,
          "heading-rot 相等的两组参数必须逐像素相同");
    CHECK(memcmp(a, b, sizeof a) != 0,
          "负向对照：rot=0 与 rot=heading 的车标必须不同");
}

int main(void)
{
    case_rot0_identical();
    case_roundtrip();
    case_bbox_superset();
    case_zoom_pan_fit();
    case_nav_car();
    if (g_fail == 0) { printf("MAPVIEW-OK\n"); return 0; }
    fprintf(stderr, "MAPVIEW FAILED: %d 项\n", g_fail);
    return 1;
}
