# NMBIN 优化提升空间评估

> **2026-11-12｜只读评估**：未改任何源码、未改任何产物、未跑回归、未碰 `settings.ini`。
> 数据全部来自本机 `products/graph/` 实测（UOS aarch64 / Wayland / Release `build_rel`），
> 文献为全网可公开检索的论文与官方规范。**文献的实验平台与本机不同，只迁移机制、不迁移数字**——
> 凡涉及「能不能更快」，一律以本项目自己的历史实测为准。

---

## 0. 结论先行

**问「NMBIN 还有没有优化空间」，必须先把「优化」拆成四个互不等价的目标。**
把它们混在一起，就会拿列存论文的压缩率去论证一个 mmap 零解码格式该改，这是最常见的误判。

| 目标 | 还有空间吗 | 判定依据 |
|---|---|---|
| **① 帧时间 / 渲染吞吐** | **基本没有** | NMBIN 已是「定长数组 + mmap 零解码」，正落在 RUM 三角的 R 最优角；项目 8 轮实测已把能试的都试过并否决（§1、§5） |
| **② 算路耗时** | **理论有、工程上不成立** | 顶点重排确有文献支撑，但会令全部派生表下标失效，且当前瓶颈是渲染不是算路（§5.5） |
| **③ 磁盘 / OTA 体积** | **有，且很大（实测 41%~56% 是纯 0 字节）**，但**别在 NMBIN 内部改** | 应在外层打包容器做块对齐压缩，运行时仍解压成零解码格式（§5.3、EROFS ATC'19） |
| **④ 格式健壮性 / 增量更新** | **有，且是真正的缺口，零性能代价** | 字段表是装饰性的、全链路无内容校验、Android manifest 无哈希（§3、§5.1、§5.2） |

**一句话**：作为「让每帧更快」的格式，NMBIN 已经接近这个架构下的天花板，**不建议再动热路径**；
作为「要分发、要 OTA、要能被别人读懂」的文件格式，它还有三处明确的、不影响帧时间的欠账。

**建议做的三件事（按优先级）**：
1. 让字段表真正生效（或至少让 `nmbin_check` 校验它与硬编码 schema 一致）——零性能代价、修的是设计目标本身。
2. 加内容校验和与块对齐 manifest——为 Android 整包更新兜底，也是做任何差分的地基。
3. 若确实要压 3.89 GiB，走**外层块对齐压缩容器**，不动 NMBIN 本体、不动读取路径。

**明确不建议**：列式/变长编码改写 NMBIN、节点/边空间重排、rec_size 对齐与 SIMD 解码、zone map 化、换语言或编译器调优。

---

## 1. 可比性声明：NMBIN 不是「数据库存储格式」

回源 `src/map/binfmt.h:7-16`、`binfmt.c:177-226`：

```
[ 128B NMBinHeader ][ 16×24B = 384B 字段表 ][ rec_count × rec_size 定长记录区 ]
                    ↑ 全部偏移 512B            ↑ sk_mmap_file 整体映射，读取时零解码、零拷贝
```

- 记录**定长、小端、绝对坐标（i32 e7）、无变长编码、无压缩、无分块**；
- 读取端 `sk_get_u32(data + NF_XXX)` 直接算偏移，一次 load 拿字段；
- 产物由 `sync_maps.sh` 构建期一次性固化，运行时**只读、不更新**；
- 空间裁剪不靠格式，而靠 `tiles.bin` 的 `[off,cnt)` 目录 + `disp.idx` 分 tile（见 §5.6）。

这正是 FlatBuffers 白皮书描述的「无临时对象、无分配、无拷贝、良好局部性」的零拷贝读取模型
（*FlatBuffers white paper*, §Motivation）。因此：

> **绝大多数「压缩 / 列存 / 编码」论文优化的是「磁盘带宽或内存放不下」的场景；
> 本项目 mmap 按需分页、热点页常驻，压缩不会带来带宽收益，只会给每条记录加一次解码。**

