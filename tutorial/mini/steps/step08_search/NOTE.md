# 第 8 课 · 地名搜索：查询串 → 命中下标
> 分类：核心代码

> 演化链第 8 步的完整可编译快照：`tutorial/mini/steps/step08_search/`
> 源码讲义：[`docs/teaching/08_search.md`](../../../../docs/teaching/08_search.md)
> 同号独立小程序：`tutorial/08_search`
> 真实工程落点：`src/search/search.c`

**这一课新增**：mini_search 子串匹配 + 命中锚点绘制。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-08-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=9 ink=… gates=…/0`。

与上一步的源码差异：[`07_to_08.diff`](../../diffs/07_to_08.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step08.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
