# skedaddle 项目长期约定

> 只留结论与"别再试"；推导看 PLAN.md、PROGRESS.md、`.workbuddy/memory/*.md`。

## 固定动作
- `sources/` 只放原始 `*.osm.pbf`；`products/` 全可删、`./scripts/sync_maps.sh` 重建。
- ★★★ `skedaddle_graph_extract --out=` 必须与脚本 `OUT_ROOT`(=`products/graph`) 逐字一致；传错留孤副本（套两层/市名当省名）。删重复前 `md5sum` + 确认无脚本依赖。
- 产物终态 `products/graph/<省>/` = `disp.idx + lanes.bin + t_*`；整省三件套自 11-03 起不再生成，tile 化失败致命、无兜底，唯一显示源 = disp tile。
- 验收 `./scripts/regress.sh`：Linux 主机 87/0；Windows 副机仅 beijing ⇒ 多省/places_auto.db/rev.bin/lm.bin 用例 FAIL，非回归。编译零警告（-Werror）；改渲染补 `bmpdiff.py 旧 新`。
- `skedaddle --ui-test` 已进 regress，现 121 OK / 0 FAIL（须隔离 ini）；完成即 commit + push origin main。前端只有 `skedaddle`（SDL 已移除）。
- ★★ 删文件/删 include 前必须 grep 该符号/该头全部引用（删 `window_sdl.c` 让 `app_run` 悬空；只 grep `label_` 漏 `NamePool`）。
- ★ `osmid.bin` 已停生成+停校验；重建：`git pull` 后删各省 `osmid.bin` 跑 regress 87/0。

## 构建环境
- ★ Linux(UOS aarch64/Wayland) Release：`source env.sh && cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/along/.local/qt5-dev/usr -DQt5_DIR=/home/along/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5 && cmake --build build_rel -j$(nproc)`；protoc-c 缺库先 `export LD_LIBRARY_PATH=/home/along/.local/usr/lib/aarch64-linux-gnu`。`build_release.sh` 是 Windows 专用。
- 无头自检 `QT_QPA_PLATFORM=offscreen` + plugin 路径；GUI 启动视图由 settings.ini 的 `view_*` 决定，交互 GUI 不认 `--lat/--lon`（仅离屏认）。
- ★★★ Windows/MSYS2 编译期 PATH 绝不能放 Qt bin（cc1plus 加载旧 libstdc++ ⇒ 退出码1、stderr 空，C 过 C++ 灭）；Qt bin 仅运行期加，且须 offscreen + plugin 路径（`libprotobuf-c-1.dll` 在 ucrt64/bin）。
- ★★ ninja 走 `build_launcher.bat`/`cc_retry.sh` 时空 stderr + 重试全败 ≠ Defender：可能是真编译错误被吞。判据 = C 全过、单个 C++ 灭 ⇒ 绕过包装器直接 `c++ -fsyntax-only` 看真 stderr。
- ★★ Windows 发布包重建：`build_release.sh` 自带 `rm -rf` 会被本机 safe-delete 拦（>50 文件拒）⇒ `cmake --build build_rel --clean-first` 全量重编 + 旧 dist 改名留底 + `build_release.sh --no-rebuild` 部署。产物 77 文件/102MB。
- 平台差异只许在 `src/utils/plat.{h,c}`；Qt5/Qt6 差异用 `QT_VERSION` 条件编译（`QDesktopWidget` Qt6 已移除 ⇒ `QScreen`/`QGuiApplication::primaryScreen()`，见 `1d99c92`）。

