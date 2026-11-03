# skedaddle 项目长期约定

> 只留结论与"别再试"；推导看 PLAN.md、PROGRESS.md、`.workbuddy/memory/2026-*.md`。

## 固定动作
- `sources/` 只放原始 `*.osm.pbf`；`products/` 全可删、`./scripts/sync_maps.sh` 重建。
- ★★★ **`skedaddle_graph_extract --out=` 必须与脚本 `OUT_ROOT`(=`products/graph`) 逐字一致**。传错会留脚本永远抓不到的孤副本（`--out=products/graph/graph` 套两层 / 市名当省名），一份 pbf 被抽多遍、纯重复。删重复前先 `md5sum` 确认字节相同 + 无脚本依赖。
- 产物终态：`products/graph/<省>/` 带 `disp.idx + lanes.bin + t_*`。**整省显示层三件套（areas/lines/polyrefs.bin）自 2026-11-03 起不再生成**（`build_display` 只构 polyrefs 缓冲 + 调 `write_disp_tiles`；tile 化失败现已**致命**，无整省兜底）。运行时唯一显示源 = disp tile。
- 验收 `./scripts/regress.sh`：Linux 主机 **87 通过 / 0 失败**。Windows/MSYS2 副机 products 仅 beijing 1 省 ⇒ 多省/places_auto.db/rev.bin/lm.bin 缺失，相关用例 FAIL、**非代码回归**。编译零警告（-Werror）。改渲染补 `python3 scripts/bmpdiff.py 旧 新`。
- `skedaddle --ui-test`(33 项) 已进 regress。完成即 commit + push origin main。**前端只有 skedaddle**（SDL 已彻底移除，原 navmap/map_shot 删）。
- ★★ **删文件/删 include 前必须 grep 该符号/该头全部引用**（删 `window_sdl.c` 让 `app_run` 成悬空声明；只 grep `label_` 漏 `NamePool` 差点误删承重依赖）。
- ★★★ **已清理两项纯冗余（均落地，2026-11-02）**：① `osmid.bin`（与 `nodes.bin NF_OSM_ID` 100% 相同，~399 MB/8 省，运行时零引用）于 `b53511f` 停生成+停校验（`graph_build.c` 删 `write_osmid`，`autobuild`/`graph_check`/`sync_maps.sh` 一并去 osmid，验证 `graph_check` 对 beijing 输出 ASSERT-OK）；② 本机 `products/graph/` 的 7 个嵌套 `beijing/` 孤副本（旧版抽取器遗留、md5 异于当前）于 2026-11-02 整棵删除，体积 **2.4 GB → 337 MB**。**回归/重建：Linux 主机 `git pull` 后删 8 省 `osmid.bin`，跑 regress 确认 87/0；真实 7 省数据缺失须 `scripts/sync_maps.sh` 从 `sources/` 重建。**

## 构建环境
- ★ 本机 Linux(UOS aarch64/Wayland) Release：`source env.sh && cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/along/.local/qt5-dev/usr -DQt5_DIR=/home/along/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5 && cmake --build build_rel -j$(nproc)`。`scripts/build_release.sh` 是 Windows 专用，本机跑不了。protoc-c 缺 lib 时先 `export LD_LIBRARY_PATH=/home/along/.local/usr/lib/aarch64-linux-gnu`。
- 无头自检：`export QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/aarch64-linux-gnu/qt5/plugins` 后跑 `--shot-offscreen` / `--ui-test`。GUI 启动视图由 `~/.config/skedaddle/settings.ini` 的 `view_lat/view_lon` 决定；**交互 GUI 不认 `--lat/--lon`（仅离屏认）**。
- ★★★ Windows/MSYS2：**编译期 PATH 绝不能放 Qt bin**（cc1plus 加载旧 libstdc++ ⇒ 退出码1 stderr 空，C 过 C++ 灭）。Qt bin 仅运行期加。运行期 `PATH` 含 mingw64/ucrt64 bin + `QT_QPA_PLATFORM=offscreen` + plugin 路径（libprotobuf-c-1.dll 在 ucrt64/bin）。
- 平台差异只许在 `src/utils/plat.{h,c}`，业务代码零 `#ifdef _WIN32`。

