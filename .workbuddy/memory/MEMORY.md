# skedaddle 项目长期约定

> 只留结论与"别再试"；推导看 PLAN.md、PROGRESS.md、`.workbuddy/memory/*.md`。

## 固定动作
- `sources/` 只放原始 `*.osm.pbf`；`products/` 全可删、`./scripts/sync_maps.sh` 重建。
- ★★★ `skedaddle_graph_extract --out=` 必须与脚本 `OUT_ROOT`(=`products/graph`) 逐字一致；传错留脚本抓不到的孤副本（套两层/市名当省名），一份 pbf 抽多遍。删重复前先 `md5sum` + 确认无脚本依赖。
- 产物终态 `products/graph/<省>/` = `disp.idx + lanes.bin + t_*`；整省显示三件套（areas/lines/polyrefs.bin）自 11-03 起不再生成，tile 化失败致命、无整省兜底，运行时唯一显示源 = disp tile。
- 验收 `./scripts/regress.sh`：Linux 主机 **87/0**；Windows/MSYS2 副机 products 仅 beijing ⇒ 多省/places_auto.db/rev.bin/lm.bin 用例 FAIL，**非回归**。编译零警告（-Werror）；改渲染补 `python3 scripts/bmpdiff.py 旧 新`。
- `skedaddle --ui-test` 已进 regress，现 **121 OK / 0 FAIL**（须隔离 ini）；完成即 commit + push origin main。前端只有 `skedaddle`（SDL 已移除）。
- ★★ 删文件/删 include 前必须 grep 该符号/该头全部引用（删 `window_sdl.c` 让 `app_run` 悬空；只 grep `label_` 漏 `NamePool`）。
- ★ `osmid.bin` 已停生成+停校验（与 `nodes.bin` 100% 同、~399MB/8省、零引用）；本机嵌套孤副本已清。重建：`git pull` 后删各省 `osmid.bin` 跑 regress 87/0。

## 构建环境
- ★ Linux(UOS aarch64/Wayland) Release：`source env.sh && cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/along/.local/qt5-dev/usr -DQt5_DIR=/home/along/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5 && cmake --build build_rel -j$(nproc)`；protoc-c 缺库先 `export LD_LIBRARY_PATH=/home/along/.local/usr/lib/aarch64-linux-gnu`。`build_release.sh` 是 Windows 专用。
- 无头自检 `QT_QPA_PLATFORM=offscreen` + plugin 路径；GUI 启动视图由 `~/.config/skedaddle/settings.ini` 的 `view_*` 决定，**交互 GUI 不认 `--lat/--lon`（仅离屏认）**。
- ★★★ Windows/MSYS2 **编译期 PATH 绝不能放 Qt bin**（cc1plus 加载旧 libstdc++ ⇒ 退出码1、stderr 空，C 过 C++ 灭）；Qt bin 仅运行期加，且须 offscreen + plugin 路径（`libprotobuf-c-1.dll` 在 ucrt64/bin）。
- ★★ ninja 走 `build_launcher.bat`/`cc_retry.sh` 时**空 stderr + 重试 8 次全败不等于 Defender**：也可能是真编译错误被包装器吞掉。判据 = C 全过、单个 C++ 灭 ⇒ 绕过包装器直接 `c++ -fsyntax-only` 看真 stderr（11-06 Qt6 `QDesktopWidget` 就这么定位）。
- ★★ Windows 发布包重建：`scripts/build_release.sh` 自带 `rm -rf build_rel/dist`，本机 safe-delete 对 >50 文件会拒 ⇒ 改 `cmake --build build_rel --clean-first` 全量重编 + 旧 `dist/skedaddle-release` 改名留底 + `build_release.sh --no-rebuild` 部署。产物 77 文件/102MB，exe 同目录带 fonts/lang/platforms。
- 平台差异只许在 `src/utils/plat.{h,c}`；业务代码零 `#ifdef _WIN32`。Qt5/Qt6 差异用 `QT_VERSION` 条件编译（`QDesktopWidget` Qt6 已移除 ⇒ 用 `QScreen`/`QGuiApplication::primaryScreen()`，见 `1d99c92`）。

