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
- ★ `qobject_cast` 要求 `Q_OBJECT` ⇒ 自绘子类用 `dynamic_cast`。改 `.hpp` 后 ninja 不自动重编 ⇒ `touch` 对应 `.cpp`；**用 `cp -p` 备份/恢复源文件也会让 mtime 倒退、make 跳过重编**（负向对照撤销时踩过：`--ui-test` 仍报注入版的 2 项 FAIL）⇒ 恢复后必须 `touch` 再编。C++ 用 C 数学须 `#include <cmath>` + `std::floor`。图标符号不能用字体字符 ⇒ `qt_icons.cpp` 自绘。
- ★ 无边框窗口（2026-11-06，二次改版后终态）：`MainWindow` 构造里 `setWindowFlags(windowFlags() | Qt::FramelessWindowHint)`（**必须在 show() 前**，会重建 native window），`SKEDADDLE_FRAMELESS=0` 回退。**最小化/最大化/关闭三个自绘按钮已按需求删除**：退出走主菜单「退出」(Ctrl+Q) 或 WM 的 Alt+F4（实测两条都能干净退出），最小化/最大化交给 WM 快捷键。只留 `buildWindowDragStrip()` 的**顶部 10px 全宽透明拖动带**（画布子控件、零可见像素、与 y=10 起的搜索/右栏浮层不重叠），保住拖动 + 双击最大化/还原。硬判据 `xprop _NET_FRAME_EXTENTS`：默认 `not found` / 回退 `0,0,40,0`（UOS 标题栏 40px）。
- ★★ `showMaximized()` 在 UOS/Deepin WM 上对 frameless 窗口 ⇒ `_NET_WM_STATE_HIDDEN` 且几何不变（实测点按钮后仍 1280×800）⇒ **别依赖 WM 的 maximized 状态**；改成自己记 `geometry()` → `setGeometry(QApplication::desktop()->availableGeometry())` → 还原的状态机，X11/Wayland/Windows 一致。Wayland 下 `QWidget::move()` 对顶层窗口无效（Qt5.11 无 `startSystemMove`），拖动只在 X11/XWayland 生效，其余平台用 WM 的 Alt+拖动兜底。
- ★ `regress.sh:qt_ovl_alpha` 的硬编码矩形是"每次 UI 改版重测"的活资产：自绘控制条时期右栏下移 34px ⇒ 高 430→470（否则判据 3「矩形外零变化」假红 996px）；按钮删除后右栏恢复贴顶 ⇒ 回到 `--rect=740,0,60,430`（负向对照：故意用 400 会漏 852px ⇒ FAIL）。顶部拖动带零可见像素、不跟随 `alpha_rcol`。
- ★★ 无边框拖动 eventFilter：MouseButtonPress **不能吞**（吞了 Qt 隐式鼠标 grab 不成立、后续 MouseMove 收不到 ⇒ 假"拖动无效"），只吞 dragging 中的 move。xdotool 合成点击必须**分步 + 每步 sleep**：`mousemove X Y mousedown 1 mousemove ... mouseup 1` 链式会被引擎合并成一次 ⇒ 假"按钮无效"。

