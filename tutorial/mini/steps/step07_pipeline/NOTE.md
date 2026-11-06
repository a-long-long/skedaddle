# 第 7 课 · 渲染管线：一帧的 9 个阶段
> 分类：核心代码

> 演化链第 7 步的完整可编译快照：`tutorial/mini/steps/step07_pipeline/`
> 源码讲义：[`docs/teaching/07_pipeline.md`](../../../../docs/teaching/07_pipeline.md)
> 同号独立小程序：`tutorial/07_pipeline`
> 真实工程落点：`src/ui/window.c`、`src/render/paint.c`、`src/ui/chrome.c`

**这一课新增**：mini_paint_frame 把前面 7 课编成固定顺序的 9 阶段，阶段号可验证。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-07-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=8 ink=… gates=…/0`。

与上一步的源码差异：[`06_to_07.diff`](../../diffs/06_to_07.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step07.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
