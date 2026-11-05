# skedaddle 项目长期约定

> 只留结论与"别再试"；推导看 PLAN.md、PROGRESS.md、`.workbuddy/memory/*.md`。

## 固定动作
- `sources/` 只放原始 `*.osm.pbf`；`products/` 全可删、`./scripts/sync_maps.sh` 重建。
- ★★★ `skedaddle_graph_extract --out=` 必须与脚本 `OUT_ROOT`(=`products/graph`) 逐字一致；传错留脚本抓不到的孤副本（套两层/市名当省名），一份 pbf 被抽多遍。删重复前先 `md5sum` 确认字节相同 + 无脚本依赖。
- 产物终态：`products/graph/<省>/` 带 `disp.idx + lanes.bin + t_*`。**整省显示三件套（areas/lines/polyrefs.bin）自 2026-11-03 起不再生成**（`build_display` 只构 polyrefs + `write_disp_tiles`；tile 化失败致命、无整省兜底）。运行时唯一显示源 = disp tile。
- 验收 `./scripts/regress.sh`：Linux 主机 **87 通过 / 0 失败**；Windows/MSYS2 副机 products 仅 beijing ⇒ 多省/places_auto.db/rev.bin/lm.bin 缺失用例 FAIL，**非回归**。编译零警告（-Werror）。改渲染补 `python3 scripts/bmpdiff.py 旧 新`。
- `skedaddle --ui-test`(33 项) 已进 regress。完成即 commit + push origin main。**前端只有 skedaddle**（SDL 已移除）。
- ★★ 删文件/删 include 前必须 grep 该符号/该头全部引用（删 `window_sdl.c` 让 `app_run` 成悬空声明；只 grep `label_` 漏 `NamePool`）。
- ★ `osmid.bin` 已停生成+停校验（与 `nodes.bin` 100% 同、~399MB/8省、零引用）；本机 `products/graph/` 嵌套孤副本已清（2.4GB→337MB）。重建：`git pull` 后删各省 `osmid.bin` 跑 regress 87/0。

## 构建环境
- ★ 本机 Linux(UOS aarch64/Wayland) Release：`source env.sh && cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/along/.local/qt5-dev/usr -DQt5_DIR=/home/along/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5 && cmake --build build_rel -j$(nproc)`。`build_release.sh` 是 Windows 专用。protoc-c 缺库先 `export LD_LIBRARY_PATH=/home/along/.local/usr/lib/aarch64-linux-gnu`。
- 无头自检：`QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/aarch64-linux-gnu/qt5/plugins`。GUI 启动视图由 `~/.config/skedaddle/settings.ini` 的 `view_lat/view_lon` 决定；**交互 GUI 不认 `--lat/--lon`（仅离屏认）**。
- ★★★ Windows/MSYS2：**编译期 PATH 绝不能放 Qt bin**（cc1plus 加载旧 libstdc++ ⇒ 退出码1 stderr 空，C 过 C++ 灭）。Qt bin 仅运行期加；运行期须 Qt bin + `QT_QPA_PLATFORM=offscreen` + plugin 路径（libprotobuf-c-1.dll 在 ucrt64/bin）。
- 平台差异只许在 `src/utils/plat.{h,c}`；业务代码零 `#ifdef _WIN32`。

## 字体 / Qt GUI（2026-11-03）
- 候选链 `sk_font_candidates()`(plat.c)：内置 `fonts/` Noto + **DroidSansFallback.ttf(CJK 兜底)**，后系统字体；`get_font_dir` 优先 env `SKEDADDLE_FONT_DIR`。
- ★ `paint_frame` 的 `if(a->font)` 是**文字门控**：`font_open_default()` 返 NULL ⇒ 文字全静默丢弃、几何照常（"有图无线无字"）。失败须置 `font_missing` + 响亮 stderr 告警（列尝试来源）；诊断看 `font <路径>(已加载|★加载失败)` / `skedaddle_font_probe`。
- ★★★ 地图画布缓冲 alpha 必须置 0xFF：`raster_rgb` 只写 RGB，缓冲恒 `0x00RRGGBB`；Deepin Wayland 按 ARGB8888 上传 ⇒ alpha=0 全透明"一片花"（xcb 正常，勿误判成渲染 bug）。修在 `map_canvas renderNow`（渲染后 OR 0xFF000000）。
- 截 Wayland：`gdbus call --session --dest org.kde.KWin --object-path /Screenshot --method org.kde.kwin.Screenshot.screenshotFullscreen false` 返回图片路径；xcb 对照 `ffmpeg -f x11grab -i :0.0`。