## 字体 / Qt GUI
- 候选链 `sk_font_candidates()`(plat.c)：内置 `fonts/` Noto + DroidSansFallback.ttf(CJK 兜底)，后系统字体；`SKEDADDLE_FONT_DIR` 优先。`notocjk.otf` 是 OTTO/CFF，stbtt 读不了。
- ★ `paint_frame` 的 `if(a->font)` 是文字门控：`font_open_default()` 返 NULL ⇒ 文字全静默丢弃、几何照常。诊断 `font <路径>(已加载|★加载失败)` / `skedaddle_font_probe`。
- ★★★ 地图画布缓冲 alpha 必须置 0xFF（`raster_rgb` 只写 RGB）：Wayland 按 ARGB8888 上传 ⇒ alpha=0 全透明"一片花"。修在 `map_canvas renderNow` 渲染后 OR 0xFF000000；Android JNI 每帧同理。
- 截屏：Wayland 用 KWin `gdbus ...Screenshot.screenshotFullscreen false`，xcb 用 `ffmpeg -f x11grab -i :0.0`，Windows 真实 GUI 用 `ffmpeg -f gdigrab -i title=Skedaddle`。

## Qt 前端
- ★★ checkable 按钮 `setChecked()` 会发 `toggled` ⇒ 递归 SIGSEGV，同步勾选态用 `QSignalBlocker`。`isVisible()` 对未 show() 子控件恒 false ⇒ 用 `!isHidden()`。
- ★ 不要顶栏（QToolBar 切高移画布），按钮一律画布子浮层；`QGraphicsOpacityEffect` 连文字一起淡 ⇒ 弃用。
- ★ 自绘子类用 `dynamic_cast`（`qobject_cast` 要 `Q_OBJECT`）；改 `.hpp` / `cp -p` 恢复后必须 `touch` 对应 `.cpp`。C++ 用 C 数学须 `<cmath>` + `std::floor`；图标自绘在 `qt_icons.cpp`。
- ★ 无边框窗口（11-06 终态）：构造里 `setWindowFlags(...|Qt::FramelessWindowHint)` 必须在 show() 前，`SKEDADDLE_FRAMELESS=0` 回退；三个自绘窗口按钮已删，退出走 Ctrl+Q/Alt+F4；只留顶部 10px 全宽透明拖动带。硬判据 `xprop _NET_FRAME_EXTENTS`。
- ★★ `showMaximized()` 在 UOS/Deepin 对 frameless ⇒ HIDDEN ⇒ 自记 `geometry()` → `setGeometry(可用区)` → 还原状态机；可用区 Qt5 `QApplication::desktop()`、Qt6 `screen()/primaryScreen()`。Wayland 下顶层 `move()` 无效，拖动只在 X11/XWayland 生效。
- ★ `regress.sh:qt_ovl_alpha` 硬编码矩形是活资产：按钮删除后贴顶 ⇒ `--rect=740,0,60,430`（用 400 会漏 852px ⇒ FAIL）。
- ★★ 无边框拖动 eventFilter：MouseButtonPress 不能吞（吞了收不到 MouseMove），只吞 dragging 中的 move。xdotool 合成点击必须分步 + sleep。

## 渲染边界（SDL 已彻底移除）
- 交互前端唯一 `skedaddle`；离屏唯一 `src/ui/offscreen_shot.c`；审计计数源走 C 核心缓冲 ⇒ 不可删 `chrome.c`/`paint.c`/`navdraw.c`；共享审计 `src/render/audit.{c,h}`。
- ★ `--shot=<png>` 不调 `win.start()` ⇒ PNG 只有 chrome 空白底（非 bug）；验证地图内容用 `--shot-offscreen`。销毁 renderer 前 `font_drop_textures()`，不能每帧调（毁 CJK 缓存）。
- ★★ 放大后地名消失（11-05 修）：bbox 跨度比（<0.8 或 >1.25 重查）+ 缓存 zoom 判据；只在同进程连续缩放复现，别用离屏单帧否认。
- ★★ 中低 zoom 区名大字分级只在 z<14（`labels.c` 缓存 `font_open_default(font_size*2)` 大字+白描边；别复用 `a->font_big`：无 CJK 回退 ⇒ 方框）；路名门槛 14→12。白描边/字形锐化全比例尺生效（`halo_enabled()`/`glyph_sharpen()`），负向对照 `SKEDADDLE_LABEL_HALO=0` + `SKEDADDLE_FONT_SHARP=1.0` + `SKEDADDLE_POI_R=6` 三关。无 area/boundary label 链路。
- ★ 路名等级按 zoom 放开（`road_name_cls_max`：z12 主干/z14 次干/z16 支路，11-06，z15 路名 60→268、z16 13→113）；z17 偏稀是视口内要素少，非门槛截断。
- ★ POI 总开关 `show_poi`（11-06）：关 ⇒ kind 11（设施 POI）图标+名称都不画、不占避让格，SQL `AND (?7 OR kind<>11)` 把 Top-N 名额让给行政地名/村名；`LabelCache.cached_poi` 切换即重查；开（默认）出图与旧路径逐字节一致。面板/菜单标签「POI 显示」。

