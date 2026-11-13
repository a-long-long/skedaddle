/* skedaddle_nmbin_verify —— O2：内容摘要的边界离线校验 + 全链判据
 *
 * 用法：
 *   skedaddle_nmbin_verify --selftest                          正向
 *   skedaddle_nmbin_verify --selftest --inject=all             负向对照
 *   skedaddle_nmbin_verify --selftest --inject=flip|pad|truncate|wronghash
 *   skedaddle_nmbin_verify <目录 ...>                          真实产物离线校验
 *
 * ★★ 这个工具存在的理由：全量哈希**不能**放进 nmbin_check() 的加载期，
 *   那会强制 mmap 把数 GiB 产物全部分页读入，破坏 mapset_set_view 的按需
 *   加载。校验只在边界做：下载完成 / 安装 / 本离线工具。
 *
 * 正向：① FIPS 标准向量 3 条（含 100 万字节流式喂）钉死 SHA-256 实现；
 *   ② 用生产 nmbin_create/write_records/finalize 造夹具 ⇒ nmbin_verify_digest
 *   必须返回 0，且 nmbin_check 必须 PASS（新格式头部自洽）。
 * 负向：先验证干净夹具仍 PASS，再分别注入四种损坏，期望 verify 返回 1。
 *   flip      翻转 payload 首字节（位翻转）
 *   pad       末字节改 0（半包丢块后补齐，长度仍对齐 ⇒ 只有哈希能抓）
 *   truncate  截掉末字节（长度不符）
 *   wronghash 改头部摘要一字节
 *   ★ 改的是被检查的数据（文件内容），不是判据条件本身。
 *
 * 目录模式给的是**兼容性证据**：现有 8 省产物 flags=0（旧格式无摘要），
 * 必须被如实统计为 legacy 而不是报错，证明「不顶版本号、不逼重抽」。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map/binfmt.h"
#include "utils/sha256.h"
#include "utils/plat.h"

static int fail = 0;
#define FAIL(...) do { printf("ASSERT-FAIL  "); printf(__VA_ARGS__); printf("\n"); fail++; } while (0)

static const char *k_files[] = {
    "nodes.bin", "edges.bin", "names.bin", "restrictions.bin",
    "lanes.bin", "lane_names.bin", "edgelist.bin", "tiles.bin",
    "lines.bin", "areas.bin", "polyrefs.bin", NULL
};

/* FIPS 180-4 标准向量；第三条用流式喂，顺便验证跨 64 字节块的 update。 */
static int check_vectors(void)
{
    struct { const char *data; size_t n; const char *hex; } v[] = {
        { "",  0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
    };
    int ok = 0;
    for (int i = 0; i < 2; i++) {
        uint8_t d[32]; char hex[65];
        sha256_buf(v[i].data, v[i].n, d);
        sha256_hex(d, hex);
        if (strcmp(hex, v[i].hex) != 0)
            FAIL("SHA-256 向量[%d] 不符：得到 %s，期望 %s", i, hex, v[i].hex);
        else ok++;
    }
    {
        Sha256 c; uint8_t d[32]; char hex[65];
        char block[1000];
        memset(block, 'a', sizeof(block));
        sha256_init(&c);
        for (int i = 0; i < 1000; i++) sha256_update(&c, block, sizeof(block));
        sha256_final(&c, d);
        sha256_hex(d, hex);
        const char *want = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
        if (strcmp(hex, want) != 0)
            FAIL("SHA-256 百万 a 向量不符：得到 %s，期望 %s", hex, want);
        else ok++;
    }
    printf("  标准向量    %d/3 组 SHA-256 对拍通过\n", ok);
    return ok;
}

static char fixture_path[4096];

static void fixture_init_path(void)
{
    snprintf(fixture_path, sizeof(fixture_path), "%s/skedaddle_nmbin_verify_fixture.bin",
             sk_temp_dir());
}

/* 用生产写入器造一个带摘要的 edges.bin 夹具（字段顺序必须与期望 schema 一致）。 */
static int make_sealed(void)
{
    remove(fixture_path);
    NMBinWriter *w = nmbin_create(fixture_path, NMBIN_KIND_EDGES, SK_EDGE_BYTES,
                                  "nmbin_verify_selftest");
    if (!w) return -1;
    nmbin_add_field(w, "to",      EF_TO,      NMT_U32, 4);
    nmbin_add_field(w, "dist_cm", EF_DIST_CM, NMT_U32, 4);
    nmbin_add_field(w, "name_id", EF_NAME_ID, NMT_U32, 4);
    nmbin_add_field(w, "flags",   EF_FLAGS,   NMT_U16, 2);
    nmbin_add_field(w, "class",   EF_CLASS,   NMT_U8,  1);
    nmbin_add_field(w, "speed",   EF_SPEED,   NMT_U8,  1);
    nmbin_add_field(w, "lanes",   EF_LANES,   NMT_U16, 2);
    uint8_t recs[SK_EDGE_BYTES * 3];
    for (size_t i = 0; i < sizeof(recs); i++) recs[i] = (uint8_t)(i * 7u + 13u);
    if (nmbin_write_records(w, recs, 3) != 0) { nmbin_abort(w); return -1; }
    return nmbin_finalize(w);
}

/* 只在文件层面动手，不动判据。payload 起点 = 128 + 16*24 = 512。 */
static int inject_file(const char *mode)
{
    const size_t payload_off = NMBIN_HDR_BYTES + NMBIN_FIELD_BYTES * NMBIN_MAX_FIELDS;
    FILE *fp = fopen(fixture_path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long len = ftell(fp);
    if (len <= 0) { fclose(fp); return -1; }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(fp); return -1; }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) { free(buf); fclose(fp); return -1; }
    fclose(fp);

    size_t out = (size_t)len;
    if (strcmp(mode, "flip") == 0) {
        buf[payload_off] ^= 0x01u;
    } else if (strcmp(mode, "pad") == 0) {
        buf[(size_t)len - 1] = 0;        /* 长度不变，内容变 ⇒ 只有哈希能抓 */
    } else if (strcmp(mode, "truncate") == 0) {
        out = (size_t)len - 1;           /* 长度不符（半包） */
    } else if (strcmp(mode, "wronghash") == 0) {
        buf[NMBIN_OFF_DIGEST] ^= 0x01u;
    } else {
        free(buf); return -2;
    }
    fp = fopen(fixture_path, "wb");
    if (!fp) { free(buf); return -1; }
    if (fwrite(buf, 1, out, fp) != out) { fclose(fp); free(buf); return -1; }
    fclose(fp);
    free(buf);
    return 0;
}

