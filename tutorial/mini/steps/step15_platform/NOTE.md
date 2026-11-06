# 第 15 课 · 平台抽象层：差异收拢到一处
> 分类：跨平台代码

> 演化链第 15 步的完整可编译快照：`tutorial/mini/steps/step15_platform/`
> 源码讲义：[`docs/teaching/15_platform.md`](../../../../docs/teaching/15_platform.md)
> 同号独立小程序：`tutorial/15_platform`
> 真实工程落点：`src/utils/plat.c`、`src/utils/plat.h`

**这一课新增**：mini_plat_sep / join / font_dir / now_ms —— 全链唯一允许 #if defined(_WIN32) 的文件。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-15-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=16 ink=… gates=…/0`。

与上一步的源码差异：[`14_to_15.diff`](../../diffs/14_to_15.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step15.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