## 判据纪律（最高价值）
- 判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。
- ★★ 修前二进制别拷出 `build_rel/`（字体按 exe 相对 `../fonts` 解析，拷出对拍失真）；基线/新图一律用发布包 exe（`build_rel` 直接跑 rc=127 缺 Qt DLL）。
- ★★★ 手挑样本下结论必翻车；负向对照必须能失败；判"成功"看退出码不是输出行数。
- ★★★ 「绘制数变了」先切加载路径、再谈数据（整省 nmbin < PBF 解码 < disp tile 跨边界重复 +2.3%）：①同数据切路径 ②同路径重抽对拍 ③才疑数据。
- ★★ 浮层半透明后精确配色计数失效 ⇒ 数混合后颜色；判"前景没被淡"须逐像素相同。
- ★ 傍晚后（北京 11 月 ~17:10 日落）用全新 ini 出图/截图：`day_night` 缺省=2 自动 ⇒ 判夜间 ⇒ 全屏夜幕蒙版 RGB(8,14,34)@96，出图整体发灰（米白 244,241,234→177,175,176）；判颜色问题先在 ini 固定 day_night=0。
- ★★★ 实验参数与 `--ui-test` 必须隔离配置（`SKEDADDLE_SETTINGS` 指全新 ini），否则污染真实 settings.ini ⇒ 断言假红；完整 regress 也会把 view_* 写成默认 ⇒ 跑前备份、跑完恢复。
- ★★ UI 改版让像素判据硬编码坐标失效 ⇒ 坐标是活资产。
- ★★★ 教程行号审计：工具报的"漂移"必须回源逐条按真实函数 span 核验，不能盲改；历史补记别写成 `file:N`。
- ★★ 讲义逐字块由 `scripts/regen_tutorial_blocks.py` 生成（16_qt + 11_gates）；改 `main_window.*`/`regress.sh` 后跑，`--check` 过期 rc=1。Python 读写 `newline=''`。`check_doc_numbers.sh` 是 644 ⇒ regress 跳过，需手动跑。
- ★★★ 教程重编号两段式（11-06）：`docs/teaching/NN_*` 与 `tutorial/NN_*` 同号对应，核心 00–14 / 跨平台 15–17；「第 N 课」/`L` 简写/`NN_slug.md`/`tutorial/NN` 4 类引用一次性回调替换；`check_tutorial_tracks.py` 守分类标签+两表递增；三条讲义门禁未接 regress。

## 语音播报
- `voice.c` 决定"说什么"、`plat.c:sk_tts_speak()` 发声；接线唯一 `voice_wire.c:nav_voice_frame()`。`say()` 唯一出口（15s 同文本冷却）。到达帧先播报后置 `sim_paused`。判真实发声 `SKEDADDLE_TTS_DRYRUN=<文件>`。

## 设置项与 C/C++
- ★★★ `app_init` 逐字段读设置（不走 `app_apply_settings`）⇒ 新设置项三处都改：`SETTINGS_SCHEMA` + `app_init` + `app_persist_settings`。C++ include 的 C 头须 `extern "C"`；注释别写 `src/x/*.cpp`（`/*` 触发 -Wcomment ⇒ -Werror）。