## 字体 / Qt GUI
- 候选链 `sk_font_candidates()`(plat.c)：内置 `fonts/` Noto + **DroidSansFallback.ttf(CJK 兜底)**，后系统字体；`get_font_dir` 优先 env `SKEDADDLE_FONT_DIR`。`notocjk.otf` 是 OTTO/CFF，stbtt 读不了。
- ★ `paint_frame` 的 `if(a->font)` 是**文字门控**：`font_open_default()` 返 NULL ⇒ 文字全静默丢弃、几何照常。失败须置 `font_missing` + 响亮 stderr 告警；诊断 `font <路径>(已加载|★加载失败)` / `skedaddle_font_probe`。
- ★★★ 地图画布缓冲 alpha 必须置 0xFF（`raster_rgb` 只写 RGB）：Deepin Wayland 按 ARGB8888 上传 ⇒ alpha=0 全透明"一片花"（xcb 正常）。修在 `map_canvas renderNow` 渲染后 OR 0xFF000000；Android JNI 每帧同理。
- 截屏：Wayland 用 KWin `gdbus ...Screenshot.screenshotFullscreen false`，xcb 用 `ffmpeg -f x11grab -i :0.0`，Windows 真实 GUI 用 `ffmpeg -f gdigrab -i title=Skedaddle`。

## Qt 前端
- ★★ checkable 按钮 `setChecked()` 会发 `toggled` ⇒ 递归 SIGSEGV，同步勾选态用 `QSignalBlocker`。`isVisible()` 对未 show() 子控件恒 false ⇒ 用 `!isHidden()`。
- ★ **不要顶栏**（QToolBar 切高移画布），按钮一律画布子浮层；`setWindowOpacity()` 仅顶层，`QGraphicsOpacityEffect` 会连文字图标一起淡 ⇒ 弃用（浮层语义 = 只淡底板、前景清晰）。
- ★ 自绘子类用 `dynamic_cast`（`qobject_cast` 要 `Q_OBJECT`）；改 `.hpp` / `cp -p` 恢复后必须 `touch` 对应 `.cpp` 再编（mtime 倒退 ninja 跳过）。C++ 用 C 数学须 `<cmath>` + `std::floor`；图标自绘在 `qt_icons.cpp`。
- ★ 无边框窗口（11-06 终态）：构造里 `setWindowFlags(...|Qt::FramelessWindowHint)`（**必须在 show() 前**），`SKEDADDLE_FRAMELESS=0` 回退；三个自绘窗口按钮已删，退出走 Ctrl+Q 或 Alt+F4；只留 `buildWindowDragStrip()` 顶部 10px 全宽透明拖动带（零可见像素、不压右栏）。硬判据 `xprop _NET_FRAME_EXTENTS`：默认 `not found` / 回退 `0,0,40,0`。
- ★★ `showMaximized()` 在 UOS/Deepin 对 frameless ⇒ HIDDEN 且几何不变 ⇒ 自记 `geometry()` → `setGeometry(可用区)` → 还原的状态机；可用区 Qt5 走 `QApplication::desktop()`、Qt6 走 `screen()/primaryScreen()`。Wayland 下 `QWidget::move()` 对顶层无效，拖动只在 X11/XWayland 生效。
- ★ `regress.sh:qt_ovl_alpha` 硬编码矩形是"每次 UI 改版重测"的活资产：控制条时期右栏下移 34px ⇒ 高 430→470；按钮删除后贴顶 ⇒ `--rect=740,0,60,430`（故意用 400 会漏 852px ⇒ FAIL）。顶部拖动带零可见像素、不跟随 `alpha_rcol`。
- ★★ 无边框拖动 eventFilter：MouseButtonPress **不能吞**（吞了隐式 grab 不成立、收不到 MouseMove），只吞 dragging 中的 move。xdotool 合成点击必须分步 + 每步 sleep，链式会被引擎合并 ⇒ 假"按钮无效"。

