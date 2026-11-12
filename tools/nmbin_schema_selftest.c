/* skedaddle_nmbin_schema_selftest —— O1：期望 schema 一致性判据
 *
 * 用法：
 *   skedaddle_nmbin_schema_selftest [目录 ...]                 正向
 *   skedaddle_nmbin_schema_selftest --inject=all [目录 ...]    负向对照
 *   skedaddle_nmbin_schema_selftest --inject=offset           只跑一种注入
 *
 * 为什么需要它：nmbin_check() 原先只验「offset+width <= rec_size」这类
 * 结构自洽。把 edges.bin 字段表里 class 的 offset 从 14 改成 15 仍在界内，
 * 但源码里 EF_CLASS 是 14 —— 文件自称的布局与编译进去的常量已经不一致，
 * 旧判据却照样 PASS（NMBIN-OPTIMIZATION-2026-11-12.md §3.1 在内存里复现过）。
 *
 * 正向：遍历 nmbin_schema_count() 个登记组合各造一个干净夹具，nmbin_check
 *   必须全部通过；给了目录就再扫真实产物（不存在的/.idx/.db 自动跳过）。
 * 负向：故意制造「界内但语义错」的字段表，判据**必须**拒绝。
 *   ★ 负向模式先验证干净夹具仍 PASS，再验证注入被抓到 —— 只证明「能失败」
 *     不够，还得证明判据没有整体坏掉（双向判据）。
 *   ★ 改的是被检查的数据（夹具字段表），不是把判据条件改成常量 0。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map/binfmt.h"

static int fail = 0;
#define FAIL(...) do { printf("ASSERT-FAIL  "); printf(__VA_ARGS__); printf("\n"); fail++; } while (0)

/* 省图目录里可能存在的 NMBIN 文件；非 NMBIN（rev.bin / disp.idx 等）不列，
 * nmbin_open 靠 magic 再挡一层。用固定列表而不是目录遍历 ⇒ 跨平台零依赖。 */
static const char *k_files[] = {
    "nodes.bin", "edges.bin", "names.bin", "restrictions.bin",
    "lanes.bin", "lane_names.bin", "edgelist.bin", "tiles.bin",
    "lines.bin", "areas.bin", "polyrefs.bin", NULL
};

static void make_fixture(NMBinFile *f, const NMBinSchema *s, uint8_t *buf)
{
    memset(f, 0, sizeof(*f));
    memcpy(f->hdr.magic, NMBIN_MAGIC, 8);
    f->hdr.version       = NMBIN_VERSION;
    f->hdr.kind          = s->kind;
    f->hdr.rec_size      = s->rec_size;
    f->hdr.n_fields      = s->n_fields;
    f->hdr.rec_count     = 1;
    f->hdr.payload_bytes = s->rec_size;
    f->hdr.flags         = 0;          /* 旧格式，避开 O2 摘要分支 */
    snprintf(f->hdr.source, sizeof(f->hdr.source), "%s", "schema_selftest");
    for (uint32_t i = 0; i < s->n_fields; i++) f->fields[i] = s->fields[i];
    memset(buf, 0, s->rec_size);
    f->data     = buf;
    f->data_len = s->rec_size;
    f->mapped   = 0;                /* 内存夹具，不要 nmbin_close 去 munmap */
}

static int run_clean(void)
{
    int ok = 0;
    uint32_t n = nmbin_schema_count();
    for (uint32_t i = 0; i < n; i++) {
        const NMBinSchema *s = nmbin_schema_at(i);
        if (!s) { FAIL("nmbin_schema_at(%u) 返回 NULL", i); continue; }
        uint8_t buf[64];
        NMBinFile f;
        char err[256];
        make_fixture(&f, s, buf);
        if (nmbin_check(&f, err, sizeof(err)) != 0)
            FAIL("干净夹具 kind=%u rec_size=%u 被拒：%s", s->kind, s->rec_size, err);
        else ok++;
    }
    printf("  干净夹具    %d/%u 登记组合 nmbin_check PASS\n", ok, n);
    return ok;
}

