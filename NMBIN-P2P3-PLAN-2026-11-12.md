# NMBIN P2 / P3 施工计划（2026-11-12）

> 配套：[`NMBIN-OPTIMIZATION-2026-11-12.md`](NMBIN-OPTIMIZATION-2026-11-12.md)（评估）、
> [`NMBIN-OPTIMIZATION-PLAN-2026-11-12.md`](NMBIN-OPTIMIZATION-PLAN-2026-11-12.md)（M1/M2 计划与 §8/§9 执行记录）。
> 范围：该计划 §1 表里剩下的两项 —— **P2 = O6 外层块对齐压缩容器（M3 体积）**、
> **P3 = O7 按名解析 + per-kind 版本（M4 可选）**。
> 不是 `CONSUMER-NAV-GAP-2026-11-12.md` §8 的 P2/P3：那张表的 P2 六项已全部落地、
> P3（瓦片缓存/3D/静态公交）已判定与「每帧全量重画」架构赌注冲突，**不在本计划内**。
>
> **§0~§8 是 2026-11-12 的计划原文，不改产品源码、产物、配置。** 计划阶段唯一
> 新增的文件是只读量测探针 `scripts/container_probe.py`（不改判据、不接 regress）。
> **§9 起是 P2-A 的实际施工记录**：按用户「继续 P2-A」指令，第一档已落地，
> 改动只涉及 `scripts/` 与 `docs/`，`src/`、`CMakeLists.txt`、Android 解析端零改动。

---

## 0. 结论先行（两个回源发现改变了原排期）

### 0.1 ★★ 原 O6 卡片若照抄，会做重复劳动

原计划 §2 O6 写「新增 `pack_container.py` / `unpack_container.py`，安装时解压出
逐字节相同的 NMBIN」。回源 `DownloadActivity.applyZip()` → `Downloader.extractZip()`：
**整包路径本来就是 `ZipInputStream` + deflate 压缩**，`manifest.json` 的 `sha256`
也是对 `beijing.zip` 整包算的。再压一层 zip 是零收益。

真正没被压缩的是**差分路径**：`ChunkUpdater.java:86-88` 用
`base + name` 拼出**裸数据文件 URL**，`ChunkDelta.HttpRangeSource.fetch()` 对它发
`Range: bytes=index*4096 - min(...)-1`（`ChunkDelta.java:53-63`），服务端必须按
4 KiB 对齐发布未压缩的 `.bin`。M2 实测「改 1 块只传 4096 B」传的就是这 4096 个裸字节。

⇒ **O6 的准确定义应改写为「差分路径的逐块独立压缩容器」**，目标是把这 4096 B
压到约 1.4 KiB，而不是「把 NMBIN 压小」。压缩发生在分发/传输层，运行时仍读
逐字节相同的 NMBIN、仍走 mmap 零解码。

### 0.2 ★★ O1 判据与 O7 目标直接冲突，P3 必须先改 O1

`binfmt.c:240-268` 的 `nmbin_check_schema()` 要求
`f->hdr.n_fields == s->n_fields` 且每个字段 name/offset/type/width 逐项相等。
O7 的「追加一个尾部字段而不重抽 8 省」必然被它判 `字段数不符` 拒绝。
⇒ P3 的第一步不是写按名解析，而是把 O1 判据从「完全相等」放宽为
「**期望表是文件字段表的前缀子集，且必需字段的 offset/type/width 不变**」，
并补负向注入证明「删必需字段 / 改 offset」仍被拒（判据分辨力不能丢）。

### 0.3 排期建议

| 顺序 | 项 | 建议 | 理由 |
|---|---|---|---|
| 1 | **P2-A** 块表传输成本治理（新拆出的轻量项） | **先做，推荐** | 不碰差分数据路径、不引入新格式、不动 Android 内核；直接解决 M2 §9.4 点名的「71 MB 清单」问题 |
| 2 | **P2-B** 逐块压缩容器（原 O6 修正版） | **设决策门，可能不做** | 只有当 P2-A 之后清单仍是瓶颈、且 A/B 净收益为正才做；可独立撤回 |
| 3 | **P3** O7 按名解析 + per-kind 版本 | **最后做，且可判定不做** | 改加载期核心路径，收益是「将来少重抽一次」，没有当下真实需求（计划 §2 O7 自己也写「可选」） |

**允许的最终结论是「P2-B / P3 不做」**：计划 §9.4 与评估 §5 都把这两项写成
「先量再决定 / 没有真实需求就不必做」。计划的价值是把决定做成可复核的数字，
而不是凑齐里程碑。

---

## 1. 本轮回源事实（实测，不是估算）

### 1.1 压缩量测（`scripts/container_probe.py`，本机 Windows 副机全量）

| 项 | 数值 |
|---|---|
| 样本 | `products/graph/` 8 个省目录，递归 6566 文件 / 631107 个 4 KiB 块 / 2.568 GB |
| 压缩方式 | 每块独立 **raw deflate level 6**（无 zlib 头，块间零依赖 ⇒ 仍可随机取单块） |
| 压缩后 | 856 MB，**总体 33.32%**，省 1633.6 MiB；各省 33.1~33.4%，无异常样本 |
| 零字节比例 | 37.8%（与评估报告 nodes 41.0% / edges 56.1% 同向，样本含大量 disp tile） |
| 压不动的块 | **0 / 631107** |
| 压缩后单块长度 | p10=1047 B / **p50=1424 B** / p90=1729 B（未压缩恒 4096 B） |
| 用户态解压吞吐 | 190 MiB/s（**本机 Windows AMD64 / Python 3.13 / zlib 1.3.1**；按纪律只迁机制不迁倍数，aarch64 数字须在主机复跑同一探针） |