RUM Conjecture（EDBT 2016）给了这个取舍的形式化表述：**读开销 R、更新开销 U、空间开销 M 三者最多同时优化两个**。
NMBIN 选的是「R 最优 + U 干脆放弃（全量重抽）+ 付 M 的代价（3.89 GiB）」。
这不是失误，是 `binfmt.h:19-26` 注释里写明的自觉选择——每次格式变动都主动顶版本号、逼 8 省全量重抽。

### 本项目已经实测否决的方向（回源，不是推测）

| 实验 | 结果 | 出处 |
|---|---|---|
| 三级软件预取（深度 48/24） | 455 → 452 ms，无效 | `PLAN.md:1163`、`2026-09-22.md` |
| 缓存「上一条目起点」 | 命中 388/27 万 ≈ 0 | 同上 |
| 16K 槽节点坐标缓存 | 460 → **487 ms，更慢** | 同上 |
| L1 紧凑节点坐标表（8B/点，仍随机） | z9 −7 ms，代价 +397 MB | `2026-09-23.md:359-372` |
| L2 内联环坐标（顺序读） | z9 −16 ms，代价 +139 MB / 需重抽 8 省 | 同上 |
| **结论：贵的是间接寻址不是 stride** | 改 stride 只值 7 ms | 同上 |
| `-O3` / `-march=native` | 零收益（native 已含自动向量化） | `MEMORY.md`「别再试」 |
| 换语言 C→Rust | 同一 LLVM 后端，1:1 移植预期 0%（±5%） | 同上 |
| **已落地**：edgelist v2（8B→28B 自带坐标/等级，渲染不再随机访存） | 路绘制 −23%~32%，8 省 z9 370→284 ms | `PLAN.md:1205-1230` |
| **已落地**：分带并行 | 1.7~1.9×，与串行逐字节一致 | `MEMORY.md` |

**这张表是本次评估最重要的约束**：任何新提案若落在「减少随机访存 / 改 stride / 加解码」三类里，
先看它是不是已经被上面的实验判过死刑。

---

## 2. 真实冗余度实测（北京，`products/graph/beijing/`）

只读解析 `nodes.bin` / `edges.bin` / `edgelist.bin` 的 v3 头与记录区，统计如下：

| 文件 | 体积 | 记录数 | rec_size | **纯 0 字节占比** | 记录跨 64B cacheline |
|---|---:|---:|---:|---:|---:|
| `nodes.bin` | 115.4 MiB | 4,322,181 | 28 | **41.0%**（47.3 MiB） | 37.5% |
| `edges.bin` | 48.8 MiB | 2,843,824 | 18 | **56.1%**（27.4 MiB） | 25.0% |
| `edgelist.bin` | 78.0 MiB | 2,922,001 | 28 | 16.7%（13.0 MiB） | 37.5% |

**字段位利用率（香农熵 vs 实际占用）**：

| 字段 | 存储 | 取值基数 | 熵 | 浪费 |
|---|---|---:|---:|---|
| `lanes` (u16) | 16 bit | 8 | **0.32 bit** | ~98% |
| `flags` (u16) | 16 bit | 30 | 1.24 bit | ~92% |
| `edge_count` (u16) | 16 bit | 7 | 1.32 bit | ~92% |
| `class_mask` (u32) | 32 bit | 29 | 1.69 bit | ~95% |
| `class` (u8) | 8 bit | 5 | 1.89 bit | ~76% |
| `speed` (u8) | 8 bit | 16 | 2.39 bit | ~70% |
| `name_id` (u32) | 32 bit | — | — | **76.6% 的记录是 0（无名路）** |
| `osm_id` (u64) | 64 bit | — | — | **100% 单调递增**（可 delta） |

**坐标列的 delta 潜力**（zigzag-varint，按记录顺序差分）：

