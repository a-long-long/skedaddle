# 第 4 课 · 建图：节点与边 → 前向星邻接表
> 分类：核心代码

> 演化链第 4 步的完整可编译快照：`tutorial/mini/steps/step04_graph/`
> 源码讲义：[`docs/teaching/04_graph.md`](../../../../docs/teaching/04_graph.md)
> 同号独立小程序：`tutorial/04_graph`
> 真实工程落点：`src/map/graph_build.c`、`src/map/road_tags.c`

**这一课新增**：mini_graph_build / mini_graph_deg / mini_graph_reach，画出玩具路网。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-04-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=5 ink=… gates=…/0`。

与上一步的源码差异：[`03_to_04.diff`](../../diffs/03_to_04.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step04.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
