# 第 9 课 · 跨省路由的骨架：Dijkstra 与路径回溯
> 分类：核心代码

> 演化链第 9 步的完整可编译快照：`tutorial/mini/steps/step09_routing/`
> 源码讲义：[`docs/teaching/09_routing.md`](../../../../docs/teaching/09_routing.md)
> 同号独立小程序：`tutorial/09_routing`
> 真实工程落点：`src/route/route.c`、`src/route/lmtable.c`

**这一课新增**：mini_route 在前向星上松弛、回溯，画出亮蓝最短路。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-09-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=10 ink=… gates=…/0`。

与上一步的源码差异：[`08_to_09.diff`](../../diffs/08_to_09.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step09.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
