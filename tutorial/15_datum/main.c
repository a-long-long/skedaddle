/* tutorial/15_datum/main.c — 进阶课 · 坐标系基准（datum）

 * 第 1 课讲了"内部坐标永远是 WGS84、量化成 1e-7 度"，但没讲：同一个物理点，
 * 在 北京1954 / 西安1980 这类"参心坐标系"下，经纬度会和 WGS84 差出几十米。
 * 本课把 datum.c 的核心数学（ECEF + 三参数赫尔默特 + 高斯-克吕格）重新实现一遍，
 * 让你亲手看到：
 *   1) 为什么 BJ54/XA80 必须靠平移参数体现（参心偏离地心近 200 m）；
 *   2) 一个点的"水平偏移"在北京约几十米、在西安约一百多米；
 *   3) "EPSG 没登记 XA80↔WGS84" 不等于"没参数"——公开文献有全国 3 参数，且能被
 *      国测局公布的图幅平移量区间交叉验证（落在区间内才可信）；
 *   4) 方里格（高斯-克吕格公里网）与经纬网是两套不同的网格，在视图里一个是直的、
 *      一个是弯的。
 *
 * 编译： gcc -std=c11 -O2 -o tut main.c && ./tut
 * （不依赖项目 src/，纯自包含；生产实现见 src/utils/datum.c）
 */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <math.h>
#include "../common.h"

#define W 640
#define H 420
static uint32_t buf[W * H];

/* 视图范围：中国大致范围 */
static double LON0 = 73.0, LON1 = 135.0;
static double LAT0 = 18.0, LAT1 = 54.0;
static int proj_x(double lon) { return (int)((lon - LON0) / (LON1 - LON0) * (W - 1)); }
static int proj_y(double lat) { return (int)((LAT1 - lat) / (LAT1 - LAT0) * (H - 1)); }

/* —— 椭球（长半轴 a / 扁率 f）—— */
typedef struct { double a, f; } Ell;
static const Ell E_WGS84 = { 6378137.0,       1.0 / 298.257223563 };
static const Ell E_BJ54  = { 6378245.0,       1.0 / 298.3 };   /* 克拉索夫斯基 1940 */
static const Ell E_XA80  = { 6378140.0,       1.0 / 298.257 };  /* IAG-75 */

/* 三参数（基准 → WGS84，与 +towgs84 同语义；旋转/尺度=0）。
 *   BJ54：EPSG:4214 默认 TOWGS84（PROJ/GDAL 同款，但适用区是塔里木盆地西南）。
 *   XA80：EPSG 没登记，公开文献（赵玉明等 2013）全国统一 3 参数。 */
static const double BJ54_T[3] = {  15.8, -154.4,  -82.3 };
static const double XA80_T[3] = { -104.57, -45.94,  5.30 };

/* 大地坐标 → ECEF（在给定椭球上） */
static void geod_to_ecef(double lat, double lon, double h, const Ell *e,
                         double *X, double *Y, double *Z)
{
    double la = lat * M_PI / 180.0, lo = lon * M_PI / 180.0;
    double e2 = e->f * (2.0 - e->f);
    double sl = sin(la), cl = cos(la);
    double N = e->a / sqrt(1.0 - e2 * sl * sl);
    *X = (N + h) * cl * cos(lo);
    *Y = (N + h) * cl * sin(lo);
    *Z = (N * (1.0 - e2) + h) * sl;
}

/* ECEF → 大地坐标（Bowring 闭式反算，精度 ~mm） */
static void ecef_to_geod(double X, double Y, double Z, const Ell *e,
                         double *lat, double *lon, double *h)
{
    *lon = atan2(Y, X) * 180.0 / M_PI;
    double b = e->a * (1.0 - e->f);
    double e2 = e->f * (2.0 - e->f);
    double ep2 = (e->a * e->a - b * b) / (b * b);
    double p = sqrt(X * X + Y * Y);
    double th = atan2(Z * e->a, p * b);
    double st = sin(th), ct = cos(th);
    *lat = atan2(Z + ep2 * b * st * st * st, p - e2 * e->a * ct * ct * ct);
    double N = e->a / sqrt(1.0 - e2 * sin(*lat) * sin(*lat));
    if (h) *h = p / cos(*lat) - N;
    *lat *= 180.0 / M_PI;
}

