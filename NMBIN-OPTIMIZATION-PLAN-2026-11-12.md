# NMBIN 优化改进计划（2026-11-12）

> 配套评估：[`NMBIN-OPTIMIZATION-2026-11-12.md`](NMBIN-OPTIMIZATION-2026-11-12.md)（只读评估，308 行）。
> 本文件是**施工计划**：只列改动点、判据、负向对照与验收纪律，**不改任何源码、产物、配置**。
> 计划里的每条任务都必须能回答三个问题：改哪个文件、怎么证明做对了、怎么证明没改坏。

---

## 0. 排序原则（先说为什么不从性能开始）

三个过滤器，按序生效：

1. **不碰运行时热路径** —— 帧时间已被 8 轮实测测到头（软件预取 455→452 ms、16K 节点缓存 460→487 ms、L2 内联坐标只值 16 ms 却 +139 MB、`-O3`/`-march=native` 零收益）；评估报告 §1 / §5.4 已判过。
2. **判据必须能失败** —— 每条正向判据都配一条改「被检查的数据」的负向注入，而不是把判据条件改成常量。
3. **不逼 8 省全量重抽** —— 能用兼容性位（`flags` / `reserved` 语义）解决的，不顶 `NMBIN_VERSION`。

⇒ 结论：**先做健壮性与增量更新地基（O1–O5），压缩单独一层最后做（O6，可独立撤回），性能类一律不做**。
评估报告的四个目标里，只有 ③体积 与 ④健壮性/增量更新 有真实空间；①帧时间 与 ②算路耗时 没有。

---

## 1. 阶段总览

| 阶段 | 任务 | 优先级 | 帧时间影响 | 需重抽 8 省 | 交付判据 |
|---|---|---|---|---|---|
| **M1 健壮性** | O1 字段表一致性校验 | P0 | **零**（只加加载期一次性比对） | 否 | `SCHEMA-OK` |
| | O2 产物内容摘要 | P0 | **零**（不在加载期做全量哈希） | 否 | `NMBIN-HASH-OK` |
| **M2 增量更新** | O3 4 KiB 块哈希 sidecar | P1 | 零 | 否 | `CHUNKS-OK` |
| | O4 Android manifest 接哈希 | P1 | 零 | 否 | 下载后拒错包 |
| | O5 按块差分下载 | P1 | 零 | 否 | 改 1 块只传 1 块 |
| **M3 体积** | O6 外层块对齐压缩容器 | P2 | 零（运行时仍读零解码 NMBIN） | 否 | `CONTAINER-OK` |
| **M4 可选** | O7 按名解析 + per-kind 版本 | P3 | 零（每省构建一次缓存） | 否 | `SCHEMA-EVOLVE-OK` |

依赖：`O1 → O7`；`O2 + O3 → O4 → O5`；`O6` 依赖 `O2/O3` 提供的校验地基，但可独立撤回。
M1 可单独交付，不必等后面阶段。

---

## 2. 逐任务卡片

### O1 字段表 ↔ 硬编码 schema 一致性校验（P0，建议先做）

- **现状**：`nmbin_open()` 只把 `fields[]` 读进来不消费，生产路径 101 处全是硬编码 `NF_*`/`EF_*`；`nmbin_check()`（`src/map/binfmt.c`）只验 5 条结构自洽，对「offset 改到界内另一值」无分辨力（评估报告 §3.1 已在内存复现：改成 15 仍 `PASS`）。
- **改动**：
  - `src/map/binfmt.c`：新增 `static const NMBinField nmbin_expected(kind)` 期望表（name / offset / type / width 四元组），在 `nmbin_check()` 末尾逐项比对；错误串要点名具体字段。
  - `tools/graph_check.c`：加 `--verify-schema`，打印「文件字段表 ↔ 期望表」逐项结果。
  - 新增 `tools/nmbin_schema_selftest.c` + CMake target（走 `sk_link_tool()`）。
