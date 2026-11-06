# 演化链合流点：`mini/` ↔ 最终 `src/` 代码

> 第 17 课 · 演化链终点。前 17 课把一份工程从「一块像素缓冲」养到「两个前端」；
> 这一页负责把它和真正的生产代码**对上**，并如实写清差在哪。
> 入口：[`README.md`](README.md)；终点快照：[`steps/step17_android/main.c`](steps/step17_android/main.c)。

## ① 「最后到最终代码」的准确含义

先排除一个误会：`mini/` **不是** `src/` 的祖先分支，也不是把 `mini/` 改个名字就能编译出
`skedaddle`。它是**同一套概念骨架的教学投影**。"对上"指下面三件事，且只指这三件：

1. **不变量对齐** —— 像素缓冲 `0x00RRGGBB`、经纬度 1e7 度 int32、`tag = field<<3 | wire`、
   可见 bbox 驱动加载、9 阶段图层顺序、同文本 15s 冷却、alpha 交回前端前置 0xFF、
   平台差异收拢到一处 —— 这些约定在 mini 与生产代码里同名同义，不是两套说法。
2. **顺序对齐** —— `step00`→`step17` 的实现顺序 = [`docs/teaching/index.md`](../../docs/teaching/index.md)
   课程表：**先核心 00–14，最后跨平台 15–17**。第 15 课之前不出现任何平台宏，
   第 16/17 课只加外壳、不改前面任何一行。
3. **纪律对齐** —— 每课至少新增一条确定性判据；第 11/12 课的负向对照必须能失败；
   时间只作兜底；讲义与生成物有 `--check` 门禁。这些不是教学修辞，是
   [`scripts/regress.sh`](../../scripts/regress.sh) 同一套做事方式。

**不承诺**：mini 的算力、数据规模、跨省能力、字体与事件循环都不等价。第 ③ 节逐条列出。

## ② 逐模块对照表（18 行）

| 课 | `mini/` 落点 | 对齐的不变量 / API | 真实工程落点 | 真实版多做了什么 |
|---|---|---|---|---|
| 0 | `step00.h` 的 `MiniCtx` / `mini_write_bmp` | 一块 `uint32_t` 缓冲 + `0x00RRGGBB` + 24 位 BMP | `src/render/raster.c`、`src/ui/offscreen_shot.c` | 裁剪、alpha、LOD；离屏审计计数 |
| 1 | `step01.h` 的 `mini_geo_q` / `mini_project` | 1e7 度 int32、Web Mercator 256px 瓦片 | `src/utils/geo.c`、`src/map/map_view.c` | 快速墨卡托与逆变换的精度对拍 |
| 2 | `step02.h` 的 `mini_pbf_varint` | protobuf `tag = field<<3 \| wire`、varint | `src/pbf/pbf_stream.c`、`src/pbf/pbf_block.c` | zlib blob、StringTable、DenseNodes delta |
| 3 | `step03.h` 的 `mini_bin_locate` | header + section 表 + 定长数组，按 tag 定位 | `src/map/binfmt.c`、`src/map/mapset.c` | 逐 tile 开→拼→关，规避 Windows 映射句柄上限 |
| 4 | `step04.h` 的 `mini_graph_build` | 前向星挂链、度数、可达性 | `src/map/graph_build.c`、`src/map/road_tags.c` | 车道/转向限制、rev.bin 反向表、道路等级 |
| 5 | `step05.h` 的 `mini_view_bbox` | 中心 + zoom → 屏幕像素 → 瓦片键 | `src/map/map_view.c`、`src/map/tileidx.c` | 跨省按 bbox 进/出省加载、disp tile |
| 6 | `step06.h` 的 `mini_clip_line` / `mini_polyfill` | Liang–Barsky 裁剪、扫描线、alpha 混合 | `src/render/raster.c`、`src/render/paint.c` | 面/路/线统一分层、bbox 缓存、LOD 档位 |
| 7 | `step07.h` 的 `mini_paint_frame` | 一帧固定 9 阶段、后画盖先画 | `src/ui/window.c`、`src/render/paint.c`、`src/ui/chrome.c` | 真实 `paint_frame` 吃 8 省数据、夜间蒙版、自绘界面 |
| 8 | `step08.h` 的 `mini_search` | 查询串 → 命中下标，供渲染消费 | `src/search/search.c` | SQLite 地名库、拼音/前缀索引、多路召回 |
| 9 | `step09.h` 的 `mini_route` | 权重松弛 + 前驱回溯 | `src/route/route.c`、`src/route/lmtable.c` | 跨省分层、地标 A*、启发权重整定、质量对拍 |
| 10 | `step10.h` 的 `mini_nav_say` | 路径 → 机动 → 播报；15s 同文本冷却 | `src/route/maneuver.c`、`src/route/voice.c`、`src/ui/voice_wire.c` | 到达帧先播报后置暂停、TTS 真实发声、DRYRUN 判据 |
| 11 | `step11.h` 的 `mini_gate_negative` | 判据必须能失败（负向对照） | `scripts/regress.sh`、`src/ui/audit.c` | 87 条用例、像素/展开数等确定性量、环境隔离 |
| 12 | `step12.h` 的改点闭环 | 改一个点 → 加判据 → 改回去验证 FAIL | `scripts/regress.sh`、`scripts/bmpdiff.py` | 逐像素 BMP 对拍、量化「净损失面积」 |
| 13 | `step13.h` 的 `g_mini_datums` | CGCS2000 平移 0；西安 80 三参数；北京 54 有 TOWGS84 | `src/utils/datum.c` | 椭球/基准分离的交叉验证，分区参数已否决 |
| 14 | `step14.h` 的 `mini_tr` | 中文作 msgid、未登记回退原串 | `src/i18n/i18n.c`、`lang/en.xml` | XML 加载、全量词条、运行时切换语言 |
| 15 | `step15.h` 的 `mini_plat_sep` | 平台差异只此一处，业务代码零平台宏 | `src/utils/plat.c`、`src/utils/plat.h` | 线程/mmap/字体目录/TTS 的 Windows 与 Android 分支 |
| 16 | `step16.h` 的 `mini_front_render` | C 核心只写 RGB ⇒ 交回 Qt 前 `\|= 0xFF000000` | `src/ui_qt/map_canvas.cpp`、`src/ui/offscreen_shot.c` | Qt 事件循环、无边框窗口、浮层、DPI/输入 |
| 17 | `step17.h` 的 `mini_jni_render_frame` | 第二个前端复用同一 C 核心，alpha 同样要补 | `android/app/src/main/cpp/skedaddle_jni.cpp`、`src/utils/plat.c` | 真 JNI 签名、NDK/CMake、SurfaceView、OTA |

