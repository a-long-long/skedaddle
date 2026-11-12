#include "render/labels.h"
#include "render/band.h"
#include "search/search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int halo_enabled(void);   /* 定义见下；字形压实与描边共用同一负向对照开关 */

/* ------------------------------------------------------------------ */
/* 行政区名（区域名）分级显示 —— 低 zoom 专用                           */
/* ------------------------------------------------------------------ */
/* 为什么单独开一份 Font：字号是 Font 创建时定的，没有"临时放大"接口；且必须
 * 走 font_open_default()（多面候选链 + CJK 回退），单文件 font_open 画中文会
 * 变方框。按 pt 缓存跨帧复用，app_shutdown 调 label_admin_font_free() 释放。 */
static Font *g_admin_font;
static int   g_admin_pt;

static Font *admin_font(int pt)
{
    if (g_admin_font && g_admin_pt == pt) return g_admin_font;
    font_close(g_admin_font);
    g_admin_font = font_open_default(pt);
    g_admin_pt = pt;
    return g_admin_font;
}

void label_admin_font_free(void)
{
    font_close(g_admin_font);
    g_admin_font = NULL;
    g_admin_pt = 0;
}


/* 白描边总开关：默认对所有比例尺生效。SKEDADDLE_LABEL_HALO=0 恢复旧的
 * 1px 黑影 + 不钳制路径，配合 SKEDADDLE_FONT_SHARP=1.0 可逐字节复现
 * 改动前的出图（负向对照）。进程内只读一次环境变量，零逐帧开销。 */
static int halo_enabled(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("SKEDADDLE_LABEL_HALO");
        v = !(e && (!strcmp(e, "0") || !strcmp(e, "off")));
    }
    return v;
}

/* POI 子类配色开关（定义在本文件 POI 段，refresh_items 提前用到，前置声明） */
static int poi_sub_enabled(void);

/* ------------------------------------------------------------------ */
/* 地名标注                                                             */
/* ------------------------------------------------------------------ */

/* 外部 POI 文件：products/poi_ext.csv（环境变量 SKEDADDLE_POI_EXT 可覆盖路径）。
 * 格式（UTF-8，'#' 注释，空行忽略）：
 *     名称,纬度,经度[,权重[,子类]]
 * 名称里不能有逗号；权重缺省 50 —— 量纲对齐 places_build：车站/机场 60、
 * 大学/医院 50，镇名 22。给 50 = 与重要设施同级；想压过村名可给 80+。
 * 子类缺省 0（通用蓝圆），可给 1~5 吃 poi_style 的分形状配色：
 * 1交通(紫方块) 2教育(褐圆) 3医疗(红十字) 4旅游(绿圆) 5生活(琥珀圆)。
 * kind 恒为 11（设施 POI：z13 起可见、受「POI 显示」开关控制）。
 * 文件不存在 = 空表：poi_source=1 时画不出设施 POI，属预期（不是错误）。
 * ★ init 载一次、不做热重载：外部 POI 是"一份数据"不是"实时源"，
 *   改文件重启生效——省掉每帧 stat 的开销和半途重载的一致性问题。 */
static void poi_ext_load(LabelCache *lc)
{
    const char *path = getenv("SKEDADDLE_POI_EXT");
    if (!path || !*path) path = "products/poi_ext.csv";
    /* ★ 与搜索层共用同一份 CSV 解析：显示和检索必须看到完全相同的条目集合，
       否则「画了 3 个点只搜到 2 个」这类问题极难归因。
       render -> search 是允许方向（见 scripts/check_layers.sh）。 */
    SearchExtPoi *items = NULL;
    int n = 0;
    if (search_ext_load_csv(path, &items, &n) != 0 || n <= 0) {
        free(items);
        return;
    }
    lc->ext = (LabelItem *)calloc((size_t)n, sizeof(LabelItem));
    if (!lc->ext) { free(items); return; }
    for (int i = 0; i < n; i++) {
        LabelItem *it = &lc->ext[i];
        snprintf(it->name, sizeof(it->name), "%s", items[i].name);
        it->lat = items[i].lat; it->lon = items[i].lon;
        it->kind = 11;
        it->weight = items[i].weight;
        it->sub = items[i].sub;
    }
    lc->n_ext = lc->ext_cap = n;
    free(items);
    if (lc->n_ext)
        fprintf(stderr, "外部POI: %s 载入 %d 条\n", path, lc->n_ext);
}