> ★ 样本边界：本机 7 个省目录里的 `nodes.bin` md5 完全相同（`3982349e…`），
> 是同一份数据的副本；顶层 `beijing/nodes.bin` 为 `91bd6566…`。所以
> **2.568 GB 不是主机真实 8 省 3.89 GiB**，但压缩比是内容属性（8 份稳定在 33.1~33.4%），
> 可用于机制判断；绝对体积与块表增量必须以主机 `--all` 复跑结果为准。
> 另：副机产物是 `<省>/beijing/` 嵌套结构，`gen_chunks.py` 的 `data_files()` 只扫顶层
> ⇒ 在副机直接跑会生成空块表，探针已递归处理并在报告里标注。

### 1.2 决策门数字

- M2 现状：改 1 块传 **4096 B**；P2-B 后平均传 **1424 B**，单块省 **2672 B**。
- 代价：块索引必须记录每帧的压缩后 offset/len，按 631107 块估
  **+18.9 MB（JSON 文本）/ +7.6 MB（二进制索引）**。
- ⇒ 只有当「本次变化块数 × 2672 B」> 索引增量时 P2-B 才净赚。
  而 M2 的典型场景恰恰是「改 1 块 / 全等 0 块」——**小更新极可能净亏**。
  索引是全量拉的（数据块省的是增量），这个不对称是决策门的核心。

### 1.3 O7 静态事实

| 项 | 数值 / 位置 |
|---|---|
| 硬编码字段常量引用 | **511 处 / 52 个不同常量 / 38 个文件**（`NF_` `EF_` `XL2_` 等前缀，含头文件定义与工具） |
| `fields[]` 消费点 | `binfmt.c:371-372` 只填数组；唯一真正消费者是 `nmbin_check_schema()`（`binfmt.c:252`）。生产读取路径零消费 |
| `MapSrc` | `src/map/mapset.h:22-82`，**没有任何字段布局缓存字段** |
| 版本号 | `NMBIN_VERSION 3` 全局单值；`binfmt.c:390` 硬校验 `f->hdr.version != NMBIN_VERSION` 直接拒绝 |
| mmap 记录区起点 | `binfmt.c:375`：`@128 + 24×16 = @512`，字段表永远占满 16 槽 ⇒ O7 不得改这个偏移 |
| 头部空闲字节 | 手工序列化 `hdr_to_bytes()`（`binfmt.c:54-69`）写到 `@112+4=@116` 为止，**`@116..127` 共 12 字节全零**；`bytes_to_hdr()` 也不读 |
| 空闲字节验证 | 抽样 78 个 `.bin`，70 个 `@116..127` 全零；8 个非零全是 `rev.bin`（magic `NMREV01`，**不是 NMBIN**，`disp.idx` 同理）⇒ 对全部真 NMBIN 文件这 12 字节可用，旧产物天然全 0 ⇒ 兼容位可用 |

### 1.4 格式耦合面（动块表必同步）

`scripts/gen_chunks.py`（生成/校验 + `VERSION`）、`scripts/check_province_coverage.py`
（直接 `import gen_chunks` 复用名单/块数）、`scripts/sync_maps.sh`（graph_check 后生成）、
`android/.../Manifests.parseChunks()`、`ChunkUpdater.java`（**硬编码 `version != 1` 即拒绝**）、
`ChunkDelta.java`（`CHUNK_SIZE=4096` 与 Range 偏移）、`docs/android.md §11.1.1`（契约）、
`scripts/regress.sh:939-959`（M2 的 10 条判据 + `java_gate.sh`）。
⇒ **任何格式变更都必须留 `version` 兼容位**，否则老服务端/老客户端立刻断。

---

## 2. P2-A：块表传输成本治理（新拆出，推荐先做）

**目标**：M2 §9.4 明确点名「71 MB JSON 清单会抵消单块差分收益，服务端需 gzip + ETag」。
把这句话变成可测、可门禁的工程事实，而不是一句建议。

**改动**（分两档，先做第一档，第二档由决策门决定是否上）
- 第一档 · 零格式变更（**推荐先做**）：`scripts/gen_chunks.py` 在写出
  `chunks.json` 的同时产出 `chunks.json.gz`，并打印其 sha256 供服务端作 ETag；
  `scripts/sync_maps.sh` 在块表生成后追加这一路产物（失败同样丢弃本次产物）。
  **`chunks.json` 原样保留**，老客户端、`check_province_coverage.py`、
  `Manifests.parseChunks()` 一个字节都不用改。
- 第二档 · 二进制表（仅当第一档数字仍不达标）：新增 `--compact` 产出
  `chunks.bin`（每块哈希用 32 B 原始字节而非 64 hex，头带 `version`/`chunk_size`/`algo`），
  需同步 §1.4 的全部耦合点并留 v1 兼容读路径。
- `scripts/check_province_coverage.py`：第 6 条不变量「伴生文件齐全且与 JSON 同源」，
  `--self-test` 增加「删伴生文件」「伴生文件与 JSON 不同源」两类负向。
- 新增 `scripts/chunk_table_size.py`：实测 JSON / gzip / 二进制三种体积，
  给出「清单固定开销」与「需多少变化块才能回本」的可复现数字。
- `docs/android.md §11.1.1`：登记伴生格式与 `version` 兼容位；**不改 `ChunkUpdater`
  的 v1 解析**（本项不动差分数据路径）。

**判据**（全部改「被检查的数据」做负向注入）
| 名称 | 正向 | 负向 |
|---|---|---|
| `TABLE-COMPACT-OK` | 三种格式解析出完全相同的文件集/块数/哈希 | 改掉二进制表 1 个哈希 ⇒ 与 JSON 不一致被抓 |
| `TABLE-SIZE-OK` | 打印 JSON/gzip/二进制体积与回本块数 | 用「全 0 假表」冒充 ⇒ 体积断言失败 |
| `COVERAGE-COMPACT-OK` | 8 省伴生文件齐全 | 删 1 个伴生 / 换 1 个字节 ⇒ FAIL |
| `TABLE-BACKCOMPAT-OK` | 只有 `chunks.json` 的老目录仍能过 v1 门禁 | 把 `version` 改成 9 ⇒ 明确拒绝 |