- **不要做**：把字段查找放进每条记录的循环里 —— 那会把「一次间接寻址」放大成「每记录一次」，正好撞上「贵的是间接寻址」的实测结论。彻底路线留给 O7。
- **正向判据**：`SCHEMA-OK`，8 省 × 每类 `.bin` 全 PASS；`graph_check --verify-schema` 逐项一致。
- **负向注入**：`--inject` 在内存夹具把 `edges.bin` 的 `class` offset 14→15（仍在 `rec_size=18` 界内）⇒ 必须输出 `SCHEMA-INJECT-CAUGHT`；再注入「删一个必需字段」⇒ 加载必须拒绝。
- **零回归**：改前/改后同一份发布包 exe 的 `--shot-offscreen` 出图 **bmpdiff 0 像素**；全量 regress 绿。
- **兼容性**：现有产物字段表本来就是按这些常量写的 ⇒ 存量 8 省无需重抽、无需顶版本号。

### O2 产物内容摘要（P0，为 OTA 兜底）

- **现状**：`NMBinHeader.reserved[44]` 全空，全链路零 checksum；`autobuild.c:prov_check()` 验的是**源 PBF 指纹**，不是产物；`sync_maps.sh` 的 `manifest.txt` 也只记源指纹与格式版本（评估报告 §3.2）。
- **改动**：
  - `src/map/binfmt.h`：`flags` 用 bit0 表示「本文件含内容摘要」；`reserved[44]` 里划出摘要区（建议全文件 sha256/xxHash 128 + 算法/版本标记）。
  - ★ **偏移必须回源 `binfmt.c:hdr_to_bytes()` 确认**：NMBIN 头是**手工序列化**（`rec_count` 在 `@20`，不是 C struct 对齐后的 `@24`），不能照 struct 布局推断。
  - `nmbin_finalize()` 写完记录区后回填摘要；新增 `tools/nmbin_verify.c` 做**离线全量**校验。
  - `nmbin_check()` 只验「摘要区自洽 + 标记位」，**不在这里算全文件哈希**。
- ★★ **关键约束（最容易做错的一步）**：绝不能在 `nmbin_check()` 的加载期对 3.89 GiB 产物做全文件哈希。那会强制 mmap 全部分页读入，直接破坏 `mapset_set_view` 的按需加载 + LRU，冷启动变成灾难。全量校验只放在**边界**：OTA 下载完成后、安装/落盘时、`nmbin_verify` 离线工具。
- **正向判据**：`NMBIN-HASH-OK`，8 省全文件离线校验 PASS；标记位与摘要区长度自洽。
- **负向注入**：`--inject` 翻转 1 字节 / 截断最后一块（长度补齐）/ manifest 换错哈希 ⇒ 三种都必须 `NMBIN-HASH-INJECT-CAUGHT`。
- **兼容性**：`flags.bit0=0` 的旧产物跳过摘要并打印告警，**不顶 `NMBIN_VERSION`、不逼 8 省重抽**；新产物由下次 `sync_maps.sh` 自然带上。

### O3 4 KiB 块哈希 sidecar（P1，差分更新的地基）

- **改动**：
  - 新增 `scripts/gen_chunks.py`：对每省每个 `.bin` 按 4096 B 切块，输出 `<省>/chunks.json`（块数、每块 sha256、整包 sha256、源文件大小）。
  - `scripts/sync_maps.sh`：在 `skedaddle_graph_check` 通过**之后**生成块表（失败产物不生成）。
  - `scripts/check_province_coverage.py`：增加「块表齐全」不变量；`--self-test` 继续用临时夹具（正常检查不进 regress，避免副机缺 7 省假红）。
