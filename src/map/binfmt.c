#include "map/binfmt.h"
#include "utils/mem.h"
#include "utils/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NMBinWriter {
    FILE    *fp;
    char     path[4096];
    NMBinHeader hdr;
    NMBinField  fields[NMBIN_MAX_FIELDS];
    uint64_t recs_written;
    Sha256   sha;             /* O2：只喂记录区，边写边算，零额外 IO */
    int      failed;
};

void sk_put_u16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)(v & 0xff); p[1] = (uint8_t)(v >> 8); }

void sk_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff); p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff); p[3] = (uint8_t)((v >> 24) & 0xff);
}

void sk_put_i32(uint8_t *p, int32_t v) { sk_put_u32(p, (uint32_t)v); }

void sk_put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xff);
}

uint16_t sk_get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t sk_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int32_t sk_get_i32(const uint8_t *p) { return (int32_t)sk_get_u32(p); }

uint64_t sk_get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* ★ 手工序列化：不要改成 memcpy(&hdr)，见 binfmt.h 顶部关于 @20 与
 * struct 对齐 @24 差异的说明。摘要区落在 @76，既有字段一个都没挪。 */
static void hdr_to_bytes(const NMBinHeader *h, uint8_t *b)
{
    memset(b, 0, NMBIN_HDR_BYTES);
    memcpy(b, h->magic, 8);
    sk_put_u32(b + 8,  h->version);
    sk_put_u32(b + 12, h->kind);
    sk_put_u32(b + 16, h->rec_size);
    sk_put_u64(b + 20, h->rec_count);
    sk_put_u32(b + 28, h->n_fields);
    sk_put_u32(b + 32, h->flags);
    sk_put_u64(b + 36, h->payload_bytes);
    memcpy(b + 44, h->source, 32);
    memcpy(b + NMBIN_OFF_DIGEST, h->digest, 32);
    sk_put_u32(b + NMBIN_OFF_DIG_ALGO, h->digest_algo);
    sk_put_u32(b + NMBIN_OFF_DIG_VER,  h->digest_ver);
}

static void bytes_to_hdr(const uint8_t *b, NMBinHeader *h)
{
    memset(h, 0, sizeof(*h));
    memcpy(h->magic, b, 8);
    h->version       = sk_get_u32(b + 8);
    h->kind          = sk_get_u32(b + 12);
    h->rec_size      = sk_get_u32(b + 16);
    h->rec_count     = sk_get_u64(b + 20);
    h->n_fields      = sk_get_u32(b + 28);
    h->flags         = sk_get_u32(b + 32);
    h->payload_bytes = sk_get_u64(b + 36);
    memcpy(h->source, b + 44, 32);
    h->source[31] = '\0';
    memcpy(h->digest, b + NMBIN_OFF_DIGEST, 32);
    h->digest_algo   = sk_get_u32(b + NMBIN_OFF_DIG_ALGO);
    h->digest_ver    = sk_get_u32(b + NMBIN_OFF_DIG_VER);
}

static void field_to_bytes(const NMBinField *f, uint8_t *b)
{
    memset(b, 0, NMBIN_FIELD_BYTES);
    memcpy(b, f->name, 16);
    sk_put_u16(b + 16, f->offset);
    b[18] = f->type;
    b[19] = f->width;
}

static void bytes_to_field(const uint8_t *b, NMBinField *f)
{
    memset(f, 0, sizeof(*f));
    memcpy(f->name, b, 16);
    f->name[15] = '\0';
    f->offset = sk_get_u16(b + 16);
    f->type   = b[18];
    f->width  = b[19];
}

/* ---------------------------------------------------------------------------
 * O1 期望 schema 表
 *
 * ★ 逐条复制各写入器 nmbin_add_field() 的实际调用序列（graph_build.c /
 *   gen_contours.c / pbfmap.c 的内存版），而不是从 rec_size 推导。
 *   特别注意：restrictions.bin 的 prev_edge 槽（@16）没有登记字段；
 *   kind=12 被 lanes.bin(rec_size=4) 与 lane_names.bin(rec_size=1) 共用，
 *   所以索引必须是 (kind, rec_size) 二元组。
 *
 * ★ 2026-11-12 从 8 省真实产物反推并与源码交叉验证，11 种组合全一致。
 * ★ 未登记组合返回 NULL ⇒ 跳过比对，保证历史 v1 布局 / 未来新 kind 不误伤。
 * ------------------------------------------------------------------------- */
