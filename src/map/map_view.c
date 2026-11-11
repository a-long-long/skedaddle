#include "map/map_view.h"
#include "utils/geo.h"
#include <math.h>

void mv_init(MapView *v, double lat, double lon, double zoom, int w, int h)
{
    v->lat = lat; v->lon = lon; v->zoom = zoom; v->w = w; v->h = h;
    /* ★ 显式清零旋转与投影缓存：本函数可能在**复用**的 MapView 上调用
     * （改窗口尺寸 / 换视图），不写这两行就会带着上一份 rot 与 stale 缓存
     * 继续画，且 c_rot 恰好相等时惰性失效判据还发现不了。 */
    v->rot = 0.0;
    v->c_valid = 0;
}

double mv_scale(const MapView *v)
{
    return 256.0 * pow(2.0, v->zoom);
}

/* 填好"视口中心墨卡托 + 缩放"这份缓存。
 * 为什么值得：一帧里 mv_project 被调用几十万到几百万次（z9 光路面就有
 * 上百万次投影），而视口中心和缩放是常量——原先每次都替它们重算一遍
 * sin + log + pow，等于把投影成本翻了一倍。
 * 惰性失效（比对缓存键）而不是要求调用方通知：window.c 里到处是直接改
 * view.lat / view.zoom 的，要求"改完必须调 invalidate"迟早会漏一处，
 * 漏了就是画面错位——这类 bug 靠肉眼很难发现。 */
static void mv_prepare(const MapView *v)
{
    MapView *m = (MapView *)v;        /* 只写缓存字段，逻辑上仍是 const */
    if (m->c_valid &&
        m->c_lat == v->lat && m->c_lon == v->lon && m->c_zoom == v->zoom &&
        m->c_rot == v->rot &&
        m->c_w == v->w && m->c_h == v->h)
        return;
    sk_mercator(v->lat, v->lon, &m->cx, &m->cy);
    m->scale = 256.0 * pow(2.0, v->zoom);
    m->c_lat = v->lat; m->c_lon = v->lon; m->c_zoom = v->zoom;
    m->c_rot = v->rot;
    m->c_w = v->w; m->c_h = v->h;
    m->c_valid = 1;
}

int mv_project(const MapView *v, double lat, double lon, int *x, int *y)
{
    mv_prepare(v);
    MapView *m = (MapView *)v;
    /* ★ 这里不用 sk_mercator：那个 sin+log 版本在每帧几十万次调用的场合
       是真花钱的（z9 一帧 30 ms）。墨卡托的 x 本来就只跟经度线性相关，
       y 只跟纬度相关且足够光滑 → 走查表插值版，见 geo.c 的误差账。 */
    double wx = (lon + 180.0) / 360.0;
    double wy = sk_mercator_y_fast(lat);
    double sx = (wx - m->cx) * m->scale;
    double sy = (wy - m->cy) * m->scale;
    if (m->rot != 0.0) {
        double a = -m->rot * (M_PI / 180.0);
        double ca = cos(a), sa = sin(a);
        double rx = sx * ca - sy * sa;
        double ry = sx * sa + sy * ca;
        sx = rx; sy = ry;
    }
    double px = sx + v->w / 2.0;
    double py = sy + v->h / 2.0;
    *x = (int)lround(px);
    *y = (int)lround(py);
    return (*x < 0 || *y < 0 || *x >= v->w || *y >= v->h) ? 1 : 0;
}

void mv_proj_begin(const MapView *v, MvProj *p)
{
    mv_prepare(v);                 /* 与 mv_project 同一份缓存，值完全一致 */
    MapView *m = (MapView *)v;
    p->cx = m->cx; p->cy = m->cy; p->scale = m->scale;
    p->hw = v->w / 2.0; p->hh = v->h / 2.0;
    p->rot = v->rot;
}

void mv_unproject(const MapView *v, int x, int y, double *lat, double *lon)
{
    mv_prepare(v);
    MapView *m = (MapView *)v;
    double sx = x - v->w / 2.0;
    double sy = y - v->h / 2.0;
    if (m->rot != 0.0) {
        double a = m->rot * (M_PI / 180.0);
        double ca = cos(a), sa = sin(a);
        double rx = sx * ca - sy * sa;
        double ry = sx * sa + sy * ca;
        sx = rx; sy = ry;
    }
    double wx = m->cx + sx / m->scale;
    double wy = m->cy + sy / m->scale;
    sk_mercator_inv(wx, wy, lat, lon);
}

void mv_visible_bbox(const MapView *v, double bbox[4])
{
    /* rot==0：沿用原来的「左下 + 右上两角」精确口径，逐位不变。 */
    if (v->rot == 0.0) {
        double la0, lo0, la1, lo1;
        mv_unproject(v, 0, v->h, &la0, &lo0);          /* 左下 */
        mv_unproject(v, v->w, 0, &la1, &lo1);          /* 右上 */
        bbox[0] = la0; bbox[1] = lo0; bbox[2] = la1; bbox[3] = lo1;
        return;
    }

    /* ★★ 旋转视图下只用对角两角会把 bbox 算小：
     *   屏幕矩形反投影到墨卡托平面是一个**旋转过的**矩形，它的最北/最南
     *   点可能落在另外两个角上。bbox 估小 ⇒ paint_areas/paint_lines 的
     *   地理裁剪把本该画出来的要素整块剔掉 ⇒ 车头朝上时地图缺块。
     *   四个角全反投影再取 min/max，得到的是包住旋转矩形的轴对齐超集
     *   （墨卡托坐标与经纬度单调对应 ⇒ 四角极值即整块极值）。 */
    const int xs[4] = { 0, v->w, 0, v->w };
    const int ys[4] = { 0, 0, v->h, v->h };
    double la0 = 90.0, lo0 = 180.0, la1 = -90.0, lo1 = -180.0;
    for (int k = 0; k < 4; k++) {
        double la, lo;
        mv_unproject(v, xs[k], ys[k], &la, &lo);
        if (la < la0) la0 = la;
        if (la > la1) la1 = la;
        if (lo < lo0) lo0 = lo;
        if (lo > lo1) lo1 = lo;
    }
    bbox[0] = la0; bbox[1] = lo0; bbox[2] = la1; bbox[3] = lo1;
}