## 坐标系（`src/utils/datum.c`）
- CGCS2000 平移 0；北京1954=EPSG:4214 默认 TOWGS84；西安1980=全国 3 参数(−104.57,−45.94,+5.30)。两条独立证据互印才落值；分区参数已否决。

## 教程三轨（11-06）
- ★ 三轨并存：`docs/teaching/NN_*.md`（最终 src 导读）、`tutorial/NN_*/`（各自独立小程序）、`tutorial/mini/`（同一份工程 18 快照 + 17 相邻 diff）；前两轨不是「同一代码逐步长大」。
- ★ `check_tutorial_tracks.py` 用 `tutorial.glob("NN_*")` **排序取第一个**目录 ⇒ 重编号残留的旧编号目录（只装 `out.bmp`/`tut.exe`）会顶掉同号真目录，报「README 不存在」的假 FAIL；删残留即恢复（11-07，清 12 个）。
- `tutorial/mini/_parts/` 唯一人写源；`regen_mini_chain.py` 生成 steps/diffs（224 文件，`--check` 过期 rc=1）；`check_mini_chain.py` 守编译运行 + gates + 5 类负向对照，未接 regress。末课 MAP.md 是 mini ↔ src 18 行对照（有门禁）。
- ★ C 注释里 `NN_*/` 会提前闭合块注释、`src/x/*.c` 的 `/*` 炸 -Werror；`steps/stepNN_slug/` 回 `docs/` 是 4 层 `../../../../`。负向对照测 RUN 要改「被检查的数据」，别把条件改常量 0。

## 分层与模块边界
- 终态 DAG 零环：`utils ← pbf ← map ← route ← render ← ui ← ui_qt`（search/settings 横切）。★★ `-Wl,--start-group` 已移除 ⇒ 反向依赖立刻 undefined-reference；查符号归属别只 grep 函数前缀（`mapset.h` 那行 `render/labels.h` 承重 NamePool）。
- ★ `./scripts/check_layers.sh` 解析真实 include 须剥块/行注释。重构验收：零警告 + 门禁 + 出图逐字节一致 + `--ui-test` + regress 87/0。

