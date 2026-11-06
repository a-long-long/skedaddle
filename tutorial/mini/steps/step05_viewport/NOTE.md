# 第 5 课 · 视口：可见 bbox、屏幕投影与瓦片键
> 分类：核心代码

> 演化链第 5 步的完整可编译快照：`tutorial/mini/steps/step05_viewport/`
> 源码讲义：[`docs/teaching/05_viewport.md`](../../../../docs/teaching/05_viewport.md)
> 同号独立小程序：`tutorial/05_viewport`
> 真实工程落点：`src/map/map_view.c`、`src/map/tileidx.c`

**这一课新增**：MiniView / mini_view_bbox / mini_view_screen / mini_tile_key，追加视口边框。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-05-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=6 ink=… gates=…/0`。

与上一步的源码差异：[`04_to_05.diff`](../../diffs/04_to_05.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step05.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
