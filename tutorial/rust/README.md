# `tutorial/rust/` — Rust 版「从最小框架长到迷你导航端」演化链

> 这是 C 版三条学习资料之外的第 4 份：**用 Rust 把同一份工程从最小框架重新长一遍**。
> 回答的问题与 `tutorial/mini/` 相同——「教程是不是从最小框架开始，逐步完善？」——
> 但换一门语言，让学生在**两种语言里做同一组不变量**，看哪些不变量交给程序员（C），
> 哪些交给类型系统（Rust）。

## 一、与 C 版资料的关系

| C 版资料 | 形态 | Rust 版对应 |
|---|---|---|
| `docs/teaching/NN_*.md` | 18 篇源码导读讲义（读最终 `src/`） | 本 README 的课程映射表 + `src/main.rs` 各段头注释（**为什么不这么写**是逐段讲透的；Rust 生态另有 idiom 讲义，不重复） |
| `tutorial/NN_*/` | 17 个单课独立小程序 | 未单独展开——每步自包含在单文件的一个段里，职责相同 |
| **`tutorial/mini/`** | **同一份 C 工程的 18 个连续快照** | **`src/main.rs` 单文件演化链：15 段（step00–14，覆盖全部核心课）+ 锚点，截断即历史快照** |

与 C 版 mini 链同样的硬规则：

```
第 0 段  step00: frame      → 一块像素缓冲、画点、Bresenham、24 位 BMP
第 1 段  step01: coords     → 追加 1e-7 度量化、墨卡托正反算、经纬网
 ...
第 7 段  step07: routing    → 追加 Dijkstra + 路径回溯 + 画路线
第 8 段  step08: pipeline   → 追加一帧的 9 阶段管线编排（底色只校验不重涂）
 ...
第 14 段 step14: i18n       → 追加中文 msgid 查表回退 + XML 实体解码
```

- `// ============ stepNN: <主题> ============` 段**只能追加**；
- `fn main` 里 `// @@CALL@@` 锚点前**只能插入**一行 `step_NN(...)`；
  （另有 `// @@STEP@@` 锚点标记新段插入点）
- **把文件在各段边界截断 = 第 N 步的历史快照**（单文件锚点 = C 版的 stepNN 目录）；
- 每步打印 `RUST-MINI-STEP-NN-OK`，末行 `RUST-MINI-CHAIN-OK steps=N ink=… gates=ok/fail`，
  退出码即门禁——`ink`（非背景像素数）与 `gates` 都是确定性量，**不用时间当判据**
  （项目既有纪律）。dev / release 两档的 `ink` 必须逐像素一致。

## 二、运行

```bash
cd tutorial/rust
cargo run            # dev 档：RUST-MINI-CHAIN-OK steps=15 ink=9370 gates=47/0，退出码 0
cargo run --release  # release 档：同一组数字（确定性判据与优化级别无关）
```

产物：`out.bmp`（320×240，纯内存缓冲 + 手写 24 位 BMP，**零外部 crate**——
纯 std，对应 C 版「零 SDL/Qt 依赖」，离线可编译）；`mini_graph.bin`（step03 的
自描述二进制，见下）。

## 三、课程映射（Rust 段 ↔ C 讲义课 ↔ Rust 语言教学点）

| Rust 段 | C 讲义 | 这一段往工程里加了什么 | Rust 语言点（与 C 版的核心差异） |
|---|---|---|---|
| `step00: frame` | 00_overview | 像素缓冲、Bresenham、BMP 写出、判据宏 | 结构体 + `&mut self`；C 的 `ctx*` → 借用检查；越界写进 panic 而非 UB |
| `step01: coords` | 01_coords | 1e-7 度量化、墨卡托正反算、haversine、经纬网 | `f64↔i32` 必须显式转换，无隐式截断 |
| `step02: pbf` | 02_pbf | varint / 字段号 / zigzag（自编自解） | 切片 + `Result`——截断返回 `Err` 而不是 UB |
| `step03: binary` | 03_binary | magic + section 表 + 小端定长记录；文件读写 | `to_le_bytes` 显式小端（禁掉 C 的 `*(u32*)(hdr+2)` 未对齐强转）；负向判据：坏魔数必被拒收 |
| `step04: graph` | 04_graph | 前向星（CSR）、度数、BFS 可达性、画路网 | 迭代器 `all/any`；`VecDeque` BFS |
| `step05: viewport` | 05_viewport | 视口（中心 + 跨度）、墨卡托→屏幕投影 | 方法语法；变换写成纯函数，判据才能逐条核对 |
| `step06: raster` | 06_raster | Cohen–Sutherland 线段裁剪 | 模式匹配 + `Option`——"可能没有结果"进类型系统，调用方被迫处理全出界分支 |
| `step07: routing` | 09_routing | Dijkstra + 懒删除 + 路径回溯 + 画路线 | `BinaryHeap` + `Reverse` 组成最小堆；图只读借用、结果独立分配 |
| `step08: pipeline` | 07_pipeline | 一帧的 9 阶段管线编排（底色只校验不重涂、chrome 最后盖） | `Vec<u8>` 阶段表整体比对；`&mut self` 独占借用逼你排好「画/读」次序 |
| `step09: search` | 08_search | 地名子串检索 + 命中锚点 | `str::contains` 按 Unicode 字符匹配（strstr 按字节会撞 UTF-8 续字节）；返回 `Vec<usize>` 替代 out+cap |
| `step10: navvoice` | 10_navvoice | 机动指令生成 + 15s 同文本冷却 + 车头 | C 的函数级 static → 显式 `VoiceOut` 状态结构体：谁在改状态写进类型 |
| `step11: gates` | 11_gates | 判据框架 + 负向对照能失败 | 闭包 `Fn(i32)->bool` 当判据一等公民（C 用函数指针 + void\*） |
| `step12: capstone` | 12_capstone | 改一个点 → 加判据 → 负向对照闭环 | `Cell<i32>` 内部可变性：`&Cell` 也能 `set`，把「谁能改」限定进单个单元 |
| `step13: datum` | 13_datum | 四基准登记 + 三参数平移 + 「只换椭球 ≠ 基准转换」 | 查不到返回 `Option<&Datum>` 而非 NULL，忘判空在编译期消失 |
| `step14: i18n` | 14_i18n | 中文 msgid 查表回退 + XML 实体解码（单遍防二次解码） | 返回 `&str` 借用静态表绝不返回空；`String` 所有权让 out+cap 缓冲约定消失 |