## 别再试（verdicts only）
- 渲染：简化几何/去描边/面 tile 索引/顶点内联/软件预取/合并折线/NMBIN v3 全否；贵在间接寻址非 stride。
- ★★★ 换语言（C→Rust）不是渲染性能杠杆：`-O3`/`-march=native` 实测零收益（`CMakeLists.txt:18`，native 已含自动向量化）⇒ 瓶颈在内存访问与每要素固定开销，非算术吞吐；Rust 走同一 LLVM 后端，1:1 移植预期 0%（±5%）。LOD 档位（`AREA_DOT_PX` 2→3 省 15%）仍在；**多线程分带已于 11-10 落地**（见下条）。详见 `2026-11-10.md`。
- ★★★ 分带并行已落地（d8ed879）：`band.{c,h}` + `SKEDADDLE_BANDS`（0=串行基线/N=强制/缺省核数≤8）；面/线/路三阶段出图与串行逐字节一致，阶段和 z9 57→33ms、z11 60→32、z13 29→16（1.7~1.9×）。四陷阱勿再犯：①跨带线段对画布裁剪出串行同端点再逐像素滤带（重跑 Bresenham 漂 1px）②路分桶余量只给 ±3 行，搬视口 margin(64) 会让每边被 2.3 带重画反变慢 ③带早退判据须比分桶宽 4 行，相等则边界边"分进来又退掉"不画不计数 ④路名锚点钳到端带（串行画布外中点也收），按 seq 归并还原串行序。
- ★★★ 标注并行两坑（11-10）：①font_draw 的 y=基线，CJK 字形顶可超出 hhea ascent（28px 实测 22 行 vs metrics 12）⇒ 行包围盒必须用 font_vbox()（max(metrics,0.9×ptsize)），估小=带边界上方缺字；②字形缓存懒插入共享哈希，8 线程 glyph_get 锁热点（21→129ms）⇒ flush 前主线程 font_warm_text 预热、worker 走 font_draw_a_pre 无锁只读。★ cc_retry 包装器会吞真实编译错误（stderr 空+ninja FAILED）⇒ 直接 `cc -fsyntax-only` 看真 stderr，否则 build "成功"跑的是旧 exe。LOD：AREA_DOT_PX 已是设置项 lod_area（2~12 缺省 4），settings 不能 include paint.h（分层）⇒ 缺省字面值 4 双处锚定。
- ★★ 离屏出图必须从仓库根目录跑：POI/地名数据按 cwd 相对路径找，从 dist/ 里跑 `place_labels=0`/`poi_icons=0` 整图差 4.2%（白字全没）⇒ 与历史基线对拍假 DIFF。Windows regress 运行时 PATH：`dist/skedaddle-release` + `ucrt64/bin`（编译期仍禁 Qt bin）。
- ★ 瓦片缓存/条带复用全量回退：`mv_project` 逐要素 lround + `sk_mercator_y_fast` 与 `inv` 不互逆 ⇒ 复用区差 1px。
- 算路：rev.bin v2 / PQ 叉数≠4 / 状态表改布局 全败；`RouteOpts` 一律指名初始化。地标 >30 无收益；改地标数须连带重定 w。
- 一次 `nmbin_open` 全部 disp tile 耗尽 Windows 映射句柄 ⇒ 逐 tile 开→拼→关。浮层透明度：`QGraphicsOpacityEffect` 与"整块一起淡"已否。
- 新增 `Buf` 须同步设 `.esz`；`sqlite3_open` 不建父目录 ⇒ 开库前 `sk_mkdir_p`；本机 `rm` 是 safe-delete 改名 ⇒ 真删 `/bin/rm`。
- ★ `core.autocrlf=true`：Python 文本模式写文件会转 CRLF 造成假 `M` ⇒ 读写都用 `newline=''` 或 rb/wb。

## 已达成水平（参考口径，勿跨口径相减）
- 渲染 1280×800：z9 113 / z11 51 / z13 35 / z15 16 ms（8 省北京中心，4 轮最小；串行口径）。**分带并行后（北京单省，11-10）**：阶段和（面+路+线）z9 33 / z11 32 / z13 16 ms，1.7~1.9×；**标注阶段也已并行**（决策串行+命令分带绘制，z13 12→6ms，见下条）。算路 50 条(30 地标 w=1.25)：中位 123 / p90 753 / 最慢 1503 ms，展开 10.43M；比无表快 1.6×、p90 4.2×、质量 −3.21%。

## Android 原生 JNI App（11-04，见 docs/android.md）
- 原生 JNI App，不动 `ui_qt`；C 核心零改动，平台差异全在 `plat.c` 的 `__ANDROID__` 分支。工程 `android/`（Gradle + NDK CMake 复用 `src/` 编 `libskedaddle.so`），JNI 包名 `com.skedaddle.android.Skedaddle`。
- ★ JNI 桥本机校验：无 NDK 时用复刻 NDK 的 `jni.h` 垫片（C++ `JNIEnv_` + inline 转发）+ 真实 `src/` 头，`g++ -std=c++17 -D__ANDROID__ -Wall -Wextra -fsyntax-only`；别用 JDK 头当 NDK 替身。
- 第三方必须 vendor `android/app/src/main/cpp/thirdparty/{sqlite3,protobuf-c}`；JNI 每帧渲染后 `px[i]|=0xFF000000`；部署图集 `adb push products/graph /sdcard/Android/data/com.skedaddle.android/files/`。
- 限制：无真实 GPS/NMEA、TTS 仅 DRYRUN、minSdk 26、可出 APK 但均未真机验证。已落地增量更新+OTA（`Prefs`/`Downloader`/`Manifests`/`UpdateChecker`/`DownloadActivity`/`SettingsActivity` + FileProvider）；应用内装完省图返回自动 retryInit，进程内热换省仍未做。
