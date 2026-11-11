#include "render/paint.h"
#include "render/band.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>    /* snprintf：等高线高程刻度标注 */

#include "map/binfmt.h"
#include "map/coordkey.h"
#include "map/road_tags.h"
#include "utils/geo.h"
#include "render/labels.h"
#include "render/raster.h"

/* ------------------------------------------------------------------ */
/* 分带并行的启用门槛                                                   */
/* ------------------------------------------------------------------ */

/* 每条线程 spawn+join 大约 50us；要素量不够时并行纯亏（z15 一帧总共才
   2~3 ms）。门槛取"起线程能省回来的量级"，可按实测再调。 */
#define SK_BAND_MIN_AREAS 8000   /* z12 及以上 */
#define SK_BAND_MIN_LINES 20000  /* z9/z11 是 1.9 万 / 12 万条 */
#define SK_BAND_MIN_ROADS 2000   /* 道路走 tile 索引，z9 就已有 27 万条 */

/* 分桶的行余量：raster_thick_line 最粗 width=5，会画到 y±2 且不做 y 裁剪，
   分桶只按端点纬度 ⇒ 不留余量，紧贴带边的那半条描边会被上下两条带都拒收。 */
#define SK_BAND_PAD 3

/* ------------------------------------------------------------------ */
/* 路名收集（paint_roads 顺带记录，labels.c 消费）                       */
/* ------------------------------------------------------------------ */

#define ROAD_LABEL_CAP 4096
static RoadLabel g_rl[ROAD_LABEL_CAP];
static int       g_rl_n;

void road_labels_reset(void)          { g_rl_n = 0; }
void road_labels_get(const RoadLabel **out, int *n) { *out = g_rl; *n = g_rl_n; }
void road_labels_add(const NamePool *np, uint32_t name_id, int x, int y)
{
    if (g_rl_n >= ROAD_LABEL_CAP) return;
    g_rl[g_rl_n].np = np;
    g_rl[g_rl_n].name_id = name_id;
    g_rl[g_rl_n].x = x;
    g_rl[g_rl_n].y = y;
    g_rl_n++;
}

/* 路的"退化成单像素"去重：每帧开头复位一次。
   ★ 必须在 paint_frame 里调（不是 paint_roads 里）：多省时 paint_roads
     每省调一次；而 g_last_px 跨帧不清会出 holes——背景每帧重刷，上一帧
     写过的像素这帧是空的，若拿上一帧的状态去判"已写过"就会漏画。 */
static __thread int g_last_px = -1, g_last_tag = -1;

/* 小面画点阈值的运行时档位。缺省 = 编译期 AREA_DOT_PX，逐字节兼容旧出图。 */
static int g_area_dot_px = AREA_DOT_PX;
void paint_set_area_dot_px(int px)
{
    if (px < 2)  px = 2;
    if (px > 12) px = 12;
    g_area_dot_px = px;
}
void paint_roadpix_begin(void) { g_last_px = -1; g_last_tag = -1; }

/* 路面配色：高等级路更醒目，低等级路压暗 */
typedef struct { uint8_t r, g, b; uint8_t width; } Style;

static const Style kStyle[5] = {
    { 214,  94,  40, 4 },   /* 0 高速/国道：橙红 */
    { 240, 176,  64, 3 },   /* 1 主干：橙黄 */
    { 252, 226, 150, 2 },   /* 2 次干：浅黄 */
    { 150, 150, 150, 1 },   /* 3 支路/小路：灰 */
    { 190, 190, 190, 1 },   /* 4 其他 */
};

/* ------------------------------------------------------------------ */
/* 跨省去重集合（64 位指纹 + 开放寻址 + 世代标记，槽 = 1 条 cache line） */
/* ------------------------------------------------------------------ */