/* 屏幕位移向量 → 墨卡托平面位移向量（除以 scale 之前）。
 * 正投影是 R(-rot)，反变换就是 R(+rot)；rot==0 时保持原向量不变。
 * ★ 抽出来给 pan / zoom_at 共用，省得两处各写一遍还写反符号。 */
static void screen_to_world_vec(const MapView *v, double x, double y,
                                double *wx, double *wy)
{
    if (v->rot == 0.0) { *wx = x; *wy = y; return; }
    double a = v->rot * (M_PI / 180.0);
    double ca = cos(a), sa = sin(a);
    *wx = x * ca - y * sa;
    *wy = x * sa + y * ca;
}

void mv_pan_pixels(MapView *v, int dx, int dy)
{
    double cx, cy;
    sk_mercator(v->lat, v->lon, &cx, &cy);
    double s = mv_scale(v);
    /* 拖动 (dx,dy) ⇒ 中心点在**屏幕**上反向移动 (-dx,-dy)；
     * 旋转视图下必须先把这个屏幕向量反变换回墨卡托平面，
     * 否则 Ctrl+拖动/普通拖动的方向会与画面差一个 rot 角。 */
    double ux, uy;
    screen_to_world_vec(v, -(double)dx, -(double)dy, &ux, &uy);
    double wx = cx + ux / s;
    double wy = cy + uy / s;
    if (wy < 0) wy = 0; else if (wy > 1) wy = 1;
    sk_mercator_inv(wx, wy, &v->lat, &v->lon);
    if (v->lon < -180) v->lon += 360;
    if (v->lon >  180) v->lon -= 360;
}

void mv_zoom_at(MapView *v, double dzoom, int px, int py)
{
    /* 锚点：缩放前后该屏幕点对应的经纬度不变 */
    double alat, alon;
    mv_unproject(v, px, py, &alat, &alon);
    v->zoom += dzoom;
    double ax, ay, cx, cy;
    sk_mercator(alat, alon, &ax, &ay);
    sk_mercator(v->lat, v->lon, &cx, &cy);
    double s = mv_scale(v);
    /* ★ 旋转视图下锚点的屏幕偏移不能直接当世界偏移：
     *   正投影是 R(-rot) ⇒ 世界偏移 = R(+rot) × 屏幕偏移 / scale。
     *   漏了这一步，滚轮缩放时鼠标按住的地点会随 rot 角漂移。
     *   rot==0 时反变换是恒等，算式与改造前逐位一致。 */
    double ox, oy;
    screen_to_world_vec(v, px - v->w / 2.0, py - v->h / 2.0, &ox, &oy);
    double wx = ax - ox / s;
    double wy = ay - oy / s;
    if (wy < 0) wy = 0; else if (wy > 1) wy = 1;
    sk_mercator_inv(wx, wy, &v->lat, &v->lon);
}

void mv_clamp_zoom(MapView *v, double zmin, double zmax)
{
    if (v->zoom < zmin) v->zoom = zmin;
    if (v->zoom > zmax) v->zoom = zmax;
}

void mv_fit_bbox(MapView *v, double lat0, double lon0,
                 double lat1, double lon1, double margin_px)
{
    double x0, y0, x1, y1;
    sk_mercator(lat0, lon0, &x0, &y0);
    sk_mercator(lat1, lon1, &x1, &y1);
    double dx = fabs(x1 - x0), dy = fabs(y1 - y0);
    /* ★ 旋转视图下要装进视口的是**旋转之后**的矩形：把 4 个角各自按
     * 正投影 R(-rot) 转到屏幕空间，取 x/y 的最大半跨度。仍用未旋转的
     * dx/dy 会低估所需缩放（尤其 rot≈45° 时差 √2 倍），路线边框会被切掉。 */
    if (v->rot != 0.0) {
        double mx = (x0 + x1) / 2.0, my = (y0 + y1) / 2.0;
        double a = -v->rot * (M_PI / 180.0);
        double ca = cos(a), sa = sin(a);
        const double cxs[4] = { x0, x0, x1, x1 };
        const double cys[4] = { y0, y1, y0, y1 };
        double ex = 0.0, ey = 0.0;
        for (int k = 0; k < 4; k++) {
            double rx = cxs[k] - mx, ry = cys[k] - my;
            double sx = fabs(rx * ca - ry * sa);
            double sy = fabs(rx * sa + ry * ca);
            if (sx > ex) ex = sx;
            if (sy > ey) ey = sy;
        }
        dx = 2.0 * ex;
        dy = 2.0 * ey;
    }
    if (dx < 1e-9) dx = 1e-9;
    if (dy < 1e-9) dy = 1e-9;

    double aw = (double)v->w - 2.0 * margin_px;
    double ah = (double)v->h - 2.0 * margin_px;
    if (aw < 32) aw = 32;
    if (ah < 32) ah = 32;
    double s = fmin(aw / dx, ah / dy);      /* 像素/世界单位 = 256 * 2^zoom */
    v->zoom = log2(s / 256.0);

    /* 中心 = 墨卡托矩形的中点（不是经纬度中点，否则高纬会偏） */
    sk_mercator_inv((x0 + x1) / 2.0, (y0 + y1) / 2.0, &v->lat, &v->lon);
}
