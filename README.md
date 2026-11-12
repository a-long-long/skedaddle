# Skedaddle（跑路了）

纯 C 实现的**离线地图渲染与导航**程序。

自写 OSM PBF 解码器 → 编译成自有的 `.nmbin` 产物 → `mmap` 懒加载 → 自写像素光栅化渲染 +
双向加权 A\* 路由。**零第三方地图/导航库**：前端用 Qt，地名库用 SQLite3，PBF 解压用 zlib。

---

## 定位：单机离线内核，不追云端能力

> 这一节是**项目边界**，不是功能路线图。改动前请先读
> [`CONSUMER-NAV-GAP-2026-11-12.md`](CONSUMER-NAV-GAP-2026-11-12.md)。

本项目是「**构建期固化数据 + 单机离线内核**」：PBF 在构建期编译成只读产物，运行时
`mmap` 按需加载，**无账号、无网络请求、无遥测、无竞价排名**，一切渲染/算路/搜索都
发生在本机。这与现行商业导航「**云端数据服务 + 采集终端**」是两种物种，不是成熟度差异。

因此：

- **明确不追 L1（云端数据飞轮型）**：实时路况与动态避堵、红绿灯倒计时、融合历史/信号/
  天气的预测 ETA、违章与事故预警、POI 营业状态、实时公交、街景与 3D 影像、AI 对话式
  搜索。这些能力的共同前提是**亿级用户轨迹回传 + 云端算力 + 第三方数据资质**——高德
  红绿灯倒计时的官方说明是靠上亿用户经过路口的等待时间算出来的、不接信号机；Google
  称每秒纳入 500 万次路况更新。再写十万行 C 代码也不会让它们出现，要拥有就必须转型为
  「服务端 + 客户端 + 数据运营」，同时放弃上面那几项当前优势。**这是路线选择，不是功能差距。**
- **L2（本地可实现型）按确定性判据逐步补齐**：多出行方式 profile、地址层、历史、单位制、
  数据覆盖、DEM 等高线等。判据一律用像素 / 计数 / 展开数 / 退出码 + 负向对照，
  不引入运行时网络依赖。
- **L3（产品化与合规型）按定位取舍**：上架、账号体系、车机互联、测绘资质相关能力，
  默认不做。

一句话：**强项是可复现、可审计、完全离线的工程内核；短板是任何需要"活数据"的东西。**
别把 L1 写进 TODO，也别拿商业 App 的功能数当本项目的验收口径。

---

## 特性

| 方面 | 说明 |
|---|---|
| **渲染** | 面 / 路 / 线要素，自写像素光栅化（`src/render/raster.{h,c}`），地名与路名标注（路名等级随比例尺放开、POI 可整体关闭） |
| **路由** | 双向加权 A\*，ALT 地标下界加速，CSR 邻接 + 4 叉堆 + 状态哈希表 |
| **转向限制** | 允许集（`only_*`）/ 必经（`via=way`）/ 单点禁转三类，跨省接缝补查 |
| **搜索** | 地名 / POI（`amenity`/`shop`/`tourism`/… 分类加权），bigram 匹配 |
| **导航** | 偏航重算、ETA、转向指令（直行 / 左右转 / 环岛第 N 出口 / 掉头） |
| **空间索引** | 瓦片级索引 + 查表化，跨省查询与覆盖度检测 |
| **按需加载** | 运行时只 mmap 视口所在省份，滚出即卸载（`src/map/mapset.c`） |

### 实测性能（本机，8 省数据）

- **渲染**（第二帧，1280×800）：z9 115 ms / z11 56 ms / z13 42 ms / z15 31 ms
- **算路**（50 条城市对，中位 / p90 / 最慢）：123 ms / 753 ms / 1503 ms，>3 s **0 条**
- 图规模：5228 万节点 / 4072 万边；全部数据 4.2 GB（路由图占 94%）

---

## 依赖

