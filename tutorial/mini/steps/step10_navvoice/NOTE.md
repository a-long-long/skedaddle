# 第 10 课 · 导航与语音：机动指令 + 15s 同文本冷却
> 分类：核心代码

> 演化链第 10 步的完整可编译快照：`tutorial/mini/steps/step10_navvoice/`
> 源码讲义：[`docs/teaching/10_navvoice.md`](../../../../docs/teaching/10_navvoice.md)
> 同号独立小程序：`tutorial/10_navvoice`
> 真实工程落点：`src/route/maneuver.c`、`src/route/voice.c`、`src/ui/voice_wire.c`

**这一课新增**：mini_nav_build / mini_nav_say，画车头三角。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-10-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=11 ink=… gates=…/0`。

与上一步的源码差异：[`09_to_10.diff`](../../diffs/09_to_10.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step10.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