## 渲染边界（SDL 已彻底移除）
- 交互前端唯一 `skedaddle`；离屏唯一 `src/ui/offscreen_shot.c`（零 SDL，malloc 缓冲 + 内建 BMP writer）；SDL 仅存于 `blend_equiv`/`clip_equiv` 门禁对照体。
- ★★ 审计计数源走 C 核心缓冲（`offscreen_shot_main` 设 `a.qt_ui=0`）⇒ **不可删 `chrome.c`/`paint.c`/`navdraw.c`**；共享审计 = `src/render/audit.{c,h}`。
- ★ `--shot=<png>` 不调用 `win.start()` ⇒ PNG 只有 chrome 空白底（**不是渲染 bug**）；验证地图内容用 `--shot-offscreen`。状态栏由 paintEvent 自动绘制；销毁 renderer 前 `font_drop_textures()`，**不能每帧调**（毁 CJK 缓存）。
- ★★ 放大后地名消失（11-05 修）：`bbox_moved_far` 对纯缩放不敏感、z≥14 缓存不重查 ⇒ 加 bbox 跨度比（<0.8 或 >1.25 重查）+ 路名门槛 15→14。**只在同进程连续缩放复现**，别用离屏单帧否认；z17/18 稀疏属数据密度上限。
- ★★ 中低 zoom「没有区域名」不是缓存/数据 bug，是 kind<=2 市/区名与 POI 同为 14px 小字无层级 ⇒ `labels.c` 缓存 `font_open_default(font_size*2)` 画大字 + 白描边（★ 别复用 `a->font_big`：无 CJK 回退 ⇒ 方框），kind==3 加描边、冲突退回小字；路名门槛 14→12。**大字分级只在 z<14**；65bad95 起白描边/锐化**全比例尺生效** ⇒ 「z14/z15 像素 0.000%」作废，负向对照改 `SKEDADDLE_LABEL_HALO=0` + `SKEDADDLE_FONT_SHARP=1.0`（11-06 起再加 `SKEDADDLE_POI_R=6` 三关，实测 0.000%）。无 area/boundary label 链路，按行政区面绘制需重抽数据。

## 判据纪律（最高价值）
- 布尔判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。
- ★★ 修前二进制别拷出 `build_rel/`：字体候选按 exe 相对 `../fonts` 解析，拷到 /tmp 会让基线字体集合/格宽变化（标注数 125 vs 138），像素对拍失真。
- ★★★ 手挑样本下结论必翻车；**负向对照必须能失败**。判"成功"看**退出码**不是输出行数（rc=139 却显"通过"）。
- ★★★ 「绘制数变了」先切加载路径、再谈数据（整省 nmbin < PBF 解码 < disp tile 跨边界重复 +2.3%）：①同数据切路径 ②同路径重抽对拍 ③才疑数据。
- ★★ 浮层半透明后精确配色计数失效 ⇒ 数混合后颜色（有效 alpha 乘积 + 容差）；判"前景没被淡"须逐像素相同（相对底图偏离量 + `--fgrect`）。
- ★★★ 实验参数（`--ovl-alpha` 等）与 `--ui-test` 必须隔离配置（`XDG_CONFIG_HOME`+`APPDATA` / `SKEDADDLE_SETTINGS` 指全新 ini），否则污染真实 `settings.ini` ⇒ 断言默认值假红；**完整 `regress.sh` 也会把 `view_*` 写成默认 30/110/z10** ⇒ 跑前 `cp` 备份、跑完 diff 恢复（11-05/11-06 两次复现）。
- ★★ UI 改版让像素判据硬编码坐标失效 ⇒ 坐标是"每次改版重测"的活资产。
- ★★★ 教程/文档行号审计：并行多引用表格工具必误判归属 ⇒ 工具报的"漂移"必须回源逐条按真实函数 span 核验，不能盲改；`978-~1179` 近似引用跳过；历史补记「旧→新」别写成 `file:N`。
- ★★ 讲义逐字块由 `scripts/regen_tutorial_blocks.py` 源码生成（`16_qt.md` 的 `main_window.h` + 4 个 cpp 块 / `11_gates.md` 内嵌 `regress.sh`）；改 `main_window.*`/`regress.sh` 后跑，`--check` 过期 rc=1。Python 读写 `newline=`。`check_doc_numbers.sh` 是 644 ⇒ regress 中该用例本机被跳过，需手动跑。
- ★★★ 教程重编号两段式（11-06）：`docs/teaching/NN_*` 与 `tutorial/NN_*` 同号对应，**核心 00–14 / 跨平台 15–17**（旧 02_platform→15、12_qt→16、17_android 不变、03–11 减 1、13_gates→11、14_capstone→12、15_datum→13、16_i18n→14）。别只改文件名：「第 N 课」/`L` 简写/`NN_slug.md`/`tutorial/NN` 4 类引用一次性回调替换（串行 replace 二次污染），范围/并列叙述人工重写。`check_tutorial_tracks.py` 守分类标签 + 两表 0..17 递增；三条讲义门禁未接 regress。