#define SKF(n, o, t, w) { n, (uint16_t)(o), (uint8_t)(t), (uint8_t)(w), {0} }

static const NMBinField k_f_nodes[] = {
    SKF("lat_e7",     NF_LAT,        NMT_I32, 4),
    SKF("lon_e7",     NF_LON,        NMT_I32, 4),
    SKF("edge_begin", NF_EDGE_BEGIN, NMT_U32, 4),
    SKF("edge_count", NF_EDGE_COUNT, NMT_U16, 2),
    SKF("node_flags", NF_NODE_FLAGS, NMT_U16, 2),
    SKF("osm_id",     NF_OSM_ID,     NMT_U64, 8),
    SKF("class_mask", NF_CLASS_MASK, NMT_U32, 4),
};
static const NMBinField k_f_edges[] = {
    SKF("to",      EF_TO,      NMT_U32, 4),
    SKF("dist_cm", EF_DIST_CM, NMT_U32, 4),
    SKF("name_id", EF_NAME_ID, NMT_U32, 4),
    SKF("flags",   EF_FLAGS,   NMT_U16, 2),
    SKF("class",   EF_CLASS,   NMT_U8,  1),
    SKF("speed",   EF_SPEED,   NMT_U8,  1),
    SKF("lanes",   EF_LANES,   NMT_U16, 2),
};
static const NMBinField k_f_names[] = {
    SKF("pool", 0, NMT_U8, 1),
};
static const NMBinField k_f_restr[] = {
    SKF("from_edge", RF_FROM_EDGE, NMT_U32, 4),
    SKF("to_edge",   RF_TO_EDGE,   NMT_U32, 4),
    SKF("via_node",  RF_VIA_NODE,  NMT_U32, 4),
    SKF("kind",      RF_KIND,      NMT_U8,  1),
    SKF("dirmask",   RF_DIRMASK,   NMT_U8,  1),
};
static const NMBinField k_f_lanes[] = {
    SKF("lane_sid", 0, NMT_U32, 4),
};
static const NMBinField k_f_lines[] = {
    SKF("osm_id",  LF_OSM_ID,  NMT_U64, 8),
    SKF("ref_off", LF_REF_OFF, NMT_U32, 4),
    SKF("ref_cnt", LF_REF_CNT, NMT_U32, 4),
    SKF("class",   LF_CLASS,   NMT_U8,  1),
    SKF("flags",   LF_FLAGS,   NMT_U8,  1),
    SKF("name_id", LF_NAME_ID, NMT_U32, 4),
};
static const NMBinField k_f_areas[] = {
    SKF("osm_id",  AF_OSM_ID,  NMT_U64, 8),
    SKF("ref_off", AF_REF_OFF, NMT_U32, 4),
    SKF("ref_cnt", AF_REF_CNT, NMT_U32, 4),
    SKF("kind",    AF_KIND,    NMT_U8,  1),
};
static const NMBinField k_f_polyrefs[] = {
    SKF("node_idx", 0, NMT_U32, 4),
};
static const NMBinField k_f_edgelist[] = {
    SKF("from_lat", XL2_FROM_LAT, NMT_I32, 4),
    SKF("from_lon", XL2_FROM_LON, NMT_I32, 4),
    SKF("to_lat",   XL2_TO_LAT,   NMT_I32, 4),
    SKF("to_lon",   XL2_TO_LON,   NMT_I32, 4),
    SKF("flags",    XL2_FLAGS,    NMT_U16, 2),
    SKF("cls",      XL2_CLS,      NMT_U8,  1),
    SKF("edge_idx", XL2_EDGE,     NMT_U32, 4),
    SKF("node_idx", XL2_NODE,     NMT_U32, 4),
};
static const NMBinField k_f_tiles[] = {
    SKF("edge_off", TF_EDGE_OFF, NMT_U32, 4),
    SKF("edge_cnt", TF_EDGE_CNT, NMT_U32, 4),
};
static const NMBinField k_f_contours[] = {
    SKF("elev",    CF_ELEV,    NMT_U64, 8),
    SKF("ref_off", CF_REF_OFF, NMT_U32, 4),
    SKF("ref_cnt", CF_REF_CNT, NMT_U32, 4),
    SKF("class",   CF_CLASS,   NMT_U8,  1),
    SKF("flags",   CF_FLAGS,   NMT_U8,  1),
    SKF("name_id", CF_NAME_ID, NMT_U32, 4),
};
static const NMBinField k_f_contour_pts[] = {
    SKF("lat", CP_LAT, NMT_I32, 4),
    SKF("lon", CP_LON, NMT_I32, 4),
};
static const NMBinField k_f_contour_polyrefs[] = {
    SKF("idx", 0, NMT_U32, 4),
};

