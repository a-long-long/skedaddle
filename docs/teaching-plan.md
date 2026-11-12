# Skedaddle 项目教案（循序渐进版）

> 适用对象：有一定 C 语言基础、想通过**一个真实工程**理解"离线地图导航"全栈实现的学习者。
> 教学主线：先让程序**跑起来** → 再读懂**每一层在做什么** → 最后能**改得动、测得准**。
> 全部内容都锚定在本仓库当前真实代码（`src/`、`tools/`、`scripts/`），不是泛泛而谈。

---

## 〇、教学总览

### 0.1 这个工程是什么

一个**纯 C（C11）+ Qt5/Qt6 前端**的离线地图导航程序：

- 输入：OSM 的 `.osm.pbf` 原始数据（8 省约 456 MB）。
- 产出：可缩放平移的地图渲染、跨省路线规划、地名搜索、模拟导航与语音播报。
- 特点：**自写软件光栅**（不依赖 SDL 软渲染）、**坐标键接边**（跨省无需映射表）、**Landmark/ALT 加速**的双向加权 A*（不是 CH）、**纯 DAG 模块分层**、**全量可无头自测**。

### 0.2 教学原则（本项目最重要的 4 条，先讲给学生听）

1. **能实测就不要估算**——性能瓶颈、算法正确性一律靠 `scripts/regress.sh`、像素判据、确定性量说话，不靠"我觉得"。
2. **核心与窗口系统解耦**——C 渲染核心一行不动，Qt 只把像素缓冲包成 `QImage`。这就是为什么能"无头出图 + GUI 两套前端共用同一渲染入口"。
3. **依赖是纯 DAG**——`utils ← pbf ← map ← route ← render ← ui ← ui_qt`，反向依赖会立刻 `undefined-reference`。`scripts/check_layers.sh` 是守护门禁。
4. **核心零 SDL**——`skedaddle_core` 连 SDL 的 include 都不给；SDL 仅残留在两个去 SDL 化对照体（`skedaddle_blend_equiv`/`skedaddle_clip_equiv`）里做"逐像素等价"验证。

### 0.3 课程表（共 18 篇：**先核心代码 00–14，最后跨平台 15–17**）

> **两段式实现顺序（2026-11-06 重排）**：课程统一编号 **00–17**，但实现顺序不再等同于运行流程。
> **先核心代码（第 0–14 课）**：平台无关的 C 核心 + 工程化配套，段内按运行流程排
> （坐标 → 数据生产 → 每帧渲染 → 用户交互 → 门禁 / 综合项目 / 两节进阶专题）。
> **最后跨平台代码（第 15–17 课）**：平台抽象层 `utils/plat`（第 15 课）、Qt 桌面前端 `ui_qt`（第 16 课）、
> Android 原生 JNI App `android/`（第 17 课）。三者都是"换壳不换核心"，核心跑通之后统一实现。
> 分类标签写在每篇讲义 H1 下方，由 `scripts/check_tutorial_tracks.py` 做机器判据。
>
> 第 0 课是总览（讲义 [`docs/teaching/00_overview.md`](teaching/00_overview.md) + 小程序 `tutorial/00_minimal`）；
> 01–14 为核心段主题课、综合项目与进阶专题；15–17 为跨平台段。**00–16 每课都有两份同号资料**——
> 「读真实源码讲义 `docs/teaching/NN_*.md`」与「可独立编译运行的小程序 `tutorial/NN_*/`」；
> **第 17 课是唯一例外**，配套资料为真实产品工程 `android/`（需 NDK/Gradle），不提供小程序。
>
> **第 3 份资料 · 演化链（2026-11-06 新增）**：上述「讲义 + 同号小程序」回答的是
> 「成品源码为什么这么写」与「单个概念怎么跑」，都不是同一份代码逐步长大。
> 本教案标题里的「循序渐进」在本轮补上了字面意义的实现链：
> [`tutorial/mini/README.md`](../tutorial/mini/README.md) 把第 0 课的一块像素缓冲
> 逐课累加成第 17 课的两个前端（18 个连续快照 + 17 个相邻 diff，每步可编译），
> 由 [`scripts/check_mini_chain.py`](../scripts/check_mini_chain.py) 守「旧文件零删改」；
> 末课 [`tutorial/mini/MAP.md`](../tutorial/mini/MAP.md) 是 `mini/ ↔ src/` 逐模块合流对照表
> 与差距清单 —— **这就是「最后到最终代码」的落点**。三段资料并存互补，编号 / 分类不变。

| 课次 | 分类 | 主题 | 对应模块 | 关键产出 | 配套资料 |
|---|---|---|---|---|---|
| 0 | 核心代码 | 课程总览与程序运行流程（含运行流程图） | 全局 | 程序运行流程图 | `docs/teaching/00_overview.md` + `tutorial/00_minimal` |
| 1 | 核心代码 | 坐标系与大地基准 | `utils/geo`, `utils/datum` | 经纬度量化、投影、测距 | `docs/teaching/01_coords.md` + `tutorial/01_coords` |
| 2 | 核心代码 | PBF 流式解析 | `pbf/*` | 解出 node/way/relation | `docs/teaching/02_pbf.md` + `tutorial/02_pbf` |
| 3 | 核心代码 | 自描述二进制与图集 | `map/{binfmt,mapset,namepool}` | 读 `*.nmbin` | `docs/teaching/03_binary.md` + `tutorial/03_binary` |
| 4 | 核心代码 | 建图：PBF→路网 | `map/{graph_build,road_tags}` | 生成 nodes/edges | `docs/teaching/04_graph.md` + `tutorial/04_graph` |
| 5 | 核心代码 | 视口、投影与瓦片键 | `map/{map_view,tileidx}` | 经纬度↔屏幕 | `docs/teaching/05_viewport.md` + `tutorial/05_viewport` |
| 6 | 核心代码 | 软件光栅与性能旋钮 | `render/{raster,paint,navdraw}` | `AREA_DOT_PX` 权衡 | `docs/teaching/06_raster.md` + `tutorial/06_raster` |
| 7 | 核心代码 | 渲染管线编排（含标注层：缓存 / 分级 / 字形） | `ui/{window,chrome,offscreen_shot,audit}`, `render/{labels,font}` | `paint_frame` 全貌 | `docs/teaching/07_pipeline.md` + `tutorial/07_pipeline` |
| 8 | 核心代码 | 地名搜索 | `search/*` | bigram 倒排 | `docs/teaching/08_search.md` + `tutorial/08_search` |
| 9 | 核心代码 | 跨省路由算法 | `route/*` | 双向加权 A* | `docs/teaching/09_routing.md` + `tutorial/09_routing` |
| 10 | 核心代码 | 导航层与语音 | `route/{maneuver,navsim,voice}`, `ui/voice_wire` | 转向指令+播报 | `docs/teaching/10_navvoice.md` + `tutorial/10_navvoice` |
| 11 | 核心代码 | 架构纪律与验收门禁 | `scripts/*`, `tools/*` | 防回归 | `docs/teaching/11_gates.md` + `tutorial/11_gates` |
| 12 | 核心代码 | 综合项目（改一个点 + 加一条判据） | 全程 | 改一个点 + 加一条判据 | `docs/teaching/12_capstone.md` + `tutorial/12_capstone` |
| 13 | 核心代码 | 坐标系基准（datum）：为什么只换椭球等于没转换 | `utils/datum` | 三参数赫尔默特 + 交叉验证 | `docs/teaching/13_datum.md` + `tutorial/13_datum` |
| 14 | 核心代码 | 极简多语言（i18n）：为什么中文是 msgid 是安全的 | `src/i18n`, `lang/*.xml` | zh 为 msgid + 缺译回退链 | `docs/teaching/14_i18n.md` + `tutorial/14_i18n` |
| 15 | 跨平台代码 | 平台抽象层 | `utils/{plat,mem,strpool}` | 跨平台不写 `#ifdef` | `docs/teaching/15_platform.md` + `tutorial/15_platform` |
| 16 | 跨平台代码 | Qt 前端与边界（含无边框窗口） | `ui_qt/*` | 渐进式 Qt 化 | `docs/teaching/16_qt.md` + `tutorial/16_qt` |
| 17 | 跨平台代码 | Android 原生 JNI App：把同一个 C 核心搬进手机 | `android/`, `utils/plat` 的 `__ANDROID__` 分支 | JNI 桥 + 30fps 渲染线程 + 增量更新/OTA | `docs/teaching/17_android.md` + `android/` 工程 + `docs/android.md` |

