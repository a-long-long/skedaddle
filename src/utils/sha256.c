#include "utils/sha256.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

#define ROTR(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)  (ROTR(x, 2)  ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x)  (ROTR(x, 6)  ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7)  ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(Sha256 *c, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, d, e, f, g, h, t1, t2, cc;
    int i;

    for (i = 0; i < 16; i++) {
        const uint8_t *p = block + i * 4;
        w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
    }
    for (i = 16; i < 64; i++)
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];

    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + w[i];
        t2 = EP0(a) + MAJ(a, b, cc);
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void sha256_init(Sha256 *c)
{
    if (!c) return;
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->bitlen = 0;
    c->blocklen = 0;
    memset(c->block, 0, sizeof(c->block));
}

void sha256_update(Sha256 *c, const void *data, size_t n)
{
    if (!c || (!data && n)) return;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) {
        c->block[c->blocklen++] = p[i];
        if (c->blocklen == 64) {
            sha256_transform(c, c->block);
            c->bitlen += 512;
            c->blocklen = 0;
        }
    }
}

void sha256_final(Sha256 *c, uint8_t out[32])
{
    if (!c || !out) return;
    const uint64_t total_bits = c->bitlen + (uint64_t)c->blocklen * 8u;
    size_t i = c->blocklen;
    c->block[i++] = 0x80u;
    if (i > 56) {
        while (i < 64) c->block[i++] = 0;
        sha256_transform(c, c->block);
        i = 0;
    }
    while (i < 56) c->block[i++] = 0;
    for (int j = 0; j < 8; j++)
        c->block[56 + j] = (uint8_t)(total_bits >> (56 - 8 * j));
    sha256_transform(c, c->block);

    for (int j = 0; j < 8; j++) {
        out[j * 4 + 0] = (uint8_t)(c->h[j] >> 24);
        out[j * 4 + 1] = (uint8_t)(c->h[j] >> 16);
        out[j * 4 + 2] = (uint8_t)(c->h[j] >> 8);
        out[j * 4 + 3] = (uint8_t)(c->h[j]);
    }
}

void sha256_buf(const void *data, size_t n, uint8_t out[32])
{
    Sha256 c;
    sha256_init(&c);
    sha256_update(&c, data, n);
    sha256_final(&c, out);
}

void sha256_hex(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    if (!digest || !out) return;
    for (int i = 0; i < 32; i++) {
        out[i * 2 + 0] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15u];
    }
    out[64] = '\0';
}

#undef ROTR
#undef CH
#undef MAJ
#undef EP0
#undef EP1
#undef SIG0
#undef SIG1