- **Qt5 或 Qt6**（前端 UI 与控件）
- **SQLite3**（地名库 `places.db`）
- **zlib**（PBF blob 解压）
- **protobuf-c** + **protoc-c**（**仅构建期**：把 `proto/*.proto` 编成 C 代码；
  `src/pbf/` 的解码逻辑是手写的，生成产物不入库）
- CMake ≥ 3.13、支持 C11 的编译器

> 本机的 Qt / protobuf-c 装在 `~/.local`，默认 `pkg-config` 看不见 ⇒ **构建前先 source 环境脚本**。

## 构建

```bash
source env.sh          # 修正 PKG_CONFIG_PATH / LD_LIBRARY_PATH / PATH
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

`env.sh` 会在 source 时自检依赖并打印版本（Qt / `libprotobuf-c` 等），
没有输出说明环境没生效。

## 跨平台

目标平台：**Linux / macOS / Windows**（MSVC 与 MinGW）。所有"只在某平台成立"的调用
统一收进平台抽象层 `src/utils/plat.{h,c}`，业务代码里不再出现 `#ifdef _WIN32`：

| 平台差异 | 抽象层提供 |
|---|---|
| 路径分隔符 / 盘符 / 拼接 | `sk_sep` / `sk_path_join` / `sk_base_name` / `sk_is_abs` |
| 临时目录（`/tmp` 在 Windows 不存在） | `sk_temp_dir`（TMPDIR/TEMP/TMP → `/tmp` → `.`） |
| 字体路径（`/usr/share/fonts` vs `%WINDIR%\Fonts`） | `sk_font_candidates`（宋体优先链，逐平台） |
| `mmap` / `madvise` | `sk_map_file` / `sk_unmap_file`（Windows: CreateFileMapping） |
| `opendir` / `mkdir` / `unlink` / `rmdir` / `stat` | `sk_dir_*` / `sk_mkdir_p` / `sk_rmtree` / `sk_unlink` / `sk_stat_file` |
| `/proc/self/exe` / `fork`+`execv` / `kill` / `usleep` | `sk_exe_path` / `sk_spawn_wait` / `sk_pid_alive` / `sk_sleep_ms` |
| `clock_gettime` / `getrusage` | `sk_now_ms` / `sk_now_us` / `sk_peak_rss_mb` |

各平台构建：

```bash
# Linux / macOS
source env.sh && cmake -B build && cmake --build build -j8

# Windows（MSYS2 UCRT64：Qt6 / sqlite3 / zlib / protobuf-c）
# ★ 编译期 PATH 绝不能放 Qt bin（会加载旧 libstdc++，退出码 1 且 stderr 为空）
./build_release.sh
```

Windows 注意：入口 `main()` 直接用（CMake 不接管 WinMain）；数学函数无需显式链
`libm`（仅类 Unix 平台链）；峰值内存走 `psapi`。运行期 PATH 需含 Qt bin、`ucrt64/bin`
与 `dist/skedaddle-release`。字体优先级：`--font=` 命令行 > `SKEDADDLE_FONT`
环境变量 > 平台候选链。

平台层契约由 `tools/plat_selftest.c` **正反双向**钉住（回归门禁含这两类用例）：
正向断言"该成功的必须成功"（含 spawn 自身并原样带回退出码），负向断言"该失败的
必须失败"（不存在路径 / 空 PATH / 起不来的进程）。本机只有 POSIX 能真跑，Windows
分支按同一契约编写——新增平台分支时先在 skedaddle_plat_selftest 里补契约，再动业务代码。

## Qt 前端（可选）

> 注：早期 Qt 前端曾用 `SDL_Surface` 中转（`app_render_to_surface()` 画进 SDL_Surface 再包
> `QImage`），现架构已演进为**零 SDL**——`app_render_to_buf()` 直接把地图画进 C 像素缓冲，
> 再包成 `QImage(Format_RGB32)` 由 `paintEvent` blit；无头出图 `src/ui/offscreen_shot.c` 同样
> 零 SDL（malloc 缓冲 + 内建 24 位 BMP writer）。**渲染 / 算路 / 搜索核心一行不改**。