> **同步补记（2026-11-12，对齐 11-10 分带并行与 11-11 导航五项）**：
> - 第 7 课两个「完整体，逐字」块按当前源码重贴：`paint_frame`（`window.c:1309-1573`）
>   与标注白描边的并行实现（`labels.c:605-665`，`label_task` / `label_flush`）；全篇函数行号
>   回源核验 26 处，新增 §六「源码演进补记」讲清历史案例与当前实现的分界。
> - 串行 helper `font_draw_halo()` 已于 2026-11-12 **删除**（此前是零调用死代码、编译唯一
>   `-Wunused-function` 警告源），零警告门禁恢复；讲义不再把它当作执行路径，实际绘制走
>   `LabelCmd` → `label_flush()` → `label_task()`。删函数让 `labels.c` 行号上移 18 行，
>   两个逐字块与全篇行号已回源重贴。
> - [`index.md`](teaching/index.md) 新增「源码演进索引」（新功能 → 真实源码 → 课次），
>   新增 `scripts/check_teaching_sync.py` 守手工讲义的逐字块与表格行号区间
>   （与演化链门禁同纪律，未接 `regress.sh`；`--self-test` 三类注入破坏均能让它失败）。
> - 生成式讲义已同步到 `regress.sh` 973 行 / `main_window.h` 268 行；本轮再新增 17 条
>   L2/数据链用例（8 自测+负向、3 真实路网 profile、ETA 递增、2 逆地理编码、3 数据链），
>   **2026-11-12 用 `SKEDADDLE_BUILD_DIR=$PWD/build_rel` 实跑 109/0**；同日 M1 再增
>   6 条 NMBIN 判据得 **115/0**，M2 再增 10 条（块表 + Java 差分门禁）得 **125/0**。
>   旧「87/0」「109/0」「115/0」均为历史口径。
>
> **对账补记（2026-11-06，补齐最近几轮代码改动 + 讲义块改为源码生成）**：
> - **新增/扩写三节**：第 7 课 §3.6「标注层 `labels.c`」（缓存纯缩放 bug、区域名分级、白描边 / 字形锐化 / POI 与三关负向对照）；第 16 课 §3.10「无边框窗口」（`FramelessWindowHint`、顶部透明拖动带、几何状态机、退出路径、Wayland 限制）；第 11 课 §3.7「新增判据与讲义也要被验收」。
> - **逐字块不再手工摘抄**：新增 [`scripts/regen_tutorial_blocks.py`](../scripts/regen_tutorial_blocks.py)，按**方法定义边界**从当前源码生成第 16 课 `main_window.h` 全文 + 4 个 cpp 块（合计 68 个方法，不重不漏）、以及第 11 课内嵌的 `regress.sh` 全文（839 行）。`--check` 可当讲义同步门禁，脚本自带幂等自检。
> - **锚点回源逐个校正**：`window.c` +1（`paint_frame` 1190、`app_render_to_buf` 1456、`if(a->font)` 1303）、`i18n.c` +3（`TR[]` 57-449、`i18n_load` 573、`tr_lookup` 628、`i18n_tr` 638、`xml_decode` 476 等）、`qt_main.cpp` 68→69、`map_canvas.cpp` 53→55 / 145→184 / 242→281、`main_window.cpp` 1203→100、`qt_icons.cpp` 191→240；`regress.sh` 834→839 行；顺手修掉 `qt_ui=2` 的笔误（源码只设 1）。
> - **判据口径同步**：`qt_ovl_alpha` 矩形 470→430（按钮去掉、右栏贴顶）、`--ui-test` 默认 121/0 与 `SKEDADDLE_FRAMELESS=0` 114/0、注入假按钮的负向对照、C 核心出图对拍 0.000%。回归基线仍 **87 通过 / 0 失败**。
>
> **对账补记（2026-11-05，合并远端 5 提交后逐字块全量对账）**：
> - 判据用的是「源码切片 vs 讲义代码块」**逐字比对**，不是按插入行数估算位移；工具报的多行号表格漂移一律回源核验后才改：
>   `04_graph.md` 的 `write_spatial_index` 标注 `1305-1475` → 实际 `1230-1400`；`03_binary.md` 的 `mapset_load`
>   标注 `693-766` → 实际 `674-747`；`11_gates.md` / `12_capstone.md` 五处「整文件范围」尾行各多算 1 行；
>   `12_capstone.md` 两处把 CMake target 名当成文件名（`skedaddle_graph_extract.c` / `skedaddle_route_cli.c`
>   → `tools/graph_extract.c` / `tools/route_cli.c`）。
> - **第 16 课四个「完整体，逐字」大块按当前源码重新生成**：`main_window.cpp` 三段真实区间
>   `239-917` / `919-1225` / `1920-2591`，`download_dialog.cpp` 一段 `104-774`。Qt 前端在此期间新增了
>   卫星天顶图自检、菜单构建调整、`trc()` → `QStringLiteral()`、省份名 `i18n_tr()` 包裹等，旧副本已非逐字。
> - `04_graph.md` 里 5 行讲义自写注记原先混在「逐字」代码块内，已移到块外作正文，恢复代码块逐字承诺。
> - 第 17 课补 `Prefs.java`（配置中枢）进功能构成表与 §3.5：9 个 Java 类此前只列了 8 个。
> - 纪律复核：`versionCode` 仍是 1（`build.gradle:28`），讲义「下个版本用 2」**正确**，直觉怀疑但未回源前不改。
>
> **对账补记（2026-11-04，远端 Android 前端落地）**：
> - 新增 `android/` 原生 JNI App（3 个提交 / 42 文件）与 `src/utils/plat.{c,h}` 的 `__ANDROID__` 分支，已补入第 17 课 [`17_android.md`](teaching/17_android.md)；第 15 课（平台抽象层）新增 3.5 节讲目录注入，第 15/10 课的 TTS 逐字副本已同步 Android 分支。
> - `plat.c` +67 行、`plat.h` +16 行导致两课全部 `plat.c:` / `plat.h:` 锚点后移，已按行级 diff 精确重映射（含逗号列表与区间两端）。
> - 第 17 课无 `tutorial/` 小程序：它要跑的是产品工程本身，构建/部署/自托管命令见 [`docs/android.md`](android.md)。

> **对账补记（2026-11-03 完整对账，当日补齐 i18n 课次）**：
> - 上面 **00–16 课**已逐课对照当前代码。已修正的过时项集中在第 3/4 课与建图/显示层：
>   整省三件套（areas/lines/polyrefs.bin）的 **nmbin 读取路径已彻底断根**（`mapset.c` 不再打开、构建不再生成、`disp_tile_selftest` 改为统一以 PBF 解码作基准）；`osmid.bin` 已删；SDL 已从核心彻底移除（仅剩两个去 SDL 化对照体 `skedaddle_blend_equiv`/`skedaddle_clip_equiv` 做逐像素等价验证）。回归基线现为 **87 通过 / 0 失败**。
> - **i18n 缺口已补齐**：`src/i18n/`（多语言系统，配 7 套非中文语言包）现已有第 14 课 [`14_i18n.md`](teaching/14_i18n.md) + `tutorial/14_i18n` 配套小程序；第 13 课 datum（`13_datum.md`）此前漏列于本表，已补入。
> - 个别行号锚点（如 `paint_frame` 的 `dup_of` 跳过、`i18n.c` 的各函数区间）已按当前源码校正；其余函数级锚点经核对仍然有效。

