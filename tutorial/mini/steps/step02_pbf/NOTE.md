# 第 2 课 · PBF 流式解析：varint 与字段号
> 分类：核心代码

> 演化链第 2 步的完整可编译快照：`tutorial/mini/steps/step02_pbf/`
> 源码讲义：[`docs/teaching/02_pbf.md`](../../../../docs/teaching/02_pbf.md)
> 同号独立小程序：`tutorial/02_pbf`
> 真实工程落点：`src/pbf/pbf_stream.c`、`src/pbf/pbf_block.c`

**这一课新增**：mini_pbf_varint / mini_pbf_parse，解析手写 3 节点 blob 并画锚点。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-02-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=3 ink=… gates=…/0`。

与上一步的源码差异：[`01_to_02.diff`](../../diffs/01_to_02.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step02.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
