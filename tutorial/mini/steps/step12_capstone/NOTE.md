# 第 12 课 · 综合闭环：改一个点 + 加一条判据 + 负向对照
> 分类：核心代码

> 演化链第 12 步的完整可编译快照：`tutorial/mini/steps/step12_capstone/`
> 源码讲义：[`docs/teaching/12_capstone.md`](../../../../docs/teaching/12_capstone.md)
> 同号独立小程序：`tutorial/12_capstone`
> 真实工程落点：`scripts/regress.sh`、`scripts/bmpdiff.py`

**这一课新增**：不新增模块，只用第 11 课框架跑完整「改 → 测 → 守」三步。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-12-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=13 ink=… gates=…/0`。

与上一步的源码差异：[`11_to_12.diff`](../../diffs/11_to_12.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step12.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