## 渲染边界（SDL 已彻底移除）
- 交互前端唯一 `skedaddle`；离屏唯一 `src/ui/offscreen_shot.c`（零 SDL，malloc 缓冲 + 内建 BMP writer）。SDL 仅存于 `blend_equiv`/`clip_equiv` 门禁对照体。
- ★★ 审计计数源走 C 核心缓冲（`offscreen_shot_main` 设 `a.qt_ui=0`）⇒ **不可删 `chrome.c`/`paint.c`/`navdraw.c`**。共享审计 = `src/render/audit.{c,h}`。
- ★ `--shot=<png>` 不调用 `win.start()` ⇒ 画布定时 renderNow 不触发，PNG 只有 chrome 空白底（offscreen/xcb 皆然），**不是渲染 bug**；验证地图内容用 `--shot-offscreen`（C 核心缓冲）。状态栏现由 paintEvent 自动绘制，旧「须显式刷新」口径已过时。销毁 renderer 前 `font_drop_textures()`，**不能每帧调**（毁 CJK 缓存）。
- ★★ 放大后地名消失（2026-11-05 修）：`labels.c:bbox_moved_far` 旧判据对**纯缩放**不敏感（角点位移/旧跨度恒 <0.2），z≥14 `kind_max` 恒 12 后缓存不重查，高 zoom 小视口裁光旧 Top-400 大地名 ⇒ 修法=加 bbox 跨度比（<0.8 或 >1.25 重查）。路名门槛同步 15→14（`paint.c` 三处 + `labels.c` 一处），500 米档才有路名。**只在同进程连续缩放中复现**，新进程 `--shot-offscreen` 首帧永远查得到 ⇒ 别用离屏单帧统计否认这个 bug。验收：连续缩放台架对拍 + z13/z15 像素 0% + z14 6.44% 全是新增文字 + regress 87/0。z17/18 仍稀疏属数据密度上限（路名只标 cls<=2）。
- ★★ 中低 zoom「没有区域名/地名」（2026-11-05 修）：**不是缓存/数据 bug**——kind<=2 的市/区名和村名/POI 画成同一种 14px 小字+橙圈，没有视觉层级；加上路名门槛 14 让 1~10 公里档零路名。修法：`labels.c` 按 pt 缓存 `font_open_default(font_size*2)` 画 kind<=2 大字 + 白描边（★ 别复用 `a->font_big`：单文件 `font_open` 无 CJK 回退 ⇒ 中文方框），kind==3 加描边，按包围盒占格、冲突退回小字；路名门槛 14→12。**分级显示（大字层）只在 z<14**；但 65bad95 起白描边/字形锐化已**全比例尺生效** ⇒ 「z14/z15 像素 0.000%」口径作废，负向对照改用 `SKEDADDLE_LABEL_HALO=0` + `SKEDADDLE_FONT_SHARP=1.0` 双关（11-06 起再加 `SKEDADDLE_POI_R=6` 三关，实测 0.000%）。项目无 area/boundary label 链路（`paint_areas` 无 Font、`AF_*` 无 name），按行政区面绘制需重抽数据。

## 判据纪律（最高价值）
- 布尔判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。
- ★★ 修前二进制别拷出 `build_rel/`：字体候选按 exe 相对 `../fonts` 解析，拷到 /tmp 会让基线字体集合/网格格宽变化 ⇒ 标注数假变（125 vs 138），像素对拍失真。取基线前先确认两边 exe 同目录。
- ★★★ 手挑样本下结论必翻车；**负向对照必须能失败**。
- ★ 判"成功"看**退出码**不是输出行数（打完 OK 就 return、栈上对象被 worker 命中 ⇒ rc=139 却显"通过"）。
- ★★★ 「绘制数变了」先切加载路径、再谈数据（整省 nmbin < PBF 解码 < disp tile 跨边界重复 +2.3%）。顺序：①同数据切路径 ②同路径重抽对拍 ③才疑数据。
- ★★ 浮层半透明后精确配色计数失效 ⇒ 数混合后颜色（有效 alpha 乘积 + 容差）；判"前景没被淡"须逐像素相同（文字用口径 b：相对底图偏离量 + `--fgrect` 框准）。
- ★★★ 会写回配置的实验参数（`--ovl-alpha` 等）跑实验必须隔离配置（`XDG_CONFIG_HOME`+`APPDATA`），否则污染 `settings.ini` ⇒ 断言默认值用例假红。
- ★★ **完整 `scripts/regress.sh` 本身也会把真实 `settings.ini` 的 `view_*` 写成默认 `30/110/z10`**（11-05、11-06 两次复现）⇒ 跑前先 `cp` 备份真实配置，跑完 diff 并恢复；用 `--ui-test` 时另用 `SKEDADDLE_SETTINGS` 指全新 ini 可避开 datum 假红。
- ★★ UI 改版让像素判据硬编码坐标失效 ⇒ 坐标是"每次改版重测"的活资产。
- ★★★ **教程/文档行号审计：并行多引用表格（一行多符号+多行号）工具必误判归属** ⇒ 工具报的"漂移"必须回源逐条核验（拿真实函数 span 对照），不能盲改（踩过：自动 fixer 把 `bbox_overlap` 行号改成了 `src_unload` 的）。同文件 `978-~1179` 这类带 `~` 近似引用要跳过。历史补记里「旧→新」别写成 `file:N` 形式，会被锚点扫描误判成有效引用。
- ★★ **讲义逐字块已改为源码生成**：`scripts/regen_tutorial_blocks.py` 按方法定义边界生成 `16_qt.md` 的 `main_window.h` 全文 + 4 个 cpp 块（68 方法不重不漏）与 `11_gates.md` 内嵌 `regress.sh` 全文；改 `main_window.*` / `regress.sh` 后跑一次，`--check` 讲义过期 **rc=1**（注入一行实测能失败）。Python 读写一律 `newline=''`（autocrlf 假 M）。`check_doc_numbers.sh` 是 644、`regress.sh` 用 `[ -x ]` 守卫 ⇒ **doc_numbers 用例在本机被跳过**，87 个用例不含它，需 `bash scripts/check_doc_numbers.sh` 手动跑。
- ★★★ **教程物理重编号 = 两段式（2026-11-06）**：`docs/teaching/NN_*` 与 `tutorial/NN_*` 同号对应不变，课次改为 **核心代码 00–14 / 跨平台代码 15–17**。映射：旧 02_platform→15、旧 12_qt→16、旧 17_android→17（不变）、旧 03–11 各减 1、旧 13_gates→11、旧 14_capstone→12、旧 15_datum→13、旧 16_i18n→14。**别只改文件名**：全仓「第 N 课」/ `L` 简写 / `NN_slug.md` / `tutorial/NN` 共 4 类引用都要按映射一次性回调替换（串行 replace 会二次污染）；范围叙述（`第 1–12 课`、`L10–L12`）与并列（`第 7/8 课`）必须人工重写语义。`scripts/check_tutorial_tracks.py` 守 `> 分类：` 标签 + 两张课程表 0..17 严格递增（改分类/调行实测 rc=1）。三条讲义门禁均**未接 regress**（保 87/0 与副机行为）。