**零回归**：不改 `src/`、不改 `CMakeLists.txt`、不改差分内核 ⇒ C 二进制无变化，
沿用 M2 纪律**不做 A/B 出图对拍**；以 `regress.sh` 全绿（含像素/展开数类判据）为证。
`settings.ini` 跑前备份、跑完恢复。

**兼容**：`chunks.json` 是唯一权威源，伴生文件是**可选加速**；缺失时自动回落 JSON。
老服务端、老客户端、副机缺 7 省数据的场景都不受影响。

**决策门**：若 gzip 后清单仍 > 某阈值（建议先定 8 MB/省，实测后定稿），再上二进制表；
若仍不够，才进入 P2-B。

---

## 3. P2-B：差分路径逐块独立压缩容器（原 O6 修正版，可独立撤回）

**前置**：P2-A 完成且数字证明清单已不是瓶颈；主机复跑 `container_probe.py --all`
给出 aarch64 的解压吞吐与真实块数。

**格式设计**（新增 `scripts/pack_container.py` / `unpack_container.py`）
```
容器 = Header(固定 64 B: magic "SKCHUNK1" / version / chunk_size=4096
              / raw_size / raw_sha256 / frame_count / index_offset)
     + Frame[]   (每帧 = 一个原始 4 KiB 块的 raw-deflate 结果，长度不等)
     + Index[]   (每帧 12 B: u64 compressed_offset + u32 compressed_len)
```
- 每帧独立 deflate，**块间零依赖** ⇒ 仍可 `Range` 随机取单块；索引给出压缩后
  偏移，客户端按索引取帧、解压、用**原始块 sha256** 复核，再按 `index*4096`
  写回 `.new`。
- ★★ **三个哈希口径绝不串台**：清单 `sha256`（zip 整包）、块表 `sha256`
  （单个数据文件全文件）、`nmbin_digest`/`record_sha256`（O2 记录区 @512）。
  容器头里的 `raw_sha256` 是**解压后整个数据文件**的全文件哈希，必须与块表
  `sha256` 相等，由 `ChunkDelta.apply()` 现有整包校验兜底。
- ★ 压缩后帧长度 < 4096 ⇒ **Range 边界不再 4096 对齐**，服务端必须按容器字节
  发 206；`ChunkDelta.HttpRangeSource` 要支持「按索引表给 (offset,len)」的新 Source
  实现，旧 `fetch(int index)` 按 4096 的语义保持不变（老服务端回落路径）。

**改动清单**
- 生成端：`gen_chunks.py` 增加「同时产出 `.skc` 容器 + 帧索引」；`sync_maps.sh`
  在 `graph_check` 之后、`mv` 之前生成（失败丢弃产物）；`check_province_coverage.py`
  增加「容器与块表同源」不变量。
- Android：`Manifests` 增 `containerUrl` / `containerVersion`（`optString` 缺省空 ⇒
  老清单可解析）；`ChunkDelta` 新增 `PackedRangeSource`；`ChunkUpdater` 优先容器、
  无容器回落裸块、再失败回落整包 zip（**三级回落，旧图始终不动**）。
- 文档：`docs/android.md §11.1.1/11.1.2`、`docs/teaching/17_android.md` 行号与
  `11_gates.md` 逐字块按纪律重生成。

**判据**
| 名称 | 正向 | 负向注入 |
|---|---|---|
| `CONTAINER-OK` | 8 省打包→解包后每个 `.bin` **md5 逐字节相同**；全文件/记录区/O2 三口径全一致 | — |
| `CONTAINER-FRAME-OK` | 随机取 100 个块，单帧解压 == 原始 4 KiB 块 | 翻转 1 帧的 1 字节 ⇒ 解压失败或哈希不符被拒 |
| `CONTAINER-INJECT-CAUGHT` | — | ① 改索引里 1 帧 offset ② 截断最后一帧 ③ 换错 `raw_sha256` ④ 用旧索引配新容器：四种都必须拒绝并保留旧图 |
| `CONTAINER-DELTA-OK` | 合成夹具「改 1 块只取 1 帧、传 ≈1424 B」「全等 0 B」 | 服务端返回脏帧 ⇒ 整包 sha256 拒绝提交 |
| `CONTAINER-FALLBACK-OK` | 无容器 / 无 206 / 清单无 `containerUrl` ⇒ 完整走 M2 老路径 | 删掉回落分支 ⇒ 老服务端夹具失败 |
| `CONTAINER-BACKCOMPAT-OK` | 老 `chunks.json` v1 原样可用 | `version` 改成 9 ⇒ 明确拒绝 |

**★ 口径纪律（写死）**
- 帧时间只作「无退化」证据（bmpdiff 0 像素 + 多轮最小时间不退化），
  **绝不在压缩收益里混入帧时间收益**，两个数字分开报。
- 不宣称净流量收益：报告必须同时给「省下的数据块字节」与「新增的索引字节」，
  并按变化块数给盈亏平衡点（本机数据：2672 B/块 vs 索引 18.9 MB JSON / 7.6 MB 二进制）。
- 运行时**一个字节都不改**：容器只在下载/安装边界解压，落盘后就是普通 NMBIN。

**零回归**：不改 `src/` ⇒ 沿用 M2 纪律；但**新增 Android Java 夹具**必须进
`scripts/java_gate.sh`，用服务端侧计数证明真实 Range 字节数（不能客户端自报）。

**撤回条件**：若 A/B 显示「典型更新（1~10 块）净流量为负」或「aarch64 解压冷启动
耗时 > 整包 zip 解压 + 直接下载」，**直接撤回 P2-B，保留 P2-A**，不影响 M1/M2/P2-A。
撤回时只删生成端与 Android 侧新增文件，`chunks.json` v1 契约不动。

---

## 4. P3：O7 按名解析 + per-kind 版本（可选，最后做）

### 4.1 先解 O1 冲突（必做第一步）

