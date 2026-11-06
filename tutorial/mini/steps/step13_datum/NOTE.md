# 第 13 课 · 大地基准：只换椭球不等于基准转换
> 分类：核心代码

> 演化链第 13 步的完整可编译快照：`tutorial/mini/steps/step13_datum/`
> 源码讲义：[`docs/teaching/13_datum.md`](../../../../docs/teaching/13_datum.md)
> 同号独立小程序：`tutorial/13_datum`
> 真实工程落点：`src/utils/datum.c`

**这一课新增**：MiniDatum 表 + mini_datum_shift，平移前后两点对照。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-13-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=14 ink=… gates=…/0`。

与上一步的源码差异：[`12_to_13.diff`](../../diffs/12_to_13.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step13.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