> **对账补记（2026-11-03 二次补齐，对应提交 `6f1de61`）**：
> - 上一轮完整对账后，又落地了两类"显示路径健壮性"修复，本次补齐进文档：
>   **① Wayland 透明花屏**——地图画布缓冲 alpha 字节恒为 0（`raster_rgb` 只写 R/G/B），Deepin Wayland 按 ARGB8888 上传导致地图区透出桌面乱色；修复在 `map_canvas.cpp:75-76`（`m_px[i] |= 0xFF000000u`），xcb/离屏无副作用。已在第 16 课"边界原则 ③"与第 7 课 `if(a->font)` 门控处记录，并附 `QT_QPA_PLATFORM=xcb` 对照诊断。
>   **② 字体失败静默丢字**——`paint_frame` 的 `if(a->font)` 门控在 `font_open_default` 返回 NULL 时会静默丢弃全部地名/路名；修复在 `app_init`（`window.c:141-158`）置位 `a->font_missing` 并打响亮 stderr 告警。已补进第 16 课"边界原则 ④"与第 7 课门控处，并标注 `skedaddle_font_probe` / 离屏诊断 `font (已加载|★加载失败)` 两条离线确认手段。
> - 第 16 课教学目的、演示、练习、验收均已同步这四点（含 Wayland 负向对照实验）。

---

## 一、环境搭建（第 0 课前准备）

### 1.1 必做：固化本机路径

```bash
source env.sh        # 固化 PKG_CONFIG_PATH / LD_LIBRARY_PATH / PATH，指向 ~/.local 下的 protoc-c 等
```

> 为什么必须 source：本机 `libprotobuf-c` 头在 `~/.local/usr/include`、库在 `~/.local/usr/lib/<arch>`，`pkg-config` 给的 `Cflags` 是错的（见 `CMakeLists.txt:51-72`）。不 source 会 FATAL。

### 1.2 构建（本机 Linux / aarch64 / Qt5 已装）

```bash
source env.sh
cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=$HOME/.local/qt5-dev/usr \
      -DQt5_DIR=$HOME/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5
cmake --build build_rel -j$(nproc)
```

> 注意：`scripts/build_release.sh` 是 **Windows/MSYS2 专用**，本机不要跑（见工作记忆）。本机 Release 走上面这条原生 CMake 命令。

### 1.3 跑起来（GUI 与无头两条路）

```bash
# GUI（Wayland 会话里直接跑）
./build_rel/skedaddle --graph=products/graph/beijing

# 无头出图（CI/自测用，零 SDL，纯内存缓冲 + 内建 24 位 BMP）
./build_rel/skedaddle --shot-offscreen --graph=products/graph/beijing \
    --lat=39.9 --lon=116.4 --zoom=11 --w=1280 --h=800 --out=shot.bmp

# 算一条路线（CLI，可断言）
./build_rel/skedaddle_route_cli --graph=products/graph/beijing \
    --from=39.90,116.40 --to=39.99,116.30 --geojson=route.json
```

**课堂演示**：让学生各自跑出一张 `shot.bmp`，用 `scripts/bmp2png.py` 转成 png 看一眼，确认"地图真的画出来了"。

### 1.4 验收门禁（先把这套跑通，后面每课都用）

```bash
./scripts/regress.sh        # 全量回归；基线 87 通过 / 0 失败（数据齐全；2026-11-02 起原「3 项环境缺失」已消除）
./scripts/check_layers.sh   # 分层门禁：任何反向 #include 都会打印文件行号并退出非 0
```

---

## 二、分课教案 · 核心代码段（第 1–11 课）

> **本段先做**：平台无关的 C 核心，按运行流程实现（坐标 → 数据生产 → 每帧渲染 → 用户交互 → 门禁）。
> 跨平台段（第 15–17 课）见本教案「四」，**必须在核心段全部实现、`scripts/regress.sh` 87/0 全绿之后**才动手。
> 每节完整讲义见 `docs/teaching/NN_*.md`，可运行小程序见 `tutorial/NN_*/`。

---

### 第 1 课 · 世界观与坐标系

**课时目标**：理解"地球坐标 → 屏幕像素"这条主线的两端，以及本项目为什么坚持 WGS84 内部坐标。

**前置知识**：经纬度、墨卡托投影、大地基准（datum）的直觉。

**必读代码**
- `src/utils/geo.h` / `geo.c`：`sk_lat_to_i32`/`sk_i32_to_deg`（坐标量化成 1e-7 度 int32）、`sk_haversine_m`（真实测距）、`sk_mercator`/`sk_mercator_y_fast`（每帧几十万次的投影，**快版故意不逐位等于精确版**，换 5~6× 速度）。
- `src/utils/datum.h` / `datum.c`：`DatumId`（WGS84/CGCS2000/BJ54/XA80）、`datum_wgs84_to`、`datum_gk_forward/inverse`（高斯方里格）、`DatumSevenParam`（Bursa-Wolf 七参数）。

**核心讲解**
1. **内部几何永远是 WGS84**：坐标系只影响"显示读数"（经纬网、右下角坐标、方里格），**不重投影底图**。这是坐标系教学的关键不变量。
2. **测距一律 haversine**，Web Mercator 只用于显示，不能拿来做距离（会随纬度失真）。
3. **精确版 vs 快版投影**：`sk_mercator_y_fast` 用查表+插值，与 `sk_mercator_inv` 不互逆——讲"精度 vs 性能"取舍的绝佳案例。

**课堂演示**
```bash
./build_rel/skedaddle_datum_selftest        # 坐标系枚举/往返/位移合理性 + 负向对照
```

**练习**：用 `sk_haversine_m` 和 `sk_fast_dist_m` 分别算北京→上海距离，对比误差与速度。

**验收**：能说清"为什么内部用 WGS84、为什么投影要分快慢两版"。

---

### 第 2 课 · PBF 流式解析

**课时目标**：读懂 OSM PBF 的块结构，并避开最经典的坑——stringtable 不是 NUL 结尾。

**必读代码**
- `src/pbf/pbf_stream.h` / `pbf_stream.c`：`pbf_stream_open/next/close`；`pbf_stream_next` 返回 `type`（`"OSMHeader"`/`"OSMData"`）、解压缓冲、`PBF_DONE/PBF_ERR`，上限 `PBF_MAX_BLOB_BYTES=64MB`。
- `src/pbf/pbf_block.h` / `pbf_block.c`：`pbf_unpack_block` + `PbfBlock`；`pbf_str(sid)` 返回 NUL 结尾 C 串。

**核心讲解**
1. **块结构**：4 字节大端长度 + `BlobHeader` + `Blob`（内部 zlib 压缩，少数 lz4）→ `HeaderBlock` / `PrimitiveBlock`。
2. **⚠️ stringtable 坑**：protobuf-c 的 `ProtobufCBinaryData` **不保证 `\0`**。直接拿 `data` 当 C 串，`strcmp` 会把 `"path"` 当作 `"path\x7f"`，highway 分类整类落空。`pbf_unpack_block` 因此把所有 stringtable 拷成一份 NUL 结尾副本——这是"序列化数据 ≠ C 字符串"的反面教材。
3. **delta 编码**：`DenseNodes` 的 id/lat/lon 都是 delta 编码，必须累加还原。

**课堂演示**
```bash
./build_rel/skedaddle_pbf_info sources/beijing.osm.pbf     # 看 block 结构、way 数、node 数
```