## 语音播报
- `voice.c` 决定"说什么"、`plat.c:sk_tts_speak()` 发声；**接线唯一** `voice_wire.c:nav_voice_frame()`（生产与 `voice_e2e.c` 共用）。`say()` 是唯一出口（15s 同文本冷却）。顺序：到达帧先播报后置 `sim_paused=1`，收尾语不受 paused 挡。判真实发声 `SKEDADDLE_TTS_DRYRUN=<文件>`。

## 设置项与 C/C++
- ★★★ `app_init` 逐字段读设置（不走 `app_apply_settings`）⇒ 新设置项三处都改：`SETTINGS_SCHEMA` + `app_init` + `app_persist_settings`。C++ include 的 C 头须 `extern "C"`；注释别写 `src/x/*.cpp`（`/*` 触发 -Wcomment ⇒ -Werror FAIL）。

## 坐标系（`src/utils/datum.c`）
- CGCS2000 平移 0；北京1954=EPSG:4214 默认 TOWGS84；西安1980=全国 3 参数(−104.57,−45.94,+5.30)。"EPSG 没登记"≠"没参数"；两条独立证据互印才落值；分区参数已否决（选错代价 > 全国最大 14.7m）。

## 教程三轨（11-06）
- ★ 三轨并存：`docs/teaching/NN_*.md`（最终 `src/` 导读）、`tutorial/NN_*/`（各自独立小程序）、`tutorial/mini/`（**同一份工程 18 快照 + 17 相邻 diff**，真渐进式）；前两轨都不是「同一代码逐步长大」。
- `tutorial/mini/_parts/` 是唯一人写源（`main.c.tpl` + 18 个 header-only `stepNN.h` + `chain.json`）；`regen_mini_chain.py` 生成 `steps/`+`diffs/`（224 文件，`--check` 过期 rc=1）；`check_mini_chain.py` 守旧文件零删改 + 18 步 `-Werror` 编译运行 + gates/ink 确定性 + 5 类负向对照，**未接 regress.sh**。末课 `tutorial/mini/MAP.md` 是 mini ↔ src 模块 18 行对照，门禁校验其中路径真实存在。
- ★ C 注释里 `NN_*/` 会提前闭合块注释、`src/x/*.c` 的 `/*` 炸 -Werror；`steps/stepNN_slug/` 回 `docs/` 是 4 层 `../../../../`。负向对照测 RUN 要改「被检查的数据」，别把条件改常量 0（变量未使用先被 BUILD 拦）。