int label_cache_init(LabelCache *lc, const char *db_path)
{
    memset(lc, 0, sizeof(*lc));
    poi_ext_load(lc);            /* 地名库打不开也照载：可无库纯外部 POI 跑 */
    if (sqlite3_open_v2(db_path, &lc->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        fprintf(stderr, "地名库打不开 %s（无地名标注）\n", db_path);
        lc->db = NULL;
        return -1;
    }
    return 0;
}

void label_cache_free(LabelCache *lc)
{
    if (lc->db) sqlite3_close(lc->db);
    free(lc->items);
    free(lc->ext);
    memset(lc, 0, sizeof(*lc));
}

/* ===========================================================================
 * 最近的上级行政区（"这是哪儿"）
 *
 * 地名库里没有省界/市界多边形，只有带坐标的点 ⇒ 只能报"最近的那个市/区"。
 * 这在城区完全够用；在省界、市界附近可能报成邻省邻市的，调用方要按"大致
 * 位置"来用（头文件里已就此写明）。
 *
 * 性能：place 表有 place_latlon(lat,lon) 索引，所以先用 lat/lon 的矩形粗筛
 * （能走索引），再在候选集里按真实平方距离排序。半径按级放大，一找到就停 ——
 * 城区第一级 0.08°(≈9 km) 就命中，荒漠才会一路放大到 6°。
 * 只在"无图区提示"这条路径上调用（位置移动 450 m 才重算一次），不在渲染热路径。
 * ========================================================================= */
static int nearest_admin_point(sqlite3 *db, double lat, double lon,
                               int kind_lo, int kind_hi, char *out, size_t cap)
{
    if (out && cap) out[0] = '\0';
    if (!db) return 0;

    /* 经度方向按 cos(lat) 压缩后再比距离，否则中纬度会把东西方向拉长 30% */
    double clat = cos(lat * 3.14159265358979 / 180.0);
    double kx = clat * clat;
    if (kx < 0.01) kx = 0.01;

    /* 半径只放大到约 67 km（0.6°）：再大就会在无人区报出几百公里外的市名，
     * 那种"反查结果"比不报更有害。城区第一级 0.08°(≈9 km) 基本就命中了。 */
    static const double RADII[] = { 0.08, 0.25, 0.60 };
    const char *sql =
        "SELECT name FROM place"
        " WHERE kind BETWEEN ?1 AND ?2"
        "   AND lat BETWEEN ?3 AND ?4 AND lon BETWEEN ?5 AND ?6"
        " ORDER BY (lat-?7)*(lat-?7) + (lon-?8)*(lon-?8)*?9"
        " LIMIT 1";

    for (size_t r = 0; r < sizeof(RADII) / sizeof(RADII[0]); r++) {
        double d = RADII[r];
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_int(st, 1, kind_lo);
        sqlite3_bind_int(st, 2, kind_hi);
        sqlite3_bind_double(st, 3, lat - d);
        sqlite3_bind_double(st, 4, lat + d);
        sqlite3_bind_double(st, 5, lon - d);
        sqlite3_bind_double(st, 6, lon + d);
        sqlite3_bind_double(st, 7, lat);
        sqlite3_bind_double(st, 8, lon);
        sqlite3_bind_double(st, 9, kx);

        int rc = sqlite3_step(st);
        int got = 0;
        if (rc == SQLITE_ROW) {
            const unsigned char *nm = sqlite3_column_text(st, 0);
            if (nm && out && cap) {
                snprintf(out, cap, "%s", (const char *)nm);
                got = 1;
            }
        }
        sqlite3_finalize(st);
        if (got) return 1;
    }
    return 0;
}

int label_nearest_admin(LabelCache *lc, double lat, double lon,
                        char *city, size_t ccap,
                        char *district, size_t dcap)
{
    if (city && ccap)     city[0]     = '\0';
    if (district && dcap) district[0] = '\0';
    if (!lc || !lc->db) return 0;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return 0;

    /* 市级：kind 1 = 省会/直辖市城区，2 = 地级市 */
    int n = nearest_admin_point(lc->db, lat, lon, 1, 2, city, ccap);
    /* 区县级：kind 3 = 县 / 镇 */
    n += nearest_admin_point(lc->db, lat, lon, 3, 3, district, dcap);
    return n;
}

/* zoom → 地名等级上限。kind 语义按抽取约定：数值小 = 行政级别高。
 * 2026-10-24 调整：把 POI（kind 11）的可见起点从 z15 下探到 z13，
 * 让"POI 显示"在城区尺度就能看到（此前只在 z15 才画到，基本看不见）。 */
static int kind_max_for_zoom(double zoom)
{
    if (zoom < 6)  return 1;
    if (zoom < 8)  return 2;
    if (zoom < 10) return 3;
    if (zoom < 11) return 4;
    if (zoom < 12) return 6;
    if (zoom < 13) return 9;
    if (zoom < 14) return 11;     /* POI(kind 11) 自 z13 起可见 */
    return 12;
}

/* 视口变化超过 20% 才重查 SQL；否则平移只重投影。
 * ★ 缩放必须单独判：旧实现只把四角位移按「上次缓存 bbox 跨度」归一化，
 *   而纯缩放时位移恰与跨度成正比，比值恒 <0.2（0.5 档约 0.08~0.14），
 *   于是 zoom>=14 后 kind_max 恒为 12、缓存长时间不刷新，高 zoom 小视口里
 *   旧的大范围 Top-400 大地名被裁光 —— 表现为「放大到 500 米以下没地名」。
 *   这里先比 bbox 跨度：0.5 档缩放跨度变 0.707/1.414 倍，必然触发重查。 */
static int bbox_moved_far(const double *a, const double *b)
{
    double dw = (a[2] - a[0]) + (a[3] - a[1]);
    double dn = (b[2] - b[0]) + (b[3] - b[1]);
    if (dw <= 1e-12 || dn <= 1e-12) return 1;
    double ratio = dn / dw;
    if (ratio < 0.8 || ratio > 1.25) return 1;   /* 缩放：跨度变了 20% 以上 */
    double d = 0;
    for (int i = 0; i < 4; i++) {
        double x = (a[i] - b[i]) / dw;
        if (x < 0) x = -x;
        if (x > d) d = x;
    }
    return d > 0.2;
}

/* 合并 Top-N 的排序键：weight 降序，并列时原序（SQL 结果在前）升序。 */
typedef struct { int w, seq; } LOrd;
static int lord_cmp(const void *a, const void *b)
{
    const LOrd *x = (const LOrd *)a, *y = (const LOrd *)b;
    if (x->w != y->w) return y->w - x->w;
    return x->seq - y->seq;
}

static int refresh_items(LabelCache *lc, const MapView *v, int show_poi)
{
    double bb[4];
    mv_visible_bbox(v, bb);
    int kind_max = kind_max_for_zoom(v->zoom);

    lc->loaded = 1;
    lc->cached_kindmax = kind_max;
    lc->cached_zoom = v->zoom;
    lc->cached_poi = show_poi;
    lc->cached_poi_source = lc->poi_source;
    memcpy(lc->cached_bb, bb, sizeof(bb));

    /* 外扩 30%：小幅平移不重查。★ 必须在 goto ext_merge 之前初始化：
       无地名库（db==NULL）只走外部层时，ext_merge 同样要用 dla/dlo
       做视口过滤（11-10 外部 POI 首版把初始化放在 goto 之后，
       -Wmaybe-uninitialized 报警 + 脏值进过滤条件）。 */
    double dla = (bb[2] - bb[0]) * 0.3 + 1e-6;
    double dlo = (bb[3] - bb[1]) * 0.3 + 1e-6;

    if (!lc->db) { lc->n_items = 0; goto ext_merge; }   /* 无地名库：只剩外部层 */

    /* sub 列是 11-10 加的：老库没有这列，prepare 会报 "no such column"。
     * 回退到 "0 AS sub"（全部当通用蓝圆）——老库不必重建也能继续用，
     * 只是画不出子类配色。 */
    const char *sql =
        "SELECT name,lat,lon,kind,weight,sub FROM place "
        "WHERE lat BETWEEN ?1 AND ?2 AND lon BETWEEN ?3 AND ?4 AND kind<=?5 "
        "AND (?7 OR kind<>11) "
        "ORDER BY weight DESC LIMIT ?6";
    const char *sql_old =
        "SELECT name,lat,lon,kind,weight,0 FROM place "
        "WHERE lat BETWEEN ?1 AND ?2 AND lon BETWEEN ?3 AND ?4 AND kind<=?5 "
        "AND (?7 OR kind<>11) "
        "ORDER BY weight DESC LIMIT ?6";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(lc->db, sql, -1, &st, NULL) != SQLITE_OK) {
        if (sqlite3_prepare_v2(lc->db, sql_old, -1, &st, NULL) != SQLITE_OK)
            return -1;
    }
    sqlite3_bind_double(st, 1, bb[0] - dla);
    sqlite3_bind_double(st, 2, bb[2] + dla);
    sqlite3_bind_double(st, 3, bb[1] - dlo);
    sqlite3_bind_double(st, 4, bb[3] + dlo);
    /* 中低 zoom 视口大、候选多，全局按权重取 Top-400 会把郊区低权重
     * 村名/POI 全截断，只剩城区街道名 ⇒ 文字挤在城区的那一半。
     * z<14 放宽到 1200；z>=14（500 米档）维持 400，出图逐字节不变。 */
    sqlite3_bind_int(st, 5, kind_max);
    sqlite3_bind_int(st, 6, v->zoom < 14.0 ? 1200 : 400);
    /* kind 11（设施 POI）的 SQL 层排除：POI 总开关关闭，或 POI 来源切到
     * 外部文件（设施层由外部 CSV 接管，内置的不能再占 Top-N 名额）。
     * 行政地名（kind 1~10）不受 poi_source 影响，始终查地名库。 */
    sqlite3_bind_int(st, 7, (show_poi && lc->poi_source == 0) ? 1 : 0);

    lc->n_items = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (lc->n_items >= lc->cap_items) {
            int ncap = lc->cap_items ? lc->cap_items * 2 : 256;
            LabelItem *ni = realloc(lc->items, (size_t)ncap * sizeof(LabelItem));
            if (!ni) break;
            lc->items = ni; lc->cap_items = ncap;
        }
        LabelItem *it = &lc->items[lc->n_items];
        const unsigned char *nm = sqlite3_column_text(st, 0);
        snprintf(it->name, sizeof(it->name), "%s", nm ? (const char *)nm : "");
        it->lat    = sqlite3_column_double(st, 1);
        it->lon    = sqlite3_column_double(st, 2);
        it->kind   = sqlite3_column_int(st, 3);
        it->weight = sqlite3_column_int(st, 4);
        /* SKEDADDLE_POI_SUB=0：负向对照，强制全部走通用蓝圆 ⇒ 与加列前
         * 的出图逐字节一致（与 LABEL_HALO / POI_R 同一套玩法）。 */
        it->sub    = poi_sub_enabled() ? sqlite3_column_int(st, 5) : 0;
        lc->n_items++;
    }
    sqlite3_finalize(st);