- **正向判据**：`CHUNKS-OK`，块数 = ceil(size/4096)，整包哈希与 O2 摘要一致。
- **负向注入**：`--inject` 改一个块哈希 / 删一个块条目 ⇒ 必须 `CHUNKS-INJECT-CAUGHT`（改的是被检查的数据）。
- **边界**：块表是**校验清单**，不是「把 NMBIN 变成分块格式」；运行时读取路径一个字节都不改。

### O4 Android manifest 接哈希（P1）

- **现状**：`Manifests.java:21-27` 的 `ProvinceEntry` 只有 `id/name/version/size/url`，`DownloadActivity` 按整包 URL 下载后直接安装。
- **改动**：`ProvinceEntry` 增 `sha256` / `chunksUrl` / `chunkSize`；`Downloader.java` 下载完先算整包哈希再落盘；`docs/android.md §11` 补清单格式。
- **正向判据**：带哈希的夹具解析正确、校验通过后才触发安装；缺哈希时允许解析但标记「未校验」（向后兼容老服务端）。
- **负向注入**：喂错 `sha256` ⇒ 必须拒绝安装并删除半包。
- **风险**：Android 真机 GPS / 离线 TTS 尚未验证，本项目最大未闭环项 ⇒ 本阶段只能在编译与单元夹具层验收，**不宣称真机通过**。

### O5 按块差分下载（P1，依赖 O3 + O4）

- **改动**：客户端比对本地 `chunks.json` 与远端，只下载哈希变化的 4 KiB 块，按偏移原地重组；生成端 `sync_maps.sh` 不动。
- **正向判据**：合成夹具「改 1 块 ⇒ 只传 1 块」「全等 ⇒ 传 0 字节」「重组后整包哈希 == 远端哈希」。
- **负向注入**：重组时故意写错一个块 ⇒ 必须被 O2 摘要 / O3 块表抓到，不能静默加载。
- **边界**：只有 O2 整包摘要 + O3 块表都在，这条才闭环；单独做「按 version 整包重下」不算差分。

### O6 外层块对齐压缩容器（P2，体积，可独立撤回）

- **依据**：实测零字节 41%~56%、低熵字段位利用率 <10%、坐标 delta 可省约一半；但 VLDB 2024 列存实证显示**块压缩损害端到端查询速度**，且变长编码会破坏 `rec_size` 定长与 O(1) 随机访问。⇒ 压缩只能放在**分发/打包层**。
- **改动**：新增 `scripts/pack_container.py`（4 KiB 块对齐压缩 + 块索引）与 `scripts/unpack_container.py`；安装时解压出**逐字节相同**的 NMBIN，运行时仍走 mmap 零解码。EROFS（USENIX ATC '19）只是思路来源，本机 UOS aarch64 无内核 EROFS 挂载 ⇒ 做**用户态块解压**，别原样搬论文方案。
- **正向判据**：`CONTAINER-OK`，解压前后 NMBIN **md5 逐字节相同**；报 OTA 包体积与冷启动解压耗时两个数字。
- **负向注入**：故意解压错一个块 ⇒ 被 O2/O3 校验抓到。
- ★ **口径纪律**：帧时间只作「无退化」证据（bmpdiff 0 像素 + 多轮最小时间不退化），**绝不在压缩收益里混入帧时间收益**，两者分开报。
- **前置**：先量 aarch64 用户态解压冷启动耗时；若代价超过 OTA 体积收益，直接撤回本阶段，不影响 M1/M2。

### O7 按名解析 + per-kind 版本（P3，可选，彻底路线）

- **改动**：`nmbin_open()` 构建期按字段名解析偏移、把结果缓存进 `MapSrc`（每省一次、每帧零开销）；用 `flags`/`reserved` 放 per-kind 版本，支持字段追加不重抽。
- **收益**：解决 `binfmt.h:19-26` 自己承认的「全局单版本号」问题 —— 将来只改 `contours` 记录布局时，不必重抽 8 省路网。
- **前置**：O1 全绿后才做；**明确禁止**在每记录循环里查字段。
- **判据**：`SCHEMA-EVOLVE-OK`，追加一个尾部字段的夹具能按名读到新字段、旧字段偏移全不变；负向：删必需字段必须拒绝。