**练习**：用 `skedaddle_pbf_info` 对比 `bbbike.pbf`（截断文件）与正常文件，确认截断文件**报错退出而非崩溃**。

**验收**：能解释为什么 `pbf_str` 不能直接返回 protobuf-c 的原始 `data` 指针。

---

### 第 3 课 · 自描述二进制与图集

**课时目标**：理解"为什么不用 8 字节头+定长记录，而要做自描述格式"，以及多省图集如何按需加载。

**必读代码**
- `src/map/binfmt.h` / `binfmt.c`：`NMBinFile`（`data/hdr/mapped`）、`NMBinHeader`/`NMBinField`——**字段名/偏移/类型/宽度写进文件头**；落盘统一小端、定长整型（避免 aarch64 跨编译器布局漂移）。`NMBIN_MAGIC="NAVMBIN\x01"`，`NMBIN_VERSION=3`。
- `src/map/mapset.h` / `mapset.c`：`MapSrc`/`MapSet`，每省一组 `nodes/edges/restrictions/lanes/disp(tiles)/edgelist/contours/...`；`lines/areas/polyrefs` 为**旧版整省三件套**的缓冲，**自 2026-11-03 起构建不再生成、也不再从此 nmbin 打开**（整省 nmbin 读取路径已断根，products 可整体重建）；该字段现由 disp tile 拼接或 PBF 解码的手 malloc 缓冲填充，运行时显示从 disp tile 拼接。含 `bbox`、`dup_of`、后台解码线程。
- `src/map/namepool.h` / `namepool.c`：每省 `names.bin` 解析结果。

**核心讲解**
1. **self-describing 的设计哲学**：上一版"8 字节头+定长记录"的字段语义随源码丢失，换新机器/新编译器就读不出来。自描述头部让"丢掉源码也能读"。
2. **副本省与去重**：`dup_of` 在路由与渲染都跳过；跨省重复靠 `DedupSet`（64 位指纹 + 世代）判定。
3. **模块归属**：`namepool`/`coordkey` 已下沉到 map 层（原在 render），消除 `map→render` 倒置。

**课堂演示**
```bash
./build_rel/skedaddle_dmap_selftest            # 后台异步解码：set_view 不阻塞 + 进度回调
./build_rel/skedaddle_disp_tile_selftest
```

**练习**：用 `xxd` 看一个 `.nmbin` 文件头的 magic 与字段表，手动解析出第一个字段的偏移。

**验收**：能说清"自描述二进制 vs 定长记录"各自的取舍，以及为什么本项目选前者。

---

### 第 4 课 · 建图：PBF → 路网

**课时目标**：理解从原始 OSM 要素到可导航图（CSR 边 + 限制）的抽取过程。

**必读代码**
- `src/map/road_tags.h` / `road_tags.c`：`RoadTags`（`road_class`/`drivable`/`oneway`/`speed_kmh`/`lanes`/`turn_lanes_*`）+ 边标志位 `EF_REVERSE/EF_ROUNDABOUT/EF_TOLL/EF_FERRY/EF_NO_ACCESS…`）；`road_tags_parse` 解析 way 的 keys/vals。
- `src/map/graph_build.h` / `graph_build.c`：**两遍扫描**——挑可通行 way → 节点下标映射 → 建 CSR 边 + 限制。

**核心讲解**
1. **最小导航集**：只抽 `highway=*`、`railway`、`waterway/natural`（底图）、`boundary`、`name`、通行标志等。
2. **CSR 存储**：邻接表用压缩稀疏行（offset + edge list），适合 A* 的高效遍历。
3. **限制（restriction）**：`relation` 里的 `type=restriction` 在建图阶段就抽成 `restrictions.bin`。

**课堂演示**
```bash
./build_rel/skedaddle_graph_extract --help      # 看抽取参数（被 autobuild 调）
./build_rel/skedaddle_graph_check               # 校验抽取结果
./build_rel/skedaddle_tag_stats sources/beijing.osm.pbf   # highway 取值统计
```

**练习**：挑一个 `highway=*` 取值（如 `tertiary`），在 `road_tags.c` 里追它如何变成 `road_class`。

**验收**：能描述"给一个 way，建图后它变成了图里的什么结构"。

---

### 第 5 课 · 视口、投影与瓦片键

**课时目标**：打通"地理坐标 ↔ 屏幕像素"的转换，理解视口驱动按需加载与瓦片空间索引。

**必读代码**
- `src/map/map_view.h` / `map_view.c`：`MapView`（`lat/lon/zoom/w/h/rot`）+ **投影缓存** `cx,cy,scale` 与惰性失效键 `c_lat/c_lon/c_zoom/c_rot/c_w/c_h`）；`mv_project`/`mv_unproj`/`mv_fit_bbox`、`mv_proj_begin`/`mv_proj`（批量投影，与逐点等价但省缓存键比对）。
- `src/map/tileidx.h` / `tileidx.c`：瓦片键三版本——`sk_tile_of`（查表+边界回退）/`sk_tile_of_raw`（纯查表，仅供自检证"边界回退必要"）/`sk_tile_of_exact`（sin+log 参考）。

**核心讲解**
1. **投影缓存的惰性失效**：只有视口参数真正变了才重算 `cx,cy,scale`，否则直接复用——避免每帧重投影。
2. **瓦片键三版本**：`lround` 会掩盖 ULP 差异，所以 `skedaddle_tile_selftest` 对 5228 万真实节点**逐点**比对而非抽样。

**课堂演示**
```bash
./build_rel/skedaddle_tile_selftest       # 查表版 vs 精确版 逐点等价
./build_rel/skedaddle_tile_xcheck         # 跨省接边验证
```

**练习**：给定两个经纬度，手算 Z14 瓦片坐标，再用 `sk_tile_of` 验证。

**验收**：能写出"经纬度 → 屏幕像素"的调用链，并说明投影缓存为什么能省时间。

---

### 第 6 课 · 软件光栅与性能旋钮

**课时目标**：理解自写光栅的动机，以及"画质 ↔ 帧时间"如何用确定性量来权衡。

**必读代码**
- `src/render/raster.h` / `raster.c`：`Raster`（`px/w/h/stride/clip`）；`raster_bind_mem`、`raster_line`（逐字移植 `SDL_RenderDrawLine` + Cohen-Sutherland `raster_clip_line`，64 位中间量防溢出）、`raster_fill_rect`/`raster_px_rgba`（带 alpha）。
- `src/render/paint.h`：`#define AREA_DOT_PX 4`（面 LOD 旋钮）；`PaintStats`（`ms_bg/ms_areas/ms_roads/...` + `areas_drawn/areas_dotted/...`）。
- `src/render/paint.c`：`paint_areas`/`paint_roads`/`paint_lines`；`paint_thick_line`。
- `src/render/navdraw.c`：`paint_route`/`paint_nav_car`/`paint_nav_banner`（`NAV_ROUTE_*` 蓝芯白边配色）。

**核心讲解**
1. **为什么自写光栅**：SDL 软渲染每次 `DrawLine` 约 65ns 固定税，z9 一帧 91 万次 ≈ 60ms（占 39%）；自写后这笔税消失，且与 SDL **逐像素等价**是硬要求（`raster_clip_line` 逐字移植，`tools/clip_equiv.c` 用 20 万随机线段门禁）。
2. **★ `AREA_DOT_PX` 旋钮（定档 4）**：面外接框小于阈值走"只点一个像素"快速路径。档位 4 时 `z9 −21%`、净损失面积 3.46%——这是"画质↔帧时间"权衡的**实证范例**。
3. **线要素 bbox 缓存**：线无瓦片索引，每帧扫 10 万条线、每条最多 4096 点全投影，占 z14 一帧 75%；`line_bbox` 懒填缓存消除它。

