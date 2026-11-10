#ifndef SKEDADDLE_RENDER_BAND_H
#define SKEDADDLE_RENDER_BAND_H

/* ---------------------------------------------------------------------------
 * 分带并行渲染（2026-11-10）
 *
 * 背景：渲染是纯软件光栅，且**完全单线程** —— 面/路/线三类要素各自遍历完整
 * 要素集、写同一块像素缓冲，8 核机器上 7 个核在睡觉。实测 z9 一帧 57 ms
 * （面 19 / 路 19 / 线 16 / 标注 2，1280×800，第 2 帧）。
 *
 * 唯一的风险是**像素写冲突**，所以设计上把画布按行切成 n 条带，一条带只由
 * 一条线程写 ⇒ 不需要任何锁，也不引入新的可见差异。
 *
 * ★ 正确性前提（改这里之前先读，三条缺一条就会出"越界/缺块/压盖颠倒"）：
 *   ① 所有绘制原语必须尊重 Raster.clip —— raster.h 的 hspan / point /
 *      px_rgba / fill_rect / line 已统一按 clip 裁剪（2026-11-10 改）。
 *      clip = 全画布时与改动前逐字节等价，这是负向对拍的地基。
 *   ② 每个要素必须**完整地**交给每条相交的带：跨带要素要在每个带各画一次，
 *      各自靠 clip 取自己那一半。只按中心行归带会在带边界缺一条线。
 *   ③ 带内的要素处理顺序必须与串行一致（面/线按索引升序）。顺序变了，
 *      重叠要素的压盖关系就变，逐字节对拍立刻抓到。
 *
 * 环境变量 SKEDADDLE_BANDS：未设 = 自动；0 = 关闭（走纯串行基线）；
 * N = 强制 N 带。用于负向对照与压测。
 * ------------------------------------------------------------------------- */

#include "render/raster.h"

#define SK_BAND_MAX 16

typedef struct {
    int    n;                        /* 带数（= 并行线程数） */
    int    edge[SK_BAND_MAX + 1];    /* 带 t 覆盖 y ∈ [edge[t], edge[t+1]) */
    Raster base;                     /* 全画布 Raster 的副本 */
    Raster band[SK_BAND_MAX];        /* 每带的 Raster：clip.y/h 限定到该带 */
} BandView;

/* 把 rs 垂直切成 n 条带（n 夹到 [1, SK_BAND_MAX]），返回实际带数。 */
int  band_init(BandView *bv, const Raster *rs, int n);

/* 阻塞式并行：对 t ∈ [0, n) 调 f(arg, t)，全部返回后才返回。
 * 直接用 &bv->band[t] 当绘制目标即可（它的 clip 已限定在该带）。
 * 资源不足或线程起不来时自动退回串行 —— 宁慢不可缺块。 */
int  band_run(BandView *bv, void (*f)(void *, int), void *arg);

/* SKEDADDLE_BANDS 的解析结果：-1 = 自动（未设置），0 = 关闭，>0 = 指定带数。 */
int  band_requested(void);

/* 自动档的带数：核数，上限 8（再多会被内存带宽和线程开销吃掉）。 */
int  band_auto_count(void);

/* 按当前配置决定实际带数：处理 SKEDADDLE_BANDS 的三种取值。 */
int  band_pick_count(void);

#endif /* SKEDADDLE_RENDER_BAND_H */