`skedaddle` 用 **Qt 做真控件**（QMainWindow 菜单栏 / QLineEdit 搜索 / 步骤面板；
★ 不放 QToolBar —— 切换时会抬高并移动画布，按钮一律做成画布子浮层），地图仍走
现有 C 光栅化器，仅把最终像素缓冲交给 Qt 显示。

```bash
cmake -B build -DCMAKE_PREFIX_PATH=<Qt5 前缀>   # 找得到 Qt5Widgets 即可
cmake --build build --target skedaddle -j8
./build/skedaddle --graph=products/graph/beijing        # 参数与旧版一致
QT_QPA_PLATFORM=offscreen ./build/skedaddle --shot=x.png # 无人值守出图
```

要点（踩过的坑）：

- **像素格式 + alpha**：C 核心 `raster_rgb()` 只写 RGB、`QImage` 用
  `Format_RGB32`（小端内存序 B,G,R,A）。★ 上传前必须把每个像素 OR 上
  `0xFF000000`——Wayland 按 ARGB8888 上传时 alpha=0 会整屏「一片花」；Android JNI
  每帧同理。用 `Format_RGBA8888` 整帧全透黑屏、`Format_RGBX8888` 红蓝互换。
- **无边框窗口**：`FramelessWindowHint` 必须在 `show()` 之前设（`SKEDADDLE_FRAMELESS=0`
  可回退）；`showMaximized()` 在 UOS/Deepin 上对无边框窗口会得到 HIDDEN，改为自记
  `geometry()` → `setGeometry(可用区)` 的还原状态机。checkable 按钮同步勾选态用
  `QSignalBlocker`，否则 `setChecked()` 触发 `toggled` 递归 SIGSEGV。
- **平移不用 `mv_pan_pixels`**（它不知道旋转）：改用两次 `mv_unproject` 取地理差加到
  视口中心，旋转由投影自动处理。
- Qt 5.11 的 `QMenu::addAction(text, functor)` 模板重载匹配不上，统一显式 `QAction`。

## 验收

```bash
# ★ 缺省用 build/；本机正式构建在 build_rel/，必须显式指过去，否则跑的是旧二进制
SKEDADDLE_BUILD_DIR="$PWD/build_rel" ./scripts/regress.sh
SKEDADDLE_BUILD_DIR="$PWD/build_rel" ./scripts/regress.sh --build   # 重编 + 零警告门禁
python3 scripts/regen_tutorial_blocks.py --check   # 改过 regress.sh 后必须重生成讲义逐字块
bash scripts/check_layers.sh --self-test           # 分层门禁的负向对照
```

**115 项回归全绿（2026-11-12 实跑 115/0）、编译零警告**是硬门禁
（`--build` 把任何新警告当失败）。回归覆盖：逐省 + 跨省渲染、跨省算路、导航模拟、
转向限制正/负对照、反向索引校验、四叉堆自检、瓦片索引全图对拍、覆盖度、搜索、
POI 分类自检、ALT 地标表、平台抽象层正/负向契约；11-12 新增的 L2 能力各自带
**可失败的负向对照注入**（单位制 / 出行方式 profile / 地址层 / 最近地点 / 等高线全链 /
扩省不变量）；同日 M1 的 NMBIN 字段表一致性与内容摘要再带 6 条判据（14 种 schema
夹具 + 8 省真实产物扫描、FIPS 标准向量对拍、四种界内/文件级负向注入）。
完整回归会把 `view_*` 写成默认值 ⇒ 跑前备份真实 `settings.ini`、跑完恢复。

## 显示层按需解码（PBF 模式）

`--pbf-display=<sources目录>[:LRU上限]` 让显示层（面/线要素）**从原始 PBF 运行时解码**，
不再从 nmbin 打开 `lines/areas/polyrefs` —— 这三个文件因此可以整个不落盘。

```bash
./build/skedaddle --graph=products/graph --pbf-display=sources:3
```