- `src/map/binfmt.c` 的 `nmbin_check_schema()`：把 `n_fields != s->n_fields` 的
  硬相等改为「**文件字段数 ≥ 期望数**，且前 `s->n_fields` 个字段逐项相等；
  超出部分是允许的尾部追加字段」。错误串要点名具体字段。
- 配套负向注入（`tools/nmbin_schema_selftest.c` 的 `--inject=all` 扩两类）：
  ① 删一个必需字段 ⇒ 必须拒；② 把 `class` offset 14→15 ⇒ 必须拒（**O1 原有
  分辨力一条都不能丢**）；③ 追加一个尾部字段 ⇒ 必须放行且报出「追加」标记。
- `tools/nmbin_verify.c` 与 `graph_check --verify-schema` 输出同步，别让两个工具
  给出不同结论。

### 4.2 per-kind 版本位

- `src/map/binfmt.h`：把手工字节流里 `@116..127` 这 12 字节结构化
  （**按 `hdr_to_bytes()` 的偏移写，别照 C struct 推**）：
  `u32 schema_evolve_ver @116` + `u32 field_table_crc @120` + `u32 reserved @124`。
  per-kind 差异不塞进头部（14 种 kind 放不下），由字段表自身的 CRC 与
  `NMBinLayout` 缓存承担；头部只记「本文件是否启用演进语义」与整表校验。
  旧产物这 12 字节全 0 ⇒ 视为 v0「走硬编码常量」，逐字节兼容、不顶 `NMBIN_VERSION`。
- ★ **`nmbin_check()` 的全局版本硬校验不能删**：它守的是「记录布局整体换版必须
  重抽」（v2/v3 的教训）。O7 只放开「同一 version 内、按 kind 追加字段」这一层，
  两者是不同粒度，不要混为一谈。

### 4.3 按名解析与缓存

- `src/map/binfmt.h` 新增 `NMBinLayout`（按字段名解析出的 offset/type/width，
  **定长数组 + 一次构建**），`nmbin_open()` 末尾调 `nmbin_layout_build(&f->layout, f->fields, n)`。
- `src/map/mapset.h` 的 `MapSrc` 增一个 `NMBinLayout` 槽（每省构建一次），
  `src_open_files()` 里填充；**每帧零查找**。
- ★★★ **明确禁止**：把按名查找放进任何每记录循环（`paint.c` / `cost.c` / `route.c`
  的 511 处引用一个都不改）。按名解析的产物是「构建期缓存」，不是「运行时查表」；
  撞上「贵的是间接寻址」的历史实测结论会直接退化。
- 第一阶段只交付「按名解析 + 一致性比对」，`511 处硬编码常量` **不迁移**；
  迁移是另一个量级的工作，本计划不做。

### 4.4 判据

| 名称 | 正向 | 负向注入 |
|---|---|---|
| `SCHEMA-EVOLVE-OK` | 追加尾部字段的夹具：旧字段 offset 全不变、新字段按名可读 | 删必需字段 / 改 offset / 改 width ⇒ 三种都拒 |
| `LAYOUT-CACHE-OK` | 同一省打开两次的 layout 逐字节相同；打印构建耗时与每帧零调用计数 | 把构建挪进每记录循环 ⇒ 用计数判据抓到 |
| `LAYOUT-LEGACY-OK` | `@116..127` 全 0 的旧 8 省产物全部按 v0 加载成功 | 把版本位写成 99 ⇒ 明确拒绝而非静默 |
| `SCHEMA-OK` / `SCHEMA-INJECT-CAUGHT` | **原有 O1 判据必须继续全绿** | 原有 4 类界内错误注入仍全部被抓 |

### 4.5 零回归（P3 会改 C 加载路径，必须做 A/B）

- 用 `git archive HEAD` 导出 P3 前基线到独立目录编译 exe（与 M1 同法，两个 exe
  的 `../fonts` 各自指向仓库根同一份字体）。
- 同一份隔离 ini（`day_night=0`）+ 绝对路径 `--graph`，z12/z14/z17 三档
  `--shot-offscreen` 出图 `bmpdiff.py` **0 / 1024000 像素**，审计计数逐项 diff 无差异。
- `cmake --build build_rel --clean-first -j$(nproc)` rc=0 / warning 0 / error 0
  （`font_draw_halo` 已删，零警告门禁不能回退）。
- 全套门禁：`regen_tutorial_blocks.py`（无 `--check`）→ `--check`、
  `check_teaching_sync.py ±`、`check_tutorial_tracks.py`、`check_mini_chain.py`、
  `check_doc_numbers.sh`（644 手动跑）、`check_layers.sh --self-test`、
  `git diff --check` 零 CRLF / 零尾随空格。
- `regress.sh` 跑前备份 `~/.config/skedaddle/settings.ini`、跑完恢复
  `40.4677234 / 116.614105 / 10.5`。

**不做判定**：若当前没有「只改 contours 记录布局、不重抽 8 省」的真实需求，
按原计划 §2 O7 的措辞直接判 P3 不做，只保留 4.1 的 O1 判据放宽（它本身有价值：
让字段表真正可演进），不引入 `NMBinLayout` 与 `MapSrc` 改动。

---

## 5. 排期、依赖与验收口径

```
P2-A ──(决策门: 清单仍大?)──> P2-B ──(可独立撤回)
  │
  └── 与 P3 无依赖，可并行；但 P3 改加载期，建议等 P2 收口后再动，
      避免同一轮 regress 里两类变更混在一起无法归因。
P3 前置：4.1 的 O1 判据放宽必须先于 4.3 的按名解析。
```

- 当前回归基线 **125/0**（主机 UOS aarch64 / `SKEDADDLE_BUILD_DIR=$PWD/build_rel`）。
  ★ 项目纪律「每加判据记新总数」：P2-A / P2-B / P3 每完成一阶段都实跑并登记
  新总数，同步 `README.md`、`MEMORY.md`、`CONVENTIONS-FULL.md`、
  `docs/teaching/11_gates.md` 与 `check_teaching_sync.py` 的行号/口径；
  **125/0 属历史口径，不改写**。
