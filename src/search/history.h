#ifndef SKEDADDLE_SEARCH_HISTORY_H
#define SKEDADDLE_SEARCH_HISTORY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 历史记录（2026-11-12，L2 本地能力）
 *
 * 定位：**最近选中的地点**（搜索结果点选 / 以后可扩到导航目的地）。
 * 有意做得很小、很笨：固定容量数组 + 一行一条的文本文件，不建 SQLite 表、
 * 不进 places_auto.db、不联网、不记轨迹。隐私口径一目了然 —— 就是用户
 * 明确点过的地点，删掉 history.txt 即彻底清空。
 *
 * 为什么不复用 gpx 的 tracks.db：那是「轨迹/收藏」，语义是长期保存的作品；
 * 历史是「最近用过」，会被自动淘汰（MRU + 容量上限）。混在一起会让
 * 「清空历史」误删用户收藏，也会让收藏列表被随手一点撑满。 */

#define HISTORY_MAX   32
#define HISTORY_NAME  96

typedef struct {
    char     name[HISTORY_NAME];
    double   lat, lon;
    int      kind;      /* 与 SearchHit.kind 同语义 */
    uint64_t ts;        /* 由调用方给（测试可传固定值 ⇒ 确定性） */
} HistoryItem;

typedef struct {
    HistoryItem v[HISTORY_MAX];
    int         n;                 /* v[0] 是最近一次 */
    char        path[1024];
} History;

/* path=NULL 时用 sk_config_dir()/history.txt。init 不清空已有内容，需再 load。 */
void history_init(History *h, const char *path);
void history_clear(History *h);

/* 载入：文件不存在 / 格式坏 ⇒ 按空历史处理并返回 -1（非致命）。
 * ★ 坏行跳过而不是整份作废，避免一次写坏就永远丢光。 */
int  history_load(History *h);
int  history_save(const History *h);

/* 加一条（MRU）。同名且坐标差 <1e-5 视为同一地点 ⇒ 提到队首并更新时间，
 * 不重复占位；超过 HISTORY_MAX 时淘汰最旧的。返回该项最终所在下标。 */
int  history_add(History *h, const char *name, double lat, double lon,
                 int kind, uint64_t ts);

/* 供 UI 列菜单：把最近 n 条格式化成 "名称 · lat,lon"（调试/自检也可读）。 */
int  history_format(const History *h, int i, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SKEDADDLE_SEARCH_HISTORY_H */