## 字体（2026-11-03 新增）
- 候选链 `sk_font_candidates()`(plat.c)：先内置 `fonts/` 下 Noto 系列 + **DroidSansFallback.ttf(CJK 兜底)**，后系统字体（Linux `/usr/share/fonts` GB/Noto；Windows `%WINDIR%/Fonts/simsun.ttc` 等；macOS 宋体/PingFang）。`get_font_dir` 优先 env `SKEDADDLE_FONT_DIR`，否则 exe 旁 `fonts/` 或 `../fonts/`。
- ★ **`paint_frame:1285 if(a->font)` 是文字门控**：`font_open_default()` 返回 NULL 时所有文字被静默丢弃、几何照常 ⇒ 表现为"有图无线无字"。旧代码只打"提示"级，极难排查。
- 修复：App 加 `font_missing`；字体彻底失败时置位 + 打**响亮 stderr 告警**（列已尝试来源 + 排查指引）；离屏诊断打印 `font <路径> (已加载|★加载失败)`。若某机器只画几何无文字，先查 stderr 这行 / 跑 `skedaddle_font_probe` 看"载入 N 个面"。

## Qt 前端（2026-10-25 实证）
- ★★ checkable 按钮 `setChecked()` 会发 `toggled` ⇒ 递归 SIGSEGV，同步勾选态用 `QSignalBlocker`。
- ★ `isVisible()` 对未 show() 的子控件恒 false ⇒ 浮层贴位用 `!isHidden()`。**不要顶栏**（QToolBar 切高移画布），按钮一律画布子浮层。
- ★★ `setWindowOpacity()` 仅顶层有效；子控件淡用 `QGraphicsOpacityEffect` 会把文字图标一起淡掉 ⇒ 弃用。浮层语义=只淡底板、前景清晰（IconButton 自绘 alpha + QSS rgba + viewport 透明）。
- ★ `qobject_cast` 要求 `Q_OBJECT` ⇒ 自绘子类用 `dynamic_cast`。改 `.hpp` 后 ninja 不自动重编 ⇒ 必须 `touch` 对应 `.cpp`。新增 C++ 用 C 数学须 `#include <cmath>` + `std::floor`。图标符号不能用字体字符 ⇒ `qt_icons.cpp` 自绘。

## 渲染边界（SDL 已彻底移除 #88）
- 交互前端唯一 `skedaddle`；离屏出图唯一 `src/ui/offscreen_shot.c`（零 SDL，malloc 缓冲 + 内建 BMP writer）。SDL 仅存于 `skedaddle_blend_equiv`/`clip_equiv` 门禁对照体。
- ★★ 审计计数源走 C 核心地图缓冲（`offscreen_shot_main` 设 `a.qt_ui=0` ⇒ chrome/横幅/搜索框全画进缓冲）。**不可删 `chrome.c`/`paint.c`/`navdraw.c`**。共享审计 = `src/render/audit.{c,h}`。
- `--shot` 截图路径须显式刷新状态栏（主循环不启动 ⇒ 状态栏空 ⇒ 截图在撒谎）。销毁 renderer 前须 `font_drop_textures()`；stb_truetype 后端只清 CPU 字形缓存，**不能每帧调**（毁 CJK 缓存）。

## 判据纪律（最高价值）
- 布尔判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。
- ★★★ 手挑样本下结论必翻车；**负向对照必须能失败**。
- ★ 判"成功"看**退出码**不是输出行数（打完 OK 就 return、栈上对象被 worker 命中 ⇒ rc=139 却显"通过"）。
- ★★★ **「绘制数变了」必须先切加载路径、再谈数据**：同数据同视口三条显示路径 areas_drawn 各不相同（整省 nmbin < PBF 解码 < disp tile 跨边界重复 +2.3%）。判断顺序：①同数据切路径 ②同路径重抽对拍 ③才疑数据。
- ★★ 浮层半透明后精确配色计数失效 ⇒ 数混合后颜色（有效 alpha 乘积 + 容差）。判"前景没被淡"不能比值≈1（边角像素凑假前景）⇒ 必须逐像素相同（文字用口径 b：相对底图偏离量 + `--fgrect` 框准）。
- ★★★ 会写回配置的实验参数（`--ovl-alpha` 等）跑实验必须隔离配置（`XDG_CONFIG_HOME=/tmp/xxx` + `APPDATA=/tmp/xxx/appdata`），否则污染真实 `settings.ini` ⇒ 后续断言默认配置用例假红。
- ★★ UI 改版会让像素判据硬编码坐标失效 ⇒ 坐标是"每次改版重测"的活资产。

## 语音播报（2026-10-25）
- 分层：`voice.c` 决定"说什么"，`plat.c:sk_tts_speak()` 发声；**接线唯一** = `voice_wire.c:nav_voice_frame()`（生产与 `voice_e2e.c` 共用）。`say()` 是所有播报唯一出口（15s 同文本冷却）。顺序耦合：到达帧先播报后置 `sim_paused=1`，收尾语单独判不受 paused 挡。判真实发声：`SKEDADDLE_TTS_DRYRUN=<文件>` 同步写文本。