---

## 3. 明确不做（写死，避免重复劳动）

| 方向 | 否决依据 |
|---|---|
| 变长 / 列存改写 NMBIN 本体 | 每条记录加解码，撞「瓶颈在每要素固定开销」实测；破坏定长 O(1) 随机访问；VLDB 2024 块压缩实证反证 |
| `rec_size` 对齐（18→32 / 28→32）、SIMD 解码 | 历史实测「改 stride 只值 7 ms」「L1 紧凑坐标表 +397 MB 只值 7 ms」；`-O3`/`-march=native` 零收益；8 省体积再涨约 0.5 GiB |
| zone map / min-max skipping | `tiles.bin` 的 `[off,cnt)` 目录 + `disp.idx` 分 tile 已是等价物，论文只是印证方向正确 |
| 顶点/边空间重排（Hilbert / GMDO） | `edgelist/rev/lanes/lm/restrictions` 全按内部下标绑定 ⇒ 全量重抽 8 省 + 正确性风险；且瓶颈在渲染不在算路 |
| 换语言（C→Rust）、编译器调优 | 走同一 LLVM 后端，`-O3`/native 实测零收益 ⇒ 1:1 移植预期 0%（±5%） |
| 把校验放进 `nmbin_check()` 加载期 | 会强制 mmap 全部读入 3.89 GiB，破坏按需加载（见 O2 关键约束） |

---

## 4. 判据矩阵（沿用评估 §6 与项目纪律）

| 任务 | 正向判据 | 负向注入（必须 FAIL） | 零回归证据 |
|---|---|---|---|
| O1 | `SCHEMA-OK` 8 省全 PASS | 字段 offset 界内错位 / 删必需字段 | `--shot-offscreen` bmpdiff 0 像素 |
| O2 | `NMBIN-HASH-OK` 8 省全 PASS | 翻 1 字节 / 截断补齐 / 换错哈希 | 同上 + 冷启动按需加载页数不增 |
| O3 | `CHUNKS-OK` 块表齐全 | 改块哈希 / 删块条目 | `check_province_coverage.py` 正常检查仍绿 |
| O4 | 带哈希夹具校验后安装 | 错哈希 ⇒ 拒绝安装 | 老服务端无哈希仍可解析 |
| O5 | 改 1 块只传 1 块 / 全等传 0 | 重组写错块 ⇒ 被校验抓到 | 重组产物 md5 == 原产物 |
| O6 | 解压 md5 逐字节相同 | 解压错一块 ⇒ 被校验抓到 | 帧时间只作无退化证据，不报收益 |
| O7 | 追加字段按名可读 | 删必需字段 ⇒ 拒绝 | 旧字段偏移逐项不变 |

---

## 5. 全局验收与固定动作（每个阶段都要过）

1. `cmake --build build_rel --clean-first -j$(nproc)` → **rc=0 / warning 0 / error 0**；新增工具走 `sk_link_tool()`，不越 `check_layers.sh` 分层。
2. `SKEDADDLE_BUILD_DIR="$PWD/build_rel" ./scripts/regress.sh` → 当前基线 **115/0**（M1 后）；每加一条判据记新总数，旧数字作废。
3. 改过 `scripts/regress.sh` ⇒ 跑 `python3 scripts/regen_tutorial_blocks.py`（无 `--check`）再 `--check`；动 `binfmt.h` ⇒ 回源同步 `docs/teaching/03_binary.md`（讲二进制格式那一课）。
4. `bash scripts/check_layers.sh --self-test`、`check_teaching_sync.py`、`regen_mini_chain.py --check`、`check_doc_numbers.sh` 全绿。
5. 完整 regress 会写脏 `view_*` ⇒ 跑前备份 `~/.config/skedaddle/settings.ini`、跑完恢复（`40.4677234 / 116.614105 / 10.5`）。
6. 改前/改后出图**用同一份发布包 exe**（别从 `build_rel/` 拷出，字体按 exe 相对 `../fonts` 解析）；判「成功」看退出码，不是输出行数。
7. `git diff --check` 零 CRLF / 零尾随空格；每阶段完成即 commit + push origin main。