/* WGS84 → 基准（读数/标注用）：X_src = X_wgs − T，再投到基准椭球。 */
static void wgs84_to_datum(const Ell *dst, const double T[3],
                           double lat, double lon, double *olat, double *olon)
{
    double Xw, Yw, Zw;
    geod_to_ecef(lat, lon, 0.0, &E_WGS84, &Xw, &Yw, &Zw);
    double Xs = Xw - T[0], Ys = Yw - T[1], Zs = Zw - T[2];
    ecef_to_geod(Xs, Ys, Zs, dst, olat, olon, NULL);
}

/* 高斯-克吕格正算（到 1.5° 项；Y 含 500 km 假东偏移） */
static void gauss_fwd(const Ell *e, double lon0, double lat, double lon,
                      double *X, double *Y)
{
    double e2 = e->f * (2.0 - e->f);
    double L  = (lon - lon0) * M_PI / 180.0;
    double B  = lat * M_PI / 180.0;
    double sinB = sin(B), cosB = cos(B), tanB = tan(B);
    double N = e->a / sqrt(1.0 - e2 * sinB * sinB);
    double T = tanB * tanB;
    double C = (e2 / (1.0 - e2)) * cosB * cosB;
    double Ap = cosB * L; double A2 = Ap * Ap, A4 = A2 * A2, A6 = A4 * A2;
    double M = e->a * ((1.0 - e2 / 4.0 - 3.0 * e2 * e2 / 64.0 - 5.0 * e2 * e2 * e2 / 256.0) * B
                    - (3.0 * e2 / 8.0 + 3.0 * e2 * e2 / 32.0 + 45.0 * e2 * e2 * e2 / 1024.0) * sin(2.0 * B)
                    + (15.0 * e2 * e2 / 256.0 + 45.0 * e2 * e2 * e2 / 1024.0) * sin(4.0 * B)
                    - (35.0 * e2 * e2 * e2 / 3072.0) * sin(6.0 * B));
    *X = M + N * tanB * (A2 / 2.0
                    + (5.0 - T + 9.0 * C + 4.0 * C * C) * A4 / 24.0
                    + (61.0 - 58.0 * T + T * T + 600.0 * C - 330.0 * e2) * A6 / 720.0);
    *Y = N * (Ap + (1.0 - T + C) * Ap * A2 / 6.0
                    + (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * e2) * Ap * A4 / 120.0)
         + 500000.0;
}

/* 高斯-克吕格反算（平面 → 经纬度，用同一 lon0 才闭合） */
static void gauss_inv(const Ell *e, double lon0, double X, double Y,
                      double *lat, double *lon)
{
    double e2 = e->f * (2.0 - e->f);
    double e1 = (1.0 - sqrt(1.0 - e2)) / (1.0 + sqrt(1.0 - e2));
    double yc = Y - 500000.0;
    double M = X;
    double mu = M / (e->a * (1.0 - e2 / 4.0 - 3.0 * e2 * e2 / 64.0 - 5.0 * e2 * e2 * e2 / 256.0));
    double B0 = mu + (3.0 * e1 / 2.0 - 27.0 * e1 * e1 * e1 / 32.0) * sin(2.0 * mu)
                   + (21.0 * e1 * e1 / 16.0 - 55.0 * e1 * e1 * e1 * e1 / 32.0) * sin(4.0 * mu)
                   + (151.0 * e1 * e1 * e1 / 96.0) * sin(6.0 * mu)
                   + (1097.0 * e1 * e1 * e1 * e1 / 512.0) * sin(8.0 * mu);
    double sinB0 = sin(B0), cosB0 = cos(B0), tanB0 = tan(B0);
    double Ww = sqrt(1.0 - e2 * sinB0 * sinB0);
    double N0 = e->a / Ww;
    double T0 = tanB0 * tanB0;
    double C0 = (e2 / (1.0 - e2)) * cosB0 * cosB0;
    double R0 = e->a * (1.0 - e2) / pow(Ww, 1.5);
    double D = yc / N0; double D2 = D * D, D4 = D2 * D2, D6 = D4 * D2;
    double B = B0 - (N0 * tanB0 / R0) * (D2 / 2.0
                    - (5.0 + 3.0 * T0 + 10.0 * C0 - 4.0 * C0 * C0 - 9.0 * e2) * D4 / 24.0
                    + (61.0 + 90.0 * T0 + 298.0 * C0 + 45.0 * T0 * T0 - 252.0 * e2) * D6 / 720.0);
    double L = (D - (1.0 + 2.0 * T0 + C0) * D2 * D / 6.0
                + (5.0 - 2.0 * C0 + 28.0 * T0 - 3.0 * C0 * C0 + 8.0 * e2 + 24.0 * T0 * T0) * D4 * D / 120.0)
               / cosB0;
    *lat = B * 180.0 / M_PI;
    *lon = lon0 + L * 180.0 / M_PI;
}