#define SCHEMA(k, rs, arr) { (k), (rs), (uint32_t)(sizeof(arr)/sizeof((arr)[0])), (arr) }

static const NMBinSchema k_schemas[] = {
    SCHEMA(NMBIN_KIND_NODES,            SK_NODE_BYTES,          k_f_nodes),
    SCHEMA(NMBIN_KIND_EDGES,            SK_EDGE_BYTES,          k_f_edges),
    SCHEMA(NMBIN_KIND_NAMES,            1,                      k_f_names),
    SCHEMA(NMBIN_KIND_RESTRICTIONS,     SK_RESTR_BYTES,         k_f_restr),
    SCHEMA(NMBIN_KIND_LANES,            4,                      k_f_lanes),
    SCHEMA(NMBIN_KIND_LANES,            1,                      k_f_names),
    SCHEMA(NMBIN_KIND_LINES,            SK_LINE_BYTES,          k_f_lines),
    SCHEMA(NMBIN_KIND_AREAS,            SK_AREA_BYTES,          k_f_areas),
    SCHEMA(NMBIN_KIND_POLYREFS,         4,                      k_f_polyrefs),
    SCHEMA(NMBIN_KIND_EDGELIST,         SK_XL2_BYTES,           k_f_edgelist),
    SCHEMA(NMBIN_KIND_TILES,            SK_TILE_REC_BYTES,      k_f_tiles),
    SCHEMA(NMBIN_KIND_CONTOURS,         SK_CONTOUR_BYTES,       k_f_contours),
    SCHEMA(NMBIN_KIND_CONTOUR_PTS,      SK_CONTOUR_PT_BYTES,    k_f_contour_pts),
    SCHEMA(NMBIN_KIND_CONTOUR_POLYREFS, 4,                      k_f_contour_polyrefs),
};

#undef SCHEMA
#undef SKF

uint32_t nmbin_schema_count(void)
{
    return (uint32_t)(sizeof(k_schemas) / sizeof(k_schemas[0]));
}

const NMBinSchema *nmbin_schema_at(uint32_t i)
{
    return i < nmbin_schema_count() ? &k_schemas[i] : NULL;
}

const NMBinSchema *nmbin_expected_schema(uint32_t kind, uint32_t rec_size)
{
    for (uint32_t i = 0; i < nmbin_schema_count(); i++)
        if (k_schemas[i].kind == kind && k_schemas[i].rec_size == rec_size)
            return &k_schemas[i];
    return NULL;
}

/* P3/4.1：期望表是文件字段表的**前缀子集**，尾部允许追加描述性字段。
 * ★★ 只放宽「同一 version 内追加字段」这一层；nmbin_check() 里
 *    f->hdr.version != NMBIN_VERSION 的全局硬校验绝不删 —— 它守的是
 *    「记录布局整体换版必须重抽」，两者粒度不同。
 * ★★ 原有分辨力一条不能丢：删必需字段 / 改必需字段的 name、offset、
 *    type、width 仍必须拒；追加字段本身仍受 nmbin_check() 的越界检查。
 * 错误串点名具体字段与 kind/rec_size，便于定位是哪一份产物不对。 */
