/* tutorial/mini · 第 10 课新增：导航层与语音播报（含 15s 同文本冷却）
 *
 * 对照 src/route/maneuver.c、src/route/navsim.c、src/route/voice.c、
 * src/ui/voice_wire.c。真实工程里「到达帧先播报后置 paused」的顺序也在这里体现。
 */
#ifndef MINI_STEP10_H
#define MINI_STEP10_H

#include <stdio.h>
#include <string.h>

#define MINI_NAV_TEXT 48

static inline int mini_nav_build(const int *path, int n, char out[][MINI_NAV_TEXT]) {
    int k = 0;
    if (n < 1) return 0;
    snprintf(out[k++], MINI_NAV_TEXT, "出发");
    for (int i = 1; i + 1 < n; i++)
        snprintf(out[k++], MINI_NAV_TEXT, "直行 至 %d 号点", path[i]);
    snprintf(out[k++], MINI_NAV_TEXT, "到达目的地");
    return k;
}

/* 唯一发声出口的语义等价物：同一文本 15 秒内不重复 */
static inline int mini_nav_say(const char *text, int now_ms) {
    static char last[64] = "";
    static int last_ms = -16000;
    if (strcmp(last, text) == 0 && now_ms - last_ms < 15000) return 0;
    snprintf(last, sizeof last, "%s", text);
    last_ms = now_ms;
    return 1;
}

static void mini_step_10(MiniCtx *c) {
    int path[MINI_GN], total = -1;
    int n = mini_route(0, 3, path, MINI_GN, &total);
    char txt[8][MINI_NAV_TEXT];
    int k = mini_nav_build(path, n, txt);
    MINI_ASSERT(c, k == 3, "step10: 路径生成 3 条机动指令");
    int a = mini_nav_say(txt[0], 1000);
    int b = mini_nav_say(txt[0], 1500);
    int d = mini_nav_say(txt[1], 1600);
    int e = mini_nav_say(txt[0], 17000);
    MINI_ASSERT(c, a == 1 && b == 0, "step10: 15s 同文本冷却挡住重复播报");
    MINI_ASSERT(c, d == 1 && e == 1, "step10: 换文本 / 冷却后可再播");
    /* 追加：把车头画成三角形（用第 6 课的扫描线填充） */
    int ex = g_mini_gx[path[n - 1]], ey = g_mini_gy[path[n - 1]];
    int xs[3] = { ex, ex - 7, ex + 7 }, ys[3] = { ey - 9, ey + 5, ey + 5 };
    mini_polyfill(c, xs, ys, 3, 0x00E04030u);
    printf("MINI-STEP-10-OK  maneuvers=%d ink=%ld\n", k, mini_ink(c));
    c->step_count++;
}

#endif /* MINI_STEP10_H */
