# 第 6 课 · 软件光栅：线段裁剪、扫描线填充、alpha 混合
> 分类：核心代码

> 演化链第 6 步的完整可编译快照：`tutorial/mini/steps/step06_raster/`
> 源码讲义：[`docs/teaching/06_raster.md`](../../../../docs/teaching/06_raster.md)
> 同号独立小程序：`tutorial/06_raster`
> 真实工程落点：`src/render/raster.c`、`src/render/paint.c`

**这一课新增**：mini_clip_line / mini_polyfill / mini_blend，配确定性像素判据。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-06-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=7 ink=… gates=…/0`。

与上一步的源码差异：[`05_to_06.diff`](../../diffs/05_to_06.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step06.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