/* 经纬度差 → 北/东向米数（局部曲率半径） */
static void ddeg_to_m(double lat, double dlat, double dlon, double *dn, double *de)
{
    double e2 = E_WGS84.f * (2.0 - E_WGS84.f);
    double la = lat * M_PI / 180.0;
    double sl = sin(la), cl = cos(la);
    double Ww = sqrt(1.0 - e2 * sl * sl);
    double N = E_WGS84.a / Ww;
    double M = E_WGS84.a * (1.0 - e2) / (Ww * Ww * Ww);
    *dn = dlat * M * M_PI / 180.0;
    *de = dlon * N * cl * M_PI / 180.0;
}

/* 画一条高斯"方里格"线：固定 X（北向）或固定 Y（东向），扫另一维后投到屏幕。
 * lon0 取 117°（北京所在 3° 带中央经线）。线在视图外的段靠 tut_pp 逐点裁剪。 */
static void draw_gauss_line(double lon0, int fix_x, double fixed, double lo, double hi,
                            uint32_t c)
{
    const int STEPS = 200;
    double prev_x = 0, prev_y = 0, has = 0;
    for (int i = 0; i <= STEPS; i++) {
        double t = lo + (hi - lo) * i / STEPS;
        double X = fix_x ? fixed : t;
        double Y = fix_x ? t : fixed;
        double lat, lon;
        gauss_inv(&E_WGS84, lon0, X, Y, &lat, &lon);
        double px = (double)proj_x(lon), py = (double)proj_y(lat);
        if (has) tut_line(buf, W, H, (int)prev_x, (int)prev_y, (int)px, (int)py, c);
        prev_x = px; prev_y = py; has = 1;
    }
}