## Qt 前端
- ★★ checkable 按钮 `setChecked()` 会发 `toggled` ⇒ 递归 SIGSEGV，同步勾选态用 `QSignalBlocker`。
- ★ `isVisible()` 对未 show() 子控件恒 false ⇒ 用 `!isHidden()`。**不要顶栏**（QToolBar 切高移画布），按钮一律画布子浮层。
- ★★ `setWindowOpacity()` 仅顶层；子控件淡用 `QGraphicsOpacityEffect` 会连文字图标一起淡 ⇒ 弃用。浮层语义=只淡底板、前景清晰（IconButton 自绘 alpha + QSS rgba + viewport 透明）。
- ★ `qobject_cast` 要求 `Q_OBJECT` ⇒ 自绘子类用 `dynamic_cast`。改 `.hpp` 后 ninja 不自动重编 ⇒ `touch` 对应 `.cpp`。C++ 用 C 数学须 `#include <cmath>` + `std::floor`。图标符号不能用字体字符 ⇒ `qt_icons.cpp` 自绘。

## 渲染边界（SDL 已彻底移除）
- 交互前端唯一 `skedaddle`；离屏唯一 `src/ui/offscreen_shot.c`（零 SDL，malloc 缓冲 + 内建 BMP writer）。SDL 仅存于 `blend_equiv`/`clip_equiv` 门禁对照体。
- ★★ 审计计数源走 C 核心缓冲（`offscreen_shot_main` 设 `a.qt_ui=0`）⇒ **不可删 `chrome.c`/`paint.c`/`navdraw.c`**。共享审计 = `src/render/audit.{c,h}`。
- `--shot` 须显式刷新状态栏（主循环不启动 ⇒ 截图撒谎）。销毁 renderer 前 `font_drop_textures()`，**不能每帧调**（毁 CJK 缓存）。

## 判据纪律（最高价值）
- 布尔判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。
- ★★★ 手挑样本下结论必翻车；**负向对照必须能失败**。
- ★ 判"成功"看**退出码**不是输出行数（打完 OK 就 return、栈上对象被 worker 命中 ⇒ rc=139 却显"通过"）。
- ★★★ 「绘制数变了」先切加载路径、再谈数据（整省 nmbin < PBF 解码 < disp tile 跨边界重复 +2.3%）。顺序：①同数据切路径 ②同路径重抽对拍 ③才疑数据。
- ★★ 浮层半透明后精确配色计数失效 ⇒ 数混合后颜色（有效 alpha 乘积 + 容差）；判"前景没被淡"须逐像素相同（文字用口径 b：相对底图偏离量 + `--fgrect` 框准）。
- ★★★ 会写回配置的实验参数（`--ovl-alpha` 等）跑实验必须隔离配置（`XDG_CONFIG_HOME`+`APPDATA`），否则污染 `settings.ini` ⇒ 断言默认值用例假红。
- ★★ UI 改版让像素判据硬编码坐标失效 ⇒ 坐标是"每次改版重测"的活资产。
- ★★★ **教程/文档行号审计：并行多引用表格（一行多符号+多行号）工具必误判归属** ⇒ 工具报的"漂移"必须回源逐条核验（拿真实函数 span 对照），不能盲改（踩过：自动 fixer 把 `bbox_overlap` 行号改成了 `src_unload` 的）。同文件 `978-~1179` 这类带 `~` 近似引用要跳过。

## 语音播报
- 分层：`voice.c` 决定"说什么"，`plat.c:sk_tts_speak()` 发声；**接线唯一** `voice_wire.c:nav_voice_frame()`（生产与 `voice_e2e.c` 共用）。`say()` 是所有播报唯一出口（15s 同文本冷却）。顺序：到达帧先播报后置 `sim_paused=1`，收尾语单独判不受 paused 挡。判真实发声：`SKEDADDLE_TTS_DRYRUN=<文件>`。

## 设置项与 C/C++
- ★★★ `app_init` 逐字段读设置（不走 `app_apply_settings`）⇒ 新设置项三处都改：`SETTINGS_SCHEMA` + `app_init` + `app_persist_settings`。C++ include 的 C 头须 `extern "C"`；注释别写 `src/x/*.cpp`（`/*` 触发 -Wcomment ⇒ -Werror FAIL）。

## 坐标系（`src/utils/datum.c`）
- CGCS2000 平移 0；北京1954=EPSG:4214 默认 TOWGS84；西安1980=全国 3 参数(−104.57,−45.94,+5.30)。"EPSG 没登记"≠"没参数"；两条独立证据互印才落值；分区参数已否决（选错代价 > 全国最大 14.7m）。