ext_merge:
    /* poi_source=1：外部 POI 接管设施层。并入视口内的外部条目后与 SQL 结果
       一起按 weight 重排、截回 Top-N —— SQL 的 LIMIT 只预算了地名库那部分，
       两层必须共享名额，否则外部 POI 等于"免费插队"，挤掉的是行政地名。
       并列时序号小者（= SQL 结果，行政地名）在前：确定性优先。
       show_poi 关闭或 zoom<13（kind 11 本就不可见）时不加——?7 已把
       设施层整个排除，这里保持同口径。 */
    if (lc->poi_source == 1 && show_poi && kind_max >= 11) {
        int limit = v->zoom < 14.0 ? 1200 : 400;
        int added = 0;
        for (int i = 0; i < lc->n_ext; i++) {
            const LabelItem *e = &lc->ext[i];
            if (e->lat < bb[0] - dla || e->lat > bb[2] + dla ||
                e->lon < bb[1] - dlo || e->lon > bb[3] + dlo) continue;
            if (lc->n_items >= lc->cap_items) {
                int ncap = lc->cap_items ? lc->cap_items * 2 : 256;
                LabelItem *ni = (LabelItem *)realloc(
                    lc->items, (size_t)ncap * sizeof(LabelItem));
                if (!ni) break;
                lc->items = ni; lc->cap_items = ncap;
            }
            lc->items[lc->n_items++] = *e;
            added++;
        }
        /* ★ 只要并入了外部条目就要排序：避让是按列表序"先到先得"，
           不排的话外部条目永远最后入场，weight 再高也抢不过中心区的
           村名/街区（实测 w=500 的中心点就这样被挤掉）。总数没超 Top-N
           也要排——排序顺带把截断做了，成本 ≤1200 个 int 的一次 qsort。 */
        if (added > 0 && lc->n_items > 0) {
            LOrd *o = (LOrd *)malloc((size_t)lc->n_items * sizeof(LOrd));
            LabelItem *tmp = (LabelItem *)malloc((size_t)limit * sizeof(LabelItem));
            if (o && tmp) {
                for (int i = 0; i < lc->n_items; i++) {
                    o[i].w = lc->items[i].weight; o[i].seq = i;
                }
                qsort(o, (size_t)lc->n_items, sizeof(LOrd), lord_cmp);
                int keep = lc->n_items < limit ? lc->n_items : limit;
                for (int i = 0; i < keep; i++) tmp[i] = lc->items[o[i].seq];
                memcpy(lc->items, tmp, (size_t)keep * sizeof(LabelItem));
                lc->n_items = keep;
            }
            free(o); free(tmp);
        }
    }
    return 0;
}

/* 网格避让：每格最多一个标签（先到先得，SQL 已按 weight 排序）。
 * 格子尺寸按字号自适应 —— 固定 110x34 的话，字号调大后标签会互相压字，
 * 调小又会稀到浪费空间。以单个汉字宽度为单位估算。 */
#define GRID_COLS 64
#define GRID_ROWS 40

static uint8_t g_grid[GRID_COLS * GRID_ROWS];
static int g_grid_w = 110, g_grid_h = 34;

/* 网格必须覆盖整块画布：固定 64x40 在 1280x800 下够用，但窗口一高，
 * gy = y / g_grid_h 就 >= GRID_ROWS，整片底部标签被 continue 丢掉 ——
 * 表现为「下半屏没有字」。视口超大时按视口放大格距，保证全覆盖。 */
static void grid_for_font(Font *font, int view_w, int view_h)
{
    int cw = font ? font_text_w(font, "\xe4\xb8\xad") : 14;   /* "中" */
    if (cw <= 0) cw = 14;
    g_grid_w = cw * 8;   if (g_grid_w < 60) g_grid_w = 60;
    g_grid_h = (int)(cw * 2.4); if (g_grid_h < 22) g_grid_h = 22;
    if (view_w > 0 && (view_w + g_grid_w - 1) / g_grid_w > GRID_COLS)
        g_grid_w = view_w / GRID_COLS + 1;
    if (view_h > 0 && (view_h + g_grid_h - 1) / g_grid_h > GRID_ROWS)
        g_grid_h = view_h / GRID_ROWS + 1;
}