| 列 | 定长 | varint 平均 | 体积 |
|---|---:|---:|---|
| `nodes.lat` | 4 B | 2.06 B | 16.5 → 8.5 MiB |
| `nodes.lon` | 4 B | 2.16 B | 16.5 → 8.9 MiB |
| `edgelist` 四列坐标合计 | 16 B/条 | ≈7.56 B/条 | 44.6 → 21.1 MiB |
| `edges.dist_cm` / `name_id` | 4 B | 2.13 / 1.27 B | 各约 −47% / −68% |

`edgelist` 里 `edge_idx` 唯一值 2,843,824 恰等于 `edges.bin` 记录数 ⇒ **长边跨 tile 重复率仅 2.7%**，
说明「空间换时间」的冗余不是体积大头，大头是定长字段里的零字节。

**8 省汇总**：`nodes.bin` 1.36 GiB + `edgelist.bin` 1.11 GiB + `edges.bin` 0.68 GiB +
`rev.bin` 0.35 GiB + `lanes.bin` 0.15 GiB，7 类合计 3.65 GiB；`products/graph` 全部 3.89 GiB。

> 这些数字说明**体积上确实有 2× 量级的空间**；但下面的分析会说明，为什么这≠「该改 NMBIN」。
> 顺带一提：OSM PBF 原格式本身就用了 `DenseNodes` 的 delta 编码、stringtable、
> `granularity`/`lat_offset` 与按 PrimitiveBlock 独立压缩（OSM Wiki *PBF Format*）。
> 本项目是**主动**把「小体积 + 需解压」换成了「大体积 + 零解码」，不是没意识到。

---

## 3. 回源发现的两个真实缺口（不是性能问题）

### 3.1 字段表是装饰性的，「丢掉源码也能读」没有真正兑现

`binfmt.h:7-12` 写明设计动机是「字段语义随源码一起丢了……丢掉源码也能读」。但实测：

- `nmbin_open()`（`binfmt.c:186-190`）**只把字段表读进 `f->fields[]`**，之后不再使用；
- 生产读取路径（`mapset.c` / `paint.c` / `route.c` / `graph_build.c` / `revidx.c` …）
  全部使用 `binfmt.h` 里硬编码的 `NF_*` / `EF_*` / `XL2_*` 常量；按
  `sk_get_*(... + XX_YY)` 形式计到 **101 处**；
- `fields[]` 唯一的消费者是 `tools/graph_check.c:24-28`（打印给人看）
  和 `src/map/pbfmap.c:104`（另一处写入器）；
- `nmbin_check()`（`binfmt.c:204-226`）只做 5 条判据：magic / version / rec_size /
  payload 长度 / 字段 `offset+width <= rec_size`。**它不校验字段表与硬编码 schema 是否一致。**

**负向对照（按 `binfmt.c:204-226` 逐条复现，只在内存改字节，未写回产物）**：
把 `edges.bin` 字段表里 `class` 的 offset 由 14 改成 15（仍在 `rec_size=18` 界内、语义已错）：

```
原始字段表: [['to',0],['dist_cm',4],['name_id',8],['flags',12],['class',14],['speed',15],['lanes',16]]
注入后:     [['to',0],['dist_cm',4],['name_id',8],['flags',12],['class',15],['speed',15],['lanes',16]]
nmbin_check 正常文件 → PASS
nmbin_check 注入文件 → PASS     ← 判据无分辨力
```

⇒ **付了 384 B/文件的元数据开销，却既没拿到 schema 演进、也没拿到跨版本自描述读取。**
FlatBuffers 白皮书说明这种能力通常要靠 vtable 的一次间接寻址换；本项目连间接寻址都不必付——
字段表已在头里，只差消费它。

连带问题：`binfmt.h:19-26` 明确承认当前是**全局单版本号**，任何字段变动都逼 8 省全量重抽。
按 kind 独立版本 + 按字段名解析，可以让「只改 `contours` 记录布局」不必重抽 8 省路网。

### 3.2 全链路没有任何内容校验和

