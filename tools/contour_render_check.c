/* skedaddle_contour_render_check —— 等高线三件套的「加载 + 绘制」判据
 *
 * 输入一个目录（里面有 contour_contours.bin / contour_contour_polyrefs.bin /
 * contour_contour_pts.bin），用生产代码 nmbin_open + paint_contours 画到一块
 * 内存画布上，打印记录数与「非背景像素数」。
 *
 * ★ 这是 DEM 链路的最后一环：生成器写出来的文件能不能被 mapset 的 NMBinFile
 *   读、能不能真的画出像素，光数文件字节数是证明不了的。
 * ★ font 传 NULL：paint_contours 只在有字体时画高程数字，本判据只数线条像素。
 *
 * 输出固定两行（scripts/contour_chain_check.py 解析）：
 *   CONTOUR-RECORDS <contours> <polyrefs> <pts>
 *   CONTOUR-PIXELS <n>
 * 返回 0 = 三件套都能打开并画完；1 = 打不开；2 = 参数错。
 */

#include "map/binfmt.h"
#include "map/map_view.h"
#include "render/paint.h"
#include "render/raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    const char *dir = argv[1];
    char pc[1024], pp[1024], pt[1024];
    snprintf(pc, sizeof(pc), "%s/contour_contours.bin", dir);
    snprintf(pp, sizeof(pp), "%s/contour_contour_polyrefs.bin", dir);
    snprintf(pt, sizeof(pt), "%s/contour_contour_pts.bin", dir);

    NMBinFile c, p, t;
    if (nmbin_open(pc, &c) != 0 || nmbin_open(pp, &p) != 0 ||
        nmbin_open(pt, &t) != 0) {
        printf("CONTOUR-RECORDS -1 -1 -1\nCONTOUR-PIXELS -1\n");
        return 1;
    }
    printf("CONTOUR-RECORDS %llu %llu %llu\n",
           (unsigned long long)c.hdr.rec_count,
           (unsigned long long)p.hdr.rec_count,
           (unsigned long long)t.hdr.rec_count);

    const int w = 400, h = 300;
    uint32_t *buf = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
    if (!buf) { printf("CONTOUR-PIXELS -1\n"); return 1; }
    Raster rs;
    raster_bind_mem(&rs, buf, w, h, (size_t)w,
                    0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    uint32_t bg = raster_rgb(&rs, 250, 250, 250);   /* 与 paint 背景一致的浅底 */
    for (int i = 0; i < w * h; i++) buf[i] = bg;

    MapView v;
    mv_init(&v, 39.4, 115.4, 12.0, w, h);
    PaintStats st; memset(&st, 0, sizeof(st));
    int32_t *bbox = NULL;
    paint_contours(&rs, &v, &c, &p, &t, &bbox, &st, NULL);

    long n = 0;
    for (int i = 0; i < w * h; i++) if (buf[i] != bg) n++;
    printf("CONTOUR-PIXELS %ld\n", n);

    free(bbox);
    free(buf);
    nmbin_close(&c); nmbin_close(&p); nmbin_close(&t);
    return 0;
}
