# 第 14 课 · 极简多语言：中文 msgid 是安全的
> 分类：核心代码

> 演化链第 14 步的完整可编译快照：`tutorial/mini/steps/step14_i18n/`
> 源码讲义：[`docs/teaching/14_i18n.md`](../../../../docs/teaching/14_i18n.md)
> 同号独立小程序：`tutorial/14_i18n`
> 真实工程落点：`src/i18n/i18n.c`、`lang/en.xml`

**这一课新增**：mini_tr 查表回退 + mini_xml_unescape，追加语言切换框。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-14-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=15 ink=… gates=…/0`。

与上一步的源码差异：[`13_to_14.diff`](../../diffs/13_to_14.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step14.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