/* ------------------------------------------------------------------ */
/* POI / 地名图标（矢量标记，按类别着色 + 形状区分）                     */
/* ------------------------------------------------------------------ */

typedef enum { SH_DISC, SH_TRI, SH_DIAMOND, SH_SQUARE, SH_CROSS } PoiShape;

/* POI 子类配色总开关：默认开。SKEDADDLE_POI_SUB=0 强制全部设施 POI 走
 * 通用蓝圆（sub=0 路径），用于"加列前后出图逐字节一致"的负向对照。
 * 进程内只读一次环境变量，零逐帧开销（与 halo_enabled 同构）。 */
static int poi_sub_enabled(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("SKEDADDLE_POI_SUB");
        v = !(e && (!strcmp(e, "0") || !strcmp(e, "off")));
    }
    return v;
}

/* kind 语义见 skedaddle_places_build.c：数值小 = 行政级别高。按类别给颜色与形状。
 * sub 只在 kind==11（设施 POI）时有意义，0 = 通用蓝圆（与加列前完全一致）：
 * ★ sub=0 必须保持 (40,120,232, SH_DISC) 原样——负向对照的锚点就是它。 */
static void poi_style(int kind, int sub, uint8_t *r, uint8_t *g, uint8_t *b,
                      PoiShape *sh)
{
    if      (kind <= 2) { *r = 214; *g = 48;  *b = 48;  *sh = SH_DISC; }   /* 首都/直辖市 红 */
    else if (kind <= 4) { *r = 232; *g = 128; *b = 24;  *sh = SH_DISC; }   /* 市/镇 橙 */
    else if (kind <= 7) { *r = 236; *g = 192; *b = 32;  *sh = SH_DISC; }   /* 村/屯 金 */
    else if (kind == 9) { *r = 72;  *g = 182; *b = 96;  *sh = SH_DISC; }   /* 街区 绿 */
    else if (kind == 10){ *r = 40;  *g = 162; *b = 172; *sh = SH_TRI; }     /* 自然地物 青三角 */
    else if (kind == 11 && sub == 1) { *r = 108; *g = 78;  *b = 216; *sh = SH_SQUARE; } /* 交通 紫 */
    else if (kind == 11 && sub == 2) { *r = 150; *g = 100; *b = 50;  *sh = SH_DISC;   } /* 教育 褐 */
    else if (kind == 11 && sub == 3) { *r = 226; *g = 62;  *b = 74;  *sh = SH_CROSS;  } /* 医疗 红十字 */
    else if (kind == 11 && sub == 4) { *r = 52;  *g = 156; *b = 88;  *sh = SH_DISC;   } /* 旅游 绿 */
    else if (kind == 11 && sub == 5) { *r = 196; *g = 148; *b = 24;  *sh = SH_DISC;   } /* 生活 琥珀 */
    else if (kind == 11){ *r = 40;  *g = 120; *b = 232; *sh = SH_DISC; }   /* POI 蓝圆 */
    else                { *r = 150; *g = 150; *b = 160; *sh = SH_DIAMOND; }  /* 其他地物 灰菱 */
}

/* 实心圆（逐扫描线填充一条 hspan）。 */
static void fill_disc(Raster *rs, int cx, int cy, int R, uint32_t c)
{
    if (R <= 0) { raster_point(rs, cx, cy, c); return; }
    for (int dy = -R; dy <= R; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= (unsigned)rs->h) continue;
        int dx = (int)sqrtf((float)(R * R - dy * dy));
        raster_hspan(rs, y, cx - dx, cx + dx + 1, c);
    }
}

/* 凸多边形扫描线填充（三角 / 菱形）。 */
static void fill_poly(Raster *rs, const int *px, const int *py, int n, uint32_t c)
{
    int ymin = py[0], ymax = py[0];
    for (int i = 1; i < n; i++) {
        if (py[i] < ymin) ymin = py[i];
        if (py[i] > ymax) ymax = py[i];
    }
    if (ymin < 0) ymin = 0;
    if (ymax >= rs->h) ymax = rs->h - 1;
    for (int y = ymin; y <= ymax; y++) {
        double yc = y + 0.5;
        int xs[8]; int nx = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            if ((py[i] <= yc && py[j] > yc) || (py[j] <= yc && py[i] > yc)) {
                xs[nx++] = (int)(px[i] +
                           (yc - py[i]) / (double)(py[j] - py[i]) * (px[j] - px[i]));
            }
        }
        for (int a = 0; a + 1 < nx; a++)
            for (int b = a + 1; b < nx; b++)
                if (xs[b] < xs[a]) { int t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
        for (int a = 0; a + 1 < nx; a += 2)
            raster_hspan(rs, y, xs[a], xs[a + 1] + 1, c);
    }
}

/* 十字（医疗）：竖臂全高、横臂收窄到 ~1/3 高度。t=core 时 7x7 十字。 */
static void fill_cross(Raster *rs, int cx, int cy, int t, uint32_t c)
{
    int arm = t / 3;                     /* 臂厚的一半（t=3 -> 1） */
    if (arm < 1) arm = 1;
    for (int dy = -t; dy <= t; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= (unsigned)rs->h) continue;
        int dx = (dy < -arm || dy > arm) ? arm : t;
        raster_hspan(rs, y, cx - dx, cx + dx + 1, c);
    }
}

static void shape_fill(Raster *rs, int x, int y, int t, PoiShape sh, uint32_t c)
{
    if (sh == SH_DISC) { fill_disc(rs, x, y, t, c); return; }
    if (sh == SH_TRI) {
        int px[3] = { x, x - t + 1, x + t - 1 };
        int py[3] = { y - t, y + t, y + t };
        fill_poly(rs, px, py, 3, c);
        return;
    }
    if (sh == SH_CROSS) { fill_cross(rs, x, y, t, c); return; }
    if (sh == SH_SQUARE) {
        int px[4] = { x - t, x + t + 1, x + t + 1, x - t };
        int py[4] = { y - t, y - t, y + t + 1, y + t + 1 };
        fill_poly(rs, px, py, 4, c);
        return;
    }
    int px[4] = { x, x + t, x, x - t };
    int py[4] = { y - t, y, y + t, y };
    fill_poly(rs, px, py, 4, c);
}