> 第 16 课那一行不是举例：Wayland 下按 ARGB8888 上传 alpha=0 的缓冲会整片透明花屏，
> 是本项目真实踩过并修掉的坑；mini 把修复复刻成一条可验证的内置判据。

## ③ 差距清单：`mini/` 走到生产代码还差什么

| 维度 | `mini/`（教学投影） | 真实工程 | 去看哪里 |
|---|---|---|---|
| 数据 | 手写 3 节点 / 4 点 5 边 / 6 个地名的常量 | 8 省 OSM PBF → graph → disp tile，单省 GB 级 | `src/pbf/pbf_block.c`、`src/map/mapset.c` |
| 渲染 | 320×240 玩具图、纯几何代替字形 | 1280×800、CJK 字体、白描边、分级地名、路名 | `src/render/labels.c`、`src/render/font.c` |
| 每帧 | 9 个阶段只记录顺序，① 底色刻意不重涂 | `paint_frame` 真重涂并跨省统一分层 | `src/ui/window.c` |
| 算路 | 4 节点 O(n²) Dijkstra | 跨省分层 + 地标 A* + 反向表 | `src/route/route.c` |
| 外壳 | `void*` 桩代替 JNIEnv，无事件循环 | Qt 事件循环 + NDK/Gradle + OTA | `src/ui_qt/qt_main.cpp`、`docs/android.md` |
| 验收 | 54 条内置判据 + 5 类负向对照 | 87 条回归 + 分层门禁 + 像素对拍 | `scripts/regress.sh`、`scripts/check_layers.sh` |

**唯一刻意与生产代码相反的地方**：mini 的管线不重涂背景。因为演化链要求"上一步画的
东西下一步还在"，若真重涂就会把前几课的要素擦掉。这条差距写在这里，不藏。

## ④ 最后一课怎么上（对照阅读法）

1. 跑 [`steps/step17_android`](steps/step17_android) 的 `main.c`，确认输出里
   `MINI-STEP-00-OK` 到 `MINI-STEP-17-OK` 一个不少、`MINI-FINAL-OK` 在最后。
2. 打开 [`diffs/16_to_17.diff`](diffs/16_to_17.diff)：终点只多了一个 `step17.h`
   与 `main.c` 的两行 —— 直观看到"换壳不动核心"。
3. 按第 ② 节表格逐行，从 mini 的函数名跳到对应 `src/` 文件，只读**同名的那一个不变量**
   （例如 `mini_front_render` 对照 `src/ui_qt/map_canvas.cpp` 的缓冲上传），
   不要一上来读整个生产模块。
4. 用第 ③ 节差距清单给自己出 3 道题：任选一行，说清"真实版为什么必须多做这一步"。
5. 回到 [`docs/teaching/17_android.md`](../../docs/teaching/17_android.md) 与
   [`docs/teaching-plan.md`](../../docs/teaching-plan.md)，看讲义如何把同一个终点
   从"读源码"角度再讲一遍。

## ⑤ 这一页本身也要被验收

`scripts/check_mini_chain.py` 会抽出本页所有反引号里的 `src/` / `scripts/` /
`android/` / `lang/` / `docs/` / `tutorial/` 路径并确认它们真实存在 ——
**对照表不许指向不存在的文件**（教程行号/路径漂移是本项目踩过的坑）。
改了模块名或搬了文件，跑一次全链门禁即可发现。