## 分层与模块边界
- 终态依赖图纯 DAG 零环：`utils ← pbf ← map ← route ← render ← ui ← ui_qt`（search/settings 横切）。★★ `-Wl,--start-group` 已移除 ⇒ 新反向依赖立刻 undefined-reference；改依赖方向前查符号归属别只 grep 函数前缀（`mapset.h` 那行 `render/labels.h` 承重 `NamePool`）。
- ★ `./scripts/check_layers.sh` 解析真实 include（须剥块/行注释，否则假 FAIL）。重构验收：零警告 + 门禁 + 出图逐字节一致 + `--ui-test` + regress 87/0。

## 别再试（verdicts only）
- 渲染：简化几何/去描边/面 tile 索引/顶点内联/软件预取/合并折线/NMBIN v3 全否；贵在间接寻址非 stride。
- ★ 瓦片缓存/条带复用全量回退：`mv_project` 逐要素 lround + `sk_mercator_y_fast` 与 `inv` 不互逆 ⇒ 复用区差 1px、平移累积误差。
- 算路：rev.bin v2 / PQ 叉数≠4 / 状态表改布局 全败；`RouteOpts` 一律指名初始化。地标：>30 无收益；w 随紧度下走(10→1.3、30→1.25)，改地标数须连带重定 w。
- 一次 `nmbin_open` 全部 disp tile 耗尽 Windows 映射句柄(~168) ⇒ 逐 tile 开→拼→关。浮层透明度：`QGraphicsOpacityEffect` 与"整块一起淡"已否。
- 新增 `Buf` 须同步设 `.esz`（memset 清零 ⇒ 重 PBF 无声崩溃）。`sqlite3_open` 不建父目录 ⇒ 开库前 `sk_mkdir_p`。本机 `rm` 是 safe-delete 改名 ⇒ 真删 `/bin/rm`。
- ★ `core.autocrlf=true`：Python 文本模式写文件会转 CRLF 造成假 `M` ⇒ 读写都用 `newline=''` 或 rb/wb。

## 已达成水平（参考口径，勿跨口径相减）
- 渲染 1280×800：z9 113 / z11 51 / z13 35 / z15 16 ms（8 省北京中心，4 轮最小）。算路 50 条(30 地标 w=1.25)：中位 123 / p90 753 / 最慢 1503 ms，展开 10.43M；比无表快 1.6×、p90 4.2×、质量 −3.21%。

## Android 原生 JNI App（11-04，见 docs/android.md）
- 方向：原生 JNI App，不动 `ui_qt`；C 核心零改动，平台差异全在 `plat.c` 的 `__ANDROID__` 分支（`sk_android_set_paths/font_dir` 注入 files/cache）。工程 `android/`（Gradle + NDK CMake 复用 `src/` 编 `libskedaddle.so`），JNI 包名 `com.skedaddle.android.Skedaddle`。
- **JNI 桥本机校验**：无 NDK 时用复刻 NDK 的 `jni.h` 垫片（C++ `JNIEnv_` + inline 转发）+ 真实 `src/` 头，`g++ -std=c++17 -D__ANDROID__ -Wall -Wextra -fsyntax-only` 零告警；**别用 JDK 头当 NDK 替身**（校验不了 `env->Method()`）。
- **第三方必须 vendor** `android/app/src/main/cpp/thirdparty/{sqlite3,protobuf-c}`（NDK 不自带）；JNI 每帧渲染后 `px[i]|=0xFF000000`（同 Wayland 教训）；部署图集 `adb push products/graph /sdcard/Android/data/com.skedaddle.android/files/`。
- 限制：无真实 GPS/NMEA、TTS 仅 DRYRUN、minSdk 26、两台机器可出 APK 但**均未真机验证**。已落地增量更新+OTA（`Prefs`/`Downloader`/`Manifests`/`UpdateChecker`/`DownloadActivity`/`SettingsActivity` + FileProvider）；应用内装完省图返回会自动 retryInit，进程内热换省仍未做。