**课堂演示**
```bash
# 改 LOD 档位看效果（改 src/render/paint.h 的 AREA_DOT_PX 后重编）
./build_rel/skedaddle --shot-offscreen --graph=products/graph/beijing \
    --lat=39.9 --lon=116.4 --zoom=9 --w=1280 --h=800 --out=z9_a.bmp
python3 scripts/bmpdiff.py z9_a.bmp z9_b.bmp    # 看丢了多少要素（要素色→背景色=丢要素）
```

**练习**：把 `AREA_DOT_PX` 分别改成 2 / 4 / 8，各出一张图，用 `scripts/bmpdiff.py` 量化"净损失面积"（变背景色像素 − 从背景色变出像素，正数=丢要素）。

**验收**：能用**确定性量**（`areas_dotted`）而非时间来解释 LOD 档位的影响——因为时间会被机器负载放大一个数量级。

---

### 第 7 课 · 渲染管线编排

**课时目标**：看懂一帧是怎么从"图集+视口"变成"像素缓冲"的，以及 Qt 与离屏两套前端如何共用同一入口。

**必读代码**
- `src/ui/window.h` / `window.c`：`App` 状态机、`paint_frame`（`window.c:1190`，渲染编排）、`app_render_to_buf`（`window.c:1456`，绑定 `Raster` 后调 `paint_frame`）。
- `src/ui/offscreen_shot.c` / `offscreen_shot.h`：`offscreen_shot_main`（`skedaddle --shot-offscreen` 单一可信源；纯内存缓冲 `0x00RRGGBB` + 内建 24 位 BMP 写出，零 SDL）。
- `src/ui/chrome.c` / `chrome.h`：`ChromeID`/`paint_chrome`/`chrome_hit`/`chrome_act`（自绘标题栏/顶栏/右栏/菜单/面板）。
- `src/ui/audit.c` / `audit.h`：`app_offscreen_audit`（像素计数 + 断言共享模块）。
- `src/render/labels.c` / `font.c`：地名/POI/路名标注的 SQL 缓存、行政区名分级、白描边与字形锐化（第 7 课 §3.6）。

**核心讲解**
1. **`paint_frame` 的 9 步**：背景 → 视口 bbox → `mapset_set_view`（进省加载/离省卸载）→ 分阶段跨省统一层级（先所有省的面、再所有省的路）→ 标注 → 叠加层（GPX/路线/车头）→ 昼夜蒙版 → 面板/chrome。
2. **★ `app_render_to_buf` 是唯一渲染入口**：Qt 把缓冲包成 `QImage::Format_RGB32`，离屏通道额外 `write_bmp24` + `app_offscreen_audit`。C 核心一行不动。
3. **分阶段计时**：`TICK(ms_bg)` 等是定位帧时间瓶颈的唯一手段——没有它就会去优化"投影贵"这种猜错的方向。
4. **★ 标注层（§3.6）**：`paint_frame` 第 6 步的"字"全在 C 核心。三个真实案例：① 纯缩放骗过 `bbox_moved_far`（跨度比判据）⇒ 500 米以下没字；② 中低 zoom 把市/区名画成和 POI 一样的小字 ⇒ "没有区域名"，修法是 `admin_font` 大字分级（必须 `font_open_default` 才有 CJK 回退）；③ 14px CJK 无 hinting + 白描边洗淡主色 ⇒ z12 字发灰，修法是 `glyph_sharpen` + 主色二次绘制。**负向对照要三关齐关**（`SKEDADDLE_LABEL_HALO=0` + `SKEDADDLE_FONT_SHARP=1.0` + `SKEDADDLE_POI_R=6`）。
**课堂演示**
```bash
./build_rel/skedaddle --shot-offscreen --graph=products/graph/beijing \
    --lat=39.9 --lon=116.4 --zoom=11 --w=1280 --h=800 --out=frame.bmp
# 看 stdout 里 ms_bg/ms_areas/ms_roads/ms_lines/ms_labels 的分阶段耗时
```

**练习**：在 `paint_frame` 里新加一个 `TICK(ms_mystage)`，包住某段代码，看耗时出现在哪；再用三关旧路径出一张 z12 图跑 `scripts/bmpdiff.py`，确认能逐字节复现改动前出图。

**验收**：能口述一帧的渲染顺序，并说明"为什么 Qt 前端和离屏共用 `app_render_to_buf`"；能说清"没字"的四步排查顺序（字体 → 缓存 → 阈值/避让 → 数据）。

---

### 第 8 课 · 地名搜索

**课时目标**：理解用 bigram 倒排索引替代 `LIKE '%x%'` 全表扫描的经典索引设计。

**必读代码**
- `src/search/search.h` / `search.c`：`SearchHit`（`name/lat/lon/kind/weight/dist_m/score`）；`search_open`/`search_query`/`search_bigrams`。

**核心讲解**
1. **bigram 切分**：按**字符**（不是字节）切成 `中关`/`关村` → 查 `gram` 表取交集 `place_id` → 逐条取详情 → **再用子串复核**（bigram 全中不等于连续）。
2. **precision/recall 权衡**：子串复核这一步是必须的，否则会召回"中"和"关村"恰好相邻但非连续串的误命中。
3. **数据来源**：直接吃现成 `places_auto.db`（SQLite），用 bigram 倒排索引。

**课堂演示**
```bash
./build_rel/skedaddle_search_cli --db=products/places_auto.db --q=中关村
./build_rel/skedaddle_places_build --help      # 看地名库怎么生成
```

**练习**：把"中关村"换成单字查询，观察它如何退化到 `gram` 前缀 + 全表 `LIKE`。

**验收**：能解释"为什么 bigram + 子串复核"比纯 `LIKE` 快且准。

---

### 第 9 课 · 路由算法（核心）

**课时目标**：读懂跨省双向加权 A* 的全过程，以及坐标键接边、Landmark 下界、转向限制的配合。

**必读代码**
- `src/route/route.h` / `route.c`：`RouteResult`、`RouteOpts`（**必须指名初始化**——位置初始化插字段会静默错位）、`router_route_ll`/`router_plan`/`plan_search`（**双向加权 A\* 主循环**）。
- `src/route/st.h` / `st.c`：`StEnt`/`StateTab`——开地址哈希，**状态主键 = 坐标 `coord_key_e7`**（不是 (省,下标)），前驱存 `prev_idx`（ents 下标，省一次冷哈希）。
- `src/route/revidx.h` / `revidx.c`：`RevIdx`（每省反向入边 CSR）。
- `src/route/lmtable.h` / `lmtable.c`：`LmTable`（`LM_MAGIC="LMAP"`，含图指纹 nprov/total_nodes）；`lm_dijkstra` 填表。
- `src/route/restr.h` / `restr.c`：`restr_blocked`/`restr_blocked2`（kind 0/1/2 三类，含 via=way 两段）。
- `src/route/sew.h` / `sew.c`：`sew_get`/`sew_build`（瓦片缝合缓存）。
- `src/route/cost.h` / `cost.c`：`edge_cost`（缺速用 `fallback_speed`）。

**核心讲解**
1. **★ 坐标即主键 `coord_key_e7`**：学 OsmAnd `id=(x31<<31)|y31`，跨省接边不需要"本省→全局"映射表。初版用 (省,节点) 当主键让北京→天津展开 111 万、抖 560 次，换键后消失。这是**状态空间设计**的范例。
2. **加权 A\* 的 w 是旋钮**：默认 `ROUTE_HEUR_W_DEFAULT=1.4`（京→郑 1076→88ms，代价 +2.8%）；有 lm.bin 时兜底 `1.25`（h 变紧，有效贪心度变了）；自适应两遍：第 1 遍基准 w 撞 `adapt_try=200000` 才换更重的 `1.8` 重跑。**按概率分布调参，而非手挑路线**。
3. **没有 CH，而是 Landmark（ALT）加速**：`lm.bin` 提供下界——**但它不是严格下界**（50 条里 2 条略次优），所以开 `--exact` 不再保证最优；验收对真值用 `SKEDADDLE_LM=0`。纠正"CH/contraction"的常见误解。
4. **终止条件**：`lb = exact ? max(ff,fb) : (ff+fb)`，≥`best` 即停。
5. **关键数据文件**：路由图 = `nodes.bin` + `edges.bin`（均 NMBIN 自描述）；加速文件 = `rev.bin`（每省反向入边）+ `lm.bin`（整图一份，带图指纹）。