/* POI 标记半径（外径 2R+1）：SKEDADDLE_POI_R=固定值整体覆盖（负向对照：
 * =4 即旧"全档恒定 4"行为，配合 SKEDADDLE_LABEL_HALO=0 + SKEDADDLE_FONT_SHARP=1.0
 * 可逐字节复现改动前的出图）；缺省按缩放分档：
 *   zoom<15 → 4   z13 POI 出场档，城区一屏两三百个，10px 已是密度上限
 *   z15~16  → 5   街道级密度下降，色芯 4px 起可分辨十字/方块等子类形状
 *   z>=17   → 6   要素稀疏，读得清优先于占得少
 * 进程内只读一次环境变量；半径在决策期（主线程）算好、随命令下发，
 * worker 不碰 env/static，无竞态。 */
static int poi_radius_for_zoom(double zoom)
{
    static int forced = -1;
    if (forced < 0) {
        const char *e = getenv("SKEDADDLE_POI_R");
        int v = (e && *e) ? atoi(e) : 0;
        if (v > 0) {
            if (v < 2) v = 2;
            if (v > 12) v = 12;
        }
        forced = v;                     /* 0 = 未设置，走缩放分档 */
    }
    if (forced > 0) return forced;
    if (zoom < 15.0) return 4;
    if (zoom < 17.0) return 5;
    return 6;
}

/* 画一个 POI 标记：深灰描边 + 白底 + 类别色芯，紧贴 (x,y) 坐标点。
 * sub = kind 11 的子类（交通/教育/医疗/旅游/生活），影响色芯颜色与形状；
 * R = 半径（决策期按 zoom 分档，见 poi_radius_for_zoom）。 */
static void draw_poi_marker(Raster *rs, int x, int y, int kind, int sub, int R)
{
    uint8_t r, g, b; PoiShape sh;
    poi_style(kind, sub, &r, &g, &b, &sh);
    uint32_t dark  = raster_rgb(rs, 40, 42, 48);
    uint32_t white = raster_rgb(rs, 255, 255, 255);
    uint32_t col   = raster_rgb(rs, r, g, b);
    /* z12 一屏两三百个标记，旧外径 14px 会把小字压住、满屏彩色噪点。
     * R=4（10px，面积约减半）色芯 3px 仍能区分类别颜色；R>=6 用 R-2
     * 色芯保持描边/白底比例，SKEDADDLE_POI_R=6 时旧基线逐字节可复现。 */
    int core = (R >= 6) ? R - 2 : R - 1;
    shape_fill(rs, x, y, R + 1, sh, dark);
    shape_fill(rs, x, y, R,     sh, white);
    shape_fill(rs, x, y, core,  sh, col);
}

/* ------------------------------------------------------------------ */
/* 标注命令：串行决策、分带绘制                                          */
/* ------------------------------------------------------------------ */
/* 网格避让是"先到先得"的链式决策（第 i 个能不能画取决于前 i-1 个占了哪些格），
 * 这部分必须串行；但真正贵的是绘制——白描边 = 同一段文字按 8 邻域偏移画 10 遍，
 * 逐像素 alpha 混合。拆成两段：决策期只算位置/颜色/占格，把绘制压成命令；
 * 执行期按扫描带并行，每条命令交给所有与它像素行有交集的带（clip 过滤）。
 *
 * ★ 逐字节一致的论证：每个像素 p 属于唯一一条带；串行序中所有覆盖 p 的命令，
 *   其行包围盒都覆盖 p 所在行 ⇒ 它们必然都被分给这条带，且带内按命令序执行
 *   ——与串行对 p 的写入序完全相同（font_draw 是纯函数，无隐藏状态）。
 * ★ 文字行包围盒取 [y-PAD, y+ch+PAD]，PAD=16：stbtt 的 yoff 可为负（数字/
 *   字母的字形顶能高出 y 5~7 行，实测 z15 每条带边界上方 6 行缺字 ⇒ 4 行
 *   余量不够），白描边再 ±1。宁多分一条带（多画几行被 clip 滤掉，纯浪费
 *   几十次混合），不可漏分（漏分 = 缺字）。 */

enum { LCMD_HALO, LCMD_SHADOW, LCMD_POI, LCMD_DOT };

typedef struct {
    uint8_t      type;      /* LCMD_* */
    uint8_t      r, g, b;
    uint8_t      sub;       /* LCMD_POI 用：kind 11 子类 */
    uint8_t      R;         /* LCMD_POI 用：半径（决策期按 zoom 分档） */
    int16_t      poi_kind;  /* LCMD_POI 用 */
    const char  *text;      /* LCMD_HALO/SHADOW 用（指向 lc->items / np->blob） */
    Font        *font;      /* 小字 or 区名大字 */
    int          x, y;
    int          ylo, yhi;  /* 像素占用行范围（含），分带归属判据 */
} LabelCmd;

static LabelCmd *s_lcmd;
static int       s_lcmd_cap;
static int       s_lcmd_n;

/* 走并行的最小命令数：标注阶段整体才 2~7ms，命令太少时起线程纯亏。 */
#define SK_BAND_MIN_LABELS 96

static int lcmd_push(uint8_t type, Font *font, const char *text,
                     int x, int y, int ylo, int yhi,
                     uint8_t r, uint8_t g, uint8_t b, int poi_kind, int sub,
                     int R)
{
    if (s_lcmd_n >= s_lcmd_cap) {
        int nc = s_lcmd_cap ? s_lcmd_cap * 2 : 512;
        LabelCmd *q = (LabelCmd *)realloc(s_lcmd, (size_t)nc * sizeof(LabelCmd));
        if (!q) return -1;
        s_lcmd = q;
        s_lcmd_cap = nc;
    }
    LabelCmd *c = &s_lcmd[s_lcmd_n++];
    c->type = type; c->font = font; c->text = text;
    c->x = x; c->y = y; c->ylo = ylo; c->yhi = yhi;
    c->r = r; c->g = g; c->b = b; c->poi_kind = (int16_t)poi_kind;
    c->sub = (uint8_t)sub;
    c->R = (uint8_t)(R > 0 && R < 255 ? R : 0);
    return 0;
}

