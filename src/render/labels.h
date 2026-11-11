#ifndef SKEDADDLE_LABELS_H
#define SKEDADDLE_LABELS_H

#include <sqlite3.h>
#include "map/map_view.h"
#include "render/font.h"
#include "map/binfmt.h"
#include "map/namepool.h"   /* NamePool / name_pool_open·close：已从本头下沉到 map 层 */
#include "render/paint.h"

/* 地名标注：从 places.db（place 表）按视口查询，网格避让后画。
 * 查询结果缓存在 LabelCache 里，视口位移超阈值或缩放导致跨度变化超 20%
 * 才重新查 SQL；小幅平移时只重投影 + 重避让。 */

typedef struct {
    char   name[64];
    double lat, lon;
    int    kind, weight;
    int    sub;      /* kind 11 的子类（0=通用蓝圆 1交通 2教育 3医疗 4旅游 5生活），
                        语义与 tools/places_build.c 的 PSUB_* 对齐；其余 kind 恒 0 */
} LabelItem;

/* 沿路告警点（places.db 的 alarm 表）查询结果。
 * ★ 刻意不 include route/voice.h：mapset.h 会反向 include 本头拿 NamePool，
 *   在公共头里拉进 route 层会造成 include 环；用 POD 过渡，ui/window.c
 *   负责转成 VoiceAlarm。type 与 VOICE_ALARM_* 双处锚定（当前只用 4 测速）。 */
typedef struct {
    int    type;
    int    inst;      /* alarm 表主键：已播级别的实例身份，绝不能用序号 */
    double dist_m;    /* 沿路线距当前位置还有多远 */
} LabelAlarm;

typedef struct {
    sqlite3   *db;
    LabelItem *items;
    int        n_items, cap_items;
    double     cached_bb[4];
    double     cached_zoom;
    int        cached_kindmax;
    int        cached_poi;       /* 上次查询时 show_poi 的值 */
    int        loaded;
    /* 外部 POI（设置 poi_source=1 时启用）：CSV 在 init 时整表载入内存，
       视口过滤后与 SQL 结果按 weight 合并取 Top-N。只接管"设施 POI"
       （kind 11）这一层；行政地名（kind 1~10）始终来自地名库。 */
    LabelItem *ext;               /* 外部 POI 条目（kind 恒为 11） */
    int        n_ext, ext_cap;
    int        poi_source;        /* 0=地图自带（缺省） 1=外部文件 */
    int        cached_poi_source; /* 上次查询时 poi_source 的值（切换须重查） */
} LabelCache;

/* 查 alarm 表并把点投影到路线折线上，返回前方 LABEL_ALARM_MAX_DIST_M 内
   的点数（旧库无 alarm 表 ⇒ prepare 失败 ⇒ 静默返回 0，不报错）。
   ★ 必须放在 LabelCache 定义之后（本头被 mapset.h 反向 include 拿 NamePool，
     前置声明不够用）。rr 用基本数组传入以避开公共头的类型依赖。 */
#define LABEL_ALARM_MAX_DIST_M 1500.0
/* 点状告警 inst 的偏移：route/voice.c 里「边标志位告警」的 inst 是**段下标**
 * （非负、通常 < 几万）。VoiceRouter 用 (type,inst) 记已播级别，若这里也用
 * 小的表主键，虽然当前 type 不重叠，将来 alarm 表加隧道/收费类型就会撞槽
 * ⇒ 统一加一个大偏移，保证与段下标不冲突。 */
#define LABEL_ALARM_INST_BASE 1000000
int label_cache_query_alarms(LabelCache *lc,
                             const int32_t *lat_e7, const int32_t *lon_e7,
                             const uint32_t *seg_m, uint32_t npts,
                             double along_m, LabelAlarm *out, int cap);

int  label_cache_init(LabelCache *lc, const char *db_path);
void label_cache_free(LabelCache *lc);
/* show_poi / show_labels：show_poi 是 POI 总开关——关闭时设施类 POI（kind 11）
 * 的图标与名称都不画、不占避让格，SQL 也把 Top-N 名额让给行政地名；其余类别的
 * 矢量图标仍由它控制。show_labels 控制"文字标签"是否绘制（受 Qt『设置』菜单 /
 * C 设置面板的 POI 显示 / 地名标注 开关控制）。二者独立，共用一个避让格。
 *
 * zoom<14 时额外分两级画「区域名」：kind<=2 的市/区名用 ×2 大字号 + 白描边，
 * kind==3 的镇名加白描边，其余 POI 小字保持原样。font_size 是基础字号(pt)，
 * 大字号字体由本模块按 pt 缓存、app_shutdown 调 label_admin_font_free() 释放。
 * ★ 只在 zoom<14 生效，z>=14 逐像素保持原行为。 */
void paint_place_labels(Raster *rs, const MapView *v,
                        LabelCache *lc, Font *font, int font_size, PaintStats *st,
                        int show_poi, int show_labels);

/* 释放区域名大字号字体（可多次调用） */
void label_admin_font_free(void);

/* 反查关注点最近的上级行政区（市级 / 区县级），供"无图区提示"回答"这是哪儿"。
 *
 * 为什么放在这里而不是 map/coverage.c：地名库由 LabelCache 持有（render 层），
 * 而 map 层不能反向依赖 render ⇒ 省一级在 coverage_locate()、市/区在这里，
 * 由 UI 层把两者拼成一条 CovPlace。
 *
 * 分层取值按 skedaddle_places_build.c 的 kind 语义（数值小 = 行政级别高）：
 *   市级   kind <= 2（1=省会/直辖市城区，2=地级市）
 *   区县级 kind == 3（县/镇）
 * 输出为"最近的"同名点：地名库里没有省界多边形，只能按最近点近似，故可能
 * 在省界附近报成邻省的市——调用方应把它当作"大致位置"，不要当行政归属依据。
 * 返回填了几项（0/1/2）；lc 未打开时返回 0 且两个缓冲置空。 */
int  label_nearest_admin(LabelCache *lc, double lat, double lon,
                         char *city, size_t ccap,
                         char *district, size_t dcap);

/* 路名池：每省目录一份 names.bin，id 只在本省内有意义，所以做成可实例化的，
 * 由 MapSrc 持有；不再用全局单例。
 * ★ NamePool 与 name_pool_open/close 已搬到 map/namepool.h（PLAN.md §53 R1）——
 * 它们是纯地图数据概念，放在 render 会让 map 反向依赖 render。 */
void paint_road_labels(Raster *rs, const MapView *v, Font *font,
                       PaintStats *st);

#endif