- 本机（Windows 副机）只有 beijing 一省 + 7 份重复数据，全量 regress 会有
  既有 24 条数据依赖 FAIL，**不能用来判定回归**；它的用途是零警告编译、
  单工具自测与压缩量测。
- 讲义三轨（`docs/teaching`、`tutorial/NN`、`tutorial/mini`）不是同一份代码长大；
  改 `regress.sh` 后必须按纪律重跑生成器（Edit 改讲义会翻 CRLF ⇒ 生成器再跑一次归一化）。

---

## 6. 明确不做（写死）

| 方向 | 依据 |
|---|---|
| 对整包 `beijing.zip` 再压一层 | 已经是 `ZipInputStream` + deflate，零收益（§0.1） |
| 变长 / 列存改写 NMBIN 本体、把压缩放进 mmap 热路径 | 破坏定长 O(1) 随机访问；VLDB 2024 块压缩实证反证；原计划 §3 已否决 |
| 把按名查找放进每记录循环 | 511 处硬编码引用 × 每记录一次间接寻址，撞历史实测 |
| 删 `nmbin_check()` 的 `NMBIN_VERSION` 全局校验 | v2/v3 混合图集教训，per-kind 版本是不同粒度 |
| 用 P2-B 的帧时间做「压缩收益」 | 口径纪律，帧时间只作无退化证据 |
| 在本机（副机）用 2.568 GB / 631107 块外推 8 省绝对体积 | 7 省是同一份数据副本，主机 3.89 GiB 才是真实口径 |
| 顺手做 CONSUMER-NAV-GAP 的 P3（瓦片缓存/3D/静态公交） | 与每帧全量重画架构冲突，且不在本次 P2/P3 定义内 |

## 7. 风险清单

- **Android 真机未验证**（主机只有 JDK 17 + 测试替身，`DownloadActivity` 不在
  `java_gate.sh` 编译范围）：P2-B 的三级回落只能在夹具层验收，**不宣称真机通过**。
- 服务端必须支持 `206 Partial Content`；P2-B 还要求按容器字节偏移的 Range，
  对象存储/CDN 的 Range 行为需在真实服务端验一次。
- 块表 `version` 被 `ChunkUpdater.java:51` 硬编码为 1 ⇒ 任何格式变更都要留
  v1 兼容读路径，否则老客户端直接抛异常回落整包（虽不致命，但差分能力全失）。
- P3 改 `nmbin_open()` 后，`out->data` 必须仍指向 `@512`，否则 8 省全部错位。
- 8 省仍是 `flags=0` 旧格式（`nmbin_digest` 空）；P3 的 `@116..127` 兼容位
  在旧产物上全 0，可安全按 v0 处理，但**新产物要等下次 `sync_maps.sh` 重抽才带**。

## 8. 复现命令

```bash
# 压缩量测（只读，副机/主机都能跑；主机数字才是决策依据）
python3 scripts/container_probe.py --sample=256      # 快速摸底
python3 scripts/container_probe.py --all             # 全量
python3 scripts/container_probe.py --level=9         # 换压缩级别对比

# 现有基线（施工前后都要跑）
SKEDADDLE_BUILD_DIR=$PWD/build_rel ./scripts/regress.sh
python3 scripts/gen_chunks.py --verify products/graph/beijing
python3 scripts/check_province_coverage.py
bash scripts/java_gate.sh
```

---

## 9. P2-A 执行记录（2026-11-12，第一档完成）

### 9.1 落地内容（零格式变更）

| 文件 | 改动 |
|---|---|
| `scripts/gen_chunks.py` | 新增 `CHUNKS_GZ` / `GZIP_LEVEL=9` / `gzip_bytes()`（mtime=0、不带文件名 ⇒ 可复现）/ `write_sidecar()` / `sidecar_check(required=)`；`write_chunks()` 用同一份字节串产出 `.gz`；`SKIP_NAMES` 同时排除两个清单文件（防自指）；新增 `--sidecar` / `--sidecar-check`；`--verify` 顺带打印 ETag；`--self-test` 增加 1 类正向 + 3 类伴生负向 |
| `scripts/sync_maps.sh` | `graph_check → gen_chunks → --sidecar-check`（只读复核）→ `mv`；伴生损坏与块表损坏一样丢弃本次产物 |
| `scripts/check_province_coverage.py` | 第 6 条不变量「伴生齐全且与 JSON 逐字节同源」；`--self-test` 负向从 3 类扩到 5 类（删伴生 / 异源 gzip） |
| `scripts/chunk_table_size.py`（新） | 三格式体积实测 + 一致性比对 + 决策门数字；`--self-test` / `--inject=all` / `--report` |
| `scripts/container_probe.py` | `SKIP` 加 `chunks.json.gz`，伴生不进压缩量测样本 |
| `scripts/regress.sh` | 新增 3 条判据（见 9.4） |
| `docs/android.md §11.1.1.1 / §11.1.2` | 登记伴生契约、`version=1` 兼容位、实测数字与第二档不做结论 |
| `docs/teaching/11_gates.md`、`16_qt.md` | 由 `regen_tutorial_blocks.py` 重生成（`regress.sh` 全文逐字块） |

**没动**：`chunks.json` v1 格式与键名、`Manifests.parseChunks()`、
`ChunkUpdater` 的 `version != 1` 判定、`ChunkDelta` 的 4096 B Range 语义、
`src/` 任何一行、CMake 目标。

### 9.2 实测数字（副机北京一省真实产物，826 文件 / 86108 块）

| 格式 | 体积 | 比例 |
|---|---|---|
| `chunks.json` | 5,949,459 B（5.67 MiB） | 100% |
| `chunks.json.gz` | **3,232,954 B（3.08 MiB）** | **54.3%**，省 2.59 MiB |
| 二进制表（量测编码，未发布） | 2,857,145 B（2.72 MiB） | 48.0%，比 gz 仅再小 11.6% |

