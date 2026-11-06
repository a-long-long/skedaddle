# 第 0 课 · 最小框架：像素缓冲、Bresenham 与 BMP 写出
> 分类：核心代码

> 演化链第 0 步的完整可编译快照：`tutorial/mini/steps/step00_frame/`
> 源码讲义：[`docs/teaching/00_overview.md`](../../../../docs/teaching/00_overview.md)
> 同号独立小程序：`tutorial/00_minimal`
> 真实工程落点：`src/render/raster.c`、`src/ui/offscreen_shot.c`

**这一课新增**：MiniCtx 像素缓冲、mini_line / mini_pp / mini_fill_rect、24 位 BMP、统一判据宏 MINI_ASSERT。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-00-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=1 ink=… gates=…/0`。

本步是演化链起点，没有上一步。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step00.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