## 设置项与 C/C++
- ★★★ `app_init` **逐字段读设置**（不走 `app_apply_settings`）⇒ 新设置项三处都改：`SETTINGS_SCHEMA` + `app_init` + `app_persist_settings`。C++ include 的 C 头须 `extern "C"`；注释别写 `src/x/*.cpp`（`/*` 触发 -Wcomment ⇒ -Werror FAIL）。

## 坐标系（`src/utils/datum.c`）
- CGCS2000 平移 0；北京1954=EPSG:4214 默认 TOWGS84；西安1980=全国 3 参数(−104.57,−45.94,+5.30)。"EPSG 没登记"≠"没参数"；两条独立证据互印才落值；分区参数已否决（选错代价 > 全国最大 14.7m）。

## 分层与模块边界（2026-10-28 立规）
- 终态依赖图纯 DAG 零环：`utils ← pbf ← map ← route ← render ← ui ← ui_qt`（search/settings 横切）。
- ★★ **`-Wl,--start-group` 已从 CMake 移除**，新反向依赖立刻 undefined-reference（链接器是第二道防线）。
- ★★ 改依赖方向前**查符号归属别只 grep 函数前缀**（`mapset.h` 那行 `render/labels.h` 承重 `NamePool`）。
- ★ `./scripts/check_layers.sh` = 分层门禁（解析**真实** include，须剥块注释/行注释，否则注释里的 include 文本致大量假 FAIL）；跨模块改动前先跑。
- 重构验收：零警告 + 依赖门禁 + 出图逐字节一致(`bmpdiff.py`) + `--ui-test` 33 项 + regress 全量(87/0)。

## 别再试（ verdicts only）
- 渲染：简化几何/去描边/面 tile 索引/顶点内联/软件预取/合并折线/NMBIN v3 全否；贵在间接寻址非 stride。
- ★ 瓦片缓存/条带复用全量回退(2026-10-27)：硬障碍 `mv_project` 逐要素 lround + `sk_mercator_y_fast` 与 `inv` 不互逆 ⇒ 复用区天然差 1px、来回平移累积误差。
- 算路：rev.bin v2 / PQ 叉数≠4 / 状态表改布局 全败；`RouteOpts` 一律指名初始化。
- 地标：>30 无收益；w 随地标紧度下走(10→1.3、30→1.25)，改地标数须连带重定 w。
- 一次 `nmbin_open` 全部 disp tile 耗尽 Windows 映射句柄(~168) ⇒ 逐 tile 开→拼→关。
- 浮层透明度：`QGraphicsOpacityEffect` 与"整块一起淡"已否；Qt 唯一解=IconButton 自绘 + QSS rgba + viewport 透明。
- 新增 `Buf` 须同步设 `.esz`（memset 清零 ⇒ 重 PBF 无声崩溃）。`sqlite3_open` 不建父目录 ⇒ 开库前 `sk_mkdir_p`。本机 `rm` 是 safe-delete 改名 ⇒ 真删 `/bin/rm`。

## 已达成水平（参考口径，勿跨口径相减）
- 渲染 1280×800：z9 113 / z11 51 / z13 35 / z15 16 ms（`--shot-offscreen`，8 省北京中心，4 轮最小）。旧 SDL `map_shot` 口径(116/56/42/31) 不可相减。
- 算路 50 条(30 地标 w=1.25)：中位 123 / p90 753 / 最慢 1503 ms，展开合计 10.43M（汇总口径）；比无表快 1.6×、p90 4.2×、质量 −3.21%。

## Qt GUI / Wayland（2026-11-03）
- ★★★ 地图画布缓冲 alpha 字节必须置 0xFF：raster_rgb 只写 R/G/B，缓冲恒 0x00RRGGBB；Deepin Wayland 按 ARGB8888 上传 ⇒ alpha=0 全透明"一片花"（xcb 正常，勿被误导成渲染 bug）。修在 map_canvas renderNow（渲染后 OR 0xFF000000）。`raster_bind_mem` 的 amask 无写入点，传掩码没用。
- ★ 无 Xvfb/python-dbus 也能截 Wayland：`gdbus call --session --dest org.kde.KWin --object-path /Screenshot --method org.kde.kwin.Screenshot.screenshotFullscreen false` 返回图片路径；xcb 对照用 `ffmpeg -f x11grab -i :0.0`。build_rel 实链系统 Qt5（RUNPATH 指向的 qt5-dev 无 platforms/），插件取系统目录。