## 分层与模块边界
- 终态依赖图纯 DAG 零环：`utils ← pbf ← map ← route ← render ← ui ← ui_qt`（search/settings 横切）。
- ★★ `-Wl,--start-group` 已从 CMake 移除，新反向依赖立刻 undefined-reference（链接器第二道防线）。
- ★★ 改依赖方向前查符号归属别只 grep 函数前缀（`mapset.h` 那行 `render/labels.h` 承重 `NamePool`）。
- ★ `./scripts/check_layers.sh` = 分层门禁（解析真实 include，须剥块/行注释，否则假 FAIL）。重构验收：零警告 + 门禁 + 出图逐字节一致 + `--ui-test` 33 项 + regress 87/0。

## 别再试（verdicts only）
- 渲染：简化几何/去描边/面 tile 索引/顶点内联/软件预取/合并折线/NMBIN v3 全否；贵在间接寻址非 stride。
- ★ 瓦片缓存/条带复用全量回退：硬障碍 `mv_project` 逐要素 lround + `sk_mercator_y_fast` 与 `inv` 不互逆 ⇒ 复用区差 1px、平移累积误差。
- 算路：rev.bin v2 / PQ 叉数≠4 / 状态表改布局 全败；`RouteOpts` 一律指名初始化。
- 地标：>30 无收益；w 随地标紧度下走(10→1.3、30→1.25)，改地标数须连带重定 w。
- 一次 `nmbin_open` 全部 disp tile 耗尽 Windows 映射句柄(~168) ⇒ 逐 tile 开→拼→关。
- 浮层透明度：`QGraphicsOpacityEffect` 与"整块一起淡"已否。
- 新增 `Buf` 须同步设 `.esz`（memset 清零 ⇒ 重 PBF 无声崩溃）。`sqlite3_open` 不建父目录 ⇒ 开库前 `sk_mkdir_p`。本机 `rm` 是 safe-delete 改名 ⇒ 真删 `/bin/rm`。
- ★ `core.autocrlf=true`：Python 文本模式写文件会转 CRLF 造成假 `M`（git diff 空）⇒ 读写都用 `newline=''`。

## 已达成水平（参考口径，勿跨口径相减）
- 渲染 1280×800：z9 113 / z11 51 / z13 35 / z15 16 ms（8 省北京中心，4 轮最小）。
- 算路 50 条(30 地标 w=1.25)：中位 123 / p90 753 / 最慢 1503 ms，展开 10.43M；比无表快 1.6×、p90 4.2×、质量 −3.21%。

## Android 原生 JNI App（2026-11-04 新增，见 docs/android.md）
- 方向：原生 JNI App，不动 `ui_qt`；C 核心零改动，平台差异全在 `plat.c` 的 `__ANDROID__` 分支（`sk_android_set_paths/font_dir` 注入 files/cache 目录）。
- 工程：`android/`（Gradle + NDK CMake 复用 `src/` 核心编 `libskedaddle.so`）；JNI 桥包名 `com.skedaddle.android.Skedaddle`。
- **JNI 桥本机校验法**：无 NDK，但用**忠实复刻 NDK 的 `jni.h` 垫片**（C++ `JNIEnv_` struct + inline 转发 `functions->X(this,...)`）+ 真实 `src/` 头，`g++ -std=c++17 -D__ANDROID__ -Wall -Wextra -fsyntax-only` 零告警 ⇒ 契约正确。注意**真实 JDK `jni.h` 的 C++ 模式 `JNIEnv=JNIEnv_`(struct) 且无 inline 包装**，不足以校验 Android `env->Method()` 写法，别用 JDK 头当 NDK 替身。
- **第三方必须 vendor**：`android/app/src/main/cpp/thirdparty/{sqlite3,protobuf-c}`（NDK 不自带），缺则链接失败；取源命令见 docs §4。
- **复刻 Wayland 教训**：JNI 每帧渲染后 `px[i]|=0xFF000000`（C 光栅不写 alpha，否则 Bitmap 全透明花屏）。
- 限制：未接真实 GPS/NMEA（点选起终点）、TTS 仅 DRYRUN、minSdk 26、本机(ARM64 Linux)与 Windows 副机均可出 APK，均未真机运行验证。
- **已落地增量更新+OTA（aef95e4）**：`Prefs`(配置中枢)/`Downloader`(HTTP+zip)/`Manifests`(清单解析)/`UpdateChecker`(OTA)；`DownloadActivity` 真实化（拉 `map/manifest.json`+扫描 `graph/<省>/.skversion`+逐省下载/本地 zip 安装）；`SettingsActivity` 取代旧显示对话框（服务器/自动更新/检查更新/显示开关/关于）+ FileProvider 安装 APK。清单格式与自托管见 docs §11。**注意：应用内装完省图从下载页返回会自动 retryInit 重载（f08f4ba）；仅 adb 运行中热推/后台改目录需重启，真正的进程内热换省（app_reload_maps 接进 JNI）仍未做**。
- 部署图集：`adb push products/graph /sdcard/Android/data/com.skedaddle.android/files/`。
