# 第 11 课 · 验收门禁：判据框架与负向对照
> 分类：核心代码

> 演化链第 11 步的完整可编译快照：`tutorial/mini/steps/step11_gates/`
> 源码讲义：[`docs/teaching/11_gates.md`](../../../../docs/teaching/11_gates.md)
> 同号独立小程序：`tutorial/11_gates`
> 真实工程落点：`scripts/regress.sh`、`src/ui/audit.c`

**这一课新增**：mini_gate_eval / mini_gate_negative，证明判据「能失败」。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-11-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=12 ink=… gates=…/0`。

与上一步的源码差异：[`10_to_11.diff`](../../diffs/10_to_11.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step11.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