---

## 6. 里程碑与交付边界

- **M1（O1+O2）✅ 2026-11-12 已完成并验收**（见 §8）：纯健壮性，零帧时间影响、零重抽、零性能收益宣称。
- **M2（O3+O4+O5）** 才让「增量更新」从「整包重下」变成真正差分；Android 侧只到编译/单元夹具层。
- **M3（O6）** 只报 OTA 体积与冷启动解压耗时，可独立撤回。
- **M4（O7）** 是架构演进选项，不是缺陷修复；没有「contours 频繁改布局」的真实需求就不必做。

**别高估**：文献平台是 x86/服务器列存与 Android eMMC，与本机 UOS aarch64 不同，只迁移机制不迁移倍数；评估里的 varint 节省量是编码层理论值，未计控制流与解码成本；本机**没有真实 SRTM DEM / 新省 PBF**，压缩与差分判据都用合成夹具；Android 真机仍未验证。

---

## 8. M1 执行记录（2026-11-12，O1 + O2 已落地）

> 本节是**施工结果**，不改写 §2 的任务卡片（那里仍是原始设计）。所有数字为本机
> UOS aarch64 / `build_rel` Release 实跑，不是估算。

### 8.1 改动清单

| 文件 | 内容 |
|---|---|
| `src/utils/sha256.{h,c}`（新） | 最小流式 SHA-256（FIPS 180-4），纯 C 零依赖、带 `extern "C"`；新叶子库 `skedaddle_sha256` 挂进 `skedaddle_map` / `sk_link_tool` / `skedaddle_core` / 主程序链接面 |
| `src/map/binfmt.h` | `NMBIN_FLAG_DIGEST` bit0、`NMBIN_DIGEST_SHA256`、摘要区偏移常量（`@76/@108/@112`）；`reserved[44]` 结构化成 `digest[32]+algo+ver+reserved[4]`（struct 仍 128 B）；`NMBinSchema` 类型与三个新接口声明 |
| `src/map/binfmt.c` | ①14 种 `(kind, rec_size)` 期望 schema 表（从 8 省真实产物反推并与写入器交叉验证）+ `nmbin_check_schema()` 在加载期逐项比对；②`hdr_to_bytes`/`bytes_to_hdr` 读写摘要区；③`NMBinWriter` 带 `Sha256`，`nmbin_write_records()` 流式喂、`nmbin_finalize()` 定稿并置 flags；④`nmbin_check()` 只验摘要区**自洽**（不算哈希）；⑤`nmbin_verify_digest()` 边界离线校验 |
| `tools/nmbin_schema_selftest.c`（新） | 14 组合夹具 + 真实产物扫描；`--inject=all` 四种界内语义错误（offset/name/width/missing）必须被拒 |
| `tools/nmbin_verify.c`（新） | FIPS 三条标准向量（含百万字节流式）+ 写入/校验全链；`--inject=all` 四种文件级损坏（位翻转/半包补齐/截断/错哈希）；目录模式统计 sealed/legacy/mismatch |
| `tools/graph_check.c` | 新增 `--verify-schema`，逐字段打印「期望 vs 文件」；不带参数时行为与旧版完全一致（`sync_maps.sh` 依赖） |
| `scripts/regress.sh` | 新增 6 条判据（940 行） |
| `docs/teaching/03_binary.md` | 5 个 C 逐字块按当前源码回源重贴并逐字校验一致；6 处行号引用更新；新增 M1 演进补记 |
| `docs/teaching/11_gates.md` | `regen_tutorial_blocks.py` 重生成（regress.sh 改动后按纪律） |

