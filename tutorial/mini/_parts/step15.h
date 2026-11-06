/* tutorial/mini · 第 15 课新增：平台抽象层（跨平台段第 1 课）
 *
 * 对照 src/utils/plat.c。★ 整个 mini 工程里，只有本文件允许出现
 * `#if defined(_WIN32)` —— 「平台差异收拢到一处」由 scripts/check_mini_chain.py
 * 机械验收；其余 17 课的业务代码必须零平台宏。
 */
#ifndef MINI_STEP15_H
#define MINI_STEP15_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline char mini_plat_sep(void) {
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

static inline int mini_plat_join(char *out, size_t cap, const char *a, const char *b) {
    return snprintf(out, cap, "%s%c%s", a, mini_plat_sep(), b);
}

/* 字体目录优先级：env SKEDADDLE_FONT_DIR > 内置 fonts/ */
static inline const char *mini_plat_font_dir(void) {
    const char *e = getenv("SKEDADDLE_FONT_DIR");
    return (e && *e) ? e : "fonts";
}

static inline long mini_plat_now_ms(void) {
    return (long)((double)clock() * 1000.0 / CLOCKS_PER_SEC);
}

static void mini_step_15(MiniCtx *c) {
    char p[64];
    int n = mini_plat_join(p, sizeof p, "products", "graph");
    char expect[64];
    snprintf(expect, sizeof expect, "products%cgraph", mini_plat_sep());
    MINI_ASSERT(c, n > 0 && strcmp(p, expect) == 0, "step15: 路径拼接跟随平台分隔符");
    MINI_ASSERT(c, mini_plat_font_dir()[0] != '\0', "step15: 字体目录有兜底");
    MINI_ASSERT(c, mini_plat_now_ms() >= 0, "step15: 单调时钟桩可用（时间只作兜底）");
    /* 追加：画一条「平台层」隔离带，示意核心与外壳之间只此一层 */
    mini_line(c, 0, 18, c->w - 1, 18, 0x007A5CA8u);
    printf("MINI-STEP-15-OK  sep='%c' font_dir='%s' ink=%ld\n",
           mini_plat_sep(), mini_plat_font_dir(), mini_ink(c));
    c->step_count++;
}

#endif /* MINI_STEP15_H */