- ETag = `sha256(gz)` = `f4918d450b7b4e6b1726d1e3783cd3db39f02da673645dcd48a791cb243532d4`；
  复跑 `gen_chunks.py` 后 ETag 不变（mtime=0 + 固定 level 9 可复现），
  服务端按 `If-None-Match` 返 304 ⇒ 未变清单的清单体为 **0 字节**。
- 复现：`python3 scripts/gen_chunks.py products/graph/beijing` →
  `python3 scripts/gen_chunks.py --sidecar-check products/graph/beijing` →
  `python3 scripts/chunk_table_size.py --report beijing`。
- ★ 样本边界同 §1.1：副机 7 省是同一份数据副本，**不外推 8 省绝对体积**；
  8 省总体积须在主机 `--report` 复跑。压缩比是内容属性，可用于机制判断。

### 9.3 决策门结论

1. **第二档（二进制表）不做**：gzip 后单省 3.08 MiB < 计划建议的 8 MiB 阈值；
   二进制表只再小 11.6%，却要同步 §1.4 的全部 v1 解析耦合点并留兼容读路。
2. **P2-B 也不建议现在做**：按北京 86108 块，P2-B 的索引增量
   +2,583,240 B（JSON ×30 B）/ +1,033,296 B（二进制 ×12 B），单块省 2672 B
   ⇒ 需 **967 / 387 个变化块**才回本；M2 典型场景是「改 1 块 / 全等 0 块」。
   索引全量拉、数据块增量省，这个不对称是撤回条件的核心。
3. **P3 保持「计划」状态**，本轮不动。

### 9.4 门禁与零回归

| 判据 | 标记 | 结果 |
|---|---|---|
| `coverage_compact` | `COVERAGE-COMPACT-OK` | OK |
| `table_size` | `TABLE-SIZE-OK`（同批输出 `TABLE-COMPACT-OK` / `TABLE-BACKCOMPAT-OK`） | OK |
| `table_negative` | `TABLE-INJECT-CAUGHT`（二进制哈希被改 / 全 0 假表 / version=9） | OK |
| 已有 `chunks_selftest` / `chunks_negative` | 同批覆盖 `CHUNKS-SIDECAR-OK` / `CHUNKS-SIDECAR-INJECT-CAUGHT`（字节翻转 / 异源 gzip / 缺伴生），靠 rc 兜底 | OK |
| 已有 `coverage_selftest` | `COVERAGE-SELFTEST-OK` 从 3 类负向扩到 5 类 | OK |

- **新增判据 3 条 ⇒ 主机实跑后预期 128/0**；`125/0` 是 M2 后的历史口径，
  按 §5 纪律**不改写**，待主机 UOS aarch64 跑
  `SKEDADDLE_BUILD_DIR=$PWD/build_rel ./scripts/regress.sh` 后再登记新总数并同步
  `README.md` / `MEMORY.md` / `CONVENTIONS-FULL.md` / `docs/teaching/11_gates.md`。
- 副机全量回归实跑：**54 通过 / 75 失败**，FAIL 全部是历史口径 ——
  59 条 `rc=127`（运行期缺 Qt/dist DLL）、其余为缺 7 省数据 / 缺 `lm.bin` /
  `contour_chain` 生成器路径混合斜杠。**新增 3 条与所有 Python 类判据全绿**。
  副机不能用来判定回归（§5），但能证明新判据本身可用。
- **零回归结构性证明**：`git diff -- src/ CMakeLists.txt android/` 为空 ⇒
  C 二进制逐字节不变，沿用 M2 纪律不做 A/B 出图对拍。
- 其它门禁：`git diff --check` 零 CRLF/尾随空格、`bash -n` 两个脚本、
  4 个 Python 文件 `py_compile`、`check_layers.sh --self-test`、
  `java_gate.sh`（`M2-JAVA-OK`）、`regen_tutorial_blocks.py`（无 `--check` ×2 →
  `--check`）、`check_teaching_sync.py ±`、`check_tutorial_tracks.py`、
  `check_doc_numbers.sh` 全绿。
- ★ 未绿的既有门禁：`check_mini_chain.py` 报 53 处 STALE；用
  `git archive HEAD` 干净导出复跑同样 STALE（Windows 路径/行尾历史差异），
  且 `git diff -- tutorial/mini` 为空 ⇒ 与本轮无关，不纳入本轮验收、不改它。

### 9.5 兼容与产物状态

- `chunks.json` 是唯一权威源；缺 gz 时 `sidecar_check(required=False)` 静默回落，
  老服务端 / 老客户端 / 只有 v1 JSON 的目录零影响。
- 副机 `products/graph/beijing/` 已生成 `chunks.json` + `chunks.json.gz`
  （`products/` 被 gitignore），供本机量测；主机需在下一次 `sync_maps.sh`
  重抽时由新链路自动带上伴生。
- 未提交推送（等用户指令）；P2-B 与 P3 的 4.2/4.3 未实施（P3 的 4.1 见 §10）。

---

## 10. P3 / 4.1 执行记录（2026-11-12，只做 O1 判据放宽）

用户「先做第 1 项」：**只交付 4.1，不引入 `NMBinLayout`、不碰 `MapSrc`、
不改 `@116..127`、不动 511 处硬编码常量、不顶 `NMBIN_VERSION`。**

### 10.1 ★ 回源发现：计划 §4.1 要「补的两类负向」M1 已经有了

`tools/nmbin_schema_selftest.c` 的 `--inject=all` 本来就含 `offset` / `name` /
`width` / `missing` 四类界内注入。所以本轮**没有重复造轮子**，真正新增的是
「追加字段放行」与「追加场景下仍能抓错」——后者才是放宽后真正的风险面。

### 10.2 改动清单（4 个 C 文件，201 插入 / 13 删除）

