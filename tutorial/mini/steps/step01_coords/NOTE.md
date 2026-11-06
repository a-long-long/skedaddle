# 第 1 课 · 坐标系：1e7 度量化与墨卡托投影
> 分类：核心代码

> 演化链第 1 步的完整可编译快照：`tutorial/mini/steps/step01_coords/`
> 源码讲义：[`docs/teaching/01_coords.md`](../../../../docs/teaching/01_coords.md)
> 同号独立小程序：`tutorial/01_coords`
> 真实工程落点：`src/utils/geo.c`、`src/map/map_view.c`

**这一课新增**：mini_geo_q / mini_merc_y / mini_project / mini_haversine_m，追加浅灰经纬网。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-01-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=2 ink=… gates=…/0`。

与上一步的源码差异：[`00_to_01.diff`](../../diffs/00_to_01.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step01.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