static uint64_t dd_mix(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static int dd_alloc(DedupSet *d, size_t cap)
{
    DdSlot *m = calloc(cap, sizeof(DdSlot));
    if (!m) return -1;
    d->slot = m;
    d->cap = cap;
    d->mask = cap - 1;
    d->used = 0;
    return 0;
}

void dedup_init(DedupSet *d)
{
    memset(d, 0, sizeof(*d));
    /* 起步 64K 槽（1 MB），不够按需翻倍，够用后不缩回 */
    if (dd_alloc(d, 1u << 16) != 0) memset(d, 0, sizeof(*d));
    d->cur = 1;
}

void dedup_free(DedupSet *d)
{
    if (!d || !d->slot) return;
    free(d->slot);
    memset(d, 0, sizeof(*d));
}

/* 扩容：把当前世代还活着的条目重新散列到更大的表。
   分配失败就退回旧表（表会偏满，去重率略降，不会错）。 */
static void dd_grow(DedupSet *d)
{
    size_t ocap = d->cap;
    DdSlot *os = d->slot;
    uint32_t cur = d->cur;

    if (dd_alloc(d, ocap * 2) != 0) {
        /* 分不到更大的表：退回旧的，并把 used 顶到满载让调用方别再试 */
        d->slot = os;
        d->cap = ocap; d->mask = ocap - 1; d->used = ocap; d->cur = cur;
        return;
    }
    d->cur = cur;
    for (size_t i = 0; i < ocap; i++) {
        if (os[i].gen != cur) continue;
        size_t j = (size_t)(os[i].key & d->mask);
        while (d->slot[j].gen == cur) j = (j + 1) & d->mask;
        d->slot[j].key = os[i].key;
        d->slot[j].gen = cur;
        d->used++;
    }
    free(os);
}

void dedup_begin(DedupSet *d)
{
    if (!d || !d->slot) return;
    d->cur++;
    /* 世代回绕：整表清零并重新从 1 开始，避免和"从未使用"的 gen=0 混淆 */
    if (d->cur == 0 || d->cur == UINT32_MAX) {
        memset(d->slot, 0, d->cap * sizeof(DdSlot));
        d->cur = 1;
    }
    d->used = 0;
}

int dedup_seen(DedupSet *d, uint64_t hi, uint64_t lo)
{
    if (!d || !d->slot) return 0;       /* 未启用（单省）：一律当首次 */
    uint64_t key = dd_mix(hi ^ dd_mix(lo));
    size_t i = (size_t)(key & d->mask);
    for (;;) {
        if (d->slot[i].gen != d->cur) { /* 空槽：登记并返回"首次" */
            d->slot[i].key = key;
            d->slot[i].gen = d->cur;
            d->used++;
            if (d->used * 4 >= d->cap * 3) dd_grow(d);
            return 0;
        }
        if (d->slot[i].key == key) return 1;    /* 画过了 */
        i = (i + 1) & d->mask;
    }
}

void paint_background(Raster *rs, int w, int h)
{
    raster_fill_rect(rs, 0, 0, w, h, 244, 241, 234, 255);
}

/* 粗线：raster 一次只画 1 像素宽，这里用竖向偏移画 n 条近似加粗。
   横向/近水平的线加粗效果差，但路网里绝大多数段是斜的，观感够用。
   路线叠加（navdraw.c）也用同一份，别在别处另写。 */
void paint_thick_line(Raster *rs, int x0, int y0, int x1, int y1, int width, uint32_t col)
{
    raster_thick_line(rs, x0, y0, x1, y1, width, col);
}

/* ------------------------------------------------------------------ */
/* 「把连续边合并成折线」试过了，没采纳（留档，省得下次又想到它）      */
/*                                                                     */
/* 设想：除了岔路口，多数节点的后继边唯一，可以把首尾相接的边接成一条   */
/*   折线，用一次 SDL_RenderDrawLines 画完，顺带把顶点投影减半。        */
/*   实现过一版（node → 唯一出/入边的开放寻址表 + 两遍行走），实测：    */
/*     - 合并率很好：z9 273770 条边 → 44435 条折线，平均每条 6.16 边    */
/*     - 但更慢：z9 路网 81 → 113 ms，z11 43 → 54 ms（+30 ms）          */
/*   原因（用临时诊断开关把两项成本分开量出来的）：                     */
/*     ①  机器代价：为了找"后继"，每条边要做 2 次哈希插入 + 若干次查询， */
/*        随机探表几乎每次都是一次访存缺失，摊到每条边 ~110 ns，超过收益 */
/*     ②  收益原本就被高估：以为省的是"绘制调用次数"，可 DrawLines 相对  */
/*        DrawLine 并不便宜（真正贵的是逐像素光栅化，像素一个都没少）；   */
/*        真正能省的只有"接点被重复投影那次"，而查表版投影上线后          */
/*        投影本身只剩 9 ms（路网 80 ms 的 11%），减半也才 ~4.5 ms。      */
/*   ⇒ 除非能不查表就找后继（例如边记录里带 way id + 段序号），别再做。  */
/* ------------------------------------------------------------------ */

/* 视口覆盖的 Z14 瓦片范围（±1 格 margin）。clamp 进目录范围。 */
typedef struct {
    int      tx_lo, tx_hi, ty_lo, ty_hi;
    uint32_t minx, miny, nx;
} TileRange;

static int tile_range(const NMBinFile *tiles, const MapView *v, TileRange *tr)
{
    /* tiles.bin payload：rec0 = {minx,miny}，rec1 = {nx,ny}，其后 ntile 条目录 */
    uint32_t minx = sk_get_u32(tiles->data + TF_EDGE_OFF);
    uint32_t miny = sk_get_u32(tiles->data + TF_EDGE_CNT);
    uint32_t nx   = sk_get_u32(tiles->data + SK_TILE_REC_BYTES + TF_EDGE_OFF);
    uint32_t ny   = sk_get_u32(tiles->data + SK_TILE_REC_BYTES + TF_EDGE_CNT);
    uint64_t ntile_dir = tiles->hdr.rec_count - 2;
    if (nx == 0 || ny == 0 || (uint64_t)nx * ny > ntile_dir)
        return 0;                               /* 目录不完整，放弃 */

    double bb[4];
    mv_visible_bbox(v, bb);
    double wxa, wya, wxb, wyb;
    sk_mercator(bb[0], bb[1], &wxa, &wya);
    sk_mercator(bb[2], bb[3], &wxb, &wyb);

    double Z = (double)(1u << SK_TILE_Z);
    int tx_lo = (int)( (wxa < wxb ? wxa : wxb) * Z) - 1;
    int tx_hi = (int)( (wxa > wxb ? wxa : wxb) * Z) + 1;
    int ty_lo = (int)( (wya < wyb ? wya : wyb) * Z) - 1;
    int ty_hi = (int)( (wya > wyb ? wya : wyb) * Z) + 1;
    if (tx_lo < (int)minx) tx_lo = (int)minx;
    if (ty_lo < (int)miny) ty_lo = (int)miny;
    if (tx_hi > (int)(minx + nx - 1)) tx_hi = (int)(minx + nx - 1);
    if (ty_hi > (int)(miny + ny - 1)) ty_hi = (int)(miny + ny - 1);

    tr->tx_lo = tx_lo; tr->tx_hi = tx_hi;
    tr->ty_lo = ty_lo; tr->ty_hi = ty_hi;
    tr->minx = minx; tr->miny = miny; tr->nx = nx;
    return 1;
}

/* 某省的索引里，(tx,ty) 这一格有多少条边——用来判"这格有几个省有数据"。
   0 = 该省不覆盖这格（或索引不可用）。 */
static uint32_t tile_cnt_at(const NMBinFile *tiles, int tx, int ty)
{
    if (!tiles || tiles->hdr.rec_count < 2) return 0;
    uint32_t minx = sk_get_u32(tiles->data + TF_EDGE_OFF);
    uint32_t miny = sk_get_u32(tiles->data + TF_EDGE_CNT);
    uint32_t nx   = sk_get_u32(tiles->data + SK_TILE_REC_BYTES + TF_EDGE_OFF);
    uint32_t ny   = sk_get_u32(tiles->data + SK_TILE_REC_BYTES + TF_EDGE_CNT);
    if (nx == 0 || ny == 0) return 0;
    if (tx < (int)minx || ty < (int)miny ||
        tx >= (int)(minx + nx) || ty >= (int)(miny + ny)) return 0;
    if ((uint64_t)nx * ny > tiles->hdr.rec_count - 2) return 0;
    size_t t = ((size_t)(ty - miny) * nx) + (tx - minx);
    return sk_get_u32(tiles->data + (2 + t) * SK_TILE_REC_BYTES + TF_EDGE_CNT);
}

static void tile_dir(const NMBinFile *tiles, const TileRange *tr, int tx, int ty,
                     uint32_t *off, uint32_t *cnt)
{
    size_t t = ((size_t)(ty - (int)tr->miny) * tr->nx) + (tx - (int)tr->minx);
    const uint8_t *dir = tiles->data + (2 + t) * SK_TILE_REC_BYTES;
    *off = sk_get_u32(dir + TF_EDGE_OFF);
    *cnt = sk_get_u32(dir + TF_EDGE_CNT);
}

/* 路名等级门槛：cls 越小道路等级越高（与 paint_roads_tiled 里的 eff_max
 * LOD 同序）。z12 起主干(cls<=2)；z14 起次干(cls<=3)；z16 起支路/胡同
 * (cls<=4)。★ 之前恒为 cls<=2：z14 后视口里主干道渐少，路名从 125 条
 * 掉到 z17 只剩 3 条，"放大反而更空"——违背"比例尺越大越详细"。 */
static int road_name_cls_max(double zoom)
{
    if (zoom < 14.0) return 2;
    if (zoom < 16.0) return 3;
    return 4;
}

/* 并行路阶段的路名锚点缓冲。
 *
 * ★ 为什么不能直接调 road_labels_add：路名按"先到先得"占避让格，顺序一变
 *   最终显示的标签就会变。分带并行时每条带各自按 tile 顺序收集，直接追加会
 *   变成"带0 全部 → 带1 全部"，跨带的重叠标签会换赢家。
 *   所以每条带先攒到自己的缓冲里，带 seq（tile 线性序 + 条目序），全部结束后
 *   一次归并排序 ⇒ 顺序与串行逐项相同。 */
typedef struct {
    const NamePool *np;
    uint32_t        name_id;
    int             x, y;
    uint32_t        seq;
} SeqLabel;

typedef struct {
    SeqLabel *a;
    int       n, cap;
} RoadSink;

static int sink_push(RoadSink *s, const NamePool *np, uint32_t nid,
                     int x, int y, uint32_t seq)
{
    if (s->n >= s->cap) {
        int nc = s->cap ? s->cap * 2 : 256;
        SeqLabel *p = (SeqLabel *)realloc(s->a, (size_t)nc * sizeof(SeqLabel));
        if (!p) return -1;
        s->a = p;
        s->cap = nc;
    }
    s->a[s->n].np = np;
    s->a[s->n].name_id = nid;
    s->a[s->n].x = x;
    s->a[s->n].y = y;
    s->a[s->n].seq = seq;
    s->n++;
    return 0;
}

static int seqlabel_cmp(const void *a, const void *b)
{
    uint32_t sa = ((const SeqLabel *)a)->seq, sb = ((const SeqLabel *)b)->seq;
    return sa < sb ? -1 : (sa > sb ? 1 : 0);
}

/* 画一条路（edgelist v2 记录）。串行与分带并行共用同一份代码。
 *   count=0：不记审计计数（跨带边被多条带各画一次，只能记一次）。
 *   sink!=NULL：并行模式，路名锚点攒进本线程缓冲（见 SeqLabel 的说明）。 */
static void road_emit(Raster *rs, const MvProj *P,
                      const NMBinFile *edges, const NamePool *np,
                      DedupSet *dd, double zoom, int W, int H, int margin,
                      uint64_t ne, const uint32_t *ccls, int eff_max,
                      const uint8_t *xl, uint32_t seq,
                      int count, RoadSink *sink, PaintStats *st,
                      int bid, int nb, int cy0, int cyh)
{
    static __thread PaintStats s_dummy;
    PaintStats *dst = count ? st : &s_dummy;

    uint8_t cls = xl[XL2_CLS];
    if (cls > eff_max) return;
    if (sk_get_u16(xl + XL2_FLAGS) & EF_REVERSE) return;

    int32_t fla = sk_get_i32(xl + XL2_FROM_LAT);
    int32_t flo = sk_get_i32(xl + XL2_FROM_LON);
    int32_t tla = sk_get_i32(xl + XL2_TO_LAT);
    int32_t tlo = sk_get_i32(xl + XL2_TO_LON);

    int x0, y0, x1, y1;
    mv_proj_e7(P, fla, flo, &x0, &y0);
    mv_proj_e7(P, tla, tlo, &x1, &y1);
    if ((x0 < -margin && x1 < -margin) || (x0 > W + margin && x1 > W + margin) ||
        (y0 < -margin && y1 < -margin) || (y0 > H + margin && y1 > H + margin))
        return;

    /* 带裁剪：本带不覆盖这条边的 y 范围就早退。跨带边在每条相交带各画一次，
       各自 clip 一半 —— 与"只归一条带"的差别就在带边界会不会缺一条线。
       ★ 判据必须**比"分桶用的纬度范围"更宽**：分桶先把边分到带，这里再判一次，
         若这里更严（或恰好相等），边界上的那几条会被分进来又立刻退掉 ——
         既不画也不记账（实测 z13 少 25 条、z15 少 30 条 = 21 个像素，全是
         y 正好落在 bt-bpad / bb2+bpad 那一行上的边）。宽 4 行做量化余量，
         多画的像素会被 clip 滤掉，只是稍慢一点点。 */
    int bt = rs->clip.y, bb2 = bt + rs->clip.h;
    int bpad = margin + SK_BAND_PAD + 4;
    if ((y0 < bt - bpad && y1 < bt - bpad) ||
        (y0 > bb2 + bpad && y1 > bb2 + bpad)) return;

    if (dd) {
        /* 身份 = 两端 e7 坐标的有序对。
           ★ 不能排序成 (min,max)：双向道路的回程边会因此被判成重复，整条路消失。
           ★ 比 v1 用 OSM node id 更稳：跨省同一路口即便两个 pbf 给的 id 体系
             不同，坐标也一致（coordkey.h 里整套跨省缝合的前提）。 */
        if (dedup_seen(dd, coord_key_e7(fla, flo), coord_key_e7(tla, tlo))) {
            dst->dedup_skipped++;
            return;
        }
    }

    const Style *s = &kStyle[cls > 4 ? 4 : cls];
    int width = (zoom < 12.5) ? 1 : s->width;
    dst->drawn++;
    /* 退化成一个像素的段：若紧邻的上一条落在同一像素且同等级，再画一次是
       纯浪费（z9 一帧 59 万条里 67% 是这种段）。只认"紧邻的前一条"是刻意的
       —— 中间不可能插进别的等级把它改写，所以像素结果与逐条画完全一致
       （用逐像素记录表试过：非退化的线穿过像素时不会更新记录表，会被误判，
       16 个像素换了赢家）。g_last_px 是 __thread 的：并行时每条带独立维护，
       而"同像素"必然同带（带是不重叠的 y 区间），所以行为与串行等价。 */
    if (width == 1 && x0 == x1 && y0 == y1 &&
        (unsigned)x0 < (unsigned)W && (unsigned)y0 < (unsigned)H) {
        int px = y0 * W + x0, tag = cls > 4 ? 4 : cls;
        if (px == g_last_px && tag == g_last_tag) return;
        g_last_px = px; g_last_tag = tag;
    } else {
        g_last_px = -1;
    }
    raster_thick_line(rs, x0, y0, x1, y1, width, ccls[cls > 4 ? 4 : cls]);

    if (cls <= road_name_cls_max(zoom) && zoom >= 12.0 &&
        x0 >= 0 && x0 < W && y0 >= 0 && y0 < H) {
        uint32_t eidx = sk_get_u32(xl + XL2_EDGE);
        if ((uint64_t)eidx < ne) {
            uint32_t nid = sk_get_u32(edges->data +
                                      (uint64_t)eidx * SK_EDGE_BYTES +
                                      EF_NAME_ID);
            if (nid) {
                int ax = (x0 + x1) / 2, ay = (y0 + y1) / 2;
                if (sink) {
                    /* 每条边只收一次，归属 = 锚点所在带（带外钳到端带）。
                       ★ 不能用"锚点落在本带才收"：串行路径无条件收 ——
                         from 端点在画布内、中点在画布外（长边跨出画布）的
                         锚点串行也会加，labels.c 自己会裁。若按带收，
                         画布外的锚点没有任何带认领 ⇒ 收集集合与串行不同，
                         避让结果随之而变（实测 z15 底缘 651 个像素差异）。
                       ★ 钳到端带而不是丢弃，再按 seq 归并 ⇒ 与串行同序。 */
                    int tgt = (int)(((long long)(ay - cy0)) * nb / cyh);
                    if (tgt < 0) tgt = 0;
                    if (tgt >= nb) tgt = nb - 1;
                    if (tgt == bid)
                        sink_push(sink, np, nid, ax, ay, seq);
                } else {
                    road_labels_add(np, nid, ax, ay);
                }
            }
        }
    }
}

/* tile 驱动路径：只遍历视口覆盖的 tile（±1 格 margin）。 */
static void paint_roads_tiled(Raster *rs, const MapView *v,
                              const NMBinFile *nodes, const NMBinFile *edges,
                              const NMBinFile *tiles, const NMBinFile *edgelist,
                              const NamePool *np, DedupSet *dd,
                              int max_class, PaintStats *st,
                              const NMBinFile *const *peer, int n_peer)
{
    TileRange tr;
    if (!tile_range(tiles, v, &tr)) return;

    /* 5 个等级的颜色一次打包好：原来每条边一次 SDL_SetRenderDrawColor，
       z9 一帧 77 万次纯状态切换（实测 3.9 ms，不值但它白占着循环）。 */
    uint32_t ccls[5];
    for (int k = 0; k < 5; k++)
        ccls[k] = raster_rgb(rs, kStyle[k].r, kStyle[k].g, kStyle[k].b);

    double zoom = v->zoom;
    int W = v->w, H = v->h;
    const int margin = 64;
    uint64_t nn = nodes->hdr.rec_count, ne = edges->hdr.rec_count;

    MvProj Pstore, *P = &Pstore;
    mv_proj_begin(v, P);

    /* 按 zoom 自动降 LOD，与用户手动的 max_class 取交集。
       z12 视口覆盖全省：245096 条边全画换来 123 ms，而三级小路在屏幕上
       密得看不清、又细成 1 像素，纯属视觉噪音 + 白画。地图常识就是
       "越远越只看主干"。手动调高不会突破这里的上限——按 0-4 键时看到
       的提示要与这个表一致。 */
    int eff_max = max_class;
    if (zoom < 9.0)        { if (eff_max > 0) eff_max = 0; }
    else if (zoom < 11.5)  { if (eff_max > 1) eff_max = 1; }
    else if (zoom < 12.5)  { if (eff_max > 2) eff_max = 2; }
    else if (zoom < 14.0)  { if (eff_max > 3) eff_max = 3; }


    /* ---- edgelist v2：条目自带坐标/等级/标志 ----
       整条路径只顺序扫 edgelist，nodes.bin 一次都不读、edges.bin 只在
       z≥15 取路名时读（那时视口里就几百条边）。这就是 PLAN.md §21 里
       "预取和缓存都压不下去的随机访存"的唯一根治法：不访问。 */
    if (edgelist->hdr.rec_size == SK_XL2_BYTES) {
        for (int ty = tr.ty_lo; ty <= tr.ty_hi; ty++) {
            for (int tx = tr.tx_lo; tx <= tr.tx_hi; tx++) {
                uint32_t off, cnt;
                tile_dir(tiles, &tr, tx, ty, &off, &cnt);
                /* 只有"这一格有 ≥2 个省"才可能出跨省重复，才查去重表 */
                DedupSet *ddt = dd;
                if (ddt && n_peer > 0) {
                    int cov = 0;
                    for (int p = 0; p < n_peer && cov < 2; p++)
                        if (tile_cnt_at(peer[p], tx, ty)) cov++;
                    if (cov < 2) ddt = NULL;
                }
                const uint8_t *xl = edgelist->data +
                                    (size_t)off * SK_XL2_BYTES;
                for (uint32_t k = 0; k < cnt; k++, xl += SK_XL2_BYTES) {
                    road_emit(rs, P, edges, np, ddt, zoom, W, H, margin, ne,
                              ccls, eff_max, xl, 0, 1, NULL, st, 0, 0, 0, 0);
                }
            }
        }
        return;
    }

    /* ---- edgelist v1：条目只有两个下标，坐标/等级都要去 nodes.bin /
       edges.bin 随机读（PLAN.md §21 量到它占河北 z9 路网的一大半）。
       当前所有 v3 产物写的都是 v2 记录，这条路径只在手工搭的旧记录目录、
       或将来再改记录布局时的过渡期才会走到；别删掉，它同时是
       "v2 快路径画错了没有"的对照。 ---- */
    for (int ty = tr.ty_lo; ty <= tr.ty_hi; ty++) {
        for (int tx = tr.tx_lo; tx <= tr.tx_hi; tx++) {
            uint32_t off, cnt;
            tile_dir(tiles, &tr, tx, ty, &off, &cnt);
            for (uint32_t k = 0; k < cnt; k++) {
                const uint8_t *xl = edgelist->data + ((size_t)off + k) * SK_XL_BYTES;
                uint32_t from = sk_get_u32(xl + XL_NODE);
                uint32_t eidx = sk_get_u32(xl + XL_EDGE);
                if ((uint64_t)from >= nn || (uint64_t)eidx >= ne) continue;

                const uint8_t *nr = nodes->data + (uint64_t)from * SK_NODE_BYTES;
                const uint8_t *er = edges->data + (uint64_t)eidx * SK_EDGE_BYTES;
                uint8_t cls = er[EF_CLASS];
                if (cls > eff_max) continue;
                if (sk_get_u16(er + EF_FLAGS) & EF_REVERSE) continue;

                uint32_t to = sk_get_u32(er + EF_TO);
                if ((uint64_t)to >= nn) continue;
                const uint8_t *tnr = nodes->data + (uint64_t)to * SK_NODE_BYTES;

                int x0, y0, x1, y1;
                mv_proj_e7(P, sk_get_i32(nr + NF_LAT), sk_get_i32(nr + NF_LON), &x0, &y0);
                mv_proj_e7(P, sk_get_i32(tnr + NF_LAT), sk_get_i32(tnr + NF_LON), &x1, &y1);
                if ((x0 < -margin && x1 < -margin) || (x0 > W + margin && x1 > W + margin) ||
                    (y0 < -margin && y1 < -margin) || (y0 > H + margin && y1 > H + margin))
                    continue;

                if (dd) {
                    /* 身份 = 两端 e7 坐标的有序对（与 v2 路径同一套键）。
                       ★ 不能排序成 (min,max)：双向道路的回程边会因此被判成
                         重复，整条路会消失。
                       ★ 必须用和 v2 相同的键：否则"一半省 v1、一半省 v2"的
                         混合图集里两套键互不相认，跨省重复路会被画两遍。 */
                    if (dedup_seen(dd, coord_key_e7(sk_get_i32(nr + NF_LAT),
                                                    sk_get_i32(nr + NF_LON)),
                                       coord_key_e7(sk_get_i32(tnr + NF_LAT),
                                                    sk_get_i32(tnr + NF_LON)))) {
                        st->dedup_skipped++;
                        continue;
                    }
                }

                const Style *s = &kStyle[cls > 4 ? 4 : cls];
                int width = (zoom < 12.5) ? 1 : s->width;
                raster_thick_line(rs, x0, y0, x1, y1, width,
                                  ccls[cls > 4 ? 4 : cls]);
                st->drawn++;

                uint32_t nid = sk_get_u32(er + EF_NAME_ID);
                if (nid && cls <= road_name_cls_max(zoom) && zoom >= 12.0 &&
                    x0 >= 0 && x0 < W && y0 >= 0 && y0 < H)
                    road_labels_add(np, nid, (x0 + x1) / 2, (y0 + y1) / 2);
            }
        }
    }
}

/* 路阶段的桶元素：记录索引 + 全局 seq（归并用）+ 是否本带归属（计数用）。 */
typedef struct {
    uint32_t idx;     /* edgelist 记录序号 */
    uint32_t seq;     /* tile 线性序 * 65536 + 条目序 ⇒ 与串行遍历序同构 */
    uint32_t main_b;  /* 1 = 这条边的中心落在本带，审计计数记在我这里 */
} RoadItem;

static RoadItem *s_rbkt[SK_BAND_MAX];
static size_t    s_rbkt_cap[SK_BAND_MAX];
static uint32_t  s_rbkt_n[SK_BAND_MAX];

static int rbkt_push(int t, uint32_t idx, uint32_t seq, uint32_t main_b)
{
    if (s_rbkt_n[t] >= s_rbkt_cap[t]) {
        size_t nc = s_rbkt_cap[t] ? s_rbkt_cap[t] * 2 : 4096;
        RoadItem *q = (RoadItem *)realloc(s_rbkt[t], nc * sizeof(RoadItem));
        if (!q) return -1;
        s_rbkt[t] = q;
        s_rbkt_cap[t] = nc;
    }
    s_rbkt[t][s_rbkt_n[t]].idx = idx;
    s_rbkt[t][s_rbkt_n[t]].seq = seq;
    s_rbkt[t][s_rbkt_n[t]].main_b = main_b;
    s_rbkt_n[t]++;
    return 0;
}

typedef struct {
    BandView        *bv;
    const MapView   *v;
    MvProj           P;
    const NMBinFile *edges;
    const NMBinFile *edgelist;
    const NamePool  *np;
    double           zoom;
    int              W, H, margin;
    uint64_t         ne;
    uint32_t         ccls[5];
    int              eff_max;
    PaintStats       st[SK_BAND_MAX];
    RoadSink         sinks[SK_BAND_MAX];
    int              nb, cy0, cyh;   /* 锚点归属带用：带数 + 基准 clip 范围 */
} RoadsJob;

static void roads_task(void *arg, int tid)
{
    RoadsJob *j = (RoadsJob *)arg;
    Raster *rs = &j->bv->band[tid];
    PaintStats *st = &j->st[tid];
    memset(st, 0, sizeof *st);
    /* 新线程的 __thread g_last_px 初值是 -1，但线程复用时可能是上一帧的值；
       这里显式清一次，语义等价于串行时的 paint_roadpix_begin。 */
    g_last_px = -1;
    g_last_tag = -1;

    uint32_t n = s_rbkt_n[tid];
    for (uint32_t k = 0; k < n; k++) {
        const RoadItem *it = &s_rbkt[tid][k];
        const uint8_t *xl = j->edgelist->data + (size_t)it->idx * SK_XL2_BYTES;
        road_emit(rs, &j->P, j->edges, j->np, NULL, j->zoom, j->W, j->H,
                  j->margin, j->ne, j->ccls, j->eff_max, xl, it->seq,
                  it->main_b, &j->sinks[tid], st, tid, j->nb, j->cy0, j->cyh);
    }
}

/* 返回 0 = 已并行画完；-1 = 不适用（v1 记录 / 旋转视图 / OOM），调用方落回串行。
 * 只在 dd == NULL（单省）时调用 —— 去重表是共享哈希，多线程会写坏。 */
static int paint_roads_banded(Raster *rs, const MapView *v,
                              const NMBinFile *edges,
                              const NMBinFile *tiles, const NMBinFile *edgelist,
                              const NamePool *np, int max_class,
                              PaintStats *st, int nband)
{
    if (!edgelist || edgelist->hdr.rec_size != SK_XL2_BYTES) return -1;

    TileRange tr;
    if (!tile_range(tiles, v, &tr)) return 0;

    double zoom = v->zoom;
    int W = v->w, H = v->h;
    const int margin = 64;
    uint64_t ne = edges->hdr.rec_count;

    MvProj P;
    mv_proj_begin(v, &P);
    /* 旋转视图下"屏幕 y 与纬度单调"不成立，按纬度分桶会漏要素。 */
    if (P.rot != 0.0) return -1;

    int eff_max = max_class;
    if (zoom < 9.0)        { if (eff_max > 0) eff_max = 0; }
    else if (zoom < 11.5)  { if (eff_max > 1) eff_max = 1; }
    else if (zoom < 12.5)  { if (eff_max > 2) eff_max = 2; }
    else if (zoom < 14.0)  { if (eff_max > 3) eff_max = 3; }

    BandView bv;
    band_init(&bv, rs, nband);
    for (int t = 0; t < bv.n; t++)
        s_rbkt_n[t] = 0;   /* s_rbkt/s_rbkt_cap 的容量跨帧保留，只清计数 */

    /* 每带的纬度范围（含 SK_BAND_PAD 行余量）。用 unproject 从像素行反推
       ⇒ 分桶零投影。若逐边投影来判带，z9 的 27 万条 × N 带会把并行收益
       全部吃掉（那正是串行路径的成本）。
       ★ 余量只能给 SK_BAND_PAD（±2 行描边 + 1 行量化），**不能**把视口裁剪
         的 margin(64) 也算进来：带高只有 100~800 行，±67 行的范围让带两两
         重叠 134 行 ⇒ z9 平均每条边被 2.3 条带各画一遍（实测分桶 63.8 万次
         push / 27.4 万条边），画得多不算，还全落在空转上 —— 路 19ms 反而
         变 23ms。带归属只关心"这条边的描边会不会碰到本带"，与视口无关。 */
    const int bpad = SK_BAND_PAD;
    int32_t blat_lo[SK_BAND_MAX], blat_hi[SK_BAND_MAX];
    for (int t = 0; t < bv.n; t++) {
        double la, lo;
        mv_unproject(v, W / 2, bv.edge[t] - bpad, &la, &lo);
        double a = la;
        mv_unproject(v, W / 2, bv.edge[t + 1] + bpad, &la, &lo);
        double b = la;
        if (a > b) { double tmp = a; a = b; b = tmp; }
        blat_lo[t] = (int32_t)(a * 1e7);
        blat_hi[t] = (int32_t)(b * 1e7);
    }

    int ntx = tr.tx_hi - tr.tx_lo + 1;
    for (int ty = tr.ty_lo; ty <= tr.ty_hi; ty++) {
        for (int tx = tr.tx_lo; tx <= tr.tx_hi; tx++) {
            uint32_t off, cnt;
            tile_dir(tiles, &tr, tx, ty, &off, &cnt);
            uint32_t tlin = (uint32_t)((ty - tr.ty_lo) * ntx + (tx - tr.tx_lo));
            const uint8_t *xl = edgelist->data + (size_t)off * SK_XL2_BYTES;
            for (uint32_t k = 0; k < cnt; k++, xl += SK_XL2_BYTES) {
                uint8_t cls = xl[XL2_CLS];
                if (cls > eff_max) continue;
                if (sk_get_u16(xl + XL2_FLAGS) & EF_REVERSE) continue;

                int32_t fla = sk_get_i32(xl + XL2_FROM_LAT);
                int32_t tla = sk_get_i32(xl + XL2_TO_LAT);
                int32_t elo = fla < tla ? fla : tla;
                int32_t ehi = fla < tla ? tla : fla;

                uint32_t idx = off + k;
                uint32_t seq = tlin * 65536u + k;
                /* 归属带 = 第一条相交带（t 最小），不是"纬度中心最近"那条：
                   后者可能根本没被分进（它的纬度范围与这条边的端点区间不相交）
                   ⇒ 这条边在别的带里画了，却谁也没计数。取第一条相交带，
                   计数必然落在真实画过它的带上，且一条边只记一次。 */
                int main_b = 1;
                for (int t = 0; t < bv.n; t++) {
                    if (ehi < blat_lo[t] || elo > blat_hi[t]) continue;
                    if (rbkt_push(t, idx, seq, (uint32_t)main_b) != 0)
                        return -1;
                    main_b = 0;
                }
            }
        }
    }

    RoadsJob job;
    memset(&job, 0, sizeof job);
    job.bv = &bv;
    job.v = v;
    job.P = P;
    job.edges = edges;
    job.edgelist = edgelist;
    job.np = np;
    job.zoom = zoom;
    job.W = W; job.H = H; job.margin = margin;
    job.ne = ne;
    for (int k = 0; k < 5; k++)
        job.ccls[k] = raster_rgb(rs, kStyle[k].r, kStyle[k].g, kStyle[k].b);
    job.eff_max = eff_max;
    job.nb = bv.n;
    job.cy0 = bv.base.clip.y;
    job.cyh = bv.base.clip.h;
    band_run(&bv, roads_task, &job);

    /* 路名锚点：按 seq 归并 ⇒ 顺序与串行逐项相同（避让先到先得，顺序一变
       显示结果就变）。 */
    int total = 0;
    for (int t = 0; t < bv.n; t++) total += job.sinks[t].n;
    if (total > 0) {
        SeqLabel *all = (SeqLabel *)malloc((size_t)total * sizeof(SeqLabel));
        if (all) {
            int pos = 0;
            for (int t = 0; t < bv.n; t++) {
                int n = job.sinks[t].n;
                if (n > 0) {
                    memcpy(all + pos, job.sinks[t].a, (size_t)n * sizeof(SeqLabel));
                    pos += n;
                }
            }
            qsort(all, (size_t)total, sizeof(SeqLabel), seqlabel_cmp);
            for (int i = 0; i < total; i++)
                road_labels_add(all[i].np, all[i].name_id, all[i].x, all[i].y);
            free(all);
        }
    }
    for (int t = 0; t < bv.n; t++) free(job.sinks[t].a);

    for (int t = 0; t < bv.n; t++) {
        st->drawn         += job.st[t].drawn;
        st->dedup_skipped += job.st[t].dedup_skipped;
    }
    return 0;
}

void paint_roads(Raster *rs, const MapView *v,
                 const NMBinFile *nodes, const NMBinFile *edges,
                 const NMBinFile *tiles, const NMBinFile *edgelist,
                 const NamePool *np, DedupSet *dd,
                 int max_class, PaintStats *st,
                 const NMBinFile *const *peer_tiles, int n_peer)
{
    /* ★ 不要在这里 memset 和 reset：多省时 paint_roads 每省调用一次，
       一清就把前一个省的统计/标签收集抹掉。统一由 paint_frame 开头做。 */
    st->max_class = max_class;
    if (!rs) return;

    uint64_t nn = nodes->hdr.rec_count;
    uint64_t ne = edges->hdr.rec_count;
    if (nn == 0 || ne == 0) return;

    if (tiles && edgelist) {
        /* 分带并行：去重表 dd 是跨省共享的哈希，多线程会写坏 ⇒ 只有单省
           （dd == NULL）才并行；多省仍走串行 tiled 路径。失败落回串行。 */
        int nband = band_pick_count();
        if (nband > 1 && dd == NULL && ne >= SK_BAND_MIN_ROADS) {
            if (paint_roads_banded(rs, v, edges, tiles, edgelist, np,
                                   max_class, st, nband) == 0)
                return;
        }
        paint_roads_tiled(rs, v, nodes, edges, tiles, edgelist, np, dd,
                          max_class, st, peer_tiles, n_peer);
        return;
    }

    const uint8_t *nd = nodes->data;
    const uint8_t *ed = edges->data;

    int W = v->w, H = v->h;
    const int margin = 64;   /* 允许端点在视口外一点，避免边缘的路突然断掉 */

    /* 投影快照：本 pass 内视口不变，循环里用 mv_proj() 免去每次的缓存键比对 */
    MvProj Pstore, *P = &Pstore;
    mv_proj_begin(v, P);

    /* 先把所有节点投影一遍太贵（145 万次三角/对数），这里改成“边命中再投影”：
       先按经纬度做粗筛（视口 bbox，线性近似足够），命中后再精算像素。 */
    double bb[4];
    mv_visible_bbox(v, bb);
    double pad_lat = (bb[2] - bb[0]) * 0.1 + 1e-6;
    double pad_lon = (bb[3] - bb[1]) * 0.1 + 1e-6;
    double la0 = bb[0] - pad_lat, la1 = bb[2] + pad_lat;
    double lo0 = bb[1] - pad_lon, lo1 = bb[3] + pad_lon;

    for (uint64_t i = 0; i < nn; i++) {
        const uint8_t *nr = nd + i * SK_NODE_BYTES;
        int32_t la = sk_get_i32(nr + NF_LAT);
        int32_t lo = sk_get_i32(nr + NF_LON);

        double dla = (double)la / 1e7, dlo = (double)lo / 1e7;
        int in_a = (dla >= la0 && dla <= la1 && dlo >= lo0 && dlo <= lo1);

        uint32_t b = sk_get_u32(nr + NF_EDGE_BEGIN);
        uint16_t c = sk_get_u16(nr + NF_EDGE_COUNT);
        for (uint32_t k = 0; k < c; k++) {
            uint64_t e = (uint64_t)b + k;
            if (e >= ne) break;
            st->visited++;
            const uint8_t *er = ed + e * SK_EDGE_BYTES;
            uint8_t cls = er[EF_CLASS];
            if (cls > max_class) continue;
            /* EF_REVERSE = 双向路的回程边，几何与去程重复，画一次就够 */
            uint16_t fl = sk_get_u16(er + EF_FLAGS);
            if (fl & EF_REVERSE) continue;      /* 双向路的回程边，几何重复 */

            uint32_t to = sk_get_u32(er + EF_TO);
            if (to >= nn) continue;
            const uint8_t *tr = nd + (uint64_t)to * SK_NODE_BYTES;
            int32_t la2 = sk_get_i32(tr + NF_LAT);
            int32_t lo2 = sk_get_i32(tr + NF_LON);

            double dla2 = (double)la2 / 1e7, dlo2 = (double)lo2 / 1e7;
            int in_b = (dla2 >= la0 && dla2 <= la1 && dlo2 >= lo0 && dlo2 <= lo1);
            if (!in_a && !in_b) continue;

            int x0, y0, x1, y1;
            mv_proj_e7(P, la, lo, &x0, &y0);
            mv_proj_e7(P, la2, lo2, &x1, &y1);
            if ((x0 < -margin && x1 < -margin) || (x0 > W + margin && x1 > W + margin) ||
                (y0 < -margin && y1 < -margin) || (y0 > H + margin && y1 > H + margin))
                continue;

            if (dd) {
                /* 同上：有序对、不排序；键与 tiled 两条路径一致（e7 坐标） */
                if (dedup_seen(dd, coord_key_e7(la, lo), coord_key_e7(la2, lo2))) {
                    st->dedup_skipped++;
                    continue;
                }
            }

            const Style *s = &kStyle[cls > 4 ? 4 : cls];
            /* 缩小到全省/全市时，粗线把画面糊成色块且每条要画 5 次；
               直接退化为 1 像素，帧时间降一个数量级 */
            int width = (v->zoom < 12.5) ? 1 : s->width;
            raster_thick_line(rs, x0, y0, x1, y1, width,
                              raster_rgb(rs, s->r, s->g, s->b));
            st->drawn++;

            /* 顺带收集路名锚点：等级随 zoom 放开（road_name_cls_max）、有名、屏幕内（z12 起） */
            uint32_t nid = sk_get_u32(er + EF_NAME_ID);
            if (nid && cls <= road_name_cls_max(v->zoom) && v->zoom >= 12.0 &&
                x0 >= 0 && x0 < W && y0 >= 0 && y0 < H)
                road_labels_add(np, nid, (x0 + x1) / 2, (y0 + y1) / 2);
        }
    }
}

void paint_marker(Raster *rs, int x, int y, int radius,
                  uint8_t rr, uint8_t gg, uint8_t bb)
{
    int rx = x - radius, ry = y - radius, rw = radius * 2 + 1, rh = radius * 2 + 1;
    raster_fill_rect(rs, rx, ry, rw, rh, rr, gg, bb, 255);
    raster_rect(rs, rx, ry, rw, rh, 255, 255, 255, 255);
}

/* ------------------------------------------------------------------ */
/* 面要素：建筑 / 水面 / 绿地                                           */
/* ------------------------------------------------------------------ */

typedef struct { uint8_t fr, fg, fb, br, bg, bb; } AreaStyle;

static const AreaStyle kAreaStyle[AK_OTHER + 1] = {
    { 0, 0, 0, 0, 0, 0 },
    { 219, 213, 201, 178, 171, 158 },   /* 建筑：米灰 + 深描边（背景 244,241,234，要有对比） */
    { 186, 216, 238, 158, 196, 226 },   /* 水面 */
    { 208, 232, 202, 178, 214, 170 },   /* 绿地 */
    { 232, 229, 218, 210, 206, 196 },   /* 其他区域 */
};

/* ------------------------------------------------------------------ */
/* 描边：连续折线攒起来一次 DrawLines（第六刀）                        */
/*                                                                     */
/* 原来每个多边形/每条线的边框是"一条边一次 SDL_RenderDrawLine"。        */
/* 实测 z9 北京一帧面要素 89 ms 里**描边占 50 ms**（比填充还多一倍），   */
/* 而且里面大量边在低 zoom 下首尾投影到同一个像素（零长边）。           */
/*                                                                     */
/* 两处改动，都不改变最终像素（同一-cmd 对比 md5 完全相同）：           */
/*   ① 合并连续重复像素点：零长边本来也不产生可见像素                   */
/*   ② 整环一次 SDL_RenderDrawLines：一次绘制命令，而不是 n 次          */
/* 实测：z9 面 91→57 ms、z13 面 60→43 ms。这是一笔纯赚。               */
/* ------------------------------------------------------------------ */

/* 现在逐段直写像素，不再需要攒点缓冲（原来攒点是为了凑一次 DrawLines）。
   ★ 逐段画与 SDL_RenderDrawLines 像素等价：折线的中间接点在本段被当作
     终点、在下一段被当作起点，SDL 只画一次、我们画两次，落在同一个像素、
     同一个颜色上；最后一点 SDL 由末尾那次 DrawPoint 补上，而我们的每段
     都画满两端，所以恒等。（用 1500 条随机折线逐像素验证过 0 差异。） */
static __thread int g_sp_x, g_sp_y, g_sp_have;

static void stroke_begin(void) { g_sp_have = 0; }

static void stroke_push(Raster *rs, int x, int y, uint32_t c)
{
    if (!g_sp_have) { g_sp_x = x; g_sp_y = y; g_sp_have = 1; return; }
    if (g_sp_x == x && g_sp_y == y) return;       /* 零长边：跳过 */
    raster_line(rs, g_sp_x, g_sp_y, x, y, c);
    g_sp_x = x; g_sp_y = y;
}

static void stroke_flush(void) { g_sp_have = 0; }

/* 闭合环：末尾补回首点。 */
static void stroke_ring(Raster *rs, const int *px, const int *py, int n, uint32_t c)
{
    stroke_begin();
    for (int k = 0; k < n; k++) stroke_push(rs, px[k], py[k], c);
    stroke_push(rs, px[0], py[0], c);
    stroke_flush();
}

/* 多边形填充：扫描线 even-odd。
 * 为什么不用 SDL_RenderGeometry：本机 SDL 2.30 的软件渲染器没有实现
 * geometry 命令，调用被静默丢弃（颜色直方图里根本没有填充色才抓到的）。
 * 自绘扫描线顺带支持自交多边形与内环，比三角化更稳。 */

static void fill_polygon(Raster *rs, const int *px, const int *py, int n,
                         uint32_t color)
{
    int ymin = py[0], ymax = py[0];
    for (int i = 1; i < n; i++) {
        if (py[i] < ymin) ymin = py[i];
        if (py[i] > ymax) ymax = py[i];
    }
    if (ymin < 0) ymin = 0;
    if (ymax > 8191) ymax = 8191;   /* 防御：超大面裁掉 */
    if (ymin > ymax) return;

    /* ---- 活动边表（AET）----
     * 原写法是"每一行都扫一遍全部 n 条边"：z9 一帧 267K 行 × 平均几十条边，
     * 光这一项就 25 ms（判别实验量出来的）。改成按 ylo 排序后逐行增量维护
     * 活动集合，每行只算真正与这一行相交的那几条。
     * ★ 交点公式、半开区间 [ylo,yhi)、插入排序、跨度画法一个字都没改，
     *   所以画出来的像素必须完全相同 —— 这不是"画面换速度"的取舍，是纯赚。
     *   （同一轮里试过的"按像素抽稀顶点"会削掉小面的凸角，实测 z9 丢 10% 的
     *   绿地面积，转移方向是"要素色 → 背景色"，已否决。） */
    struct Edge { int ylo, yhi, y1, y2, x1, x2; } e[256];
    int ne = 0;
    for (int i = 0; i < n && ne < 256; i++) {
        int j = (i + 1) % n;
        int y1 = py[i], y2 = py[j];
        if (y1 == y2) continue;                    /* 与原实现一致：水平边不算相交 */
        e[ne].y1 = y1; e[ne].y2 = y2; e[ne].x1 = px[i]; e[ne].x2 = px[j];
        e[ne].ylo = y1 < y2 ? y1 : y2;
        e[ne].yhi = y1 < y2 ? y2 : y1;
        ne++;
    }
    for (int a = 1; a < ne; a++) {                 /* 按 ylo 插入排序（边数很少） */
        struct Edge t = e[a];
        int b = a - 1;
        while (b >= 0 && e[b].ylo > t.ylo) { e[b + 1] = e[b]; b--; }
        e[b + 1] = t;
    }

    static __thread int xs[128], act[256];
    int na = 0, next = 0;
    for (int y = ymin; y <= ymax; y++) {
        double yc = y + 0.5;
        while (next < ne && e[next].ylo <= yc) act[na++] = next++;
        int w = 0;
        for (int q = 0; q < na; q++)                /* 淘汰已经走完的边 */
            if (e[act[q]].yhi > yc) act[w++] = act[q];
        na = w;
        int nx = 0;
        for (int q = 0; q < na && nx < 126; q++) {
            int id = act[q];
            double t = (yc - e[id].y1) / (double)(e[id].y2 - e[id].y1);
            xs[nx++] = (int)(e[id].x1 + t * (e[id].x2 - e[id].x1) + 0.5);
        }
        /* 插入排序（每行交点很少） */
        for (int a = 1; a < nx; a++) {
            int v = xs[a], b = a - 1;
            while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; }
            xs[b + 1] = v;
        }
        for (int a = 0; a + 1 < nx; a += 2) {
            int x0 = xs[a], x1 = xs[a + 1];
            if (x1 > x0) raster_hspan(rs, y, x0, x1, color);
        }
    }
}

