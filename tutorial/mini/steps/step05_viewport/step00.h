/* tutorial/mini · 第 0 课新增：最小框架（像素缓冲 + 光栅原语 + BMP 写出）
 *
 * 对照 src/render/raster.c（画线原语）、src/ui/offscreen_shot.c（无头出图）。
 * 这一步只建立「画布」，后面 17 课都在这块缓冲上追加，绝不重涂背景。
 */
#ifndef MINI_STEP00_H
#define MINI_STEP00_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINI_W 320
#define MINI_H 240
#define MINI_BG 0x00F4F1EAu          /* 米白，与项目 paint_background 同色 */

typedef struct {
    uint32_t px[MINI_W * MINI_H];    /* 0x00RRGGBB，与 offscreen_shot.c 同约定 */
    int w, h;
    uint32_t bg;
    int step_count;                  /* 已执行的课数，必须等于当前步号 + 1 */
    int gates_ok, gates_fail;        /* 内置判据通过 / 失败计数 */
    int lines, polys, labels;        /* 确定性绘制计数，不用时间当判据 */
} MiniCtx;

/* 统一判据宏：每课至少新增一条，gates_ok 必须随课号严格递增 */
#define MINI_ASSERT(c, cond, name) do {                                      \
    if (cond) { (c)->gates_ok++;  printf("  GATE-OK   %s\n", (name)); }     \
    else      { (c)->gates_fail++; printf("  GATE-FAIL %s\n", (name)); }    \
} while (0)

static inline void mini_ctx_init(MiniCtx *c) {
    memset(c, 0, sizeof(*c));
    c->w = MINI_W; c->h = MINI_H; c->bg = MINI_BG;
    for (int i = 0; i < c->w * c->h; i++) c->px[i] = c->bg;
}

static inline void mini_pp(MiniCtx *c, int x, int y, uint32_t col) {
    if (x < 0 || x >= c->w || y < 0 || y >= c->h) return;
    c->px[y * c->w + x] = col;
}

/* Bresenham：项目 raster_line 的同源思路，真实版另加裁剪与 alpha */
static inline void mini_line(MiniCtx *c, int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        mini_pp(c, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    c->lines++;
}

static inline void mini_fill_rect(MiniCtx *c, int x, int y, int w, int h, uint32_t col) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) mini_pp(c, i, j, col);
    c->polys++;
}

static inline uint32_t mini_at(const MiniCtx *c, int x, int y) {
    if (x < 0 || x >= c->w || y < 0 || y >= c->h) return c->bg;
    return c->px[y * c->w + x];
}

/* 非背景像素数：确定性度量，证明「每课真的往图上加了东西」 */
static inline long mini_ink(const MiniCtx *c) {
    long n = 0;
    for (int i = 0; i < c->w * c->h; i++) if (c->px[i] != c->bg) n++;
    return n;
}

/* 24 位 BMP：行自下而上、BGR、4 字节对齐，同 offscreen_shot.c:write_bmp24 */
static inline int mini_write_bmp(const MiniCtx *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int row_bytes = c->w * 3;
    int pad = (4 - (row_bytes & 3)) & 3;
    int data_size = (row_bytes + pad) * c->h;
    int fs = 54 + data_size;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &fs, 4);
    uint32_t off = 54, hs = 40;
    memcpy(hdr + 10, &off, 4);
    memcpy(hdr + 14, &hs, 4);
    int32_t ww = c->w, hh = c->h;
    memcpy(hdr + 18, &ww, 4);
    memcpy(hdr + 22, &hh, 4);
    uint16_t planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &data_size, 4);
    fwrite(hdr, 1, 54, f);
    uint8_t *row = (uint8_t *)malloc((size_t)row_bytes + 4);
    if (!row) { fclose(f); return -1; }
    for (int y = c->h - 1; y >= 0; y--) {
        int p = 0;
        for (int x = 0; x < c->w; x++) {
            uint32_t v = c->px[y * c->w + x];
            row[p++] = (uint8_t)(v & 0xFF);
            row[p++] = (uint8_t)((v >> 8) & 0xFF);
            row[p++] = (uint8_t)((v >> 16) & 0xFF);
        }
        for (int i = 0; i < pad; i++) row[p++] = 0;
        fwrite(row, 1, (size_t)p, f);
    }
    free(row);
    fclose(f);
    return 0;
}

static void mini_step_00(MiniCtx *c) {
    mini_line(c, 0, c->h / 2, c->w - 1, c->h / 2, 0x002A2A2Au);
    mini_line(c, c->w / 2, 0, c->w / 2, c->h - 1, 0x002A2A2Au);
    mini_line(c, 0, 0, c->w - 1, c->h - 1, 0x001F6FB0u);
    MINI_ASSERT(c, mini_ink(c) > 0, "step00: 缓冲上真的有像素");
    MINI_ASSERT(c, c->lines == 3, "step00: 恰好三条线");
    printf("MINI-STEP-00-OK  frame=%dx%d ink=%ld\n", c->w, c->h, mini_ink(c));
    c->step_count++;
}

#endif /* MINI_STEP00_H */