static const int dx8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int dy8[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

typedef struct {
    BandView *bv;
    LabelCmd *cmd;
    int       n;
} LabelJob;

static void label_task(void *arg, int tid)
{
    LabelJob *j = (LabelJob *)arg;
    Raster *rs = &j->bv->band[tid];
    int b_lo = rs->clip.y, b_hi = b_lo + rs->clip.h;
    for (int i = 0; i < j->n; i++) {
        const LabelCmd *c = &j->cmd[i];
        if (c->yhi < b_lo || c->ylo >= b_hi) continue;
        switch (c->type) {
        case LCMD_HALO:
            for (int k = 0; k < 8; k++)
                font_draw_a_pre(c->font, rs, c->x + dx8[k], c->y + dy8[k],
                                c->text, 255, 255, 255, 0, 255);
            /* 同色再绘两次把覆盖率压回实心（1-(1-a)^2），串行时代的 2 次主色
               口径不变 —— 顺序与次数都不能变。 */
            font_draw_a_pre(c->font, rs, c->x, c->y, c->text, c->r, c->g, c->b, 0, 255);
            font_draw_a_pre(c->font, rs, c->x, c->y, c->text, c->r, c->g, c->b, 0, 255);
            break;
        case LCMD_SHADOW:
            font_draw_a_pre(c->font, rs, c->x, c->y, c->text, c->r, c->g, c->b, 1, 255);
            break;
        case LCMD_POI:
            draw_poi_marker(rs, c->x, c->y, c->poi_kind, c->sub, c->R);
            break;
        case LCMD_DOT:
            fill_disc(rs, c->x, c->y, 2, raster_rgb(rs, c->r, c->g, c->b));
            break;
        }
    }
}

/* 决策完的命令一次性执行：带数由 SKEDADDLE_BANDS 决定（0/不足阈值 = 串行，
 * 但走的仍是同一条命令路径 ⇒ 与旧逐条绘制逐字节一致）。 */
static void label_flush(Raster *rs)
{
    int n = s_lcmd_n;
    s_lcmd_n = 0;
    if (n <= 0) return;
    /* ★ 预热（主线程串行）：执行器用无锁只读查找的前提是"全部字形已在
       缓存"——串行（单带）路径同样走 glyph_find，不预热首帧会整段缺字。
       首帧建字形（stbtt 光栅化）就发生在这一步；稳态帧缓存已热，这里只剩
       每码点一次哈希。不预热的话 8 线程直接 glyph_get 会把字形锁打成热点
       （实测 21ms -> 129ms，比串行还慢 6 倍）。 */
    for (int i = 0; i < n; i++)
        if (s_lcmd[i].text) font_warm_text(s_lcmd[i].font, s_lcmd[i].text);
    int nband = (n >= SK_BAND_MIN_LABELS) ? band_pick_count() : 1;
    BandView bv;
    band_init(&bv, rs, nband);
    LabelJob j;
    j.bv = &bv; j.cmd = s_lcmd; j.n = n;
    band_run(&bv, label_task, &j);
}

void paint_place_labels(Raster *rs, const MapView *v,
                        LabelCache *lc, Font *font, int font_size, PaintStats *st,
                        int show_poi, int show_labels)
{
    if (!lc || !font) return;
    double bb[4];
    mv_visible_bbox(v, bb);
    int kind_max = kind_max_for_zoom(v->zoom);

    /* 缩放显式判据：跨度比在连续小步缩放时可能被平移抵消，缓存里留着
     * 旧视口的 Top-N，投影到新视口就整体偏到一边。 */
    if (!lc->loaded || lc->cached_kindmax != kind_max ||
        fabs(v->zoom - lc->cached_zoom) >= 0.25 ||
        lc->cached_poi != show_poi ||               /* 面板/菜单开关切换须重查 */
        lc->cached_poi_source != lc->poi_source ||  /* POI 来源切换须重查 */
        bbox_moved_far(lc->cached_bb, bb)) {
        if (refresh_items(lc, v, show_poi) != 0) return;
    }

    memset(g_grid, 0, sizeof(g_grid));
    grid_for_font(font, v->w, v->h);
    s_lcmd_n = 0;                 /* 命令缓冲每帧重用 */
    /* POI 半径在决策期一次算好随命令下发：worker 不再碰"读 env → 写 static"
     * 的懒初始化（竞态随之消失），LCMD_POI 的 R 字段只读。 */
    int poi_R = poi_radius_for_zoom(v->zoom);

    /* ★ 1/2/5/10 公里等中低 zoom 才有「区域名」层：市/区名用大字、镇名加白描边，
     *   与 POI 小字拉开视觉层级。旧实现把 kind 1~2 的行政区名和村名/POI 画成
     *   一样大的小字 + 橙色圆圈，在 5/10 公里档根本认不出哪一个是区域名。
     *   z>=14 不做分级显示（无大字层），但文字同样走白描边。 */
    int  admin_on = (v->zoom < 14.0 && show_labels);
    int  halo_on  = halo_enabled();   /* 清晰度逻辑与分级显示解耦 */
    /* 文字垂直包围盒（y=基线，字形向上伸约 ascent）：分带归属用 */
    int up_s = 0, dn_s = 0, up_b = 0, dn_b = 0;
    font_vbox(font, &up_s, &dn_s);
    Font *big = admin_on ? admin_font(font_size > 0 ? font_size * 2 : 28) : NULL;
    if (big) font_vbox(big, &up_b, &dn_b);
    int  big_ch = big ? font_text_w(big, "\xe4\xb8\xad") : 0;   /* "中" ≈ 字高 */
    if (big_ch <= 0) big_ch = 26;

    int drawn = 0, icons = 0;
    /* 贴边标签：锚点在视口内、字形却有一半在画布外，被裁成「半截字」，
     * 看着就像糊字。中低 zoom 把绘制原点钳进画布，保证整字可见；
     * SKEDADDLE_LABEL_HALO=0 时不钳制，供负向对照。 */
    int small_ch = font ? font_text_w(font, "\xe4\xb8\xad") : 14;
    if (small_ch <= 0) small_ch = 14;
    for (int i = 0; i < lc->n_items; i++) {
        LabelItem *it = &lc->items[i];
        /* POI 总开关：kind 11（设施）的图标与名称都不画、不占避让格。
         * SQL 已排除，这里兜底防任何残留缓存路径。 */
        if (!show_poi && it->kind == 11) continue;
        int x, y;
        mv_project(v, it->lat, it->lon, &x, &y);
        if (x < 0 || y < 0 || x >= v->w || y >= v->h) continue;

        int tx = x + 10, ty = y - 8;
        if (halo_on) {
            int tw = font_text_w(font, it->name);
            if (tx + tw >= v->w - 1) tx = v->w - tw - 2;
            if (tx < 1) tx = 1;
            if (ty + small_ch >= v->h - 1) ty = v->h - small_ch - 2;
            if (ty < 1) ty = 1;
        }

        int gx = x / g_grid_w, gy = y / g_grid_h;
        if (gx >= GRID_COLS || gy >= GRID_ROWS) continue;
        if (g_grid[gy * GRID_COLS + gx]) continue;

        /* 区名大字：按实际包围盒占格（通常 2x1~2x2），并先检查整块是否已被占，
         * 否则相邻区名的大字会互相压字（10 公里档实测「石景山区/北京市」重叠）。
         * 固定占 3x3 又会平白挤掉十几个小地名，加剧「地名变少」的观感。
         * SQL 已按 weight DESC 排序，区名(w=240~340)必然先到先得。 */
        int is_admin = 0, ax0 = 0, ay0 = 0, atw = 0;
        int agx0 = 0, agx1 = 0, agy0 = 0, agy1 = 0;
        if (admin_on && big && it->kind <= 2) {
            atw = font_text_w(big, it->name);
            ax0 = x - atw / 2; ay0 = y - big_ch / 2;
            if (ax0 + atw >= v->w - 1) ax0 = v->w - atw - 2;
            if (ax0 < 1) ax0 = 1;
            if (ay0 + big_ch >= v->h - 1) ay0 = v->h - big_ch - 2;
            if (ay0 < 1) ay0 = 1;
            agx0 = ax0 / g_grid_w; agx1 = (ax0 + atw) / g_grid_w;
            agy0 = ay0 / g_grid_h; agy1 = (ay0 + big_ch) / g_grid_h;
            if (agx0 < 0) agx0 = 0;
            if (agy0 < 0) agy0 = 0;
            if (agx1 >= GRID_COLS) agx1 = GRID_COLS - 1;
            if (agy1 >= GRID_ROWS) agy1 = GRID_ROWS - 1;
            int taken = 0;
            for (int yy = agy0; yy <= agy1 && !taken; yy++)
                for (int xx = agx0; xx <= agx1; xx++)
                    if (g_grid[yy * GRID_COLS + xx]) { taken = 1; break; }
            is_admin = !taken;
        }
        if (is_admin) {
            for (int yy = agy0; yy <= agy1; yy++)
                for (int xx = agx0; xx <= agx1; xx++)
                    g_grid[yy * GRID_COLS + xx] = 1;
            if (lcmd_push(LCMD_HALO, big, it->name, ax0, ay0,
                          ay0 - up_b - 3, ay0 + dn_b + 3,
                          30, 44, 76, 0, 0, 0) != 0)
                goto oom;
            /* 小小定位点取代 POI 圆圈：区名代表"一片地方"而非一个设施 */
            {
                int dy = ay0 + big_ch + 3;
                if (lcmd_push(LCMD_DOT, NULL, NULL, x, dy,
                              dy - 2, dy + 2, 30, 44, 76, 0, 0, 0) != 0)
                    goto oom;
            }
            drawn++;
            continue;
        }
        /* 大字放不下：退回普通小地名标注，地名数量不缩水 */

        g_grid[gy * GRID_COLS + gx] = 1;
        if (admin_on && it->kind == 3) {
            /* 镇名：普通字号 + 白描边深色字，形成第二级层次 */
            if (lcmd_push(LCMD_HALO, font, it->name, tx, ty,
                          ty - up_s - 3, ty + dn_s + 3,
                          38, 42, 54, 0, 0, 0) != 0)
                goto oom;
            drawn++;
            if (show_poi) {
                if (lcmd_push(LCMD_POI, NULL, NULL, x, y,
                              y - 13, y + 13, 0, 0, 0, it->kind, it->sub,
                              poi_R) != 0)
                    goto oom;
                icons++;
            }
            continue;
        }

        /* 图标与标签独立开关，但共用一个避让格（任一个显示就占格，避免重叠） */
        if (show_poi) {
            if (lcmd_push(LCMD_POI, NULL, NULL, x, y,
                          y - 13, y + 13, 0, 0, 0, it->kind, it->sub,
                          poi_R) != 0)
                goto oom;
            icons++;
        }
        if (show_labels){
            /* 全比例尺统一白描边：1px 黑影会把小字糊成一团，500 米 /
             * 200 米档同样如此。SKEDADDLE_LABEL_HALO=0 恢复旧黑影路径，
             * 配合 SKEDADDLE_FONT_SHARP=1.0 做逐字节负向对照。 */
            if (lcmd_push(halo_on ? LCMD_HALO : LCMD_SHADOW, font, it->name,
                          tx, ty, ty - up_s - 3, ty + dn_s + 3,
                          46, 48, 56, 0, 0, 0) != 0)
                goto oom;
            drawn++;
        }
    }
    label_flush(rs);
    if (st) { st->labels_drawn = drawn; st->poi_icons = icons; }
    return;
oom:
    /* 命令缓冲 OOM：已决策的照常执行，剩下的标签这帧丢弃（与旧实现
       "realloc 失败即不画" 同口径）。 */
    label_flush(rs);
    if (st) { st->labels_drawn = drawn; st->poi_icons = icons; }
}

/* ------------------------------------------------------------------ */
/* 路名标注：消费 paint_roads 收集的屏幕内有名边                          */
/* ------------------------------------------------------------------ */

void paint_road_labels(Raster *rs, const MapView *v, Font *font,
                       PaintStats *st)
{
    /* z12（比例尺 2 公里）起画路名：与 paint_roads 里三处收集门槛一致。
     * 旧门槛 15.0→14.0 仍让 1/2 公里档一条路名都没有；中低 zoom 的
     * 「没有文字」观感有一半来自这里，另一半是行政区名没做分级显示。 */
    if (!font || v->zoom < 12.0) return;

    const RoadLabel *lbl; int n;
    road_labels_get(&lbl, &n);

    memset(g_grid, 0, sizeof(g_grid));
    grid_for_font(font, v->w, v->h);
    s_lcmd_n = 0;
    int halo_on = halo_enabled();          /* 预热：worker 里首调会竞态 */
    int up_s = 0, dn_s = 0;
    font_vbox(font, &up_s, &dn_s);         /* 行包围盒：y 是基线，见 place 侧说明 */

    /* 同名只标一次（哈希位图）。注意 name_id 只在本省 names.bin 内唯一，
       不同省的同名路（如"建设路"）各自带着自己的池，哈希到同一位会互相
       去重掉——这是可接受的：同屏出现两条同名路，标一条反而更干净。 */
    static uint8_t seen[1 << 14];
    memset(seen, 0, sizeof(seen));

    int drawn = 0;
    for (int i = 0; i < n; i++) {
        uint32_t id = lbl[i].name_id;
        if (id == 0 || (id & 0xFFFF) < 0x1000) { /* 简易哈希位 */ }
        uint32_t bit = (id * 2654435761u) & 0xFFFF;
        uint32_t byte = bit >> 3, mask = 1u << (bit & 7);
        if (seen[byte] & mask) continue;

        const NamePool *np = lbl[i].np;
        const char *s = (np && np->ready && id <= np->n)
                        ? np->blob + np->off[id] : NULL;
        if (!s || !*s) continue;

        int tw = font_text_w(font, s);
        int x = lbl[i].x - tw / 2, y = lbl[i].y - 7;
        if (x < 0 || y < 0 || x + tw >= v->w || y >= v->h) continue;
        int gx = lbl[i].x / g_grid_w, gy = lbl[i].y / g_grid_h;
        if (gx >= GRID_COLS || gy >= GRID_ROWS) continue;
        if (g_grid[gy * GRID_COLS + gx]) continue;
        g_grid[gy * GRID_COLS + gx] = 1;
        seen[byte] |= mask;

        /* 路名用深蓝灰，与地名黑灰区分；同样全比例尺走白描边，
         * 否则深蓝灰小字 + 1px 黑影仍是发虚的一批。 */
        if (lcmd_push(halo_on ? LCMD_HALO : LCMD_SHADOW, font, s,
                      x, y, y - up_s - 3, y + dn_s + 3,
                      70, 80, 110, 0, 0, 0) != 0)
            break;                          /* OOM：这帧余下路名不画 */
        drawn++;
    }
    label_flush(rs);
    if (st) st->road_labels_drawn = drawn;
}


/* ------------------------------------------------------------------------
 * 沿路告警点（places.db 的 alarm 表 → route/voice.c 的告警编排）
 *
 * 为什么放在这里：LabelCache 本来就持有 places.db 的只读句柄，告警点在同一
 * 库里；复用句柄就不用再开一条 sqlite 连接（sqlite 连接不是并发安全的）。
 *
 * ★ 旧库兼容：老 places_auto.db 没有 alarm 表 ⇒ prepare 失败 ⇒ 直接返回 0，
 *   voice_tick 的告警段继续用 edge_flags 的隧道/收费/桥梁，静默降级不报错。
 * ★ 当前数据规模（北京 35 个点）下每帧查一次 SQL 的成本可忽略；将来接
 *   全国库（几十万点）时应在这里加「车移动 >50m 或 500ms」节流 + 缓存
 *   绝对里程，别把 SQL 塞进每帧热路径。
 * ---------------------------------------------------------------------- */
int label_cache_query_alarms(LabelCache *lc,
                             const int32_t *lat_e7, const int32_t *lon_e7,
                             const uint32_t *seg_m, uint32_t npts,
                             double along_m, LabelAlarm *out, int cap)
{
    if (!lc || !lc->db || !out || cap <= 0 || !lat_e7 || !lon_e7 || !seg_m ||
        npts < 2 || along_m < 0.0) return 0;

    /* 1) 车头在路线上的位置：只用于给 SQL 一个 bbox 粗筛（走 alarm_latlon 索引） */
    double acc = 0.0, total = 0.0;
    for (uint32_t i = 0; i + 1 < npts; i++) total += (double)seg_m[i];
    if (along_m >= total) return 0;
    uint32_t si = 0; double t = 0.0;
    for (si = 0; si + 1 < npts; si++) {
        double l = (double)seg_m[si];
        if (acc + l >= along_m) { t = l > 0.0 ? (along_m - acc) / l : 0.0; break; }
        acc += l;
    }
    if (si + 1 >= npts) return 0;
    double la0 = (lat_e7[si] + (lat_e7[si+1] - lat_e7[si]) * t) / 1e7;
    double lo0 = (lon_e7[si] + (lon_e7[si+1] - lon_e7[si]) * t) / 1e7;

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(lc->db,
            "SELECT id,lat,lon,type FROM alarm "
            "WHERE lat BETWEEN ?1 AND ?2 AND lon BETWEEN ?3 AND ?4",
            -1, &st, NULL) != SQLITE_OK)
        return 0;                       /* 老库无 alarm 表：静默降级 */
    sqlite3_bind_double(st, 1, la0 - 0.02);
    sqlite3_bind_double(st, 2, la0 + 0.02);
    sqlite3_bind_double(st, 3, lo0 - 0.03);
    sqlite3_bind_double(st, 4, lo0 + 0.03);

    /* 等距近似：屏幕外 2km 内用平面坐标足够，误差远小于 60m 吸附阈值 */
    const double kx = 111320.0 * cos(la0 * 3.14159265358979323846 / 180.0);
    const double ky = 110540.0;
    const double max_d2 = 60.0 * 60.0;
    int n = 0;

    while (sqlite3_step(st) == SQLITE_ROW) {
        int    id   = sqlite3_column_int(st, 0);
        double la   = sqlite3_column_double(st, 1);
        double lon  = sqlite3_column_double(st, 2);
        int    type = sqlite3_column_int(st, 3);
        double px = (lon - lo0) * kx, py = (la - la0) * ky;
        double best_d2 = max_d2, best_s = -1.0;

        /* 2) 只扫 [along-50, along+1500] 的段，长途路线不会退化成全路线扫 */
        double a = 0.0;
        for (uint32_t i = 0; i + 1 < npts; i++) {
            double len = (double)seg_m[i];
            if (a + len < along_m - 50.0) { a += len; continue; }
            if (a > along_m + LABEL_ALARM_MAX_DIST_M) break;
            double ax = (lon_e7[i]   / 1e7 - lo0) * kx;
            double ay = (lat_e7[i]   / 1e7 - la0) * ky;
            double bx = (lon_e7[i+1] / 1e7 - lo0) * kx;
            double by = (lat_e7[i+1] / 1e7 - la0) * ky;
            double dx = bx - ax, dy = by - ay;
            double dd = dx * dx + dy * dy;
            double tt = dd > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / dd : 0.0;
            if (tt < 0.0) tt = 0.0;
            if (tt > 1.0) tt = 1.0;
            double qx = ax + tt * dx, qy = ay + tt * dy;
            double d2 = (px - qx) * (px - qx) + (py - qy) * (py - qy);
            if (d2 < best_d2) { best_d2 = d2; best_s = a + tt * len; }
            a += len;
        }
        if (best_s > along_m && best_s - along_m <= LABEL_ALARM_MAX_DIST_M) {
            out[n].type   = type;
            out[n].inst   = LABEL_ALARM_INST_BASE + id;  /* 表主键：位置恒定的实例身份 */
            out[n].dist_m = best_s - along_m;
            if (++n >= cap) break;
        }
    }
    sqlite3_finalize(st);
    return n;
}
