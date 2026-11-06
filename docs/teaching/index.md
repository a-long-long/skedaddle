# Skedaddle 分课教程 · 总索引

> 本目录是 [`../teaching-plan.md`](../teaching-plan.md) 的**逐课展开版**：每一课单独成文件，统一按
> **① 教学目的 → ② 功能构成 → ③ 怎么实现 → ④ 与其他功能的联系** 四段式详述，并附课堂演示 / 练习 / 验收。
> 所有代码事实均来自当前 `src/` 真实源码。
>
> **两段式实现顺序（2026-11-06 起）**：**先核心代码（第 0–14 课），最后跨平台代码（第 15–17 课）**。
> 核心段是平台无关的 C 核心与其工程化配套，段内按程序运行流程排；跨平台段是平台抽象层
> （`utils/plat`）、Qt 桌面前端（`ui_qt`）、Android 原生 JNI App（`android/`）三个"换壳不换核心"的落点，
> 统一放到最后实现 —— 核心一旦跑通，换壳不该再动核心一行。
> 分类标签写在每篇讲义 H1 下方（`> 分类：核心代码 / 跨平台代码`），由
> [`scripts/check_tutorial_tracks.py`](../../scripts/check_tutorial_tracks.py) 守一致性。
>
> **编号约定（与 `tutorial/` 对齐）**：讲义与 `tutorial/NN_xxx` 小程序**同号对应**——
> `00_overview` ↔ `tutorial/00_minimal`（概念总览 / 最小框架动手热身），
> `01_coords` ↔ `tutorial/01_coords`，……，`11_gates` ↔ `tutorial/11_gates`，
> `12_capstone` ↔ `tutorial/12_capstone`，`13_datum` ↔ `tutorial/13_datum`，
> `14_i18n` ↔ `tutorial/14_i18n`，`15_platform` ↔ `tutorial/15_platform`，
> `16_qt` ↔ `tutorial/16_qt`。读源码式讲义看"为什么这么写"，配套小程序看"怎么跑起来"。
> **第 17 课是唯一例外**：以真实 `android/` 产品工程为配套（需 NDK / Gradle），不提供 `tutorial/17_android` 小程序。
>
> **演化链（2026-11-06 新增，第 3 份资料）**：讲义是「最终源码导读」、`tutorial/NN_*` 是「可独立编译的单课小程序」，
> 两者都**不是**同一份代码逐步长大。要看「从最小框架开始 → 逐课累加 → 最后对上 `src/`」，走
> [`tutorial/mini/README.md`](../../tutorial/mini/README.md)：18 个连续快照 + 17 个相邻 diff，
> 末课 [`MAP.md`](../../tutorial/mini/MAP.md) 给 `mini/ ↔ src/` 逐模块合流对照。
> 「只增不减」由 [`scripts/check_mini_chain.py`](../../scripts/check_mini_chain.py) 逐行验收。

**课程总览与程序运行流程图见 [`00_overview.md`](00_overview.md)（第 0 课）——建议从这里开始读。**

## 课程表（先核心代码，最后跨平台；编号 00–17）