int main(void)
{
    tut_cls(buf, W, H, 0xFFF4F1EA);

    /* (A) 经纬网（度）：每 10° 一条，灰色 —— 这是"显示层"最朴素的网格 */
    for (int lon = (int)LON0; lon <= (int)LON1; lon += 10) {
        int x = proj_x(lon);
        tut_line(buf, W, H, x, 0, x, H - 1, 0xFFD8D2C8);
    }
    for (int lat = (int)LAT0; lat <= (int)LAT1; lat += 10) {
        int y = proj_y(lat);
        tut_line(buf, W, H, 0, y, W - 1, y, 0xFFD8D2C8);
    }

    /* (B) 方里格（高斯-克吕格公里网）：每 100 km 一条，蓝色——注意它是"弯"的。
     * 仅画北京所在 3° 带（lon0=117°）内的网格，作为对照。 */
    double lon0 = 117.0;
    for (double east = 350000.0; east <= 650000.0; east += 100000.0)
        draw_gauss_line(lon0, 0, east, 3.7e6, 5.1e6, 0xFF2F6FB0);   /* 固定 Y（东向线）*/
    for (double north = 3.8e6; north <= 5.0e6; north += 100000.0)
        draw_gauss_line(lon0, 1, north, 350000.0, 650000.0, 0xFF2F6FB0); /* 固定 X（北向线）*/

    /* 标出北京，方便对照两套网格 */
    double bj_lat = 39.9042, bj_lon = 116.4074;
    int bx = proj_x(bj_lon), by = proj_y(bj_lat);
    tut_fill_rect(buf, W, H, bx - 3, by - 3, 7, 7, 0xFFB03A1F);

    /* ===== 数值演示（核心教学在 stdout） ===== */
    printf("=== 基准平移参数（基准 → WGS84，米） ===\n");
    printf("BJ54 : dx=%9.2f dy=%9.2f dz=%9.2f\n", BJ54_T[0], BJ54_T[1], BJ54_T[2]);
    printf("XA80 : dx=%9.2f dy=%9.2f dz=%9.2f\n", XA80_T[0], XA80_T[1], XA80_T[2]);

    /* 北京在两种基准下的水平偏移 */
    double bj_bj54_lat, bj_bj54_lon, bj_xa80_lat, bj_xa80_lon;
    wgs84_to_datum(&E_BJ54, BJ54_T, bj_lat, bj_lon, &bj_bj54_lat, &bj_bj54_lon);
    wgs84_to_datum(&E_XA80, XA80_T, bj_lat, bj_lon, &bj_xa80_lat, &bj_xa80_lon);
    double bj_n, bj_e, xa_n, xa_e;
    ddeg_to_m(bj_lat, bj_bj54_lat - bj_lat, bj_bj54_lon - bj_lon, &bj_n, &bj_e);
    ddeg_to_m(bj_lat, bj_xa80_lat - bj_lat, bj_xa80_lon - bj_lon, &xa_n, &xa_e);
    printf("\n=== 北京(%.4f,%.4f) 在两种基准下的水平偏移 ===\n", bj_lat, bj_lon);
    printf("BJ54 : 北 %7.1f m  东 %7.1f m  (合 %.1f m)\n", bj_n, bj_e, sqrt(bj_n*bj_n+bj_e*bj_e));
    printf("XA80 : 北 %7.1f m  东 %7.1f m  (合 %.1f m)\n", xa_n, xa_e, sqrt(xa_n*xa_n+xa_e*xa_e));
    printf("  → 参心坐标系原点偏离地心近 200 m，必须靠平移参数体现；\n"
           "    否则\"仅换椭球\"等于没转换（XA80 水平差仅 0.019 m）。\n");

    /* 西安80 双重证据交叉验证：在泾阳大地原点附近，XA80→WGS84 的北/东偏移应落进
     * 国测局公布的图幅平移量区间（北 −9~+43 m、东 +76~+119 m）。
     * 注意方向：上面 wgs84_to_datum 算的是「WGS84→XA80」偏移；而工程记录（赵玉明等
     * 2013 + 国测局区间）用的是「XA80→WGS84」偏移。三参数是常量平移，两个方向严格
     * 反对称（取负号即互逆），所以这里取反得到 XA80→WGS84，再对区间判据。 */
    double xa_yang_lat = 34.5, xa_yang_lon = 108.9;
    double yy_lat, yy_lon;
    wgs84_to_datum(&E_XA80, XA80_T, xa_yang_lat, xa_yang_lon, &yy_lat, &yy_lon);
    double yy_n, yy_e;
    ddeg_to_m(xa_yang_lat, yy_lat - xa_yang_lat, yy_lon - xa_yang_lon, &yy_n, &yy_e);
    double wgs_n = -yy_n, wgs_e = -yy_e;   /* XA80→WGS84 = −(WGS84→XA80) */
    printf("=== XA80 交叉验证（泾阳大地原点附近 %.1f,%.1f） ===\n", xa_yang_lat, xa_yang_lon);
    printf("WGS84→XA80 : 北 %7.1f m  东 %7.1f m\n", yy_n, yy_e);
    printf("XA80→WGS84 : 北 %7.1f m  东 %7.1f m  (反向，= 国测局图幅平移量)\n", wgs_n, wgs_e);
    int ok_n = (wgs_n > -9.0 && wgs_n < 43.0);
    int ok_e = (wgs_e > 76.0 && wgs_e < 119.0);
    printf("国测局区间：北[−9,+43] 东[+76,+119] → %s / %s\n",
           ok_n ? "命中" : "不中", ok_e ? "命中" : "不中");
    printf("  → \"EPSG 没登记\" ≠ \"没有参数\"：文献参数 + 官方区间互印才落值。\n");

    /* 高斯-克吕格坐标（方里格读数）：北京，lon0=117 */
    double gX, gY;
    gauss_fwd(&E_WGS84, lon0, bj_lat, bj_lon, &gX, &gY);
    printf("\n=== 北京高斯-克吕格坐标（lon0=117°）===\n");
    printf("X(北) = %.1f m   Y(东,含500km假偏移) = %.1f m  → 读数东 = %.1f km\n",
           gX, gY, (gY - 500000.0) / 1000.0);

    if (tut_write_bmp("datum.bmp", buf, W, H) != 0) { fprintf(stderr, "write failed\n"); return 1; }
    printf("\nOK: wrote datum.bmp — 灰=经纬网(直) / 蓝=方里格(弯) / 红=北京\n");
    return 0;
}