- `src/map/binfmt.c` 无 checksum/CRC 字段，`NMBinHeader.reserved[44]` 全空；
- `nmbin_check()` 只做结构自洽（长度、越界），**不检测位翻转 / 截断后恰好对齐 / 下载半包**；
- `autobuild.c:prov_check()` 用 `src_bytes` / `src_mtime` 判「源变了没」，验的是**源**，不是**产物**；
- `android/.../Manifests.java:21-27` 的 `ProvinceEntry` 只有 `id/name/version/size/url`，
  **没有内容哈希**；`DownloadActivity` 按整包 URL 下载。

⇒ 网络截断、磁盘位翻转、OTA 半包都会静默通过加载（只要长度对得上），
而且**没有块级哈希就做不了任何差分更新**——CDC 类算法的前提正是「按内容切块、按块比对」。

---

## 4. 公开文献对照（12 项，标注适用性）

| # | 文献 | 出处 | 关键结论 | 对本项目适用性 |
|---|---|---|---|---|
| 1 | Athanassoulis et al., *Designing Access Methods: The RUM Conjecture* | EDBT 2016, pp.461-466, DOI 10.5441/002/edbt.2016.42 | R/U/M 最多同时优化两个 | **直接适用**：解释 NMBIN 为何选 R 角、为何体积大 |
| 2 | Lemire & Boytsov, *Decoding billions of integers per second through vectorization* (SIMD-BP128) | Software: Practice & Experience 45(1), 2015; arXiv:1209.2137 | 位打包 + SIMD 前缀和可达 20~28 亿整数/秒 | **不适用**：本项目瓶颈是随机访存/每要素固定开销，且 `-march=native` 实测零收益 |
| 3 | Lemire, Kurz & Rupp, *Stream VByte* | Information Processing Letters 130, 2018, pp.1-6; arXiv:1709.08990 | 控制流与数据流分离，解码快于 varint-G8IU 2× | **仅用于外层容器**：若要压 OTA 包，这是候选编码；放进 NMBIN 热路径则加解码 |
| 4 | Huanchen Zhang et al., *An Empirical Evaluation of Columnar Storage Formats* | VLDB 2024（清华 IIIS） | **「块压缩对端到端查询速度无益甚至有害」**；zone map 只在数据物理聚簇时有效 | **关键反证**：直接支持「不要把列存压缩搬进 NMBIN」 |
| 5 | Kamel & Faloutsos, *Hilbert R-tree* / *On Packing R-trees* | VLDB 1994, pp.500-509 / CIKM 1993 | Hilbert 序打包静态空间数据，TIGER 上响应 +58% | **方向已实现**：`tiles.bin` + `disp.idx` 就是等价的空间分块裁剪（§5.6） |
| 6 | Fang et al., *An efficient memory data organization strategy for application-characteristic graph processing* (GMDO) | Front. Comput. Sci. 16(1):161607, 2022 | 顶点重排改善遍历局部性；不规则访存使 CPU 仅 10~45% 时间有效 | **机制适用、工程不成立**：见 §5.5 |
| 7 | Gao et al., *EROFS: A Compression-friendly Readonly File System* | USENIX ATC '19, pp.149-162 | 4 KiB 块对齐 + 定长输出压缩，随机读不放大；Android 只读分区近乎减半体积 | **最贴合**：本项目 Android JNI App 的打包路线（§5.3） |
| 8 | Sheng, Xu & Wang, *A Two-Phase Differential Synchronization Algorithm for Remote Files* (tpsync) | ICA3PP 2010, LNCS 6081, pp.65-78 | CDC + 滑动块，比 rsync 省 18.9% 带宽 | **适用**：块级 manifest 是差分地基（§5.2） |
| 9 | Zhang et al., *ParaSync: exploiting fine-grained parallelism for efficient file synchronization* | FAST '26, pp.477-492 | CDC 分块/匹配/重建可并行，同步最高 3.7× | 同上，说明分块后还有并行余量 |
| 10 | *FlatBuffers white paper* | Google, flatbuffers.dev/white_paper | vtable 一次间接寻址换前向/后向兼容；struct 不可演进、table 可 | **直接适用**：字段表该怎么做（§5.1） |
| 11 | *PBF Format* | OSM Wiki, wiki.openstreetmap.org/wiki/PBF | DenseNodes delta 编码、stringtable、granularity/offset、块独立可解压 | **对照系**：说明 PBF→NMBIN 是「体积换速度」的主动取舍 |
| 12 | *Memory-Mapped I/O Performance for Analytical Workloads* | Zenodo 2026（**预印本**）；另参 kernel-internals.org mmap 技术文档 | mmap 顺序列扫 1.8~3.2×；THP/`MADV_HUGEPAGE` 降 TLB 压力 | **谨慎**：仅作工程线索，本机 aarch64 未实测，不作为主证据 |

