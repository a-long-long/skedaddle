#ifndef SKEDADDLE_SEARCH_H
#define SKEDADDLE_SEARCH_H

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * 地名 / POI 搜索（阶段 7）
 *
 * 直接用现成的 places_auto.db（226k 地名）：
 *   place(id, name, lat, lon, kind, weight)   —— 主表
 *   gram(gram, place_id)                      —— 二元文法（bigram）倒排索引
 * arch.md 的要求：SQLite 只当**索引**用，路网主存储仍然是 nmbin 文件。
 *
 * 为什么走 gram 而不是 LIKE '%关键词%'：
 *   place 表只有 id 上的主键，没有 name 索引，'%xx%' 只能全表扫 22 万行
 *   （实测数十毫秒级，且输入过程中每敲一个字都要扫一遍）。gram 是
 *   WITHOUT ROWID 的 (gram, place_id) 主键，等值查询直接走索引。
 *
 * 查询流程：
 *   1. 关键词切 bigram（按**字符**切，不是字节——"中关村" → 中关/关村）
 *   2. gram 表取"同时含有全部 bigram"的 place_id（交集）
 *   3. 按 id 逐条取详情（主键单行查询，微秒级）
 *   4. ★ 再用子串复核一遍：bigram 全命中不等于连续（"中关"+"关村" 也能
 *      命中"中关村"，但也可能命中"中…关村"这类不相邻的），gram 只是召回，
 *      精确性靠这一步
 *   5. 按距离（给了中心点）或 weight 排序，取 top-K
 *
 * 单字查询没有 bigram 可切 → 走 gram 前缀（gram LIKE 'X%'，仍走索引），
 * 前缀也没结果时退化到全表 LIKE（保证不漏，代价是慢一点，只在单字时发生）。
 * ------------------------------------------------------------------------- */

typedef struct {
    char   name[64];
    double lat, lon;
    int    kind;        /* 与标注层同语义：数值小 = 行政级别高 */
    int    weight;
    double dist_m;      /* 到排序中心的距离（米）；无中心时为 -1 */
    double score;       /* 排序分（越小越靠前），把"距离/权重/匹配质量"合成一个数。
                         * 留着它是为了能断言排序：CLI 里直接检查它单调即可，
                         * 不用把排序公式再抄一遍。 */
} SearchHit;

typedef struct SearchDb SearchDb;

/* ---------------------------------------------------------------------------
 * 外部 POI（products/poi_ext.csv，SKEDADDLE_POI_EXT 可覆盖路径）
 *
 * 地图自带 POI 走 place/gram 表，bigram 倒排在**建库时**生成；外部 POI 是
 * 运行期附加的一份数据，从没进过建库器，所以原先只能画图标、搜不到。
 * 这里把它做成内存补充索引：查询时对每条做与 DB 行完全相同的子串复核 /
 * 过滤 / 打分，再和 DB 候选一起排序取 top-K。
 *
 * 为什么不写进 gram 表：places_auto.db 是**只读抽取产物**（sync_maps.sh
 * 随时整库重建），运行时往里写既破坏"产物可删可重建"的约定，也会让每次
 * 启动都改库文件。内存索引随进程生灭，零持久化、旧库零改动。
 * ------------------------------------------------------------------------- */
typedef struct {
    char   name[64];
    double lat, lon;
    int    kind;      /* 恒为 11（设施 POI），与 place.kind 同语义 */
    int    weight;
    int    sub;       /* 0~5 子类配色，标注层用；搜索层原样透传 */
} SearchExtPoi;

#define SEARCH_EXT_KIND 11
#define SEARCH_EXT_MAX  200000    /* 线性复核的软上限，防一份误传的巨型 CSV 吃满内存 */

