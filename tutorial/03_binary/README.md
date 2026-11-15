# 第 3 课 · 自描述二进制与图集
> 分类：核心代码

> 📖 配套读源码讲义：[`docs/teaching/03_binary.md`](../../docs/teaching/03_binary.md)（读源码式讲解，讲清楚“为什么这么写”；与本科小程序同号对应）


**累加内容**：为什么不用"8 字节头 + 定长记录"，而要做自描述格式。本课写一个迷你版 NMBIN：
文件头里存【字段名 / 偏移 / 类型 / 宽度】，数据区按字段偏移布局。读端**不 include 任何写端的
struct**，而是解析文件头反推出结构——这就是"丢掉源码也能读"。

**运行**：
```bash
gcc -std=c11 -O2 -o tut main.c && ./tut
```

**你会看到**：stdout 打印文件头字段表（name/off/type/width）和 3 条记录按字段读回的值；`out.bmp`
是 3 根柱子（高度编码纬度），证明"读回来的数据能用"。磁盘上还留下了 `demo.nmbin`。

**关键设计哲学（项目里这么选的）**：
- 上一版"8 字节头 + 定长记录"的字段语义随源码丢失——换新机器/新编译器就**读不出来**。
- 自描述头部让结构随数据走：**落盘统一小端、定长整型**，避免 aarch64 跨编译器布局漂移；
  `NMBIN_MAGIC="NAVMBIN\x01"`、`NMBIN_VERSION=3`。
- 多省图集（`MapSet`）据此按需加载：每省一组 `nodes/edges/...`，含 `bbox` 与 `dup_of`
  （被完全包含的副本省，如北京⊂河北 100%），后台解码线程按需解码。

**生产最终形态**：`src/map/binfmt.c`（`NMBinFile`/`NMBinHeader`/`NMBinField`）、
`src/map/mapset.c`（`MapSrc`/`MapSet`、后台解码线程）。

**练习**：给字段表加一个 `speed`(u32)，重新写+读，确认读端只改字段表就能解析新字段、无需改 struct。

**生产补记（2026-11-15）**：自描述头只证明“布局自洽”，不证明文件内容可信。
本批给四类加载器补了语义校验：

- `names.bin`：`count` 必须能容纳 `count+1` 个 offset，`off[0]=0`、`off[count]=blob_len`、offset 单调且不越过 blob；
  `count=UINT32_MAX` 不再因 `count+1` 回绕而跳过检查。
- `tiles.bin`：`rec_count>=2`，且 `minx+nx`、`miny+ny`、`nx*ny` 都在网格/记录数范围内；
  避免 `rec_count-2` 无符号下溢。
- `lm.bin`：头里的 `nrow*ncol*nlm*2` 先做溢出检查，并要求文件长度精确等于“头 + 表体”。
- `rev.bin`：除尾哨兵外，还要求 `start[]` 全表单调不减且不超过 `list_count`，防止正文被篡改后越界读 `list`。

**练习**：构造一个 `names.bin`，把 `off[1]` 改成 `0xFFFFFFFF`，确认 `name_pool_open()` 返回失败而不是让渲染读坏内存。

**Rust 对照**：Rust 教程 step03 的 `Result` + 切片索引已经挡住“越界读”，但“offset 语义”仍要显式校验；见 `tutorial/rust/README.md` §七。