## 语音播报
- 分层：`voice.c` 决定"说什么"，`plat.c:sk_tts_speak()` 发声；**接线唯一** `voice_wire.c:nav_voice_frame()`（生产与 `voice_e2e.c` 共用）。`say()` 是所有播报唯一出口（15s 同文本冷却）。顺序：到达帧先播报后置 `sim_paused=1`，收尾语单独判不受 paused 挡。判真实发声：`SKEDADDLE_TTS_DRYRUN=<文件>`。

## 设置项与 C/C++
- ★★★ `app_init` 逐字段读设置（不走 `app_apply_settings`）⇒ 新设置项三处都改：`SETTINGS_SCHEMA` + `app_init` + `app_persist_settings`。C++ include 的 C 头须 `extern "C"`；注释别写 `src/x/*.cpp`（`/*` 触发 -Wcomment ⇒ -Werror FAIL）。

## 坐标系（`src/utils/datum.c`）
- CGCS2000 平移 0；北京1954=EPSG:4214 默认 TOWGS84；西安1980=全国 3 参数(−104.57,−45.94,+5.30)。"EPSG 没登记"≠"没参数"；两条独立证据互印才落值；分区参数已否决（选错代价 > 全国最大 14.7m）。

## 教程三轨（2026-11-06）
- ★ 三份资料并存：`docs/teaching/NN_*.md`（最终 `src/` 源码导读）、`tutorial/NN_*/`（各自独立小程序）、`tutorial/mini/`（**同一份工程 18 个连续快照 + 17 个相邻 diff**，真渐进式）。前两轨都不是「同一代码逐步长大」；第 3 轨补上「最小框架 → 逐课累加 → 末课对上最终代码」。分类重排（核心 00–14 / 跨平台 15–17）不变。
- `tutorial/mini/_parts/` 是**唯一人写源**（`main.c.tpl` + 18 个 header-only `stepNN.h` + `chain.json`）；`scripts/regen_mini_chain.py` 生成 `steps/`+`diffs/`（224 文件，`--check` 过期 rc=1）；`scripts/check_mini_chain.py` 守「旧文件零删改（行子序列）+ 18 步 `-Werror` 编译运行 + gates/ink 确定性 + 5 类负向对照」，**未接 regress.sh**（与 `check_tutorial_tracks.py` 同理，保 87/0 与副机行为）。
- 末课合流点 `tutorial/mini/MAP.md` = mini 模块 ↔ src 模块 18 行对照 + 差距清单；门禁会校验 MAP 引用的路径真实存在 ⇒ 改模块名/搬文件要重跑全链门禁。
- ★ C 注释里写 `NN_*/` 会让 `*/` 提前闭合块注释、`src/x/*.c` 的 `/*` 触发 `-Wcomment` 炸 `-Werror`；写相对链接先数清层数（`steps/stepNN_slug/` 回 `docs/` 是 **4 层** `../../../../`）。负向对照想测 RUN 判据，改「被检查的数据」而不是把条件改成常量 0（后者让变量未使用，先被 BUILD 拦下）。

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