int nmbin_check_schema(const NMBinFile *f, char *err, size_t err_cap)
{
#define SERR(...) do { snprintf(err, err_cap, __VA_ARGS__); return -1; } while (0)
    if (!f) SERR("空的 NMBinFile");
    const NMBinSchema *s = nmbin_expected_schema(f->hdr.kind, f->hdr.rec_size);
    if (!s) { err[0] = '\0'; return 0; }        /* 未登记：保持旧行为 */
    if (f->hdr.n_fields > NMBIN_MAX_FIELDS)
        SERR("n_fields %u 超过上限 %u", f->hdr.n_fields, (uint32_t)NMBIN_MAX_FIELDS);
    if (f->hdr.n_fields < s->n_fields)
        SERR("字段数不足：文件 %u，期望至少 %u（kind=%u rec_size=%u，"
             "缺字段「%s」）",
             f->hdr.n_fields, s->n_fields, f->hdr.kind, f->hdr.rec_size,
             s->fields[f->hdr.n_fields].name);
    for (uint32_t i = 0; i < s->n_fields; i++) {
        const NMBinField *got = &f->fields[i];
        const NMBinField *exp = &s->fields[i];
        if (strcmp(got->name, exp->name) != 0)
            SERR("字段[%u] 名称不符：文件「%s」，期望「%s」（kind=%u rec_size=%u）",
                 i, got->name, exp->name, f->hdr.kind, f->hdr.rec_size);
        if (got->offset != exp->offset)
            SERR("字段 %s 偏移不符：文件 %u，期望 %u（源码硬编码常量，"
                 "kind=%u rec_size=%u）",
                 exp->name, got->offset, exp->offset, f->hdr.kind, f->hdr.rec_size);
        if (got->type != exp->type || got->width != exp->width)
            SERR("字段 %s 类型/宽度不符：文件 %u/%u，期望 %u/%u（kind=%u rec_size=%u）",
                 exp->name, got->type, got->width, exp->type, exp->width,
                 f->hdr.kind, f->hdr.rec_size);
    }
    err[0] = '\0';
    return 0;
#undef SERR
}

int nmbin_schema_extra_fields(const NMBinFile *f)
{
    if (!f) return -1;
    const NMBinSchema *s = nmbin_expected_schema(f->hdr.kind, f->hdr.rec_size);
    if (!s) return -1;                          /* 未登记：没有「追加」语义 */
    if (f->hdr.n_fields < s->n_fields) return -1;
    return (int)(f->hdr.n_fields - s->n_fields);
}

/* ---------------------------------------------------------------------------
 * 写入端
 * ------------------------------------------------------------------------- */
NMBinWriter *nmbin_create(const char *path, uint32_t kind, uint32_t rec_size,
                          const char *source_hint)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return NULL;
    NMBinWriter *w = calloc(1, sizeof(NMBinWriter));
    if (!w) { fclose(fp); return NULL; }
    snprintf(w->path, sizeof(w->path), "%s", path);
    w->fp = fp;
    memcpy(w->hdr.magic, NMBIN_MAGIC, 8);
    w->hdr.version  = NMBIN_VERSION;
    w->hdr.kind     = kind;
    w->hdr.rec_size = rec_size;
    w->hdr.n_fields = 0;
    w->hdr.rec_count = 0;
    sha256_init(&w->sha);
    if (source_hint) snprintf(w->hdr.source, sizeof(w->hdr.source), "%s", source_hint);

    uint8_t hdrb[NMBIN_HDR_BYTES];
    hdr_to_bytes(&w->hdr, hdrb);
    if (fwrite(hdrb, 1, NMBIN_HDR_BYTES, fp) != NMBIN_HDR_BYTES) { fclose(fp); free(w); return NULL; }
    /* 字段表先留白，finalize 时回填（此时还不知道有几个字段） */
    uint8_t zeros[NMBIN_FIELD_BYTES * NMBIN_MAX_FIELDS];
    memset(zeros, 0, sizeof(zeros));
    if (fwrite(zeros, 1, sizeof(zeros), fp) != sizeof(zeros)) { fclose(fp); free(w); return NULL; }
    return w;
}

int nmbin_add_field(NMBinWriter *w, const char *name, uint16_t offset,
                    uint8_t type, uint8_t width)
{
    if (!w || w->hdr.n_fields >= NMBIN_MAX_FIELDS) return -1;
    NMBinField *f = &w->fields[w->hdr.n_fields++];
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->offset = offset; f->type = type; f->width = width;
    return 0;
}

int nmbin_write_records(NMBinWriter *w, const void *recs, uint64_t n)
{
    if (!w || w->failed) return -1;
    if (n == 0) return 0;
    size_t bytes = (size_t)n * w->hdr.rec_size;
    if (fwrite(recs, 1, bytes, w->fp) != bytes) { w->failed = 1; return -1; }
    sha256_update(&w->sha, recs, bytes);  /* 摘要只覆盖记录区 */
    w->recs_written += n;
    return 0;
}