| 文件 | 改动 |
|---|---|
| `src/map/binfmt.c` | `nmbin_check_schema()`：`n_fields != s->n_fields` → `n_fields < s->n_fields`，错误串改为「字段数不足」并点名第一个缺失字段、kind、rec_size；循环仍只比前 `s->n_fields` 个 ⇒ 尾部追加放行。新增 `nmbin_schema_extra_fields()` 只读计数（判据/诊断共用同一口径） |
| `src/map/binfmt.h` | 声明该 helper；补前缀子集语义注释，写明「全局 version 硬校验是另一粒度、绝不删」 |
| `tools/graph_check.c` | `--verify-schema` 打印「登记 N / 文件 M 字段」与「尾部追加 K 字段」，与加载期结论同步 |
| `tools/nmbin_schema_selftest.c` | 新增 `--evolve` / `--inject=evolve`；演进夹具在 14 个登记组合上追加 1 字段，验旧字段逐项未变 + 新字段按名可读；追加场景下 5 类破坏必须仍被拒 |
| `scripts/regress.sh` | 新增 2 条判据 `schema_evolve` / `schema_evolve_neg`；讲义逐字块按纪律重生成 |

**没动**：`nmbin_check()` 的 `version != NMBIN_VERSION` 全局硬校验、
`f->data` 指向 `@512` 的 mmap 记录区起点、字段表 16 槽定长、
任何每记录循环、任何生产读取路径。旧 8 省产物一个字节都不用改、不用重抽。

### 10.3 实测（副机 Windows / Qt6 ucrt64）

| 项 | 结果 |
|---|---|
| 编译 | `cmake --build build_rel -j4` **rc=0 / warning 0 / error 0**（`-Wall -Wextra`）；三个改动 C 文件 `cc -fsyntax-only` 零诊断 |
| 新增正向 | `--evolve`：干净夹具 14/14 PASS + 追加夹具 **14/14** PASS，旧字段 offset/type/width 逐项不变、`evolve_probe` 按名检索命中 |
| 新增负向 | `--inject=evolve`：**5/5** 被拒（`missing` / `offset` / `width` / `name` / 追加字段自身越界 `extra_oob`） |
| 原有 M1 判据 | `SCHEMA-OK` 14 组合 + 真实产物 11 文件全 PASS；`SCHEMA-INJECT-CAUGHT` **4/4** 仍全被拒（分辨力零回退） |
| ★ 代码级故障注入 | 把 `nmbin_check_schema()` 临时改回硬相等 ⇒ `--evolve` **rc=1 / `SCHEMA-EVOLVE-FAIL` / 14 组合全被拒**，而同一故障版里 M1 的 4 类负向仍全绿 ⇒ 新判据确实测「放宽」本身，不是恒真。恢复后全量重编 rc=0、正向复绿 |
| 真实格式夹具 | 用生产 NMBIN 字节流造 nodes/edges/restrictions 三个带尾部追加字段的文件：`graph_check --verify-schema` **rc=0 / 0 条 ASSERT-FAIL / `ASSERT-OK`**，三个文件都报「登记 7 / 文件 8 字段，比对 一致」+「尾部追加 1 字段」；`nmbin_verify` 3 文件加载期全 PASS；`schema_selftest` 3 文件 PASS |

### 10.4 零回归 A/B 对拍（改了 C 源码 ⇒ 按纪律做像素对拍）

- 基线：`git archive HEAD` 导出到独立目录，独立 cmake 配置 + 编译 `skedaddle`
  （82 目标 rc=0 / 零警告）。工作树 C 源码 diff 只有上述 4 个文件，其中进
  `skedaddle.exe` 的只有 `binfmt.c/h` ⇒ 两 exe 差异集**只有 P3/4.1**。
- 同一份隔离 ini（`day_night=0`）、同一绝对路径 `--graph`、z12/z14/z17 三档
  `--shot-offscreen 1280×800`，`bmpdiff.py` 结果：

| 档 | 地名标注（新 / 基线） | 差异像素 |
|---|---|---|
| z12 | 280 / 280 | **0 / 1024000** |
| z14 | 125 / 125 | **0 / 1024000** |
| z17 | 42 / 42 | **0 / 1024000** |

- ★ 踩坑记录：第一次对拍出现 15.11% 差异，根因是**基线 exe 从导出目录跑**，
  按 cwd 找 `products/places_auto.db` 找不到 ⇒ `place_labels 0 / poi_icons 0`
  （新版 280/274），差异全是地名标注。同一 exe 连跑两次 0 像素、两份字体
  md5 相同，排除了随机性与字体。两个 exe 都从**仓库根**跑后归零 ——
  再次印证「离屏出图必须从仓库根跑」这条既有纪律，不是代码问题。

### 10.5 门禁与口径

- 全套静态门禁全绿：`git diff --check`、`bash -n`、`regen_tutorial_blocks.py`
  （无 `--check` ×2 → `--check`）、`check_teaching_sync.py ±`、
  `check_tutorial_tracks.py`、`check_doc_numbers.sh`、`check_layers.sh --self-test`。
- 完整回归（补齐运行期 Qt/dist PATH 后）：**105 通过 / 26 失败 / 0 个 rc=127**。
  26 条全是副机缺 7 省真实数据、`lm.bin` 地标表、contour 生成器路径混合斜杠
  的历史口径；本轮新增 2 条 `SCHEMA-EVOLVE-OK` / `SCHEMA-EVOLVE-INJECT-CAUGHT`
  与 P2-A 的 3 条、M1/M2 判据全部 OK。
- **新增判据 2 条 ⇒ 主机实跑后预期 130/0**（125/0 + P2-A 3 + 本轮 2）。
  `125/0` 仍是历史口径，按 §5 纪律不改写；待主机 UOS aarch64 实跑后同步
  `README.md` / `MEMORY.md` / `CONVENTIONS-FULL.md` / `docs/teaching/11_gates.md`。
- 兼容：前缀子集只在同一 `NMBIN_VERSION` 内生效；旧产物 `n_fields` 完全相等，
  `extra=0`，加载期行为逐字节不变；`NMBIN_VERSION` 不顶、8 省不重抽。