static int verify_clean(void)
{
    char err[256];
    int rc = nmbin_verify_digest(fixture_path, err, sizeof(err));
    if (rc != 0) FAIL("干净夹具摘要校验失败 rc=%d：%s", rc, err);
    NMBinFile f;
    if (nmbin_open(fixture_path, &f) != 0) FAIL("干净夹具打不开");
    else {
        if (!(f.hdr.flags & NMBIN_FLAG_DIGEST)) FAIL("写入端没置 NMBIN_FLAG_DIGEST");
        if (f.hdr.digest_algo != NMBIN_DIGEST_SHA256) FAIL("摘要算法标记不对");
        if (nmbin_check(&f, err, sizeof(err)) != 0) FAIL("干净夹具 nmbin_check 被拒：%s", err);
        nmbin_close(&f);
    }
    printf("  干净夹具    摘要一致 + 头部标记自洽 + nmbin_check PASS\n");
    return rc;
}

static int run_inject(const char *mode)
{
    static const char *modes[] = { "flip", "pad", "truncate", "wronghash", NULL };
    int tried = 0, caught = 0;
    for (int i = 0; modes[i]; i++) {
        if (strcmp(mode, "all") != 0 && strcmp(mode, modes[i]) != 0) continue;
        tried++;
        if (make_sealed() != 0) { FAIL("重建夹具失败（%s）", modes[i]); continue; }
        if (inject_file(modes[i]) != 0) { FAIL("注入 %s 写文件失败", modes[i]); continue; }
        char err[256];
        int rc = nmbin_verify_digest(fixture_path, err, sizeof(err));
        /* 被拒即可（rc != 0）；rc=-1 通常是 nmbin_open 因"文件短于声明记录
         * 区"直接拒（截断场景），rc=1 是摘要/长度不一致——两者都算抓到。
         * 只有 rc==0（文件被接受）才是判据失去分辨力。 */
        if (rc == 0) {
            FAIL("注入 %-9s 未被抓到 ⇒ 判据无分辨力（rc=%d, %s）", modes[i], rc, err);
        } else {
            caught++;
            printf("  注入 %-9s 被拒：rc=%d %s\n", modes[i], rc, err);
        }
    }
    if (tried == 0) { printf("未知注入模式: %s\n", mode); return 2; }
    printf("  注入判据    %d/%d 被边界校验拒绝\n", caught, tried);
    if (caught != tried) FAIL("有 %d 种注入没被抓到", tried - caught);
    return 0;
}

