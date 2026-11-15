# `tutorial/rust/` — Rust 版「从最小框架长到迷你路网查看器」演化链

> 这是 C 版三条学习资料之外的第 4 份：**用 Rust 把同一份工程从最小框架重新长一遍**。
> 回答的问题与 `tutorial/mini/` 相同——「教程是不是从最小框架开始，逐步完善？」——
> 但换一门语言，让学生在**两种语言里做同一组不变量**，看哪些不变量交给程序员（C），
> 哪些交给类型系统（Rust）。

## 一、与 C 版资料的关系

| C 版资料 | 形态 | Rust 版对应 |
|---|---|---|
| `docs/teaching/NN_*.md` | 18 篇源码导读讲义（读最终 `src/`） | 本 README 的课程映射表 + `src/main.rs` 各段头注释（**为什么不这么写**是逐段讲透的；Rust 生态另有 idiom 讲义，不重复） |
| `tutorial/NN_*/` | 17 个单课独立小程序 | 未单独展开——每步自包含在单文件的一个段里，职责相同 |
| **`tutorial/mini/`** | **同一份 C 工程的 18 个连续快照** | **`src/main.rs` 单文件演化链：8 段（step00–07）+ 锚点，截断即历史快照** |

与 C 版 mini 链同样的硬规则：

```
第 0 段  step00: frame      → 一块像素缓冲、画点、Bresenham、24 位 BMP
第 1 段  step01: coords     → 追加 1e-7 度量化、墨卡托正反算、经纬网
 ...
第 7 段  step07: routing    → 追加 Dijkstra + 路径回溯 + 画路线
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
cargo run            # dev 档：RUST-MINI-CHAIN-OK steps=8 ink=5833 gates=24/0，退出码 0
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

> 段号与 C 课号**不**一一对应：演化链按"工程长大顺序"排课（先能画、再有数据、
> 再会算路），C 讲义按"源码模块"排课。上表即对照表。

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
- **链级**：`steps=8`；dev 与 release 的 `ink` 一致；`gates` 有 fail ⇒ 退出码 1。

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

## 六、后续路线（本链未覆盖的 C 课在 Rust 的落点）

| C 课 | Rust 落点 |
|---|---|
| 13_datum（七参数/高斯-克吕格） | 纯数学，手写 3×3 矩阵即可（练习：与 C 版 `datum_gk_forward` 对拍同坐标） |
| 14_i18n | `rust-i18n`/`fluent`；语言点：Rust 宏生成查表 |
| 15_platform（平台层） | `std::process`/`std::net` 对应 C 的 `plat.c`——std 已覆盖大半 |
| 16_qt（窗口壳） | 见练习 5（minifb/winit + softbuffer） |
| 17_android | JNI：`uniffi` 或手写 `jni` crate 绑定；C 侧 `mini_step_*` 的对等物直接编译进 .so |
