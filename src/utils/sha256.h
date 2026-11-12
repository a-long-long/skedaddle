#ifndef SKEDADDLE_SHA256_H
#define SKEDADDLE_SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * 最小 SHA-256（FIPS 180-4），纯 C、无外部依赖、可流式增量喂数据。
 *
 * 为什么自己实现而不是链 OpenSSL：本项目产物是离线分发的，验证端（含
 * Android JNI）不该为了一个摘要引入运行时库依赖；而且 O2 需要在写入端
 * 边写记录边算摘要（一次顺序扫描，零额外 IO），流式接口刚好合用。
 *
 * ★ 正确性不是靠"看着像"：tools/nmbin_verify.c 的 --selftest 用 FIPS
 *   标准向量 "abc" / "" / 百万个 'a' 对拍，任何实现错误会让判据直接红。
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t h[8];
    uint64_t bitlen;       /* 已压入完整 512 位块的位数 */
    uint8_t  block[64];
    size_t   blocklen;
} Sha256;

void sha256_init(Sha256 *c);
void sha256_update(Sha256 *c, const void *data, size_t n);
void sha256_final(Sha256 *c, uint8_t out[32]);   /* 调用后 c 不可再 update */

/* 一次性便利接口与十六进制输出（out 至少 65 字节）。 */
void sha256_buf(const void *data, size_t n, uint8_t out[32]);
void sha256_hex(const uint8_t digest[32], char out[65]);

#ifdef __cplusplus
}
#endif

#endif /* SKEDADDLE_SHA256_H */