/* 四种「界内但语义错」的注入，全部针对 edges.bin（rec_size=18）。 */
static int run_inject(const char *mode)
{
    const NMBinSchema *s = nmbin_expected_schema(NMBIN_KIND_EDGES, SK_EDGE_BYTES);
    if (!s) { FAIL("edges schema 未登记"); return 0; }

    struct { const char *name; int kind; } cases[] = {
        { "offset",  0 },   /* class offset 14→15，仍在 rec_size 内 */
        { "name",    1 },   /* 字段名改一个字符 */
        { "width",   2 },   /* width 1→2，仍在界内 */
        { "missing", 3 },   /* 少登记一个必需字段 */
    };
    int ncases = (int)(sizeof(cases) / sizeof(cases[0]));
    int tried = 0, caught = 0;

    for (int i = 0; i < ncases; i++) {
        if (strcmp(mode, "all") != 0 && strcmp(mode, cases[i].name) != 0) continue;
        tried++;
        uint8_t buf[64];
        NMBinFile f;
        char err[256];
        make_fixture(&f, s, buf);
        switch (cases[i].kind) {
        case 0: f.fields[4].offset = 15; break;
        case 1: f.fields[4].name[0] = 'X'; break;
        case 2: f.fields[4].width = 2; break;
        case 3: f.hdr.n_fields--; break;
        default: break;
        }
        /* 先确认这条注入确实没破坏结构自洽（否则抓到的是别的原因）。 */
        if (cases[i].kind == 0 || cases[i].kind == 2) {
            uint32_t off = f.fields[4].offset;
            uint8_t w = f.fields[4].width;
            if ((uint32_t)off + w > f.hdr.rec_size)
                FAIL("注入 %s 越界了，测不到 schema 判据（off=%u w=%u）",
                     cases[i].name, off, w);
        }
        if (nmbin_check(&f, err, sizeof(err)) == 0) {
            FAIL("注入 %s 未被抓到 ⇒ 判据无分辨力（%s）", cases[i].name,
                 "nmbin_check 仍返回 PASS");
        } else {
            caught++;
            printf("  注入 %-8s 被拒：%s\n", cases[i].name, err);
        }
    }
    if (tried == 0) { printf("未知注入模式: %s\n", mode); return 2; }
    printf("  注入判据    %d/%d 被加载期拒绝\n", caught, tried);
    if (caught != tried) FAIL("有 %d 种注入没被抓到", tried - caught);
    return 0;                      /* 具体成败由 fail 计数决定 */
}

static int scan_real(int ndirs, char **dirs)
{
    int n = 0;
    for (int d = 0; d < ndirs; d++) {
        for (int k = 0; k < (int)(sizeof(k_files) / sizeof(k_files[0])) - 1; k++) {
            char path[4096];
            snprintf(path, sizeof(path), "%s/%s", dirs[d], k_files[k]);
            NMBinFile f;
            if (nmbin_open(path, &f) != 0) continue;   /* 不存在或 magic 不符 */
            char err[256];
            if (nmbin_check(&f, err, sizeof(err)) != 0)
                FAIL("真实产物 %s 校验失败：%s", path, err);
            else n++;
            nmbin_close(&f);
        }
    }
    printf("  真实产物    %d 个 NMBIN 文件 nmbin_check PASS\n", n);
    return n;
}

int main(int argc, char **argv)
{
    const char *inject = NULL;
    char *dirs[64];
    int ndirs = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--inject=", 9) == 0) inject = argv[i] + 9;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "用法: %s [--inject=all|offset|name|width|missing] [目录 ...]\n", argv[0]);
            return 2;
        } else if (ndirs < (int)(sizeof(dirs) / sizeof(dirs[0]))) {
            dirs[ndirs++] = argv[i];
        }
    }

    if (inject) {
        /* 双向：干净夹具必须仍然 PASS，注入夹具必须 FAIL。 */
        uint32_t n = nmbin_schema_count();
        int ok = run_clean();
        if (ok != (int)n) FAIL("负向模式下干净夹具不该被拒（%d/%u）", ok, n);
        int rc = run_inject(inject);
        if (rc != 0) return rc;
        if (fail) { printf("SCHEMA-INJECT-FAIL (%d 项未达预期)\n", fail); return 1; }
        printf("SCHEMA-INJECT-CAUGHT 干净夹具全 PASS / 界内语义错误全部被加载期判据抓到\n");
        return 0;
    }

    int ok = run_clean();
    uint32_t n = nmbin_schema_count();
    if (ok != (int)n) FAIL("登记组合有 %u 个，只有 %d 个通过", n, ok);
    int real = scan_real(ndirs, dirs);
    if (ndirs && real == 0) FAIL("给了 %d 个目录却一个 NMBIN 都没扫到", ndirs);
    if (fail) { printf("SCHEMA-FAIL (%d 项未达预期)\n", fail); return 1; }
    printf("SCHEMA-OK 期望 schema 与硬编码常量逐项一致（夹具 %u 组合 / 真实产物 %d 文件）\n",
           n, real);
    return 0;
}