**课堂演示**
```bash
./build_rel/skedaddle_route_cli --graph=products/graph/beijing \
    --from=39.90,116.40 --to=39.99,116.30 --geojson=route.json
./build_rel/skedaddle_heap_selftest --expect-fail     # 优先队列负向对照
./build_rel/skedaddle_revbuild_cli --verify           # rev.bin 逐元素自验收
./build_rel/skedaddle_lmbuild_cli --help
./build_rel/skedaddle_restr_ab                         # via=way 两段限制 A/B 对照
```

**练习**：把 `ROUTE_HEUR_W_DEFAULT` 改成 1.0（纯 Dijkstra）和 2.0，对比展开数与耗时（用 `skedaddle_route_cli` 的 `expanded`/`elapsed_ms` 输出）。

**验收**：能画出"双向 A* 一次展开"的数据流（取堆顶 → `members_of` → 松弛出边 → 查转向限制 → 相遇判定）。

---

### 第 10 课 · 导航层与语音

**课时目标**：理解从"一条路线"到"可播报的导航"的最后一公里。

**必读代码**
- `src/route/maneuver.h` / `maneuver.c`：`Maneuver`/`ManeuverList`——把 `RouteResult` 转成"左转/右转/到达"指令序列。
- `src/route/navsim.h` / `navsim.c`：`NavSim`——模拟定位 / 偏航 / 剩余 ETA，`navsim_start/advance/locate`。
- `src/route/voice.h` / `voice.c`：`VoiceRouter`/`VoiceFrame`——**只决定"说什么"**（纯逻辑）。
- `src/ui/voice_wire.h` / `voice_wire.c`：`nav_voice_frame`——**接线层**（取状态→编排→`sk_tts_speak`）。生产与 `tools/voice_e2e.c` 共用，是唯一裁决点。

**核心讲解**
1. **分层**：`voice.c` 只定"说什么"，发声在 `plat.c` 的 `sk_tts_speak`；接线**只有一份**在 `voice_wire.c`。接线若长在 `App` 里，无头自测够不着只能复刻 ⇒ 判据必假绿。
2. **顺序耦合**：到达那一刻既要置 `sim_paused=1` 又要播收尾语 ⇒ ①先播报后置位；②收尾语单独判、不受 paused 挡（否则永远播不出来，实测真 bug）。
3. **冷却**：官方 `say()` 是**所有**播报唯一出口，15 秒同文本冷却对限速/到达/告警/转向一视同仁。

**课堂演示**
```bash
./build_rel/skedaddle_navsim_cli --help              # 模拟定位/偏航/ETA
./build_rel/skedaddle_voice_selftest                 # 播报策略（话术/提前量/节奏）
SKEDADDLE_TTS_DRYRUN=/tmp/tts.txt ./build_rel/skedaddle_voice_e2e   # 端到端：直接断言真实发声文本
./scripts/voice_neg.sh                     # 负向对照
```

**练习**：在 `voice.c` 里加一条新播报规则（如"前方 200 米有收费站"），然后用 `skedaddle_voice_e2e` + `SKEDADDLE_TTS_DRYRUN` 验证它真的被说出。

**验收**：能解释"为什么 `voice_wire.c` 是唯一接线点、为什么收尾语要单独判"。

---

### 第 11 课 · 架构纪律与验收门禁

**课时目标**：理解"为什么这个工程敢改"——因为有一整套防回归门禁。

**必读代码 / 脚本**
- `scripts/check_layers.sh`：解析**真实** `#include`（剥块注释与行注释——本项目注释里常出现 include 路径文本），比对允许方向表，违规打印**文件行号**并退出非 0。跨模块改动前先跑它。
- `scripts/regress.sh`：用 `ASSERT-OK/ASSERT-FAIL` 标记双判据；含 `layer_dirs`（分层）、`area_lod_tier`（LOD 确定性量）、`shot/app_route/gpx_render/ovl_shot`（全走 `--shot-offscreen`）。
- `tools/core_no_sdl.c`：核心零 SDL 正反双向断言。
- `tools/blend_equiv.c` / `tools/clip_equiv.c`：自写光栅 vs SDL 软渲染逐像素等价对照体（SDL 在这里合法）。
- `scripts/bmpdiff.py`：逐字节比对两张 BMP（要素色→背景色=丢要素）。
- `scripts/ovl_px.py`：半透明比率判定（`|M−H| / |O−H| == pct/100`）。
- `scripts/check_doc_numbers.sh`：讲义 H1 编号与文件名一致（自带负向对照）。**当前无执行位**，`regress.sh` 的 `[ -x ]` 守卫会跳过它，需 `bash scripts/check_doc_numbers.sh` 手动跑。
- `scripts/regen_tutorial_blocks.py --check`：讲义逐字块 / 内嵌脚本与当前源码同步（幂等 + 能失败）。

**核心讲解**
1. **分层门禁是第二道防线**：链接器单趟解析（不用 `--start-group`）是第一道，`-Wl,--start-group` 已移除——它会让链接器多轮扫描悄悄补上反向依赖，边界形同虚设。
2. **确定性量 > 时间**：时间判据 flaky（机器负载放大一个数量级），所以用 `areas_dotted`、像素计数这类确定性量守 LOD。
3. **负向对照必须能失败**：任何判据都要注入一个"必失败"的改动来验证它真的在起作用（如 `skedaddle_heap_selftest --expect-fail`、`voice_neg.sh`）。
4. **★ 判据与讲义都是活资产（§3.7）**：UI 一改版，硬编码矩形坐标就会假红（`qt_ovl_alpha` 高度 420→470→430 三次重测），讲义逐字块也会悄悄过期。`regen_tutorial_blocks.py --check` 把"文档同步"变成退出码；`--ovl-alpha` 之类**会写回配置**的实验参数必须用 `XDG_CONFIG_HOME` + `APPDATA` 隔离，否则污染 `settings.ini` 让默认值用例假红。

**课堂演示**
```bash
./scripts/check_layers.sh     # 故意在一个 .c 里加 #include "ui/window.h" 到 map 模块，看它 FAIL
./scripts/regress.sh          # 全量回归，看基线 87 通过 / 0 失败
python3 scripts/regen_tutorial_blocks.py --check   # 讲义逐字块同步
```

**练习**：在 `src/render/paint.h` 里把 `AREA_DOT_PX` 改到 8，跑 `regress.sh` 的 `area_lod_tier`，确认它被抓出来；再改回 4。

**验收**：能独立跑全套门禁，并解释"为什么判据要用确定性量而非时间"。

---

## 三、综合项目与进阶专题（第 12–14 课）

> 逐课展开版见 [`docs/teaching/12_capstone.md`](teaching/12_capstone.md)，可运行小程序见 `tutorial/12_capstone/`。
> 两者同号，配套完成"改一个点 + 加一条判据"的闭环。

**目标**：独立完成"改一个点 + 加一条判据"，体验完整的"改 → 测 → 守"闭环。