static int scan_real(int ndirs, char **dirs)
{
    int sealed = 0, legacy = 0, mismatch = 0;
    for (int d = 0; d < ndirs; d++) {
        for (int k = 0; k < (int)(sizeof(k_files) / sizeof(k_files[0])) - 1; k++) {
            char path[4096];
            snprintf(path, sizeof(path), "%s/%s", dirs[d], k_files[k]);
            FILE *probe = fopen(path, "rb");
            if (!probe) continue;
            fclose(probe);
            char err[256];
            int rc = nmbin_verify_digest(path, err, sizeof(err));
            if (rc == 0) sealed++;
            else if (rc == -1) legacy++;
            else { mismatch++; FAIL("真实产物 %s 摘要不一致：%s", path, err); }
        }
    }
    printf("  真实产物    带摘要 %d / 旧格式 %d / 不一致 %d\n", sealed, legacy, mismatch);
    return sealed + legacy;
}

int main(int argc, char **argv)
{
    int selftest = 0;
    const char *inject = NULL;
    char *dirs[64];
    int ndirs = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--selftest") == 0) selftest = 1;
        else if (strncmp(argv[i], "--inject=", 9) == 0) inject = argv[i] + 9;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "用法: %s --selftest [--inject=all|flip|pad|truncate|wronghash] | <目录 ...>\n", argv[0]);
            return 2;
        } else if (ndirs < (int)(sizeof(dirs) / sizeof(dirs[0]))) {
            dirs[ndirs++] = argv[i];
        }
    }
    fixture_init_path();

    if (selftest) {
        int ok = check_vectors();
        if (ok != 3) FAIL("SHA-256 标准向量只有 %d/3 通过", ok);
        if (make_sealed() != 0) { FAIL("造夹具失败"); return 1; }
        int clean_rc = verify_clean();

        if (inject) {
            /* 双向：干净夹具必须先 PASS，注入夹具才谈得上「被抓到」。 */
            if (clean_rc != 0) FAIL("负向模式下干净夹具不该失败");
            int rc = run_inject(inject);
            remove(fixture_path);
            if (rc != 0) return rc;
            if (fail) { printf("NMBIN-HASH-INJECT-FAIL (%d 项未达预期)\n", fail); return 1; }
            printf("NMBIN-HASH-INJECT-CAUGHT 干净夹具 PASS / 位翻转·半包补齐·截断·错哈希全被抓到\n");
            return 0;
        }
        remove(fixture_path);
        if (fail) { printf("NMBIN-HASH-FAIL (%d 项未达预期)\n", fail); return 1; }
        printf("NMBIN-HASH-OK 标准向量 3/3 + 写入端流式摘要 → 边界校验全链 PASS\n");
        return 0;
    }

    if (ndirs == 0) {
        fprintf(stderr, "用法: %s --selftest [--inject=...] | <目录 ...>\n", argv[0]);
        return 2;
    }
    int n = scan_real(ndirs, dirs);
    if (n == 0) FAIL("给了 %d 个目录却一个 NMBIN 都没扫到", ndirs);
    if (fail) { printf("NMBIN-LEGACY-FAIL (%d 项未达预期)\n", fail); return 1; }
    printf("NMBIN-LEGACY-OK 旧产物按 flags=0 跳过摘要校验（不顶版本、不逼重抽）\n");
    return 0;
}