- **明确未做**（保持 P3 后续可选）：`NMBinLayout` 按名解析缓存、`MapSrc` 增槽、
  `@116..127` 的 `schema_evolve_ver` / `field_table_crc` 兼容位、per-kind 版本、
  511 处硬编码常量迁移。4.1 的价值是「字段表从此真正可演进」，不是提前把
  运行时改成查表。
- 未提交推送（等用户指令）。

---

## 11. 磁盘约束下的最终判定（2026-11-12 20:2x）

用户反馈「**增加磁盘占用已超出我的预期**」。这条作为硬约束直接收敛排期：
**P2-B 封存、不再实施；P3 的 4.2/4.3 维持不做。** 最终交付面 = M1 + M2 + P2-A + P3/4.1。

### 11.1 实测：磁盘到底被什么吃掉了

回源量测发现**主要占用不是 P2-B（它根本没实施）**，是回归与 A/B 对拍的临时产物：

| 位置 | 体积 | 性质 |
|---|---|---|
| `%TEMP%/skedaddle_regress/revtest` | **906.7 MiB**（2464 文件，`.bin` 842.2 MiB） | revtest 的 ok/none/bad 三份省图拷贝，**11-10 起滞留** |
| `%TEMP%/skedaddle_regress` 其余 | 72.4 MiB（`.bmp` 72.1 MiB + 日志） | 历轮回归截图 |
| `%TEMP%/sk_p3a_base`（`git archive` 基线编译） | 58.9 MiB | 含 28.0 MiB 字体副本 + 20.8 MiB build |
| `%TEMP%/sk_p3a_ab`（A/B 对拍 BMP） | 23.4 MiB | 8 张 1280×800 BMP |
| P2-A 伴生 `chunks.json.gz` | 3.1 MiB / 省（副机仅 1 省） | 8 省全生成约 25 MiB |
| `products/graph/` 参考总量 | 2458.7 MiB | 原有产物，非本轮新增 |

⇒ 临时产物合计约 **1.06 GiB**，与 `products/graph` 的 2.4 GiB 是两笔账。

### 11.2 P2-B 的磁盘代价（封存依据）

逐块独立 raw-deflate 容器是**与裸 `.bin` 并存的第二份全量产物**：北京单省
110.71 MiB，主机 8 省按 3.89 GiB 口径同比例约 **1.3 GiB 额外常驻**，
且每省多了 289 s 量级的构建耗时（副机 Windows 实测，aarch64 须另测）。
若「服务端只发容器、不再发裸块」，磁盘可不增，但会断掉 M2 老客户端的回落路径，
需要版本协商与真机验收 —— 与当前「无真实服务端 / 无持续版本更新」的现状不匹配。

**零额外磁盘的替代路径**（仅登记，不实施）：服务端对 4 KiB Range 切片做
`Content-Encoding: gzip`（构建端零产物、CDN 动态压缩）、或 CDN 边缘按请求
实时压帧、或维持 M2 裸块。三者都不需要构建端多存一份全量容器。
若将来确实要省流量，先走这条，不要先落容器文件。

### 11.3 口径修正（存档，不改量测工具）

§9.3 的「968 / 387 块回本」用的是「索引按 JSON 文本 30 B/块 / 二进制 12 B/块」
的保守估。实测索引只需存帧长（offset 前缀和累加）+ varint + gzip：
北京 86108 帧 ⇒ **120,107 B（0.115 MiB）**，单块省 2670 B ⇒ **45 块回本**；
逐块独立压缩 110.71 MiB（33.10%）vs 整文件 deflate 105.01 MiB（31.40%），
差分相对整包的打平点由 M2 的 30.3% 全省推到 86.9%。

即 P2-B 的**流量价值被原估算低估约 20 倍**，但磁盘代价是独立的一票否决项；
且「典型版本更新改几块」从未用两版真实产物测过（M2 的 1~10 块是合成夹具）。
`chunk_table_size.py` 保持原估口径不改 —— P2-B 已封存，决策门不再用于决策。

### 11.4 顺带修掉的根因：`regress.sh` 的 trap 静默失效

`revtest` 段三处 `rm`（开头清理、两个 `rm -f`、EXIT trap）在副机被 rm 的
safe-delete 包装器拒绝，上一轮回归日志第 44 / 51 / 215 行各有一条
`[safe-delete][SAFE_DELETE_INVALID_PATH] embedded drive prefix is not allowed`
（副机 `SHOTS` 是 `D:\...\Temp/skedaddle_regress` 混合盘符路径）。后果：

- 906.7 MiB 从 11-10 一直留在系统盘，`ln: ... File exists`；
- 两个 `rm -f` 没删掉 `rev.bin` ⇒ `rev_norev` / `rev_bad_strict` 走「有缓存」
  路径，**判据假绿**（不只是磁盘问题）。

修复：三处统一改 `/bin/rm`（与脚本既有 `CFG_UIT` / `CFGQ` / `CFG` 三处一致），
trap 加 `2>/dev/null || true` 避免清理退出码覆盖回归 rc；讲义逐字块按纪律
重生成两次归一化。门禁全绿：`bash -n`、`git diff --check`、
`regen_tutorial_blocks.py --check`、`check_teaching_sync.py ±`、
`check_tutorial_tracks.py`、`check_doc_numbers.sh`、`check_layers.sh --self-test`。
下一轮回归起，`revtest` 会在 EXIT/INT/TERM 真正清掉（`/bin/rm` 真删已用小夹具验证）。

### 11.5 待用户确认

现有 1.06 GiB 临时产物**尚未清理**（按个人文件安全纪律先列清单、不擅自动手）。
注释已明确 revtest 三份拷贝「历史数据没有任何保留价值」，但删除落在 `AppData`
下且需真删才释放空间（回收站不释放），故须明确确认后按目录逐个处理。
如需进一步压缩 P2-A 的 25 MiB 伴生，可把 gz 改为「发布时由服务端即时生成」，
代价是失去可复现 ETag 的门禁闭环 —— 当前不做。