> 检索口径：以「整数压缩 / 列存 / 空间索引 / 图布局 / mmap / 差分同步 / schema 演进 / 只读压缩文件系统」
> 八组关键词做全网公开检索；只把有会议/期刊/官方规范出处的纳入证据，博客与百科仅作线索。

---

## 5. 逐方向判定

### 5.1 让字段表真正生效 —— **建议做（P0，零性能代价）**

- **文献**：#10（vtable 换 schema 演进）、#1（RUM：元数据是 M，换 R/U 的灵活性）。
- **现状**：字段表已写、未被消费；`nmbin_check` 对界内错误无分辨力（§3.1）。
- **两条路线**：
  - *轻量*：`nmbin_check` 增加「字段表与期望 schema 逐项比对」，让错误产物在加载期就 FAIL。
    不改任何热路径，**帧时间零影响**。
  - *彻底*：读取端按字段名解析出偏移，构建期把偏移缓存进 `MapSrc`（每省一次、每帧零开销），
    由此支持按 kind 独立版本与字段追加，避免「改 contours 就得重抽 8 省路网」。
- **不要做**：把字段查找放进每条记录的循环里——那是把 #10 的「一次间接寻址」放大成
  「每记录一次间接寻址」，正好撞上本项目「贵的是间接寻址」的实测结论。

### 5.2 内容校验和 + 块对齐 manifest —— **建议做（P0，为 OTA 兜底）**

- **文献**：#8 / #9（CDC 差分的前提是内容分块 + 块哈希）、#7（4 KiB 块对齐）。
- **现状**：全链路零 checksum；Android manifest 无哈希；`prov_check` 验源不验产物（§3.2）。
- **做法**：头部 `reserved[44]` 里放全文件摘要 + 每 4 KiB 块的摘要表（或独立 sidecar），
  `nmbin_check` 逐级校验；manifest.json 增 `sha256` / `chunks` 字段；下载完先验再安装。
- **收益不在帧时间**：解决「半包静默加载」「位翻转画出鬼图」，并让未来的差分更新有地基。
- **成本**：加载期多一次全文件哈希（冷启动一次性，可只校验块表 + 抽样），热路径零影响。

### 5.3 体积压缩 —— **有空间，但只做外层容器（P1，有条件）**

- **文献**：#4（**块压缩损害端到端速度**）、#7（EROFS 块对齐 + 定长输出压缩）、#3 / #2（快速整数编码）、#11（PBF 本来就是这么压的）。
- **实测**：零字节 41%~56%，低熵字段位利用率 <10%，坐标 delta 可省约一半（§2）。
- **判定**：**不要**把变长编码 / 列存 / 压缩塞进 NMBIN 本体。理由有三：
  1. 会给每条记录加解码，直接撞上「瓶颈在每要素固定开销」的实测结论；
  2. 破坏 `rec_size` 定长 ⇒ 所有 `data + i*rec_size` 的 O(1) 随机访问失效，
     而 `nodes.bin` / `edges.bin` 正是路由要随机访问的；
  3. #4 在 VLDB 2024 的大规模评测里明确发现块压缩对端到端查询无益甚至有害。
