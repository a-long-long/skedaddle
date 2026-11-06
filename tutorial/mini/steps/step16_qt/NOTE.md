# 第 16 课 · Qt 外壳：换壳不换核心 + alpha=0xFF 修正
> 分类：跨平台代码

> 演化链第 16 步的完整可编译快照：`tutorial/mini/steps/step16_qt/`
> 源码讲义：[`docs/teaching/16_qt.md`](../../../../docs/teaching/16_qt.md)
> 同号独立小程序：`tutorial/16_qt`
> 真实工程落点：`src/ui_qt/map_canvas.cpp`、`src/ui/offscreen_shot.c`

**这一课新增**：mini_front_render 把 0x00RRGGBB 补成 ARGB8888，复刻 Wayland 透明花屏修复。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-16-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=17 ink=… gates=…/0`。

与上一步的源码差异：[`15_to_16.diff`](../../diffs/15_to_16.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step16.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
