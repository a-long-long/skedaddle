/* tutorial/mini · 第 12 课新增：综合闭环（改一个点 → 加一条判据 → 负向对照）
 *
 * 对照 tutorial/12_capstone/、scripts/regress.sh 的「改 → 测 → 守」三步法。
 * 与前几课不同，这一课不新增模块，只把第 11 课的判据框架用起来。
 */
#ifndef MINI_STEP12_H
#define MINI_STEP12_H

/* 新判据：改动后 15 必须落进低类（用它锁住 THRESH 的修改） */
static inline int mini_gate_lock_change(int x) { return x < g_mini_thresh; }

static void mini_step_12(MiniCtx *c) {
    int pass = 0;
    int old = g_mini_thresh;
    g_mini_thresh = 20;                                    /* ① 改一个点 */
    int locked = mini_gate_eval(mini_gate_lock_change, 15,
                                "classify(15) 属低类(新判据)", &pass);
    g_mini_thresh = old;                                   /* ② 负向对照：改回去 */
    int neg = mini_gate_negative(mini_gate_lock_change, 15,
                                 "改回 10 后新判据应 FAIL");
    g_mini_thresh = 20;                                    /* ③ 恢复改动 */
    MINI_ASSERT(c, locked == 1 && pass == 1, "step12: 新判据锁住改动");
    MINI_ASSERT(c, neg == 1 && g_mini_thresh == 20, "step12: 负向对照证明判据有效");
    /* 追加：改动前 / 改动后的阈值条，上下两行对照 */
    for (int x = 0; x < 100; x++) {
        mini_fill_rect(c, 10 + x * 2, 230, 2, 3,
                       x >= 10 ? 0x00D04040u : 0x004080C0u);
        mini_fill_rect(c, 10 + x * 2, 235, 2, 3,
                       x >= 20 ? 0x00D04040u : 0x004080C0u);
    }
    printf("MINI-STEP-12-OK  closed-loop=3/3 ink=%ld\n", mini_ink(c));
    c->step_count++;
}

#endif /* MINI_STEP12_H */