- **正确位置**：**分发/打包层**。把每省产物打成 4 KiB 块对齐的压缩容器（EROFS 思路），
  安装时解压成本地零解码 NMBIN。这样 OTA 包体积下来、运行时帧时间不变。
  对 Android 尤其贴切——#7 的论文场景就是 Android 只读分区，已在千万级设备部署。
- **注意**：不要在压缩收益里混入帧时间收益，两者必须分开报（§6 判据）。

### 5.4 rec_size 对齐 / SIMD 解码 / zone map —— **不建议**

- 实测跨 64B cacheline 比例 25%~37.5%（`rec_size` 18/28 都不是 2 的幂），看似有对齐空间；
  但历史实测「改 stride 只值 7 ms」「L1 紧凑坐标表 +397 MB 只值 7 ms」，
  且 `-O3` / `-march=native` 零收益（自动向量化已到位）⇒ 这条已被判过。
- 把 28→32 / 18→32 会让 8 省体积再涨约 0.5 GiB，换来的是已被测出上限的收益。
- zone map（#4）：项目 `tiles.bin` 的 `[off,cnt)` 目录 + `disp.idx` 分 tile
  本就是「按空间聚簇 + 跳过整块」，#5 的 Hilbert 打包思想已以另一种形态落地。
  论文只是印证方向正确，**不是新增空间**。

### 5.5 顶点/边空间重排（Hilbert / GMDO）—— **机制成立，工程上否决**

- **文献**：#6（GMDO 顶点重排改善遍历局部性）、#5（Hilbert 序打包静态空间数据）。
- **理论上**：`nodes.bin` 按 `osm_id` 单调递增（实测 100%），OSM id 只保证导入顺序、
  不保证空间局部性；Dijkstra 随机展开时确实会有 cache miss。按空间序重编号可改善。
- **工程上致命**：`edges.bin` 是 CSR 顺序，`edgelist.bin` 存 `{node_idx, edge_idx}` 内部下标，
  `rev.bin` / `lanes.bin` / `lm.bin` / `restrictions.bin` 全部按这些下标绑定。
  重排节点 = **所有派生表全部失效、8 省全量重抽**，且 `dup_of`、跨省缝合的坐标键逻辑都要复核。
- **收益面不对**：当前瓶颈在渲染（分带并行后才 1.7~1.9×），不在算路；
  为一个非瓶颈路径付「全量重抽 + 正确性风险」不划算。
- 若将来算路成为瓶颈，优先看算法层（CH / ALT 已在项目路线里），而不是动物理编号。

### 5.6 已做到位、无需再改的部分

`edgelist v2` 把绘制字段内联进条目（消除随机访存）、`tiles.bin` 空间目录、
`disp.idx` 视口分 tile、`mapset_set_view` 的按需 mmap + LRU、分带并行——
这些分别对应文献 #5 / #1 / #10 的**同一批机制**，只是用项目自己的形态实现了。
把它们列成「优化空间」会重复劳动。

---

## 6. 如果动手：判据怎么设计（沿用项目纪律）

> 判据用**展开数 / 里程 / 像素 / 退出码**，时间只兜底取多轮最小；
> **负向对照必须能失败**，且改的是「被检查的数据」不是判据条件本身。

| 改动 | 正向判据 | 负向注入（必须 FAIL） | 零回归证据 |
|---|---|---|---|
| §5.1 字段表校验 | 8 省全部 `nmbin_check` PASS | 把某字段 offset 改成界内另一值 → 必须 FAIL（§3.1 已证明当前无分辨力） | `--shot-offscreen` 出图与改前 **bmpdiff 0 像素** |
| §5.1 按名解析 | `graph_check` 打印字段表与按名解析结果逐项一致 | 删除一个必需字段 → 加载必须拒绝 | 同上 + `regress.sh` 全绿 |
| §5.2 内容校验 | 8 省全文件 + 块表校验 PASS | 翻转 1 字节 / 截断最后一块 / manifest 换错哈希 → 必须 FAIL | `check_province_coverage.py` 风格 `--self-test` |
| §5.3 外层压缩 | 解压出的 NMBIN 与原文件 **md5 逐字节相同** | 故意解压错一个块 → 必须被校验抓到 | 帧时间只作「无退化」证据，不作为收益；只报 OTA 包体积与冷启动解压耗时 |