> 段号与 C 课号**不**一一对应：演化链按"工程长大顺序"排课（先能画、再有数据、
> 再会算路），C 讲义按"源码模块"排课。上表即对照表。
> 2026-11-19 补齐：step08–step14 七段落地后，**核心课 00–14 全部有对应段**；
> 跨平台三课（15/16/17）仍按零外部 crate 纪律留在 §六 作练习。

## 四、判据（都能失败，不是"没报错就算过"）

- **step00**：`ink > 0`（真有像素落下）。
- **step01**：量化往返误差 < 半个 LSB（5e-8 度）且整数域逐位可逆；墨卡托正反算
  往返 < 1e-9 度。
- **step02**：自编消息解回来逐字段相等；zigzag 往返含负数。
- **step03**：段表三个 section 计数全对；**落盘边权与重算 haversine 一致（±2m）**；
  负向对照：把 magic 改一个字节，解析器必须返回 `Err`——证明它真有判别力。
- **step04**：CSR 邻接数 == 边数 × 2（无向图双份）；BFS 从 0 号点可达全部节点
  （样例路网必须连通）。
- **step05**：视口中心必投到画布正中；东边的点投到右侧、北边的点投到上方
  （墨卡托 y 向南增大、与屏幕同向——这条不变量错了整张地图就上下颠倒）。
- **step06**：完全在画布外的线段必返回 `None`；横穿左边界的线段裁剪后起点
  `x == 0` 且 `y` 不变。
- **step07**：路程 ≥ 直线距离（三角不等式，0.999 容取整误差）；路径连续
  （相邻节点必是真边）。当前值：`dist=11430m straight=9775m path=5`。
- **step08**：9 阶段表逐位有序（`stages == [1..=9]`）；chrome 层最后盖上
  （`(0,0)` 必是 chrome 色）；一帧画完仍有内容。
- **step09**：「区」恰命中下标 [1,2]（海淀区/朝阳区）；「中关村」恰命中 [3]；
  「上海」0 条。
- **step10**：0→3 可达且路径端点正确；指令数 == 路径节点数（出发 + 逐点
  直行 + 到达）；同文本 1.5s 内被冷却挡下；换文本 / 17s 后放行。
- **step11**：正向判据 PASS 且计数正确；负向对照（5 分应属低类）确实 FAIL
  ——证明不是假门禁。
- **step12**：阈值 10→20 后新判据 PASS；改回 10 新判据 FAIL；恢复 20——
  「改 → 测 → 守」三步闭环。
- **step13**：四基准登记在册；CGCS2000 平移为 0；CGCS2000 与 WGS84 长半轴
  相同、扁率不同；西安 80 三参数 + 北京 54 有 TOWGS84。
- **step14**：「退出」→ Quit；未登记词条回退原串；`&amp;`/`&lt;` 解码；
  已转义实体不被二次解码。
- **链级**：`steps=15`；dev 与 release 的 `ink` 一致（当前 9370）；
  `gates` 有 fail ⇒ 退出码 1。

**负向对照（动手做）**：把 `step_05` 里 `to_screen` 的 `my - cy` 改成 `cy - my`
（翻转 y 轴），step05 的"北边的点投到上方"判据立刻失败——证明判据真守着这条
不变量。改完恢复。

## 五、练习

1. **真实 PBF 对照**：step02 手写了 varint/zigzag 原子。给它们加上
   protobuf 的 length-delimited（wire_type=2）与真实 PBF 的
   `BlobHeader/Blob` 容器层（zlib 解压——C 版用 zlib，Rust 用 `flate2`，
   这是本教程**唯一允许的外部依赖**练习），读一个真实
   `sources/*.osm.pbf` 的第一个 PrimitiveBlock，数出 dense node 数量。
