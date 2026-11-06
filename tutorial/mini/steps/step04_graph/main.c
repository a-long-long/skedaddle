/* tutorial/mini · main.c — 演化链的固定骨架（第 0–17 课共用）
 *
 * ★ 这是「同一份工程逐课长大」的关键：18 个 stepNN 快照的 main.c 之间，
 *   只允许在 @@INCLUDE@@ / @@CALL@@ 两个锚点【之前插入行】，
 *   任何已有行都不许删除或改写。scripts/check_mini_chain.py 用
 *   「旧文件的行序列是新文件行序列的子序列」这条判据机械守住它。
 *
 * 编译运行（在任一个 stepNN 快照目录下）：
 *     gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
 * 会写出 mini_out.bmp，并打印 MINI-STEP-*-OK / MINI-CHAIN-OK。
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "step00.h"
#include "step01.h"
#include "step02.h"
#include "step03.h"
#include "step04.h"
/* @@INCLUDE@@ */

int main(void) {
    MiniCtx c;
    mini_ctx_init(&c);
    mini_step_00(&c);
    mini_step_01(&c);
    mini_step_02(&c);
    mini_step_03(&c);
    mini_step_04(&c);
    /* @@CALL@@ */
    if (mini_write_bmp(&c, "mini_out.bmp") != 0) {
        fprintf(stderr, "mini: write bmp failed\n");
        return 1;
    }
    printf("MINI-CHAIN-OK steps=%d ink=%ld gates=%d/%d\n",
           c.step_count, mini_ink(&c), c.gates_ok, c.gates_fail);
    return c.gates_fail ? 1 : 0;
}