/* 载入 CSV（UTF-8，'#' 注释、空行忽略）：
 *     名称,纬度,经度[,权重[,子类]]
 * 权重缺省 50、子类缺省 0（钳 0~5）；坐标越界的行丢弃。
 * 文件不存在 / 打不开 ⇒ 返回 0 且 *n_out=0（空表，不是错误）。
 * 成功时 *out 是 malloc 出来的数组，调用方 free()。返回 0 / -1（内存不足）。
 * ★ 标注层（render/labels.c）与本层共用**同一份**解析，避免两处格式漂移。 */
int search_ext_load_csv(const char *path, SearchExtPoi **out, int *n_out);

/* 把外部 POI 并入已打开的搜索库（可多次调用，逐次追加），返回实际并入条数。
 * ★ 外部 POI 与库里 POI 在搜索里是**并集**：显示层 poi_source 是"二选一"的
 *   渲染口径，搜索层不该屏蔽任何一边 —— 用户新加的数据要能被检索到，屏蔽
 *   库里的反而会减少结果（门牌 kind 14 早就是"只进搜索永不画"的先例）。
 * ★ items==NULL / n<=0 ⇒ 零追加，查询结果逐条不变（负向对照锚）。 */
int search_add_external(SearchDb *d, const SearchExtPoi *items, int n);

/* 只读打开。失败返回 NULL（库文件不存在/不是 sqlite）。 */
SearchDb *search_open(const char *db_path);
void      search_close(SearchDb *d);

/* 查询。
 *   q          UTF-8 关键词（空串 → 直接返回 0 条，不报错）
 *   have_center 非 0 时按到 (clat,clon) 的距离排序
 *   max_dist_m  > 0 时丢弃更远的结果
 *   kind_max    > 0 时只保留 kind<=kind_max 的（0 = 不限）
 *   limit       > 0 为返回上限；<=0 时用默认 20（上限 64）
 * 返回 0 成功（*n_out 是结果条数），-1 参数/库问题。
 * elapsed_ms 可空，回填本次查询耗时。 */
int search_query(SearchDb *d, const char *q,
                 int have_center, double clat, double clon,
                 double max_dist_m, int kind_max, int limit,
                 SearchHit *out, int cap, int *n_out, uint64_t *elapsed_ms);

/* 关键词切 bigram 的可见入口（测试用）：返回 gram 个数，写到 grams[]（每个
   最多 16 字节）。chars 回填关键词的字符数。 */
int search_bigrams(const char *q, char grams[][16], int max_grams, int *chars);

/* 地址串规范化：去空白（含全角空格）、全角数字/字母转半角、ASCII 大写转
   小写、去掉末尾的「号 / 號」。返回写入 out 的字节数（不含 NUL）。
 * ★ 建库端（places_build 拼 addr:street + addr:housenumber）与查询端
 *   （search_query）必须调同一份，否则「建国路 88 号」搜不到库里的
 *   「建国路88号」——bigram 与子串复核会同时失配（沿用 search_bigrams
 *   那份「两端一份实现」的纪律）。
 * ★ 普通地名不带空格/号尾缀 ⇒ 规范化是恒等变换，旧查询结果逐条不变。 */
int search_normalize_addr(const char *in, char *out, size_t cap);

/* 逆地理编码（2026-11-12，L2）：在半径 radius_m 内按距离取
 *   「市/省会」→「区县/片区」→「街道名」→「门牌」四级，
 *   用 addr_join 合成「A · B · C · D」写入 out。
 * ★ 只用现成的 place 表（kind 1..4 / 13 / 14），不建新表、不联网、
 *   不改产物 ⇒ 存量 places_auto.db 直接可用。
 * ★ place 表没有经纬度索引，这里先按粗 bbox 过滤再逐条算大圆距离；
 *   一次点击才触发，226k 行全表扫也在几十毫秒级。
 * 返回：0 = 合成成功；1 = 半径内没有任何地址要素（out 为空，不是错误）；
 *       -1 = 参数 / 数据库错误。radius_m<=0 时取默认 3000 m。
 * 四级全缺时返回 1；只缺某几级时照常合成（addr_join 跳过空字段）。 */
int search_reverse(SearchDb *d, double lat, double lon, double radius_m,
                   char *out, size_t cap);

#endif