2. **读 C 产物**：`products/graph/<省>/` 的 `nodes.bin`/`edges.bin` 是 C 版
   的自描述二进制（布局见 `src/map/graph.h`）。仿照 step03 写一个解析器
   读真实建库产物，把 4.3M 节点的北京路网画到 320×240 上（提示：
   bbox 统计先行，逐点线性投影会爆）。
3. **A\* 对照**：step07 的 Dijkstra 换成 A\*（启发 = haversine），在同一
   样例上对比 popped 节点数——判据：结果路程相等、popped 更少。
4. **链守卫**：仿照 `scripts/check_mini_chain.py` 写 `check_rust_chain.py`：
   用「旧段行序列是新文件行序列的子序列」守住"只增不减"，并做 5 类注入破坏
   的负向对照。
5. **GUI 对应**：C 版第 16 课用 Qt 包像素缓冲；Rust 对应方案是
   `minifb`/`softbuffer` + `winit`（同样是"核心零依赖、窗口壳另算"的边界）。
   把 `MiniCtx::buf` 喂给窗口壳即为第 16 课的 Rust 版——C 核心/Rust 核心
   都不碰窗口库。

## 六、后续路线（跨平台三课在 Rust 的落点；2026-11-19 起仅剩这三课）

核心课 00–14 已全部进链（step00–14，见 §三）。剩下的三课都是**跨平台外壳**，
按本链「零外部 crate」纪律不进单文件演化链，留作练习：

| C 课 | Rust 落点 |
|---|---|
| 15_platform（平台层） | `std::process`/`std::net` 对应 C 的 `plat.c`——std 已覆盖大半 |
| 16_qt（窗口壳） | 见练习 5（minifb/winit + softbuffer） |
| 17_android | JNI：`uniffi` 或手写 `jni` crate 绑定；C 侧 `mini_step_*` 的对等物直接编译进 .so |

## 七、2026-11-15 C 版生产化修复对照

C 版这一批不是新功能，而是把“正常数据能跑”补成“坏数据、线程退出、Windows 路径、下载目录名都不能骗过”。
Rust 链不复制这些生产状态机（它仍是单文件、15 段、无 mmap/无 Qt），但每条都能在现有段上做一个小练习：

| C 版修复 | Rust 现有落点 | 教学点 / 练习 |
|---|---|---|
| `names.bin` 的 `count`/offset 校验 | step03 `parse_bin` | `Result` 挡住越界读，但 offset 语义仍要显式查；构造 `off[1]=0xFFFFFFFF` 必须 `Err` |
| `tiles.bin` 的 `rec_count - 2` 下溢 | step03/04 | 用 `checked_sub(2)` → `Option`，而不是依赖无符号回绕；`nx*ny` 用 `checked_mul` |
| `lm.bin` 的维度乘法溢出 | step07 | `usize::try_from` + `checked_mul`；先反推文件长度，再分配 |
| `rev.bin` 的 `start[]` 单调性 | step07 | `start.windows(2).all(|w| w[0] <= w[1])`；尾哨兵不能代替全表校验 |
| `Router`/`MapSet` 重载 UAF | 本链无 mmap | 让 `Router<'a>` 借 `MapSet`，或搜索持有 `Arc<MapSet>`；重载时类型系统会先拦住借用 |
| 算路期间省份被卸载 | step04/07 | `Arc<RwLock<MapSet>>` + 搜索期间持有 `Arc`；Rust 的共享所有权把“pin 住”写进类型 |
| `maxspeed` 的 mph/knots | step04 的图属性 | `split_whitespace()` + `parse::<f64>()`；断言 `50 mph=80 km/h`、`30 knots=56 km/h` |
| 省 slug / `-latest` 目录 | 本链无下载层 | `strip_suffix("-latest")` + `Path::components()`；拒绝 `..` 和绝对路径 |
| Android 清单 id 白名单 / APK sha256 | 本链无 Android | `Path` 组件白名单；哈希用 `sha2`（唯一允许的依赖练习） |

### 给 Rust 链的负向对照

1. 在 step03 的二进制头里把某个 section 长度改成大于文件长度，解析必须返回 `Err`，不能 panic 到读越界。
2. 把 step03 的 `rec_count` 改成 0，再算 `rec_count - 2`；如果代码 panic 或回绕，就说明还没学会用 `Option`/`checked_sub`。
3. 把 step07 的 `start[]` 改成非单调，路径回溯必须拒绝这份索引，而不是读出错误路径。
4. 给道路属性加 `maxspeed="50 mph"`，确认速度是 80 而不是 50。

> 这批判定的共同点：**“文件头自洽”不等于“内容可信”**，**“编译通过”不等于“对象生命周期正确”**。
> C 版用显式校验和停线程顺序补上；Rust 版用 `Result`、`Option`、借用/`Arc` 把同样的不变量前移。