int nmbin_finalize(NMBinWriter *w)
{
    if (!w) return -1;
    if (w->failed) { fclose(w->fp); remove(w->path); free(w); return -1; }

    w->hdr.rec_count = w->recs_written;
    w->hdr.payload_bytes = w->recs_written * (uint64_t)w->hdr.rec_size;
    /* O2：写入完成后定稿摘要。finalize 之后不再允许 write_records。 */
    sha256_final(&w->sha, w->hdr.digest);
    w->hdr.flags        |= NMBIN_FLAG_DIGEST;
    w->hdr.digest_algo   = NMBIN_DIGEST_SHA256;
    w->hdr.digest_ver    = NMBIN_DIGEST_VER;

    if (fseek(w->fp, 0, SEEK_SET) != 0) { fclose(w->fp); free(w); return -1; }
    uint8_t hdrb[NMBIN_HDR_BYTES];
    hdr_to_bytes(&w->hdr, hdrb);
    if (fwrite(hdrb, 1, NMBIN_HDR_BYTES, w->fp) != NMBIN_HDR_BYTES) { fclose(w->fp); free(w); return -1; }

    uint8_t fb[NMBIN_FIELD_BYTES];
    for (uint32_t i = 0; i < NMBIN_MAX_FIELDS; i++) {
        if (i < w->hdr.n_fields) field_to_bytes(&w->fields[i], fb);
        else memset(fb, 0, sizeof(fb));
        if (fwrite(fb, 1, sizeof(fb), w->fp) != sizeof(fb)) { fclose(w->fp); free(w); return -1; }
    }
    fclose(w->fp);
    free(w);
    return 0;
}

void nmbin_abort(NMBinWriter *w)
{
    if (!w) return;
    if (w->fp) fclose(w->fp);
    remove(w->path);
    free(w);
}

/* ---------------------------------------------------------------------------
 * 读取端
 * ------------------------------------------------------------------------- */
int nmbin_open(const char *path, NMBinFile *out)
{
    memset(out, 0, sizeof(*out));
    size_t flen = 0;
    void *p = sk_mmap_file(path, &flen);      /* 平台层：mmap / CreateFileMapping */
    if (!p) return -1;

    bytes_to_hdr((const uint8_t *)p, &out->hdr);
    if (memcmp(out->hdr.magic, NMBIN_MAGIC, 8) != 0) { sk_munmap(p, flen); return -1; }
    const uint8_t *fp = (const uint8_t *)p + NMBIN_HDR_BYTES;
    for (uint32_t i = 0; i < out->hdr.n_fields && i < NMBIN_MAX_FIELDS; i++)
        bytes_to_field(fp + i * NMBIN_FIELD_BYTES, &out->fields[i]);

    out->data = (uint8_t *)p + NMBIN_HDR_BYTES + NMBIN_FIELD_BYTES * NMBIN_MAX_FIELDS;
    out->data_len = flen - (NMBIN_HDR_BYTES + NMBIN_FIELD_BYTES * NMBIN_MAX_FIELDS);
    out->mapped_len = flen;
    out->mapped = 1;
    return 0;
}

void nmbin_close(NMBinFile *f)
{
    if (f && f->mapped && f->data) sk_munmap(f->data - (NMBIN_HDR_BYTES + NMBIN_FIELD_BYTES * NMBIN_MAX_FIELDS),
                                             f->mapped_len);
    if (f) { f->data = NULL; f->mapped = 0; }
}

