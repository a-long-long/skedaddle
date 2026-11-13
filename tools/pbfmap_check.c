/* 校验工具：PBF 运行时解码出的显示层 ↔ 落盘 disp tile，双向集合等价。
 *
 * 用法: skedaddle_pbfmap_check <province_dir> <xxx.osm.pbf> [--self-test]
 *
 * 背景：整省 areas/lines/polyrefs.bin 自 2026-11-03 起不再落盘（195 MB 冗余，
 * 见 graph_build.c:1165），运行时显示层改由 disp.idx + t_<c>_<r>.{areas,lines,
 * polyrefs} 按视口拼接。旧版工具仍去打开整省三件套，必然 "打不开 areas.bin"
 * （rc=2），是**永远红**的死判据 —— 本项目 2026-11-13 修复。
 *
 * 为什么不能逐字节比（旧版语义在这里根本不成立）：
 *   1) 同一要素跨 tile 边界会重复写入（大要素边界保护）；
 *   2) 每条记录的 ref_off 被 disp_append 重基到该 tile 自己的 polyrefs 段；
 *   3) 拼接顺序按空间网格，不是按 osm_id。
 * 所以判据按**要素内容**做集合比较：把每条要素规范化成
 *   [整条记录，ref_off 字段置 0] + [ref_cnt 个 u32 节点内部下标]
 * 排序去重后，要求 PBF 集合 == tile 集合（数量相等且逐条相等，双向）。
 *
 * 与 disp_tile_selftest 的分工：后者只断言「整省要素全部能在 tile 里找到」
 * （单向覆盖，抓不到 tile 里多出来的伪造要素）；本工具补上反向缺口。
 *
 * 负向对照（--self-test）：分别注入「改 PBF 侧一条要素」与「删 PBF 侧一条
 * 要素」，判据必须报 DIFF；正常态必须 IDENTICAL。三者都成立才打印
 * PBFMAP-SELFTEST-OK，避免"把条件改成常量 0"式的假门禁。
 *
 * 抓到并已修的 bug（2026-11-13，本工具正是抓到它的那个判据）：
 *   首跑时 8 省只有 beijing / tianjin 为 IDENTICAL，其余 6 省 DIFF；
 *   disp_tile_selftest 独立同向失败 ⇒ 不是解码器坏、不是误报。根因在
 *   graph_build.c 的 write_disp_tiles()：ncols 被算成经度格数、nrows 被算成
 *   纬度格数，但 c 走纬度、r 走经度，下游一律按「ncols 是 c 的格数」书写
 *   （钳位 / tc=r*ncols+c / 写出反解 / 运行时边界），于是 bbox 越不接近
 *   正方形，r 的上界被截得越狠，r0>r1 时写入循环一次都不执行，要素静默
 *   丢失（shandong 1.93 → 丢 197817 条面要素 = 52.6%）。
 *   修复只交换那两行赋值：NMBIN 记录布局与 disp.idx 字段都没变，运行时
 *   代码一行未改，新旧二进制 / 产物双向可读，北京天津产物逐字节不变。
 *   8 省已由 sync_maps.sh --force 重抽，现全量 IDENTICAL；主机侧 8 省门禁
 *   scripts/check_disp_coverage.py 守（regress.sh 只测北京，副机缺 7 省数据）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map/binfmt.h"
#include "map/pbfmap.h"

typedef struct { uint8_t *p; uint32_t n; } Elem;
typedef struct { Elem *v; size_t n, cap; } ElemVec;

static int push_elem(ElemVec *v, const uint8_t *rec, uint32_t recsz,
                     int off_field, int cnt_field,
                     const uint8_t *poly, uint64_t poly_n)
{
    uint32_t off = sk_get_u32(rec + off_field);
    uint32_t cnt = sk_get_u32(rec + cnt_field);
    /* 越界按结构性错误处理，绝不静默跳过 —— 跳过会让脏数据混过门禁 */
    if ((uint64_t)off + cnt > poly_n) return -1;

    uint32_t n = recsz + cnt * 4;
    uint8_t *p = malloc(n ? n : 1);
    if (!p) return -1;
    memcpy(p, rec, recsz);
    /* ref_off 在整省基准里是全局偏移、在 tile 里是段内偏移，置 0 消除差异 */
    memset(p + off_field, 0, 4);
    if (cnt) memcpy(p + recsz, poly + (size_t)off * 4, (size_t)cnt * 4);

    if (v->n == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 64;
        Elem *t = realloc(v->v, nc * sizeof(Elem));
        if (!t) { free(p); return -1; }
        v->v = t; v->cap = nc;
    }
    v->v[v->n].p = p; v->v[v->n].n = n; v->n++;
    return 0;
}