/* 取一个环的顶点 → 投影。返回投影后的点数（<0 = 索引越界）。
 */
#define RING_CAP 8192

/* ★★ 「批量投影优化」试过了，**收益不足以做**（2026-10-29 实测归因，留档）
 *
 * 设想：把 mv_proj_e7 改成真正的批量/向量化投影（SIMD、或把除法换成乘法、
 * 或把查表内联进快照），把 z9 的面 55ms 压下去。
 * ⇒ **没做**：实测上限只有 6%，且唯一能拿到的那部分会破坏像素等价。
 *
 * 实测方法（三种独立手段互相印证，不是反推）：
 *   ① 标定法（§13「额外多做 N 次」）：每帧多投影 125 万次 ⇒ +7 ms，
 *      单次 5.6 ns ⇒ 真实投影整项 = 125 万 × 5.6 ns = **7 ms**。
 *   ② 替代法：把投影换成"仍在视口内的线性近似"（x 精确 + y 用视口中心处
 *      的 dy/dlat 线性系数）⇒ z9 省 **7 ms**、z11 2 ms、z13 1 ms。与 ① 吻合。
 *      ⚠ 第一版这个替代版漏了"锚在视口中心"，导致整幅图被平移到视口外
 *        （edges_drawn 从 588302 掉到 218，99.96% 的路被裁掉），
 *        测出来的"更快"全是假的 —— 替代版必须保持下游行为不变（§9-1）。
 *   ③ 位开关判别实验：面不填 −11 ms / 面不描 −8 ms / 路不画 −2 ms / 线不画 −2 ms；
 *      全部不落笔仍要 84 ms。
 *
 * 完整归因（z9，第2帧 110 ms）：
 *   落笔（填充 11 + 描边 8 + 路 2 + 线 2）  23 ms
 *   顶点投影（125 万次 × 5.6 ns）           7 ms   ← 批量化的天花板
 *   顶点取数（project_ring 整项 21 − 投影 7）14 ms
 *     └ 其中"消除随机访存"能省的部分        8 ms   ← 影子表预演实测（像素 0 差异）
 *   剩余（遍历 67.7 万条面记录 + 预筛 + 分支）≈ 66 ms
 *
 * 结论与为什么不划算：
 *   - "批量/向量化投影"上限 **7 ms / 110 ms = 6%**。
 *   - §19 实测：把除法换成乘法（唯一能拿到 ~9 ms 的手段）会改浮点结合顺序，
 *     **整图 diff 抓到 1 个像素**（lround 只在 .5 边界暴露 ULP 差异，
 *     抽样很容易"全绿"）⇒ 不可用。
 *   - §20 实测：纯等价改法（把查表内联 + 表指针随快照传）只值 **1 ms**，
 *     不值得多养一套"内联版 vs 外部版"的双实现（两套迟早漂移）。
 *     ⇒ 而且本项目**已经拿到这 1 ms**：LTO 下 sk_mercator_y_fast 已被完全内联
 *       （exe 里 nm 里它没有任何调用点）。
 *   - "坐标内联进 polyrefs"（消除 nodes.bin 随机读）实测只省 **8 ms / 7%**，
 *     却要改落盘格式 + 全量重建 8 省 + polyrefs 4B→8B ⇒ 代价远大于收益。
 *
 * ⇒ 真正的大头是"剩余约 66 ms"，但**它不是"遍历 67.7 万条记录 + 预筛"**——
 *   那是差值推出来的，不准确。用位开关实测（面循环只遍历不处理）时，
 *   **面阶段只剩 1 ms** ⇒ 遍历 + bbox 预筛几乎不花钱（那 67.7 万条里
 *   视口内只有 11.4 万，命中率 17%，但预筛是顺序读，很便宜）。
 *   那 66 ms 的真实构成没被完全拆开，但已知它落在"处理"路径里
 *   （落笔 19 + 投影 4 + 取数 14 = 37 ms，另有约 17~29 ms 未归因，
 *    疑在小面快速路径的 7.6 万次调用与 project_ring 的 3.8 万次调用开销上）。
 *
 * ⇒ 要压这一块只能**减少要处理的要素数**（LOD / 抽稀），而那会改画面
 *   （§10/§11 实测转移方向是"要素色 → 背景色"＝丢要素）⇒ 属产品取舍，
 *   应出档位表交决策者拍板，不要自己悄悄改默认值。
 *
 * ★★ 2026-10-29 已出的两张档位表：
 *   ① "投影后均匀抽稀到 N 个顶点"（本段下方实验）—— **实测更差，已撤**：
 *      提速只有 5 ms（z9 109→104、z11 50→45、z13 33→33），而**净损失面积
 *      始终为正**（z9 NMAX=8 净损 14048 px、NMAX=16 净损 8215 px），
 *      且加不加"只对小面生效"的尺寸门限都一样为正 ⇒ 连 PLAN.md §24 当年
 *      "差异方向全部是填充色↔描边色互换、没有要素色→背景色"的结论在**当前
 *      代码 + 当前数据**上已不成立（当年是 SDL 时代、单省口径、且判据只看
 *      Top 转移没算净损失方向）。视觉确认：绿地肉眼可见变稀。
 *   ② "小面画点阈值 AREA_DOT_PX 调大"（**本项目已有的旋钮**）—— 见
 *      paint.h 里 AREA_DOT_PX 的注释：阈值 2→4 是 z9 −21% / 净损失 3.5%，
 *      2→8 是 z9 −33% / 净损失 8.4%；阈值 3 是"几乎看不出 + 省 15%"的一档。
 *      ⇒ **这才是有效杠杆**（对比抽稀方案的 5 ms，它是 20 ms+），
 *        但同样是画质取舍 ⇒ 默认值保持 2 不动，等决策者定档。
 */