**可选课题（按难度）**
1. **【易】新增一个 CLI 工具**：比如 `skedaddle_route_cli` 加 `--via` 途经点（接 `router_route_via`），并加一条 `skedaddle_alts_selftest` 式断言。
2. **【中】调一个 LOD 旋钮**：把 `AREA_DOT_PX` 从 4 调到 6，用 `bmpdiff.py` 量化净损失，更新 `docs/PERF-AREA-LOD-TIERS.md` 档位表，收紧 `regress.sh` 的 `area_lod_tier` 区间。
3. **【难】加一个新要素类型**：在 `paint.c` 加一种渲染要素（如 `paint_contours` 已存在，可复制其骨架做"等高线标注"），并在 `offscreen_shot` 输出里加一个计数，补一条 regress 判据。
4. **【难】路由新策略**：在 `cost.c` 的 `edge_cost` 里加一个 `avoid_unpaved` 的惩罚项，用 `skedaddle_route_cli` 对比开关前后的 `total_m`/`expanded`。

**交付要求**
- 改动走 `scripts/check_layers.sh` 零违规。
- 新增/改动有对应 `regress.sh` 判据（确定性量优先）。
- 提交前 `./scripts/regress.sh` 全绿（或仅剩已知环境缺失项）。

---

> 以下两节是核心段的进阶专题，仍属「核心代码」，排在综合项目之后、跨平台段之前。

### 第 13 课 · 坐标系基准（datum）：为什么"只换椭球"等于没转换

**课时目标**：讲清 `datum.c` 的数学——只换椭球不等于坐标转换；三参数赫尔默特变换与两条独立证据交叉验证。

**必读代码**：`src/utils/datum.{c,h}`、`tutorial/13_datum/`（自包含重实现，可独立 `gcc` 跑）；完整讲义 [`docs/teaching/13_datum.md`](teaching/13_datum.md)。

**核心讲解**：内部几何永远是 WGS84（OSM PBF 原生），坐标系只影响显示读数，不重投影底图；西安 1980 用全国 3 参数 (−104.57, −45.94, +5.30)，分区参数已否决（选错代价 > 全国最大 14.7 m）。

**课堂演示**：`cd tutorial/13_datum && gcc -std=c11 -O2 -Wall -o tut main.c -lm && ./tut`，看交叉验证"命中/命中"与 `datum.bmp`（灰直 / 蓝弯 / 红点）。

**验收**：能解释"EPSG 没登记 ≠ 没参数"，以及为什么两条独立证据互印才落值。

---

### 第 14 课 · 极简多语言（i18n）：为什么"中文是 msgid"是安全的

**课时目标**：默认 zh 模式等于"没装 i18n"——既有像素 / UI 自检（断言中文串、中文渲染）行为与接入前逐字节一致，这是它能无侵入挂进已有一整套回归的工程里的根本原因。

**必读代码**：`src/i18n/i18n.{c,h}`、`lang/*.xml`、`tutorial/14_i18n/`；完整讲义 [`docs/teaching/14_i18n.md`](teaching/14_i18n.md)。

**课堂演示**：`cd tutorial/14_i18n && gcc -std=c11 -O2 -Wall -o tut main.c && ./tut`，看 [A]–[E] 五场景（中文原样 / 内置英文兜底 / 文件覆盖 / 缺译回退 / 只译部分）。

**练习**：补一条 `skedaddle_i18n_selftest`，断言 `i18n_tr("设置")` 在 zh 模式 `== "设置"`、en 模式 `== "Settings"`。

**验收**：能说清 `skedaddle_i18n` 为何是叶子库、没有在 `utils/map/route/render` 与 `ui_qt` 之间引入反向依赖。

---

## 四、分课教案 · 跨平台代码段（第 15–17 课 · 最后实现）

> **本段最后做**：核心代码跑通、回归全绿之后，才依次实现平台抽象层、Qt 桌面前端、Android 原生 JNI App。
> 三节共同的不变量是「**C 核心一行不改**」——边界原则见第 16 课，分层门禁见第 11 课。
> 注意分类 ≠ 依赖顺序：`utils/plat` 是纯 DAG 的最底端叶子（全程底座），但它的**实现**刻意留到最后，
> 因为核心段先用 POSIX 路径跑通即可，平台分支是补上去的，不是前置条件。

---

### 第 15 课 · 平台抽象层（utils）

**课时目标**：理解"平台差异 100% 收口到 `plat.c`"这件事为什么重要。

**必读代码**
- `src/utils/plat.h` / `plat.c`：`SkMutex`/`SkCond`/`SkProc`/`SkDir`、`sk_thread_spawn`、`sk_now_ms/us`、`sk_tts_speak`、`sk_font_candidates`（中文字体候选链）。
- `src/utils/mem.h` / `mem.c`：`sk_mmap_file`（钉死 glibc `mmap_threshold` 避免主堆崩溃）。
- `src/utils/strpool.h`：`StrPool` + `sp_intern/sp_get`（路名去重池，id 0 = "无名"）。

**核心讲解**
1. **业务代码零 `#ifdef _WIN32`**：所有"换个平台就不成立"的调用都只在 `plat.c` 里出现。这是**可移植性 / 依赖倒置**的范本。
2. **链接纪律**：`plat.c` 的线程用 pthread 实现，故 `-pthread` 由 `skedaddle_plat` 库统一承载（CMakeLists.txt:213-222）。漏挂会在链接 `skedaddle_tts_selftest` 时报 `pthread_create` 未定义。

**课堂演示**
```bash
./build_rel/skedaddle_plat_selftest        # 平台层全接口自验收（正反双向）
```

**练习**：在 `plat.c` 里加一个 `sk_sleep_ms`（用 `usleep`/`Sleep` 适配），并在一个 selftest 里调用它。

**验收**：能在不碰任何业务 `.c` 的前提下，新增一个跨平台小工具函数。

---

### 第 16 课 · Qt 前端与边界原则

**课时目标**：理解"遗留 C 系统如何渐进式 Qt 化"——核心一行不动，只换缓冲归属与叠加层。

**必读代码**
- `src/ui_qt/qt_main.cpp`：**唯一的 GUI 进程入口**（`int main` 在 `qt_main.cpp:69`）。
- `src/ui_qt/map_canvas.h` / `map_canvas.cpp`：`MapCanvas`（`tick()`/`renderNow()`/`paintEvent`）；`m_px` 自持 `uint32_t` 缓冲，`app_render_to_buf` 画进它再包成 `QImage::Format_RGB32`（**踩过坑：RGBA8888→黑屏、RGBX8888→红蓝互换，用 RGB32 最稳**）；`drawScaleBar/drawCompass/drawGraticule/cursorCoordsText`（坐标网叠加层）。
- `src/ui_qt/main_window.cpp` / `download_dialog.cpp`：主窗口、下载对话框；`main_window.{h,cpp}` 还含**无边框窗口**的 `m_frameless` / `m_winStrip` / `m_winNormalGeom`（§3.10）。
- `src/ui_qt/qt_icons.cpp` / `qt_icon_button.hpp`：图标自绘（字体字符 `inFont()` 全 false ⇒ 自绘路径）、`IconButton`（自绘底板按系数算 alpha）。
- `scripts/regen_tutorial_blocks.py`：从源码生成讲义 §3.4/§3.5 的逐字块，改完 `main_window.*` 记得跑。