int nmbin_check(const NMBinFile *f, char *err, size_t err_cap)
{
#define ERR(...) do { snprintf(err, err_cap, __VA_ARGS__); return -1; } while (0)
    if (memcmp(f->hdr.magic, NMBIN_MAGIC, 8) != 0) ERR("magic 不对");
    if (f->hdr.version != NMBIN_VERSION) ERR("版本 %u 不支持", f->hdr.version);
    if (f->hdr.rec_size == 0) ERR("rec_size 为 0");
    if (f->hdr.payload_bytes != f->hdr.rec_count * (uint64_t)f->hdr.rec_size)
        ERR("payload_bytes %llu != rec_count %llu × rec_size %u",
            (unsigned long long)f->hdr.payload_bytes,
            (unsigned long long)f->hdr.rec_count, f->hdr.rec_size);
    if (f->data_len != f->hdr.payload_bytes)
        ERR("文件实际记录区 %zu 字节，头部声明 %llu 字节",
            f->data_len, (unsigned long long)f->hdr.payload_bytes);
    if (f->hdr.n_fields > NMBIN_MAX_FIELDS)
        ERR("n_fields %u 超过上限 %u", f->hdr.n_fields, (uint32_t)NMBIN_MAX_FIELDS);
    for (uint32_t i = 0; i < f->hdr.n_fields; i++) {
        const NMBinField *fd = &f->fields[i];
        if ((uint32_t)fd->offset + fd->width > f->hdr.rec_size)
            ERR("字段 %s 越界（offset %u + width %u > rec_size %u）",
                fd->name, fd->offset, fd->width, f->hdr.rec_size);
    }

    /* O1：文件自称的字段表必须与源码编译进去的硬编码 schema 逐项一致。
     * 只在加载期做一次，不进任何每记录循环。 */
    {
        char serr[256];
        if (nmbin_check_schema(f, serr, sizeof(serr)) != 0) ERR("%s", serr);
    }

    /* O2：只验摘要区**自洽**，不算哈希。全量哈希属边界动作，
     * 放在这里会强制 mmap 读满整个文件，破坏按需加载。 */
    if (f->hdr.flags & NMBIN_FLAG_DIGEST) {
        if (f->hdr.digest_algo != NMBIN_DIGEST_SHA256)
            ERR("内容摘要算法 %u 不支持", f->hdr.digest_algo);
        if (f->hdr.digest_ver != NMBIN_DIGEST_VER)
            ERR("内容摘要方案版本 %u 不支持", f->hdr.digest_ver);
        int allzero = 1;
        for (int i = 0; i < 32; i++) if (f->hdr.digest[i]) { allzero = 0; break; }
        if (allzero) ERR("声明含内容摘要，但摘要区全 0");
    }
    err[0] = '\0';
    return 0;
#undef ERR
}

/* O2 边界校验：只在下载完成 / 安装 / 离线工具里调用。
 * 0=摘要一致，1=摘要不一致（或长度对不上），-1=旧格式无摘要 / 打不开。 */
int nmbin_verify_digest(const char *path, char *err, size_t err_cap)
{
    NMBinFile f;
    int rc;
    if (err && err_cap) err[0] = '\0';
    if (nmbin_open(path, &f) != 0) {
        if (err && err_cap) snprintf(err, err_cap, "打不开 %s", path);
        return -1;
    }
    if (!(f.hdr.flags & NMBIN_FLAG_DIGEST)) {
        if (err && err_cap) snprintf(err, err_cap, "旧格式无摘要（flags=0，跳过内容校验）");
        rc = -1; goto out;
    }
    if (f.hdr.digest_algo != NMBIN_DIGEST_SHA256 || f.hdr.digest_ver != NMBIN_DIGEST_VER) {
        if (err && err_cap) snprintf(err, err_cap, "摘要算法/版本不支持");
        rc = 1; goto out;
    }
    if (f.data_len != f.hdr.payload_bytes) {
        if (err && err_cap) snprintf(err, err_cap, "记录区长度 %zu 与头部 %llu 不符（半包）",
                                     f.data_len, (unsigned long long)f.hdr.payload_bytes);
        rc = 1; goto out;
    }
    {
        Sha256 c;
        sha256_init(&c);
        /* 分块喂，避免一次大范围触碰让调用方误以为这是热路径操作。 */
        const uint8_t *p = f.data;
        size_t left = f.hdr.payload_bytes;
        while (left) {
            size_t n = left > (1u << 20) ? (size_t)(1u << 20) : left;
            sha256_update(&c, p, n);
            p += n; left -= n;
        }
        uint8_t got[32];
        sha256_final(&c, got);
        if (memcmp(got, f.hdr.digest, 32) != 0) {
            if (err && err_cap) snprintf(err, err_cap, "内容摘要不一致（位翻转/半包/篡改）");
            rc = 1;
        } else {
            rc = 0;
        }
    }
out:
    nmbin_close(&f);
    return rc;
}