### 8.2 关键实现决策（都是回源后才定的）

- **期望表按 `(kind, rec_size)` 索引，不是按 kind**：`NMBIN_KIND_LANES` 被 `lanes.bin`(rs=4) 与 `lane_names.bin`(rs=1) 共用；`restrictions.bin` 的 `prev_edge` 槽(@16) 根本没登记字段 ⇒ 必须从写入器实际调用序列复制，不能从 `*_BYTES` 推导。14 种组合全部覆盖，未登记组合返回 `NULL` 跳过比对。
- ★ **踩坑并修正**：注释里写 `NF_*/EF_*/XL2_*` 时那个 `*/` 会提前闭合块注释（首轮编译 1641 个 error）。已按项目纪律改成「NF、EF、XL2 系列」，并用脚本扫过全部改动文件的注释残留。
- ★★ **按计划纠偏**：报告 §5.2 原写「加载期多一次全文件哈希，冷启动一次性」；回源发现 `mapset_set_view` 是按需 mmap + LRU，在 `nmbin_check()` 里算全文件哈希会强制读满 3.89 GiB ⇒ 改为**加载期只验头部自洽，全量重算只放 `nmbin_verify_digest()` 边界工具**。这是 M1 零帧时间影响的保证。
- **兼容性**：`flags` bit0=0 的旧 8 省产物跳过摘要并如实统计为 legacy，**不顶 `NMBIN_VERSION`、不逼重抽**；`prov_check` 的指纹逻辑（源 PBF + binfmt_version）不受摘要字节影响，已回源确认。
- **SHA-256 正确性**：三条 FIPS 向量先与 Python `hashlib` 独立核对后才写进判据，避免记错期望值造成假红。

### 8.3 验收（全部实跑）

- `cmake --build build_rel --clean-first -j$(nproc)` → **rc=0 / warning 0 / error 0**。
- `SKEDADDLE_BUILD_DIR=$PWD/build_rel ./scripts/regress.sh` → **115 通过 / 0 失败**（109 + 6）；6 条新判据全绿。
- 8 省真实产物：`SCHEMA-OK` 扫 64 个 NMBIN 文件全 PASS；`NMBIN-LEGACY-OK` 统计 sealed=0 / legacy=64 / mismatch=0。
- **零回归对拍**：从 `HEAD`（M1 前的 `b021e9f`）`git archive` 导出到临时目录独立编译基线 exe，与 M1 后 exe 用同一份隔离 ini（`day_night=0`）、同一份 `--graph` 在 z12/z14/z17 三档出图，`bmpdiff.py` 均 **0 / 1024000 像素**，审计计数逐项 `diff` 无差异。
- 全套门禁绿：`regen_tutorial_blocks.py --check`、`check_teaching_sync.py ±`、`check_tutorial_tracks.py`、`regen_mini_chain.py --check`、`check_mini_chain.py`、`check_doc_numbers.sh`、`check_layers.sh --self-test`、`check_province_coverage.py ±`、`contour_chain_check.py ±`、`git diff --check`。
- 回归写脏了 `settings.ini`（`view_*` 变默认值 + 追加 8 个键），已按纪律恢复为 `40.4677234 / 116.614105 / 10.5`。

### 8.4 边界 / 留给 M2

- 现有 8 省产物**仍是旧格式**（无摘要）；摘要只在新写入/`sync_maps.sh` 下次重建时自然带上。
- O1 走的是「轻量路线」：只校验字段表与硬编码 schema 一致，**没有**做 O7 的按名解析与 per-kind 版本；`nmbin_check` 的新比对只在加载期做一次，没有进每记录循环。
- O3 的 4 KiB 块哈希、O4 的 Android manifest 哈希、O5 的差分下载**尚未开始**；`nmbin_verify_digest()` 已为它们准备好整包摘要地基。