解码结果与落盘文件**8 省逐字节相同**（`build/skedaddle_pbfmap_check` 可验证），渲染端零改动。
代价是首次进省要同步解码（天津 0.6 s / 北京 1.6 s / 河北 4.4 s），适合后台预热。

⚠️ 省磁盘的收益有限：nmbin 的 4.2 G 里 **94% 是路由图**（A\* 需要全图），那部分省不掉。
详见 [`PROGRESS.md`](PROGRESS.md) 第五节。

## 数据

```bash
./scripts/sync_maps.sh          # 从 sources/*.osm.pbf 重建全部产物
```

| 目录 | 内容 | 是否入库 |
|---|---|---|
| `sources/` | 原始 `*.osm.pbf`（8 省，446 MB） | ❌ 上游下载获得 |
| `products/` | `.nmbin` 产物：瓦片 / 路由图 / 反向索引 / 地标表 / 地名库（4.2 GB） | ❌ 可确定性重建 |
| `build/` `build-san/` | 编译产物 / Sanitizer 构建 | ❌ |

`products/` 由 `sync_maps.sh` **确定性重建**（路网按 manifest 指纹、地名库按 `.db.meta` 指纹）。
`.nmbin` 格式自带版本号（`NMBIN_VERSION`），改格式必须顶版本。
用 `--pbf-display=sources` 可让显示层改为运行时从 PBF 解码，`lines/areas/polyrefs` 就不必落盘。

## 目录

```
src/pbf/     自写 OSM PBF 解码（zlib blob / DenseNodes / delta 编码）
src/map/     图集、路由图构建、CSR 图、瓦片索引、覆盖度
src/route/   双向 A*、转向限制、ALT 地标表、反向索引、堆、转向指令
src/render/  渲染管线、自写像素光栅、字形与地名标注
src/search/  地名 / POI 搜索
src/ui/      窗口与交互（无头离屏出图 / 状态栏 / 搜索浮层）
src/utils/   平台抽象层（plat）、内存与文件映射约定
tools/       20 个命令行工具（构建 / 自检 / 验收 / 诊断）
scripts/     回归门禁、地图同步、像素对比
proto/       OSM 官方 osmformat.proto（字段编号一致，删掉了导航用不到的 osmesis 字段）
```

## 命令行工具

`map_shot`（渲染截图，含像素级断言）、`skedaddle_route_cli`（算路基准测试）、`skedaddle_navsim_cli`
（导航模拟）、`skedaddle_restr_ab`（转向限制双向判定 + 负向对照）、`skedaddle_heap_selftest`（堆对拍）、
`skedaddle_tile_selftest`（瓦片索引全图对拍）、`skedaddle_graph_check` / `skedaddle_graph_extract`（图结构校验）、
`skedaddle_places_build`（地名库构建）、`skedaddle_lmbuild_cli`（地标表构建）等。

---

## 文档

- [`PLAN.md`](PLAN.md) —— 设计与实现记录（渲染 §19-32 / 算路 §33-50 / 转向限制 §14·§33）
- [`arch.md`](arch.md) —— 需求与架构
- [`OSMAND-APP-MODULES.md`](OSMAND-APP-MODULES.md) —— OsmAnd 模块汇总与功能域对照
- `.workbuddy/memory/` —— 长期约定与各阶段工作日志

## 授权与参考

本项目为**独立实现**。对已有开源项目的借鉴均**在代码注释中点名注明出处**（如坐标键编码
借鉴自 OsmAnd `RoutePlannerContext.cpp`，加权 A\* 启发式借鉴自 OsmAnd `RoutePlanner.cpp`），
实现均为自行改写。`proto/` 下的文件是 OSM 官方 `osmformat.proto`（字段编号与官方一致，
仅删除导航用不到的 osmesis 字段），另有 `fileformat.proto`（OSM 官方 PBF 容器格式）。

地图数据 © OpenStreetMap 贡献者，遵循 ODbL。
