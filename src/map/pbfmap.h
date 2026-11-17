#ifndef SKEDADDLE_PBFMAP_H
#define SKEDADDLE_PBFMAP_H

#include "map/binfmt.h"

/* 从 OSM PBF 直接解码出**显示层**数据（省粒度）。
 *
 * 动机：显示相关产物（lines/areas/polyrefs）只占 nmbin 的 ~200 MB
 * （8 省合计，占 nmbin 总量 4.8%），而 nmbin 整体 4.2 G 里 94% 是路由图。
 * 把显示层改成"从 PBF 按需解码 + 内存 LRU 缓存"，则这几个文件可以整个
 * 不落盘，运行时只驻留视口所在省份的显示数据。
 *
 * 为什么是**省粒度**而不是瓦片粒度：实测 PBF 的 data blob 平均覆盖全省
 * 43% 的纬度跨度（北京 621 个 blob，单个 blob 跨 0.99°/1.33°，z11 视口仍命中
 * 53% 的 blob）⇒ 按 blob 跳读的收益低于建索引的成本，**不可行**。
 * 而"整省解一次 + 缓存"是可预测的：解码一省 = 全量扫一遍该省 PBF。
 *
 * ★ 为什么**不需要**解码节点坐标：polyrefs 存的是"节点内部下标"，
 * 而这个下标空间**就是** nmbin nodes.bin 的下标空间（两者都按 osm id
 * 升序排列、去掉了文件里不存在的节点）。所以坐标直接取自 nmbin 的
 * nodes.bin，本模块只需产出 areas/lines/polyrefs 三个缓冲。
 * （8 省实测：解码结果与落盘文件**逐字节相同**。）
 *
 * 产出的 NMBinFile 是**内存缓冲**（data 由 malloc 分配、mapped=0），
 * 布局与落盘的同名文件逐字节一致 ⇒ 渲染端（paint.c）零改动。
 */

typedef struct {
    NMBinFile areas;      /* 面要素记录 */
    NMBinFile lines;      /* 线要素记录 */
    NMBinFile polyrefs;   /* u32 节点内部下标数组 */
    uint64_t n_areas;
    uint64_t n_lines;
    uint64_t n_polyrefs;
} PbfMap;

/* 进度回调：frac ∈ [0,1]，解码主循环按"已读字节/文件总长"推进后调用；
 * 用于后台线程把实时进度回报给 UI（ud 由调用方传入，可为 NULL）。
 * cb 在**解码线程**里调用，调用方负责线程安全（这里只回调，不碰锁）。 */
typedef void (*PbfMapProgress)(void *ud, double frac);

/* 协作式中止钩子（2026-11-17）：在解码主循环**每个 blob 前**调用，
 * 返回非 0 = 请求中止。中止后 pbfmap_decode_ex 内部清干净局部缓冲并返回 -1，
 * out 保持全零（未接管任何缓冲）⇒ 调用方不需要额外善后。
 * 用于 mapset_free 等"必须尽快停手"的场合：否则释放方只能干等整段解码
 * （大省 >10s），超时强行 free 就是对 worker 正在读的数据做 UAF。 */
typedef int (*PbfMapAbort)(void *ud);

/* 从 pbf_path 解码一省的显示层到 out。
 *
 * nodes_index 必须是该省 nmbin 的 nodes.bin（提供 osm_id → 内部下标的
 * 映射）。它已经在内存里（mmap），用它做二分查表省掉"再扫一遍 PBF 取坐标"。
 *
 * cb/ud：可选进度回报（后台线程解码时传，同步解码可传 NULL）。
 *
 * 返回 0 = 成功；-1 = 失败。失败时 out 的内容未定义（调用方仍应调
 * pbfmap_free 释放可能已分配的缓冲）。 */
int  pbfmap_decode(const char *pbf_path, const NMBinFile *nodes_index,
                    PbfMap *out, PbfMapProgress cb, void *ud);

/* 同上 + 可选中止钩子（abort_fn/abort_ud 可传 NULL = 不支持中止）。 */
int  pbfmap_decode_ex(const char *pbf_path, const NMBinFile *nodes_index,
                       PbfMap *out, PbfMapProgress cb, void *ud,
                       PbfMapAbort abort_fn, void *abort_ud);

/* 释放 out 持有的内存缓冲（可重复调用）。 */
void pbfmap_free(PbfMap *m);

/* 泄漏监视（判据用）：当前已解码、尚未 pbfmap_free 的缓冲字节数。
 * 正常状态应恒为 0；解码产物全部被接管（src.dmap）后由 src_close /
 * src_unload / LRU 淘汰释放，中途放弃的结果则由放弃方释放。 */
long long pbfmap_live_bytes(void);

#endif