static int collect_mono(ElemVec *v, const NMBinFile *feat, const NMBinFile *poly,
                        int off_field, int cnt_field)
{
    uint32_t rs = feat->hdr.rec_size;
    uint64_t n = feat->hdr.rec_count, pn = poly->hdr.rec_count;
    if (rs == 0) return -1;
    for (uint64_t i = 0; i < n; i++) {
        if (push_elem(v, feat->data + (size_t)i * rs, rs, off_field, cnt_field,
                      poly->data, pn) != 0) return -1;
    }
    return 0;
}

/* 遍历 disp.idx 列出的全部非空 tile，逐层收集。任一 tile 打不开即结构错误。 */
static int collect_tiles(ElemVec *v, const char *dir, const char *tile_ext,
                         int off_field, int cnt_field)
{
    char idxp[4096];
    snprintf(idxp, sizeof(idxp), "%s/disp.idx", dir);
    FILE *idx = fopen(idxp, "rb");
    if (!idx) { fprintf(stderr, "  打不开 %s\n", idxp); return -1; }

    char line[256];
    int in_list = 0, rc = 0;
    long c, r;
    while (fgets(line, sizeof(line), idx)) {
        if (strncmp(line, "ntiles", 6) == 0) { in_list = 1; continue; }
        if (!in_list) continue;
        if (sscanf(line, "%ld %ld", &c, &r) != 2) continue;

        char pa[4096], pp[4096];
        snprintf(pa, sizeof(pa), "%s/t_%ld_%ld.%s", dir, c, r, tile_ext);
        snprintf(pp, sizeof(pp), "%s/t_%ld_%ld.polyrefs", dir, c, r);
        NMBinFile A, P;
        memset(&A, 0, sizeof(A)); memset(&P, 0, sizeof(P));
        if (nmbin_open(pa, &A) != 0 || nmbin_open(pp, &P) != 0) {
            fprintf(stderr, "  打不开 tile %ld_%ld（%s / polyrefs）\n", c, r, tile_ext);
            nmbin_close(&A); nmbin_close(&P);
            rc = -1; break;
        }
        int e = collect_mono(v, &A, &P, off_field, cnt_field);
        nmbin_close(&A); nmbin_close(&P);
        if (e != 0) { fprintf(stderr, "  tile %ld_%ld 记录越界\n", c, r); rc = -1; break; }
    }
    fclose(idx);
    return rc;
}

static int cmp_elem(const void *a, const void *b)
{
    const Elem *x = (const Elem *)a, *y = (const Elem *)b;
    uint32_t n = x->n < y->n ? x->n : y->n;
    int c = memcmp(x->p, y->p, n);
    if (c) return c;
    return (x->n > y->n) - (x->n < y->n);
}

/* 排序 + 相邻去重：tile 侧的跨边界重复在这里被折叠掉 */
static void normalize(ElemVec *v)
{
    qsort(v->v, v->n, sizeof(Elem), cmp_elem);
    size_t w = 0;
    for (size_t i = 0; i < v->n; i++) {
        if (w && v->v[w - 1].n == v->v[i].n &&
            memcmp(v->v[w - 1].p, v->v[i].p, v->v[i].n) == 0) {
            free(v->v[i].p);
            continue;
        }
        v->v[w++] = v->v[i];
    }
    v->n = w;
}

static int same_set(const ElemVec *a, const ElemVec *b)
{
    if (a->n != b->n) return 0;
    for (size_t i = 0; i < a->n; i++)
        if (a->v[i].n != b->v[i].n ||
            memcmp(a->v[i].p, b->v[i].p, a->v[i].n) != 0) return 0;
    return 1;
}

static void free_vec(ElemVec *v)
{
    for (size_t i = 0; i < v->n; i++) free(v->v[i].p);
    free(v->v);
    memset(v, 0, sizeof(*v));
}

/* 注入只动 PBF 侧收集出来的 key，不碰原缓冲、不碰常量条件：
 *   mode 1 = 改一条要素的 osm_id 首字节 ⇒ PBF 集合多出 tile 没有的元素
 *   mode 2 = 删一条要素            ⇒ tile 集合多出 PBF 没有的元素
 * 两个方向分别被 same_set 抓到，证明判据是双向的。 */