| 文件 | 课次 | 分类 | 主题 | 配套 `tutorial/` |
|---|---|---|---|---|
| [00_overview.md](00_overview.md) | 0 | 核心代码 | 课程总览与程序运行流程（含运行流程图） | `00_minimal` |
| [01_coords.md](01_coords.md) | 1 | 核心代码 | 坐标系与大地基准 | `01_coords` |
| [02_pbf.md](02_pbf.md) | 2 | 核心代码 | PBF 流式解析 | `02_pbf` |
| [03_binary.md](03_binary.md) | 3 | 核心代码 | 自描述二进制与图集 | `03_binary` |
| [04_graph.md](04_graph.md) | 4 | 核心代码 | 建图：PBF→路网 | `04_graph` |
| [05_viewport.md](05_viewport.md) | 5 | 核心代码 | 视口、投影与瓦片键 | `05_viewport` |
| [06_raster.md](06_raster.md) | 6 | 核心代码 | 软件光栅与性能旋钮 | `06_raster` |
| [07_pipeline.md](07_pipeline.md) | 7 | 核心代码 | 渲染管线编排（含标注层：缓存 / 分级 / 字形） | `07_pipeline` |
| [08_search.md](08_search.md) | 8 | 核心代码 | 地名搜索 | `08_search` |
| [09_routing.md](09_routing.md) | 9 | 核心代码 | 跨省路由算法 | `09_routing` |
| [10_navvoice.md](10_navvoice.md) | 10 | 核心代码 | 导航层与语音 | `10_navvoice` |
| [11_gates.md](11_gates.md) | 11 | 核心代码 | 架构纪律与验收门禁 | `11_gates` |
| [12_capstone.md](12_capstone.md) | 12 | 核心代码 | 综合项目（改一个点 + 加一条判据） | `12_capstone` |
| [13_datum.md](13_datum.md) | 13 | 核心代码 | 坐标系基准（datum）：为什么只换椭球等于没转换 | `13_datum` |
| [14_i18n.md](14_i18n.md) | 14 | 核心代码 | 极简多语言（i18n）：为什么中文是 msgid 是安全的 | `14_i18n` |
| [15_platform.md](15_platform.md) | 15 | 跨平台代码 | 平台抽象层 | `15_platform` |
| [16_qt.md](16_qt.md) | 16 | 跨平台代码 | Qt 前端与边界（含无边框窗口） | `16_qt` |
| [17_android.md](17_android.md) | 17 | 跨平台代码 | Android 原生 JNI App：把同一个 C 核心搬进手机 | `android/ 真实工程（无 `tutorial/` 小程序）` |

> 表中第 0–14 课为**核心代码**、第 15–17 课为**跨平台代码**；这就是本教程的实现顺序。
> 运行流程图（第 0 课 §③）描述的是"程序怎么跑"，课程表描述的是"人怎么把它实现出来"，
> 两者刻意不同：Qt 入口在运行时是第一环，在实现时是最后一段。

## 阅读建议

- **实现顺序 = 课程表顺序**：先核心代码 00–14（底座 → 数据生产 → 每帧渲染 → 用户交互 → 门禁 / 综合 / 进阶专题），
  **最后**跨平台 15–17（平台抽象层 → Qt 桌面前端 → Android App）。
- 每课的"④ 与其他功能的联系"标了上游 / 下游，对照第 0 课的流程图一起读，能串起完整链路。
- 每课都可在 `tutorial/NN_xxx/` 找到同号、可独立编译运行的小程序（第 17 课除外），建议"先读源码讲义、再跑小程序"。
- 第 17 课是「第二个前端」：与 `16_qt` 并列，C 核心一行未改；动手环节就是 `android/` 工程的编译与部署，命令见 [`../android.md`](../android.md)。
- 环境搭建与构建命令见 [`../teaching-plan.md`](../teaching-plan.md) 的「一、环境搭建」。
- **讲义里的「完整体，逐字」代码块由 [`scripts/regen_tutorial_blocks.py`](../../scripts/regen_tutorial_blocks.py) 从当前源码生成**（第 16 课 `main_window.*`、第 11 课内嵌 `regress.sh`）：改完源码跑一次生成脚本，用 `--check` 确认没有过期副本。最近增补内容：第 7 课 §3.6 标注层、第 16 课 §3.10 无边框窗口、第 11 课 §3.7 新增判据。
- 改完讲义编号 / 分类，跑 `python3 scripts/check_tutorial_tracks.py` 确认两段式分类与编号一致；跑 `bash scripts/check_doc_numbers.sh` 确认 H1 编号与文件名一致。
- **要看代码怎么一课课长大**：`tutorial/mini/` 是第 3 份资料（18 个连续快照，每步都能编译跑出 BMP）；
  改完 `_parts/` 跑 `python3 scripts/regen_mini_chain.py` 重新生成，再用 `python3 scripts/check_mini_chain.py`
  验收累加不变量（含 `--self-test` 的 5 类负向对照）。该门禁与 `check_tutorial_tracks.py` 一样未接进 `regress.sh`。