**核心讲解**
1. **边界原则**：C 渲染核心一行不动，只换"缓冲归属 + 叠加层由 QPainter 画"。`qt_ui=1` 时 `paint_frame` 跳过 C 自绘 chrome/搜索框/面板（由 Qt 真控件承担），但地图渲染保留。
2. **浮层透明度语义**：只淡底板、文字/图标保持清晰（`IconButton` 自绘底板按 pct 算 alpha；`QGraphicsOpacityEffect` 已否——它会把文字一起淡掉）。
3. **★ 地图缓冲 alpha 字节必须置 0xFF（Wayland 透明花屏）**：`app_render_to_buf` 传 `amask=0`，光栅器只写 R/G/B ⇒ 缓冲恒 `0x00RRGGBB`；Deepin Wayland 按 ARGB8888 上传 ⇒ alpha=0 全透明、地图区透出桌面乱色（"双击一片花"）。修复在 `map_canvas.cpp:75-76` 渲染后 `m_px[i] |= 0xFF000000u`；xcb/离屏无副作用。
4. **★ 字体加载失败要响亮告警**：`paint_frame` 的 `if(a->font)` 门控（`window.c:1303`）一旦 `font_open_default` 返回 NULL，所有文字静默丢弃、几何照常（`a->font_missing` 置位 + 响亮 stderr 告警在 `window.c:141-158`）。离线用 `skedaddle_font_probe` 或离屏诊断 `font (已加载|★加载失败)` 确认。
5. **★ 无边框窗口（§3.10，2026-11-06）**：`FramelessWindowHint` 去掉系统标题栏（**必须在 `show()` 前**，`SKEDADDLE_FRAMELESS=0` 可回退）；三个自绘控制按钮按需求移除后，退出走菜单「退出」(Ctrl+Q) / Alt+F4，最小化/最大化交给 WM 快捷键；只留画布顶部 10px 全宽**透明拖动带**做拖动 + 双击最大化。**不要 `showMaximized()`**（UOS/Deepin 会把无边框窗口置 HIDDEN 且几何不变），用自存 `QRect` + `setGeometry(availableGeometry())` 状态机；纯 Wayland 下 `QWidget::move()` 无效、Qt 5.11 无 `startSystemMove`，靠 WM Alt+拖动兜底。`--ui-test` ⑫ 断言「窗口里没有控制按钮」，注入假按钮必须让它 FAIL。

**课堂演示**
```bash
./build_rel/skedaddle --graph=products/graph/beijing
# 在 GUI 里缩放/平移/搜索/起终点导航，肉眼确认交互
SKEDADDLE_FRAMELESS=0 ./build_rel/skedaddle --ui-test   # 对照：系统标题栏模式 114 OK / 0 FAIL
python3 scripts/regen_tutorial_blocks.py --check        # 讲义逐字块是否与源码同步
```

**练习**：在 `map_canvas.cpp` 的 `paintEvent` 里加一行 `QPainter` 叠加（如在角落画个 FPS 文本），确认不影响 C 核心；再往拖动带注入一个假「最小化」按钮，确认 `--ui-test` 立刻 FAIL。

**验收**：能说清"Qt 前端改了什么、C 核心为什么完全没动"，以及去掉系统标题栏后每一项窗口能力的落点。

---

### 第 17 课 · Android 原生 JNI App：把同一个 C 核心搬进手机

**课时目标**：理解"换壳不换核心"在移动端的落点——C 核心零改动，平台差异全收在 `plat.c` 的 `__ANDROID__` 分支。

**必读代码**
- `android/app/src/main/cpp/skedaddle_jni.cpp`：`app_render_to_buf` 画进 Bitmap，每帧后 `px[i] |= 0xFF000000u`（复刻第 16 课 Wayland alpha 教训）。
- `src/utils/plat.{c,h}` 的 `__ANDROID__` 分支：`sk_android_set_paths` / `font_dir` 必须在 `app_init` **之前**注入。
- Java 侧 `Skedaddle.java` / `MapView.java` / `DownloadActivity.java` / `Prefs.java` / `UpdateChecker.java`：省图增量更新与 OTA 清单。

**核心讲解**
1. JNI 桥只把 C 像素缓冲交给 Android Bitmap，不重写渲染；`libskedaddle.so` 由 NDK CMake 直接复用 `src/` 核心。
2. 第三方 `sqlite3` / `protobuf-c` 必须 vendor（NDK 不自带），取源命令见 [`docs/android.md`](android.md) §4。
3. 本机无 NDK 时的契约校验法：用忠实复刻 NDK 的 `jni.h` 垫片 + 真实 `src/` 头做 `-fsyntax-only`（真实 JDK 头不足以校验 Android `env->Method()` 写法）。

**课堂演示**
```bash
# 构建 / 部署 / 自托管命令见 docs/android.md
adb push products/graph /sdcard/Android/data/com.skedaddle.android/files/
```

**练习**：去掉 `skedaddle_jni.cpp` 的 `|= 0xFF000000u`，观察整屏花屏，与第 16 课 Wayland alpha 教训对照。

**验收**：能说清"Android 改了什么、C 核心为什么一行未动"，以及 `plat.c` 为何是唯一的平台差异收拢点。完整讲义见 [`docs/teaching/17_android.md`](teaching/17_android.md)。

---

## 五、附录

### A. CLI 工具速查（按"演示什么"分类）

| 类别 | 工具 |
|---|---|
| PBF/建图 | `skedaddle_pbf_info` `skedaddle_graph_extract` `skedaddle_graph_check` `skedaddle_tag_stats` `skedaddle_pbfmap_check` `skedaddle_gen_contours` |
| 路网/路由 | `skedaddle_route_cli` `skedaddle_revbuild_cli` `skedaddle_lmbuild_cli` `skedaddle_lmdist_cli` `skedaddle_restr_ab` `skedaddle_restr_probe` `skedaddle_tile_xcheck` `skedaddle_tile_selftest` `skedaddle_heap_selftest` `skedaddle_alts_selftest` `skedaddle_lane_selftest` |
| 导航层 | `skedaddle_navsim_cli` `skedaddle_voice_selftest` `skedaddle_voice_e2e` |
| 搜索/地名 | `skedaddle_search_cli` `skedaddle_places_build` |
| 覆盖/坐标 | `skedaddle_coverage_cli` `skedaddle_datum_selftest` |
| 字体/TTS | `skedaddle_font_selftest` `skedaddle_font_probe` `skedaddle_font_cjk_check` `skedaddle_tts_selftest` |
| GPX | `skedaddle_gpx_selftest` |
| 架构门禁 | `skedaddle_core_no_sdl` `skedaddle_blend_equiv` `skedaddle_clip_equiv` `skedaddle_plat_selftest` `skedaddle_dmap_selftest` `skedaddle_disp_tile_selftest` `skedaddle_disp_runtime_selftest` |
| 前端 | `skedaddle`（含 `--shot-offscreen`） |

### B. 关键不变量（贯穿全课）

1. 内部几何永远是 **WGS84（1e-7 度 int32）**；基准只动显示读数。
2. 测距一律 `haversine`，Web Mercator 只用于显示。
3. **PBF stringtable 不是 NUL 结尾**，必须走 `pbf_str`。
4. `AREA_DOT_PX`（paint.h，定档 4）是面 LOD 旋钮，回归用确定性量 `areas_dotted`。
5. `coord_key_e7` 是路由状态主键，跨省接边零映射表；`REDGE_SEAM` 标记接缝段。
6. 核心零 SDL；`app_render_to_buf` 是 Qt 与离屏共用的唯一渲染入口。
7. `.cpp` 包含 C 头必须 `extern "C"`（`datum.h`/`settings.h`/`plat.h`/`gpx.h` 等）。
8. 优先队列 `PQueue`（`pqueue.h`）是 4 叉最小堆，载荷存 **StEnt 下标**（非 key）。

### C. 常见坑（给学生提前打预防针）

- `sk_mercator_y_fast` ≠ `sk_mercator`（故意近似）。
- `RouteOpts` 必须**指名初始化**，位置初始化会静默错位。
- 改 `.hpp` 后 ninja 可能不重编对应 `.cpp` ⇒ 注入的负向对照没进 exe ⇒ 假通过，需 `touch` 对应 `.cpp`。
- 实验参数（`--ovl-alpha` 等）跑实验必须隔离配置（`XDG_CONFIG_HOME=/tmp/xxx`），否则污染真实 `settings.ini`。
- 删文件/删 include 前必须 `grep` 它提供的符号在别处的声明（`window.h` 的 `app_run` 成悬空声明就是教训）。
