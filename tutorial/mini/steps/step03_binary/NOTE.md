# 第 3 课 · 自描述二进制：magic + section 表
> 分类：核心代码

> 演化链第 3 步的完整可编译快照：`tutorial/mini/steps/step03_binary/`
> 源码讲义：[`docs/teaching/03_binary.md`](../../../../docs/teaching/03_binary.md)
> 同号独立小程序：`tutorial/03_binary`
> 真实工程落点：`src/map/binfmt.c`、`src/map/mapset.c`

**这一课新增**：mini_bin_build / mini_bin_locate，按 tag 定位定长数组。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-03-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=4 ink=… gates=…/0`。

与上一步的源码差异：[`02_to_03.diff`](../../diffs/02_to_03.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step03.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