static int project_ring(const MvProj *P, const NMBinFile *nodes,
                        const NMBinFile *polyrefs, uint32_t off, uint32_t cnt,
                        uint64_t nn, int *px, int *py, int cap)
{
    static __thread int32_t  rla[RING_CAP], rlo[RING_CAP];
    if (cnt > RING_CAP) cnt = RING_CAP;
    uint32_t m = 0;
    for (uint32_t k = 0; k < cnt; k++) {
        uint32_t idx = sk_get_u32(polyrefs->data + ((uint64_t)off + k) * 4);
        if ((uint64_t)idx >= nn) return -1;
        const uint8_t *nr = nodes->data + (uint64_t)idx * SK_NODE_BYTES;
        rla[m] = sk_get_i32(nr + NF_LAT);
        rlo[m] = sk_get_i32(nr + NF_LON);
        m++;
    }
    int n = 0;
    for (uint32_t k = 0; k < m && n < cap; k++) {
        mv_proj_e7(P, rla[k], rlo[k], &px[n], &py[n]);
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* 面阶段：分带并行                                                      */
/* ------------------------------------------------------------------ */

/* 面绘制上下文。把一次 pass 里所有不变的东西打包，串行与并行共用同一份
   area_emit —— 两条路径必须是同一份代码。历史教训：一旦复制一份"并行版"，
   几何筛选差一个条件就是"并行开着少画两百个面"，而这种漂移逐字节对拍会
   立刻抓到，但排查要花掉一整天。 */
typedef struct {
    const MvProj    *P;
    const NMBinFile *nodes;
    const NMBinFile *areas;
    const NMBinFile *polyrefs;
    uint64_t         nn;
    int32_t          vla0, vla1, vlo0, vlo1;   /* 视口经纬（e7），筛 bbox 用 */
    uint32_t         afill[AK_OTHER + 1];
    uint32_t         astroke[AK_OTHER + 1];
} AreaCtx;

/* 画一个面。种类 / LOD / 视口筛选全在函数内做（分桶预扫也调一遍同样的判据，
   重复判一次是幂等的，代价远小于维护两份筛选逻辑）。 */
static void area_emit(Raster *rs, const AreaCtx *c, uint64_t i,
                      const int32_t *bb, double zoom, int count, PaintStats *st)
{
    /* 跨带的面会在每条相交带各画一次（各自 clip 一半），但审计计数只能记一次，
       否则 areas_drawn 会随着带数上涨 —— regress 的确定性判据会直接假红。 */
    static __thread PaintStats s_dummy;
    PaintStats *dst = count ? st : &s_dummy;
    const uint8_t *ar = c->areas->data + (size_t)i * SK_AREA_BYTES;
    uint8_t kind = ar[AF_KIND];
    if (kind == AK_NONE || kind > AK_OTHER) return;
    /* LOD：低 zoom 只画大片水面/绿地，建筑到中高 zoom 才出现 */
    if (kind == AK_BUILDING && zoom < 13.0) return;
    if (zoom < 8.0) return;
    if (bb && (bb[0] > c->vla1 || bb[2] < c->vla0 ||
               bb[1] > c->vlo1 || bb[3] < c->vlo0))
        return;

    uint32_t off = sk_get_u32(ar + AF_REF_OFF);
    uint32_t cnt = sk_get_u32(ar + AF_REF_CNT);
    if (cnt < 4) return;

    /* ---- 小面快速路径（低 zoom 的面开销主要就在这里）----
     * 实测面的屏幕尺寸分布（北京城内 1280×800）：
     *   z9 : 61618 个面里 42952 个（70%）最大边 < 2 px
     *   z11: 36237 个面里 10106 个（28%）
     * 这些面填充出来最多一个像素，却要走"投影全部 cnt 个顶点 → 扫描线
     * 逐行对 n 条边求交 → 再描一遍 n 条边"这一整套，成本和大面同量级。
     * 用 geo bbox 的两个角点投影估出屏幕尺寸（2 次投影，不必投影全部顶点），
     * 够小就只点一个像素：颜色贡献保留，开销从 O(cnt×行) 降到 O(1)。
     * 估的是外接框上界，只会挑出确实很小的面，不会误伤大面。 */
    if (bb) {
        int xa, ya, xb, yb;
        mv_proj_e7(c->P, bb[0], bb[1], &xa, &ya);
        mv_proj_e7(c->P, bb[2], bb[3], &xb, &yb);
        int aw = xa > xb ? xa - xb : xb - xa;
        int ah = ya > yb ? ya - yb : yb - ya;
        if ((aw > ah ? aw : ah) < g_area_dot_px) {
            raster_point(rs, (xa + xb) / 2, (ya + yb) / 2, c->afill[kind]);
            dst->areas_drawn++;
            dst->areas_dotted++;
            return;
        }
    }

    /* 复杂面（顶点很多，如公园/湖泊边界）：填充上限 256 顶点，超出退化为描边 */
    if (cnt > 256) {
        static __thread int bx[RING_CAP], by[RING_CAP];
        int bn = project_ring(c->P, c->nodes, c->polyrefs, off, cnt, c->nn,
                              bx, by, RING_CAP);
        stroke_begin();
        for (int k = 0; k < bn; k++) stroke_push(rs, bx[k], by[k], c->astroke[kind]);
        stroke_flush();
        dst->areas_drawn++;
        return;
    }

    int px[256], py[256];
    int n = project_ring(c->P, c->nodes, c->polyrefs, off, cnt, c->nn, px, py, 256);
    if (n < 3) return;
    /* 闭合 way 首尾同点，去掉重复尾点 */
    if (n > 3 && px[0] == px[n - 1] && py[0] == py[n - 1]) n--;

    fill_polygon(rs, px, py, n, c->afill[kind]);
    stroke_ring(rs, px, py, n, c->astroke[kind]);   /* 闭合：末尾自动补回首点 */
    dst->areas_drawn++;
}

/* 分桶缓冲：按带收集面索引。分桶是单线程做的（在主线程里预扫），
   所以这几块静态缓冲不需要加锁；容量跨帧保留，避免每帧 realloc。 */
static uint32_t *s_bkt[SK_BAND_MAX];
static size_t    s_bkt_cap[SK_BAND_MAX];
static uint32_t  s_bkt_n[SK_BAND_MAX];

static int bkt_push(int t, uint32_t idx)
{
    if (s_bkt_n[t] >= s_bkt_cap[t]) {
        size_t nc = s_bkt_cap[t] ? s_bkt_cap[t] * 2 : 4096;
        uint32_t *p = (uint32_t *)realloc(s_bkt[t], nc * sizeof(uint32_t));
        if (!p) return -1;
        s_bkt[t] = p;
        s_bkt_cap[t] = nc;
    }
    s_bkt[t][s_bkt_n[t]++] = idx;
    return 0;
}

/* y 落在哪条带。y 可能为负（视口外）或超屏 ⇒ 夹到首/末带。 */
static int band_of_y(const BandView *bv, int y)
{
    for (int t = 0; t < bv->n; t++)
        if (y < bv->edge[t + 1]) return t;
    return bv->n - 1;
}

typedef struct {
    BandView      *bv;
    const AreaCtx *ctx;
    const int32_t *bball;
    double         zoom;
    PaintStats     st[SK_BAND_MAX];   /* 每带独立计数，结束后汇总 */
} AreasJob;

static void areas_task(void *arg, int tid)
{
    AreasJob *j = (AreasJob *)arg;
    Raster *rs = &j->bv->band[tid];
    PaintStats *st = &j->st[tid];
    memset(st, 0, sizeof *st);
    uint32_t n = s_bkt_n[tid];
    const uint32_t *b = s_bkt[tid];
    for (uint32_t k = 0; k < n; k++) {
        uint32_t v = b[k];
        uint32_t i = v & 0x7fffffffu;
        int is_main = (v & 0x80000000u) != 0;   /* 归属带才计数，见 area_emit */
        area_emit(rs, j->ctx, i, j->bball + (size_t)i * 4, j->zoom, is_main, st);
    }
}

/* 返回 0 = 已并行画完；-1 = 中途失败（OOM 等），调用方必须落回串行。 */
static int paint_areas_banded(Raster *rs, const AreaCtx *ctx, int32_t *bball,
                              uint64_t na, double zoom, int nband, PaintStats *st)
{
    /* 与 paint_roads_banded 同一条纪律：旋转视图下「bbox 两角点的屏幕 y」
       不再包住整个面（另外两角可能更靠上/下），按 y 分带会漏画。
       ⇒ 直接落回串行（本就按投影后的坐标逐要素裁剪，旋转安全）。
       rot==0 时这一支恒不进，输出与改造前逐字节一致。 */
    if (ctx->P->rot != 0.0) return -1;
    BandView bv;
    band_init(&bv, rs, nband);
    for (int t = 0; t < bv.n; t++) s_bkt_n[t] = 0;

    for (uint64_t i = 0; i < na; i++) {
        const uint8_t *ar = ctx->areas->data + (size_t)i * SK_AREA_BYTES;
        uint8_t kind = ar[AF_KIND];
        if (kind == AK_NONE || kind > AK_OTHER) continue;
        if (kind == AK_BUILDING && zoom < 13.0) continue;
        if (zoom < 8.0) continue;
        const int32_t *bb = bball + (size_t)i * 4;
        if (bb[0] > ctx->vla1 || bb[2] < ctx->vla0 ||
            bb[1] > ctx->vlo1 || bb[3] < ctx->vlo0)
            continue;
        uint32_t cnt = sk_get_u32(ar + AF_REF_CNT);
        if (cnt < 4) continue;

        /* 用 bbox 两角点的屏幕 y 估出跨带区间。跨带的面要在每条相交带各画
           一次，各自靠 clip 取自己那一半 —— 只按中心行归带会在带边界缺一条。 */
        int xa, ya, xb, yb;
        mv_proj_e7(ctx->P, bb[0], bb[1], &xa, &ya);
        mv_proj_e7(ctx->P, bb[2], bb[3], &xb, &yb);
        /* 外扩 2px：投影是单点浮点运算，bbox 角点算出的 y 与实际顶点可能
           差 1px；少算一条带就是"带边界缺一条线"，多算只是白画几个像素。 */
        int ylo = (ya < yb ? ya : yb) - 2;
        int yhi = (ya < yb ? yb : ya) + 2;
        int t0 = band_of_y(&bv, ylo), t1 = band_of_y(&bv, yhi);
        int main_t = band_of_y(&bv, (ylo + yhi) / 2);
        for (int t = t0; t <= t1; t++) {
            uint32_t v = (uint32_t)i | (t == main_t ? 0x80000000u : 0u);
            if (bkt_push(t, v) != 0) return -1;
        }
    }

    AreasJob job;
    memset(&job, 0, sizeof job);
    job.bv = &bv;
    job.ctx = ctx;
    job.bball = bball;
    job.zoom = zoom;
    band_run(&bv, areas_task, &job);

    for (int t = 0; t < bv.n; t++) {
        st->areas_drawn  += job.st[t].areas_drawn;
        st->areas_dotted += job.st[t].areas_dotted;
    }
    return 0;
}

int paint_areas(Raster *rs, const MapView *v,
                const NMBinFile *nodes, const NMBinFile *areas,
                const NMBinFile *polyrefs,
                 int32_t **bbox_cache, PaintStats *st)
{
    uint64_t na = areas->hdr.rec_count;
    uint64_t np = polyrefs->hdr.rec_count;
    uint64_t nn = nodes->hdr.rec_count;
    if (!na || !np || !nn) return 0;
    if (!rs) return 0;

    /* 颜色在这里一次性打包好（5 种面类型 × 填充/描边），别放到每个面的循环里：
       原来每面两次 SDL_SetRenderDrawColor，z9 一帧是 20 多万次纯状态切换。 */
    uint32_t afill[AK_OTHER + 1], astroke[AK_OTHER + 1];
    for (int k = 0; k <= AK_OTHER; k++) {
        afill[k]   = raster_rgb(rs, kAreaStyle[k].fr, kAreaStyle[k].fg, kAreaStyle[k].fb);
        astroke[k] = raster_rgb(rs, kAreaStyle[k].br, kAreaStyle[k].bg, kAreaStyle[k].bb);
    }

    /* 懒初始化每面 bbox（进程内一次），避免每帧扫 20 万个面的全部顶点 */
    if (bbox_cache && !*bbox_cache) {
        int32_t *bb = calloc((size_t)na * 4, sizeof(int32_t));
        if (!bb) return -1;
        for (uint64_t i = 0; i < na; i++) {
            const uint8_t *ar = areas->data + i * SK_AREA_BYTES;
            uint32_t off = sk_get_u32(ar + AF_REF_OFF);
            uint32_t cnt = sk_get_u32(ar + AF_REF_CNT);
            int32_t la0 = 90 * 10000000, la1 = -90 * 10000000;
            int32_t lo0 = 180 * 10000000, lo1 = -180 * 10000000;
            for (uint32_t k = 0; k < cnt; k++) {
                uint32_t idx = sk_get_u32(polyrefs->data + ((uint64_t)off + k) * 4);
                if ((uint64_t)idx >= nn) continue;
                const uint8_t *nr = nodes->data + (uint64_t)idx * SK_NODE_BYTES;
                int32_t la = sk_get_i32(nr + NF_LAT), lo = sk_get_i32(nr + NF_LON);
                if (la < la0) la0 = la;
                if (la > la1) la1 = la;
                if (lo < lo0) lo0 = lo;
                if (lo > lo1) lo1 = lo;
            }
            bb[i * 4] = la0; bb[i * 4 + 1] = lo0; bb[i * 4 + 2] = la1; bb[i * 4 + 3] = lo1;
        }
        *bbox_cache = bb;
    }

    /* 本 pass 的视口不变 → 取一次投影快照，循环里用 mv_proj（PLAN.md §30） */
    MvProj P;
    mv_proj_begin(v, &P);

    double bbl[4];
    mv_visible_bbox(v, bbl);
    int32_t vla0 = (int32_t)(bbl[0] * 1e7), vla1 = (int32_t)(bbl[2] * 1e7);
    int32_t vlo0 = (int32_t)(bbl[1] * 1e7), vlo1 = (int32_t)(bbl[3] * 1e7);
    double zoom = v->zoom;

    /* ★ 不要在这里清零：多省加载时是按省循环调用本函数的，一清零就把上一个省
       累加的结果抹掉（表现为"图上明明有建筑/水面，统计却报 0"——全 8 省加载时
       最后一个不覆盖视口的省会把计数清成 0）。一帧的清零只在 paint_frame 开头
       做一次，见那里的注释。 */
    AreaCtx ctx;
    ctx.P = &P;
    ctx.nodes = nodes;
    ctx.areas = areas;
    ctx.polyrefs = polyrefs;
    ctx.nn = nn;
    ctx.vla0 = vla0; ctx.vla1 = vla1;
    ctx.vlo0 = vlo0; ctx.vlo1 = vlo1;
    for (int k = 0; k <= AK_OTHER; k++) {
        ctx.afill[k] = afill[k];
        ctx.astroke[k] = astroke[k];
    }

    /* 分带并行：要素量不够就别起线程（每条线程 ~50us 的创建开销，z15 一帧
       总共才 2 ms）。失败一律落回串行 —— 宁慢不可缺块。 */
    int nband = band_pick_count();
    if (nband > 1 && na >= SK_BAND_MIN_AREAS && bbox_cache && *bbox_cache) {
        if (paint_areas_banded(rs, &ctx, *bbox_cache, na, zoom, nband, st) == 0)
            return 0;
    }

    const int32_t *bball = (bbox_cache && *bbox_cache) ? *bbox_cache : NULL;
    for (uint64_t i = 0; i < na; i++)
        area_emit(rs, &ctx, i, bball ? bball + (size_t)i * 4 : NULL, zoom, 1, st);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 线要素：步行道 / 铁路 / 水系线                                        */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 线阶段：分带并行                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const MvProj    *P;
    const NMBinFile *nodes;
    const NMBinFile *lines;
    const NMBinFile *polyrefs;
    uint64_t         nn, np;
    int32_t          vla0, vla1, vlo0, vlo1;
    double           la0, la1, lo0, lo1;   /* 视口（外扩 0.02°） */
} LineCtx;

/* 画一条线（折线）。与面同样的约定：筛选全在函数内做，两条路径共用。 */
static void line_emit(Raster *rs, const LineCtx *c, uint64_t i,
                      const int32_t *lb, double zoom, int count, PaintStats *st)
{
    static __thread PaintStats s_dummy;
    PaintStats *dst = count ? st : &s_dummy;

    if (lb) {
        if (lb[2] < c->vla0 || lb[0] > c->vla1 ||
            lb[3] < c->vlo0 || lb[1] > c->vlo1)
            return;
    }
    const uint8_t *lr = c->lines->data + (size_t)i * SK_LINE_BYTES;
    uint8_t cls = lr[LF_CLASS];
    /* LOD：步行道细节多、密度大，中高 zoom 才显示 */
    if (cls == RC_PATH && zoom < 14.0) return;
    if (zoom < 8.0) return;

    /* ★ 线要素同样不去重（理由同面要素，见 PLAN.md §23）。 */

    uint32_t off = sk_get_u32(lr + LF_REF_OFF);
    uint32_t cnt = sk_get_u32(lr + LF_REF_CNT);
    if ((uint64_t)off + cnt > c->np) return;

    int vis = 0;
    int px[4096], py[4096];
    int n = 0;
    for (uint32_t k = 0; k < cnt && n < 4096; k++) {
        uint32_t idx = sk_get_u32(c->polyrefs->data + ((uint64_t)off + k) * 4);
        if ((uint64_t)idx >= c->nn) continue;
        const uint8_t *nr = c->nodes->data + (uint64_t)idx * SK_NODE_BYTES;
        int32_t ila = sk_get_i32(nr + NF_LAT), ilo = sk_get_i32(nr + NF_LON);
        double la = (double)ila / 1e7;
        double lo = (double)ilo / 1e7;
        if (la >= c->la0 && la <= c->la1 && lo >= c->lo0 && lo <= c->lo1) vis = 1;
        int x, y;
        mv_proj_e7(c->P, ila, ilo, &x, &y);
        px[n] = x; py[n] = y; n++;
    }
    if (!vis || n < 2) return;

    uint8_t cr, cg, cb, width;
    switch (cls) {
    case RC_RAIL:  cr = 120; cg = 120; cb = 132; width = (zoom < 12.5) ? 1 : 2; break;
    case RC_WATER: cr = 150; cg = 195; cb = 230; width = (zoom < 12.5) ? 1 : 2; break;
    default:       cr = 202; cg = 199; cb = 190; width = 1; break;  /* RC_PATH */
    }
    uint32_t col = raster_rgb(rs, cr, cg, cb);
    if (width <= 1) {
        stroke_begin();
        for (int k = 0; k < n; k++) stroke_push(rs, px[k], py[k], col);
        stroke_flush();
    } else {
        for (int k = 1; k < n; k++)
            raster_thick_line(rs, px[k - 1], py[k - 1], px[k], py[k],
                              width, col);
    }
    dst->lines_drawn++;
}

typedef struct {
    BandView      *bv;
    const LineCtx *ctx;
    const int32_t *lball;
    double         zoom;
    PaintStats     st[SK_BAND_MAX];
} LinesJob;

static void lines_task(void *arg, int tid)
{
    LinesJob *j = (LinesJob *)arg;
    Raster *rs = &j->bv->band[tid];
    PaintStats *st = &j->st[tid];
    memset(st, 0, sizeof *st);
    uint32_t n = s_bkt_n[tid];
    const uint32_t *b = s_bkt[tid];
    for (uint32_t k = 0; k < n; k++) {
        uint32_t v = b[k];
        uint32_t i = v & 0x7fffffffu;
        int is_main = (v & 0x80000000u) != 0;
        line_emit(rs, j->ctx, i, j->lball + (size_t)i * 4, j->zoom, is_main, st);
    }
}

static int paint_lines_banded(Raster *rs, const LineCtx *ctx, int32_t *lball,
                              uint64_t nl, double zoom, int nband, PaintStats *st)
{
    /* 同 paint_areas_banded / paint_roads_banded：旋转视图落回串行。 */
    if (ctx->P->rot != 0.0) return -1;
    BandView bv;
    band_init(&bv, rs, nband);
    for (int t = 0; t < bv.n; t++) s_bkt_n[t] = 0;

    for (uint64_t i = 0; i < nl; i++) {
        const int32_t *lb = lball + (size_t)i * 4;
        if (lb[2] < ctx->vla0 || lb[0] > ctx->vla1 ||
            lb[3] < ctx->vlo0 || lb[1] > ctx->vlo1)
            continue;
        const uint8_t *lr = ctx->lines->data + (size_t)i * SK_LINE_BYTES;
        uint8_t cls = lr[LF_CLASS];
        if (cls == RC_PATH && zoom < 14.0) continue;
        if (zoom < 8.0) continue;

        int xa, ya, xb, yb;
        mv_proj_e7(ctx->P, lb[0], lb[1], &xa, &ya);
        mv_proj_e7(ctx->P, lb[2], lb[3], &xb, &yb);
        int ylo = (ya < yb ? ya : yb) - 2;
        int yhi = (ya < yb ? yb : ya) + 2;
        int t0 = band_of_y(&bv, ylo), t1 = band_of_y(&bv, yhi);
        int main_t = band_of_y(&bv, (ylo + yhi) / 2);
        for (int t = t0; t <= t1; t++) {
            uint32_t v = (uint32_t)i | (t == main_t ? 0x80000000u : 0u);
            if (bkt_push(t, v) != 0) return -1;
        }
    }

    LinesJob job;
    memset(&job, 0, sizeof job);
    job.bv = &bv;
    job.ctx = ctx;
    job.lball = lball;
    job.zoom = zoom;
    band_run(&bv, lines_task, &job);

    for (int t = 0; t < bv.n; t++)
        st->lines_drawn += job.st[t].lines_drawn;
    return 0;
}

int paint_lines(Raster *rs, const MapView *v,
                const NMBinFile *nodes, const NMBinFile *lines,
                const NMBinFile *polyrefs,
                int32_t **bbox_cache, PaintStats *st)
{
    uint64_t nl = lines->hdr.rec_count;
    uint64_t np = polyrefs->hdr.rec_count;
    uint64_t nn = nodes->hdr.rec_count;
    if (!nl || !np || !nn) return 0;
    if (!rs) return 0;

    /* 懒初始化每条线的 bbox（进程内一次）。
       没有它就得为每条线投影全部顶点才能知道它可不可见——线要素没有 tile
       索引，10 万条线 × 平均几十个顶点的投影是 z14 一帧 248 ms 的主要来源。 */
    if (bbox_cache && !*bbox_cache) {
        int32_t *bb = calloc((size_t)nl * 4, sizeof(int32_t));
        if (!bb) return -1;
        for (uint64_t i = 0; i < nl; i++) {
            const uint8_t *lr = lines->data + i * SK_LINE_BYTES;
            uint32_t off = sk_get_u32(lr + LF_REF_OFF);
            uint32_t cnt = sk_get_u32(lr + LF_REF_CNT);
            int32_t a0 = 90 * 10000000, a1 = -90 * 10000000;
            int32_t o0 = 180 * 10000000, o1 = -180 * 10000000;
            for (uint32_t k = 0; k < cnt; k++) {
                uint32_t idx = sk_get_u32(polyrefs->data + ((uint64_t)off + k) * 4);
                if ((uint64_t)idx >= nn) continue;
                const uint8_t *nr = nodes->data + (uint64_t)idx * SK_NODE_BYTES;
                int32_t la = sk_get_i32(nr + NF_LAT), lo = sk_get_i32(nr + NF_LON);
                if (la < a0) a0 = la;
                if (la > a1) a1 = la;
                if (lo < o0) o0 = lo;
                if (lo > o1) o1 = lo;
            }
            bb[i * 4] = a0; bb[i * 4 + 1] = o0; bb[i * 4 + 2] = a1; bb[i * 4 + 3] = o1;
        }
        *bbox_cache = bb;
    }
    int32_t *bb = bbox_cache ? *bbox_cache : NULL;

    /* 同上：本 pass 视口不变，取一次投影快照 */
    MvProj P;
    mv_proj_begin(v, &P);

    double bbl[4];
    mv_visible_bbox(v, bbl);
    double la0 = bbl[0] - 0.02, la1 = bbl[2] + 0.02;
    double lo0 = bbl[1] - 0.02, lo1 = bbl[3] + 0.02;
    int32_t vla0 = (int32_t)(la0 * 1e7), vla1 = (int32_t)(la1 * 1e7);
    int32_t vlo0 = (int32_t)(lo0 * 1e7), vlo1 = (int32_t)(lo1 * 1e7);
    double zoom = v->zoom;

    LineCtx ctx;
    ctx.P = &P;
    ctx.nodes = nodes;
    ctx.lines = lines;
    ctx.polyrefs = polyrefs;
    ctx.nn = nn;
    ctx.np = np;
    ctx.vla0 = vla0; ctx.vla1 = vla1;
    ctx.vlo0 = vlo0; ctx.vlo1 = vlo1;
    ctx.la0 = la0;  ctx.la1 = la1;
    ctx.lo0 = lo0;  ctx.lo1 = lo1;

    int nband = band_pick_count();
    if (nband > 1 && nl >= SK_BAND_MIN_LINES && bb) {
        if (paint_lines_banded(rs, &ctx, bb, nl, zoom, nband, st) == 0)
            return 0;
    }

    /* 同上：不清零，多省循环时跨省累加。 */
    for (uint64_t i = 0; i < nl; i++)
        line_emit(rs, &ctx, i, bb ? bb + (size_t)i * 4 : NULL, zoom, 1, st);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 等高线（SRTM 生成，contours/polyrefs/pts 三件套）                    */
/* ------------------------------------------------------------------ */

int paint_contours(Raster *rs, const MapView *v,
                   const NMBinFile *contours, const NMBinFile *polyrefs,
                   const NMBinFile *pts,
                   int32_t **bbox_cache, PaintStats *st, Font *font)
{
    uint64_t nc = contours->hdr.rec_count;
    uint64_t np = polyrefs->hdr.rec_count;
    uint64_t npts = pts->hdr.rec_count;
    if (!nc || !np || !npts) return 0;
    if (!rs) return 0;

    /* 懒初始化每条等高线的 bbox（进程内一次），机制同 paint_lines 的 line_bbox。 */
    if (bbox_cache && !*bbox_cache) {
        int32_t *bb = calloc((size_t)nc * 4, sizeof(int32_t));
        if (!bb) return -1;
        for (uint64_t i = 0; i < nc; i++) {
            const uint8_t *cr = contours->data + i * SK_CONTOUR_BYTES;
            uint32_t off = sk_get_u32(cr + CF_REF_OFF);
            uint32_t cnt = sk_get_u32(cr + CF_REF_CNT);
            int32_t a0 = 90 * 10000000, a1 = -90 * 10000000;
            int32_t o0 = 180 * 10000000, o1 = -180 * 10000000;
            for (uint32_t k = 0; k < cnt; k++) {
                uint32_t idx = sk_get_u32(polyrefs->data + ((uint64_t)off + k) * 4);
                if ((uint64_t)idx >= npts) continue;
                const uint8_t *pp = pts->data + (uint64_t)idx * SK_CONTOUR_PT_BYTES;
                int32_t la = sk_get_i32(pp + CP_LAT), lo = sk_get_i32(pp + CP_LON);
                if (la < a0) a0 = la;
                if (la > a1) a1 = la;
                if (lo < o0) o0 = lo;
                if (lo > o1) o1 = lo;
            }
            bb[i * 4] = a0; bb[i * 4 + 1] = o0; bb[i * 4 + 2] = a1; bb[i * 4 + 3] = o1;
        }
        *bbox_cache = bb;
    }
    int32_t *bb = bbox_cache ? *bbox_cache : NULL;

    MvProj P;
    mv_proj_begin(v, &P);

    double bbl[4];
    mv_visible_bbox(v, bbl);
    double la0 = bbl[0] - 0.02, la1 = bbl[2] + 0.02;
    double lo0 = bbl[1] - 0.02, lo1 = bbl[3] + 0.02;
    int32_t vla0 = (int32_t)(la0 * 1e7), vla1 = (int32_t)(la1 * 1e7);
    int32_t vlo0 = (int32_t)(lo0 * 1e7), vlo1 = (int32_t)(lo1 * 1e7);
    double zoom = v->zoom;

    /* 标注防重叠：记录本帧已放下的高程标签屏幕坐标，新标签若落在已有标签
     * MIN_LABEL_GAP 像素内则跳过，避免一屏十几条计曲线各标一个、数字糊成一团。
     * 注意只统计"真实放下"的标签（含白光晕），不统计被跳过的。 */
    enum { MAX_PLACED = 1024, MIN_LABEL_GAP = 34 };
    int placed_x[MAX_PLACED], placed_y[MAX_PLACED];
    int nplaced = 0;

    for (uint64_t i = 0; i < nc; i++) {
        /* bbox 预筛（等高线同样没有 tile 索引） */
        if (bb) {
            if (bb[i * 4 + 2] < vla0 || bb[i * 4] > vla1 ||
                bb[i * 4 + 3] < vlo0 || bb[i * 4 + 1] > vlo1)
                continue;
        }
        const uint8_t *cr = contours->data + i * SK_CONTOUR_BYTES;
        uint8_t cls = cr[CF_CLASS];
        /* LOD：低 zoom 只画计曲线（每 index 一条，密度低不糊）；
         * zoom>=11 才把首曲线（密集）也画出来。 */
        if (zoom < 9.0) continue;
        if (cls == 0 && zoom < 11.0) continue;

        uint32_t off = sk_get_u32(cr + CF_REF_OFF);
        uint32_t cnt = sk_get_u32(cr + CF_REF_CNT);
        if ((uint64_t)off + cnt > np) continue;

        int vis = 0;
        int px[4096], py[4096];
        int n = 0;
        for (uint32_t k = 0; k < cnt && n < 4096; k++) {
            uint32_t idx = sk_get_u32(polyrefs->data + ((uint64_t)off + k) * 4);
            if ((uint64_t)idx >= npts) continue;
            const uint8_t *pp = pts->data + (uint64_t)idx * SK_CONTOUR_PT_BYTES;
            int32_t ila = sk_get_i32(pp + CP_LAT), ilo = sk_get_i32(pp + CP_LON);
            double la = (double)ila / 1e7;
            double lo = (double)ilo / 1e7;
            if (la >= la0 && la <= la1 && lo >= lo0 && lo <= lo1) vis = 1;
            int x, y;
            mv_proj_e7(&P, ila, ilo, &x, &y);
            px[n] = x; py[n] = y; n++;
        }
        if (!vis || n < 2) continue;

        /* 棕色等高线：计曲线（class=1）更深更粗，首曲线（class=0）浅细。 */
        uint8_t cr_, cg, cb, width;
        if (cls == 1) {
            cr_ = 150; cg = 105; cb = 70; width = (zoom < 12.5) ? 1 : 2;
        } else {
            cr_ = 195; cg = 170; cb = 145; width = 1;
        }
        uint32_t col = raster_rgb(rs, cr_, cg, cb);
        if (width <= 1) {
            stroke_begin();
            for (int k = 0; k < n; k++) stroke_push(rs, px[k], py[k], col);
            stroke_flush();
        } else {
            for (int k = 1; k < n; k++)
                raster_thick_line(rs, px[k - 1], py[k - 1], px[k], py[k],
                                  width, col);
        }
        /* 计曲线（class==1）标注高程刻度：在折线中点用字体画高程（米）。
         * 首曲线（class==0）密集不标，避免糊；只标计曲线符合地形图惯例。
         * ★ 防重叠：本帧已放下的标签 MIN_LABEL_GAP 像素内不再放，控制密度。 */
        if (font && cls == 1 && n >= 2) {
            char eb[24];
            uint64_t elev = sk_get_u64(cr + CF_ELEV);
            snprintf(eb, sizeof(eb), "%llu", (unsigned long long)elev);
            int mid = n / 2;
            int lx = px[mid] + 2, ly = py[mid] - 9;
            /* 邻近已有标签则跳过（控制密度，避免一屏数字糊成团） */
            int too_close = 0;
            for (int k = 0; k < nplaced; k++) {
                int dx = lx - placed_x[k], dy = ly - placed_y[k];
                if (dx * dx + dy * dy < MIN_LABEL_GAP * MIN_LABEL_GAP) {
                    too_close = 1;
                    break;
                }
            }
            if (too_close) continue;
            /* 白色光晕 + 黑正文：标准地形图可读标注。
             * ★ 正文用纯黑 (0,0,0) 而非棕色 —— 黑字叠在白光晕上只会产生
             *   灰度像素（R=G=B），与无图横幅混合色 (110,100,80)（非灰）在
             *   容差 8 内绝不可能匹配，避免离屏审计把等高线标注误判成横幅
             *   （见 audit.c 的 count_overlay_px 判据）。
             *   早期用 (45,30,15) 时，棕字在白光晕上按字形覆盖率混合恰好落在
             *   ~(110,100,89)，触发 ASSERT-FAIL。 */
            font_draw(font, rs, lx - 1, ly,     eb, 255, 255, 255, 0);
            font_draw(font, rs, lx + 1, ly,     eb, 255, 255, 255, 0);
            font_draw(font, rs, lx,     ly - 1, eb, 255, 255, 255, 0);
            font_draw(font, rs, lx,     ly + 1, eb, 255, 255, 255, 0);
            font_draw(font, rs, lx,     ly,     eb, 0, 0, 0, 0);
            if (nplaced < MAX_PLACED) { placed_x[nplaced] = lx; placed_y[nplaced] = ly; nplaced++; }
        }
        st->lines_drawn++;
    }
    return 0;
}