**共同前置**：改动前后用**同一份发布包 exe**（别从 `build_rel/` 拷出，字体按 exe 相对路径解析）；
完整 `regress.sh` 会写脏 `view_*` ⇒ 跑前备份 `settings.ini`、跑完恢复；
改过 `regress.sh` 必须重跑 `regen_tutorial_blocks.py` 再 `--check`。

---

## 7. 边界与风险（别高估这份报告）

- 本文是**只读评估**：所有体积/熵/delta 数字来自北京一省，8 省仅做体积汇总；
  外推到其他省假定数据分布相近，未逐省验证。
- 文献实验平台（x86 Haswell、服务器列存、Android eMMC）与本机 UOS aarch64 / 软件光栅 / mmap 只读
  **不同**，只迁移机制、不迁移任何加速倍数。
- `varint` 节省量是**编码层理论值**，未计入控制流、对齐、索引与解码 CPU 成本；
  真实压缩率需做出原型才能定，本文只用来判断「有没有冗余」，不用来承诺收益。
- 报告**不主张**改 NMBIN 热路径。若将来要动，必须按 §6 先立负向对照，
  并接受「8 省全量重抽 + 混合图集不兼容」这个 `binfmt.h:19-26` 已经写明的代价。
- 未验证 Android 真机、未跑 `regress.sh`（会污染真实 `settings.ini`）、未提交任何改动。

---

## 8. 来源清单（可复核链接）

1. https://infoscience.epfl.ch/entities/publication/dc3d7ac5-09ef-4ba3-bb3e-6a721d167a2a/conferencedetails ｜ https://users.cs.utah.edu/~pandey/courses/cs6530/fall22/papers/indexdesign/rum.pdf
2. https://arxiv.org/abs/1209.2137
3. https://arxiv.org/abs/1709.08990 ｜ https://r-libre.teluq.ca/1197/1/streamvbyte.pdf
4. https://people.iiis.tsinghua.edu.cn/~huanchen/publications/columnar-formats-vldb24.pdf
5. https://www.cs.cmu.edu/~christos/PUBLICATIONS.OLDER/vldb94.pdf ｜ http://www.cs.cmu.edu/~christos/PUBLICATIONS.OLDER/cikm93.pdf
6. https://journal.hep.com.cn/fcs/EN/10.1007/s11704-020-0255-y ｜ https://news.sciencenet.cn/htmlpaper/2023/1/2023169224836778264.shtm
7. https://www.usenix.org/system/files/atc19-gao.pdf ｜ http://erofs.docs.kernel.org/en/latest/design.html
8. https://doi.org/10.1007/978-3-642-13119-6_6
9. https://dlnext.acm.org/doi/10.5555/3805485.3805514
10. https://flatbuffers.dev/white_paper ｜ https://flatbuffers.dev/
11. https://wiki.openstreetmap.org/wiki/PBF
12. https://zenodo.org/records/20776270 ｜ https://kernel-internals.org/io/mmap-io/

**项目内回源**：`src/map/binfmt.h:7-238`、`src/map/binfmt.c:177-226`、
`src/map/mapset.c:294-431`、`src/map/autobuild.c:62-116,175-377`、
`tools/graph_check.c:24-28`、`android/app/src/main/java/com/skedaddle/android/Manifests.java:21-27`、
`PLAN.md:1150-1230`、`.workbuddy/memory/2026-09-22.md`、`2026-09-23.md`、`MEMORY.md`。