static void inject(ElemVec *v, int mode)
{
    if (!mode || v->n == 0) return;
    if (mode == 1) {
        v->v[0].p[0] ^= 0x5A;
    } else if (mode == 2) {
        free(v->v[0].p);
        memmove(v->v, v->v + 1, (v->n - 1) * sizeof(Elem));
        v->n--;
    }
}

static int compare_layer(const char *dir, const NMBinFile *mono,
                         const NMBinFile *poly, const char *tile_ext,
                         int off_field, int cnt_field, int inject_mode,
                         const char *name)
{
    ElemVec a = {0}, b = {0};
    int rc;
    if (collect_mono(&a, mono, poly, off_field, cnt_field) != 0 ||
        collect_tiles(&b, dir, tile_ext, off_field, cnt_field) != 0) {
        rc = -1;
        goto done;
    }
    {
        uint64_t pbf_n = a.n, tile_raw = b.n;
        inject(&a, inject_mode);
        normalize(&a);
        normalize(&b);
        int same = same_set(&a, &b);
        printf("  %-10s PBF %llu 条 / tile 原始 %llu 条（去重 %llu）/ %s\n",
               name, (unsigned long long)pbf_n, (unsigned long long)tile_raw,
               (unsigned long long)b.n, same ? "一致" : "不一致");
        rc = same ? 0 : 1;
    }
done:
    free_vec(&a); free_vec(&b);
    return rc;
}

/* 返回 0 = IDENTICAL，1 = DIFF，-1 = 结构性错误（打不开文件 / 记录越界）。
 * 错误与 DIFF 分开，避免 --self-test 把"跑不起来"当成"注入成功"。 */
static int compare_all(const char *dir, const PbfMap *pm, int inject_mode)
{
    int r1 = compare_layer(dir, &pm->areas, &pm->polyrefs, "areas",
                           AF_REF_OFF, AF_REF_CNT, inject_mode, "areas");
    int r2 = compare_layer(dir, &pm->lines, &pm->polyrefs, "lines",
                           LF_REF_OFF, LF_REF_CNT, inject_mode, "lines");
    if (r1 < 0 || r2 < 0) return -1;
    return (r1 || r2) ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *dir = NULL, *pbf = NULL;
    int self_test = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--self-test") == 0) self_test = 1;
        else if (!dir) dir = argv[i];
        else if (!pbf) pbf = argv[i];
    }
    if (!dir || !pbf) {
        fprintf(stderr, "用法: %s <province_dir> <file.osm.pbf> [--self-test]\n", argv[0]);
        return 2;
    }

    char path[4096];
    snprintf(path, sizeof(path), "%s/nodes.bin", dir);
    NMBinFile nodes;
    if (nmbin_open(path, &nodes) != 0) {
        fprintf(stderr, "打不开 %s\n", path);
        return 2;
    }
    PbfMap pm;
    memset(&pm, 0, sizeof(pm));
    if (pbfmap_decode(pbf, &nodes, &pm, NULL, NULL) != 0) {
        fprintf(stderr, "pbfmap_decode 失败: %s\n", pbf);
        pbfmap_free(&pm);
        nmbin_close(&nodes);
        return 2;
    }
    nmbin_close(&nodes);

    printf("=== %s vs %s（PBF 解码 ↔ disp tile 双向集合等价）===\n", dir, pbf);

    int rc;
    if (self_test) {
        printf("[正常态]\n");
        int r0 = compare_all(dir, &pm, 0);
        printf("[注入 A：改 PBF 侧一条要素的 osm_id]\n");
        int r1 = compare_all(dir, &pm, 1);
        printf("[注入 B：删 PBF 侧一条要素]\n");
        int r2 = compare_all(dir, &pm, 2);
        int ok = (r0 == 0 && r1 == 1 && r2 == 1);
        if (ok)
            printf("PBFMAP-SELFTEST-OK  正常 IDENTICAL / 两种注入均 DIFF\n");
        else
            printf("PBFMAP-SELFTEST-FAIL 正常=%d 注入A=%d 注入B=%d\n", r0, r1, r2);
        rc = ok ? 0 : 1;
    } else {
        rc = compare_all(dir, &pm, 0);
        if (rc == 0) printf("IDENTICAL  PBF 解码 ↔ disp tile 要素集合相等\n");
        else if (rc == 1) printf("DIFF  PBF 解码 ↔ disp tile 要素集合不等\n");
        else fprintf(stderr, "ERROR  产物缺失或记录越界，判据无法执行\n");
    }

    pbfmap_free(&pm);
    return rc < 0 ? 2 : rc;
}
