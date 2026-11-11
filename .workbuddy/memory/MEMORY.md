# skedaddle 项目长期约定

> 只留结论与"别再试"；推导看 PLAN.md、PROGRESS.md、`.workbuddy/memory/*.md`。

## 固定动作
- `sources/` 只放原始 `*.osm.pbf`；`products/` 全可删、`./scripts/sync_maps.sh` 重建。
- ★★★ `skedaddle_graph_extract --out=` 必须与脚本 `OUT_ROOT`(=`products/graph`) 逐字一致；传错留孤副本。删重复前 `md5sum` + 确认无脚本依赖。
- 产物终态 `products/graph/<省>/` = `disp.idx + lanes.bin + t_*`；tile 化失败致命、无兜底，唯一显示源 = disp tile。
- 验收 `./scripts/regress.sh`：Linux 主机 87/0；Windows 副机仅 beijing ⇒ 多省/places_auto.db/rev.bin/lm.bin 用例 FAIL，非回归。编译零警告（-Werror）；改渲染补 `bmpdiff.py 旧 新`。`--ui-test` 已进 regress（须隔离 ini）；完成即 commit + push origin main。前端只有 `skedaddle`（SDL 已移除）。
- ★ Windows 副机跑全量 regress：`SKEDADDLE_BUILD_DIR=$PWD/build_rel ./scripts/regress.sh`，运行期 PATH 必须同时含 `dist/skedaddle-release`（disp_runtime 等工具缺它 ⇒ rc=127）+ `/d/Qt/6.11.2/mingw_64/bin` + `ucrt64/bin`。11-11 口径 67/24（24 条全是缺 7 省数据）。改 `regress.sh` 后必跑 `regen_tutorial_blocks.py`（不带 --check）→ 再 `--check`；★ 用 Edit 改讲义会翻 CRLF ⇒ --check 假红，生成器再跑一次即归一化。
- ★★ 删文件/删 include 前必须 grep 该符号/该头全部引用（删 `window_sdl.c` 让 `app_run` 悬空；只 grep `label_` 漏 `NamePool`）。
- ★ `osmid.bin` 已停生成+停校验；重建：`git pull` 后删各省 `osmid.bin` 跑 regress 87/0。

## 构建环境
- ★ Linux(UOS aarch64/Wayland) Release：`source env.sh && cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/along/.local/qt5-dev/usr -DQt5_DIR=/home/along/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5 && cmake --build build_rel -j$(nproc)`；protoc-c 缺库先 `export LD_LIBRARY_PATH=/home/along/.local/usr/lib/aarch64-linux-gnu`。`build_release.sh` 是 Windows 专用。
- 无头自检 `QT_QPA_PLATFORM=offscreen` + plugin 路径；GUI 启动视图由 settings.ini 的 `view_*` 决定，交互 GUI 不认 `--lat/--lon`（仅离屏认）。
- ★★★ Windows/MSYS2 编译期 PATH 绝不能放 Qt bin（cc1plus 加载旧 libstdc++ ⇒ 退出码1、stderr 空，C 过 C++ 灭）；Qt bin 仅运行期加 + offscreen。★ 新 exe 运行期 Qt6 DLL 在 `/d/Qt/6.11.2/mingw_64/bin`（不在 msys64）；`libprotobuf-c-1.dll` 在 ucrt64/bin。
- ★★ ninja 走 `build_launcher.bat`/`cc_retry.sh` 时空 stderr + 重试全败 ≠ Defender：可能是真编译错误被吞（cmake 报成功跑旧 exe）⇒ 绕过包装器直接 `c++ -fsyntax-only` 看真 stderr。
- ★★ Windows 发布包重建：`build_release.sh` 的 `rm -rf` 被本机 safe-delete 拦（>50 文件拒）⇒ `cmake --build build_rel --clean-first` 全量重编 + 旧 dist 改名留底 + `build_release.sh --no-rebuild` 部署。产物 77 文件/102MB。
- 平台差异只许在 `src/utils/plat.{h,c}`；Qt5/Qt6 差异用 `QT_VERSION` 条件编译（`QDesktopWidget` Qt6 已移除 ⇒ `QScreen`/`primaryScreen()`，见 `1d99c92`）。

## 字体 / Qt GUI
- 候选链 `sk_font_candidates()`(plat.c)：内置 `fonts/` Noto + DroidSansFallback.ttf(CJK 兜底)，后系统字体；`SKEDADDLE_FONT_DIR` 优先。`notocjk.otf` 是 OTTO/CFF，stbtt 读不了。
- ★ `paint_frame` 的 `if(a->font)` 是文字门控：字体加载失败 ⇒ 文字全静默丢弃、几何照常。诊断 `font <路径>(已加载|★加载失败)` / `skedaddle_font_probe`。
- ★★★ 地图画布缓冲 alpha 必须置 0xFF（`raster_rgb` 只写 RGB）：Wayland 按 ARGB8888 上传 ⇒ alpha=0 全透明"一片花"。修在 `map_canvas renderNow` 渲染后 OR 0xFF000000；Android JNI 每帧同理。
- 截屏：Wayland 用 KWin `gdbus ...Screenshot.screenshotFullscreen false`，xcb 用 `ffmpeg -f x11grab -i :0.0`，Windows 真实 GUI 用 `ffmpeg -f gdigrab -i title=Skedaddle`。

## Qt 前端
- ★★ checkable 按钮 `setChecked()` 发 `toggled` ⇒ 递归 SIGSEGV，同步勾选态用 `QSignalBlocker`；`isVisible()` 对未 show() 子控件恒 false ⇒ 用 `!isHidden()`。
- ★ 不要顶栏（QToolBar 切高移画布），按钮一律画布子浮层；`QGraphicsOpacityEffect` 连文字一起淡 ⇒ 弃用。
- ★ 自绘子类用 `dynamic_cast`（`qobject_cast` 要 `Q_OBJECT`）；改 `.hpp`/`cp -p` 恢复后必须 `touch` 对应 `.cpp`；C++ 用 C 数学须 `<cmath>`+`std::floor`；图标自绘在 `qt_icons.cpp`。
- ★ 无边框窗口（11-06 终态）：`setWindowFlags(...|FramelessWindowHint)` 必须在 show() 前，`SKEDADDLE_FRAMELESS=0` 回退；三个自绘窗口按钮已删，退出走 Ctrl+Q/Alt+F4；只留顶部 10px 全宽透明拖动带。硬判据 `xprop _NET_FRAME_EXTENTS`。
- ★★ `showMaximized()` 在 UOS/Deepin 对 frameless ⇒ HIDDEN ⇒ 自记 `geometry()` → `setGeometry(可用区)` → 还原状态机；可用区 Qt5 `QApplication::desktop()`、Qt6 `screen()/primaryScreen()`。Wayland 下顶层 `move()` 无效，拖动只在 X11/XWayland 生效。
- ★ `regress.sh:qt_ovl_alpha` 硬编码矩形是活资产：按钮删除后贴顶 ⇒ `--rect=740,0,60,430`（用 400 会漏 852px ⇒ FAIL）。
- ★★ 无边框拖动 eventFilter：MouseButtonPress 不能吞（吞了收不到 MouseMove），只吞 dragging 中的 move。xdotool 合成点击必须分步 + sleep。

## 渲染边界（SDL 已彻底移除）
- 交互前端唯一 `skedaddle`；离屏唯一 `src/ui/offscreen_shot.c`；审计计数走 C 核心缓冲 ⇒ 不可删 `chrome.c`/`paint.c`/`navdraw.c`；共享审计 `src/render/audit.{c,h}`。
- ★ `--shot=<png>` 不调 `win.start()` ⇒ PNG 只有 chrome 空白底（非 bug）；验证地图内容用 `--shot-offscreen`。销毁 renderer 前 `font_drop_textures()`，不能每帧调（毁 CJK 缓存）。
- ★★ 放大后地名消失（11-05 修）：bbox 跨度比（<0.8 或 >1.25 重查）+ 缓存 zoom 判据；只在同进程连续缩放复现，别用离屏单帧否认。
- ★★ 中低 zoom 区名大字分级只在 z<14（`labels.c` 缓存 `font_open_default(font_size*2)` 大字+白描边；别复用 `a->font_big`：无 CJK 回退 ⇒ 方框）。路名等级按 zoom 放开（z12 主干/z14 次干/z16 支路）；z17 偏稀是视口内要素少，非门槛截断。白描边/字形锐化全比例尺生效，负向对照 `SKEDADDLE_LABEL_HALO=0` + `SKEDADDLE_FONT_SHARP=1.0` + `SKEDADDLE_POI_R=4` 三关。无 area/boundary label 链路。
- ★ POI 体系四件套（11-06~11-10）：①总开关 `show_poi`：关 ⇒ kind 11 图标+名称都不画、不占避让格，SQL `AND (?7 OR kind<>11)` 把 Top-N 让给行政地名；`cached_poi` 切换即重查。②子类 `sub` 列（64968c6）：0 通用蓝圆/1 交通紫方/2 教育褐圆/3 医疗红十字/4 旅游绿圆/5 生活琥珀圆，语义在 places_build.c PSUB_*；旧库无列自动回退 `0 AS sub`（不重建也能跑）；`SKEDADDLE_POI_SUB=0` 负向对照；sub 不参与避让 ⇒ poi_icons 数不变。③尺寸按 zoom 分档（d9ddafa）：zoom<15 恒 4（z≤14 出图与旧基线逐字节一致）/z15~16→5/z≥17→6；半径决策期随 `LCMD_POI.R` 下发（worker 无 env/static 竞态）；`SKEDADDLE_POI_R=固定值` 整体覆盖（=4 即旧恒定行为，负向对照锚）。④外部 POI `poi_source`（0 地图自带/1 外部文件）：`products/poi_ext.csv`（`SKEDADDLE_POI_EXT` 覆盖路径），格式 `名称,纬度,经度[,权重[,子类]]`（391c23b 起第 5 列可直接带子类配色，钳 0~5）；init 载一次不热重载；外部 POI 不进地名搜索（bigram 只在建库时生成）。

## 判据纪律（最高价值）
- 判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。
- ★★ 修前二进制别拷出 `build_rel/`（字体按 exe 相对 `../fonts` 解析，拷出对拍失真）；基线/新图一律用发布包 exe。
- ★★★ 手挑样本下结论必翻车；负向对照必须能失败；判"成功"看退出码不是输出行数。
- ★★★ 「绘制数变了」先切加载路径、再谈数据（整省 nmbin < PBF 解码 < disp tile 跨边界重复 +2.3%）：①同数据切路径 ②同路径重抽对拍 ③才疑数据。
- ★★ 浮层半透明后精确配色计数失效 ⇒ 数混合后颜色；判"前景没被淡"须逐像素相同。
- ★ 傍晚后（北京 11 月 ~17:10 日落）用全新 ini 固定 `day_night=0` 出图（缺省=2 自动判夜 ⇒ 全屏夜幕蒙版 RGB(8,14,34)@96 整体发灰，判颜色问题先排夜幕）。
- ★★★ 实验参数与 `--ui-test` 必须隔离配置（`SKEDADDLE_SETTINGS` 指全新 ini），否则污染真实 settings.ini ⇒ 断言假红；完整 regress 会把 view_* 写成默认 ⇒ 跑前备份、跑完恢复。
- ★★ UI 改版让像素判据硬编码坐标失效 ⇒ 坐标是活资产。视觉模型看 BMP 不可靠（颜色命名漂移、底图蓝色建筑块会被当成 POI），判据必须用像素统计；手写 BMP 裁剪注意 BGR 序（翻转两次=通道错位假图）。
- ★★★ 教程行号审计：工具报的"漂移"必须回源逐条按真实函数 span 核验，不能盲改；历史补记别写成 `file:N`。
- ★★ 讲义逐字块由 `scripts/regen_tutorial_blocks.py` 生成（16_qt + 11_gates）；改 `main_window.*`/`regress.sh` 后跑，`--check` 过期 rc=1。`check_doc_numbers.sh` 是 644 ⇒ regress 跳过，需手动跑。

## 语音播报
- `voice.c` 决定"说什么"、`plat.c:sk_tts_speak()` 发声；接线唯一 `voice_wire.c:nav_voice_frame()`。`say()` 唯一出口（15s 同文本冷却）。到达帧先播报后置 `sim_paused`。判真实发声 `SKEDADDLE_TTS_DRYRUN=<文件>`。

## 设置项与 C/C++
- ★★★ 新设置项三处都改：`SETTINGS_SCHEMA` + `app_init` + `app_persist_settings`（`app_init` 逐字段读、不走 `app_apply_settings`）。C++ include 的 C 头须 `extern "C"`；C 注释别写 `src/x/*.cpp`（`/*` 触发 -Wcomment ⇒ -Werror）。

## 坐标系（`src/utils/datum.c`）
- CGCS2000 平移 0；北京1954=EPSG:4214 默认 TOWGS84；西安1980=全国 3 参数(−104.57,−45.94,+5.30)。两条独立证据互印才落值；分区参数已否决。

## 教程三轨（11-06）
- ★ 三轨并存：`docs/teaching/NN_*.md`（最终 src 导读）、`tutorial/NN_*/`（各自独立小程序）、`tutorial/mini/`（同一份工程 18 快照 + 17 相邻 diff）；前两轨不是「同一代码逐步长大」。重编号两段式：同号对应，核心 00–14 / 跨平台 15–17；`check_tutorial_tracks.py` 守分类标签+两表递增。
- ★ `check_tutorial_tracks.py` 用排序取第一个目录 ⇒ 重编号残留的旧编号目录（只装 `out.bmp`/`tut.exe`）会顶掉同号真目录报假 FAIL；删残留即恢复（11-07 清 12 个）。
- `tutorial/mini/_parts/` 唯一人写源；`regen_mini_chain.py` 生成 steps/diffs（`--check` 过期 rc=1）；`check_mini_chain.py` 守编译运行+gates+负向对照，未接 regress。末课 MAP.md 是 mini ↔ src 18 行对照（有门禁）。
- ★ C 注释里 `NN_*/` 会提前闭合块注释炸 -Werror；`steps/stepNN_slug/` 回 `docs/` 是 4 层 `../../../../`。负向对照测 RUN 要改「被检查的数据」，别把条件改常量 0。

## 导航闭环五项（11-11，`99350be` avoid/ETA + `55dbaba` 门牌 + 本次 alarm/车头朝上）
- ★ 路线偏好 `pref_*` 三档只改搜索代价 `g`（`edge_cost_pref()`，软惩罚 `ROUTE_PREF_ADD=0.6` × 加法因子），**不碰** `total_s`/`seg_ds`/lmdist/navsim ETA；`h` 仍是物理时间下界 ⇒ admissible。硬排除 `=2` 与旧 `avoid_*` 合并 continue。ETA 校准只留 `navsim_set_eta_calibrator()` 挂在 `navsim_remaining_s()` 唯一收敛点，缺省 NULL 恒等。
- 门牌 `kind 14` 复用 place 表，照抄路名「只进搜索永不画」；`search_normalize_addr()` 两端共用，`search_query()` 只在查询串含数字时召回 `kind>=14`（否则挤占候选）。旧库无 kind14 行天然返回 0。MSYS2 中文命令行转 GBK ⇒ `search_cli --q-file=<UTF-8>`。
- ★★ 测速告警：建库端**独立 `alarm` 表**（不复用 place，避免污染 bigram/Top-N），北京 35 点；查询端 `label_cache_query_alarms()` 复用 `LabelCache.db`，投影到路线折线（60m 吸附 / 1.5km 上限），旧库无表 `prepare` 失败静默返回 0。语音端 `voice_scan_alarm_ex()` / `voice_tick_ex()` 把点状与边标志位告警放**同一张候选表**共用 `alarm_priority()`；`voice_scan_alarm()` / `voice_tick()` 是 `NULL/0` 薄封装 ⇒ 无数据时逐位不变。★ `inst = 1000000 + alarm.id`（错开段下标，防撞 `alarm_slot`）；★ `NavVoiceCtx` 加字段后所有构造点必须 `= {0}`（野值 n_alarms 会读野指针）。e2e C 类用**内存 SQLite** 自洽，注入 `n_al=0` 实测 rc=1 判据有分辨力。
- ★★★ 车头朝上：投影层原先只有 `mv_proj`/`mv_unproject` 认 rot。四个接口必须一起修：①`mv_visible_bbox` 旋转时反投影**四角**取 min/max（两角口径 bbox 偏小 ⇒ 地理裁剪整块丢要素）②`mv_pan_pixels`/`mv_zoom_at` 屏幕位移要经 `R(+rot)` 反变换（共用 `screen_to_world_vec`）③`mv_fit_bbox` 四角按 `R(-rot)` 算屏幕跨度（45° 差 √2）④`paint_nav_car` 屏幕角 = `heading - rot`。★ `paint_areas_banded`/`paint_lines_banded` 加 `rot!=0 → return -1` 落回串行（与 roads 已有 guard 同纪律）。★ 指北针北向是 `(-R·sin rot, -R·cos rot)`，原 `+R·sin` 反号。点指北针/手动旋转必须同时清 `heading_up`，否则下一帧被航向覆盖。
- 判据：`skedaddle_mapview_selftest`（MAPVIEW-OK，数值+车标像素）+ regress `rot45_shot`/`rot90_shot`；改前发布包 exe vs 改后 `--rot=0` 出图 md5 相同、bmpdiff 0/1024000。`--rot=` 必须同时加进 `offscreen_shot.c` 和 `qt_main.cpp` 参数白名单。

## 分层与模块边界
- 终态 DAG 零环：`utils ← pbf ← map ← route ← render ← ui ← ui_qt`（search/settings 横切）。★★ `-Wl,--start-group` 已移除 ⇒ 反向依赖立刻 undefined-reference；查符号归属别只 grep 函数前缀（`mapset.h` 那行 `render/labels.h` 承重 NamePool）。
- ★ `./scripts/check_layers.sh` 解析真实 include 须剥块/行注释。重构验收：零警告 + 门禁 + 出图逐字节一致 + `--ui-test` + regress 87/0。

## 别再试（verdicts only）
- 渲染：简化几何/去描边/面 tile 索引/顶点内联/软件预取/合并折线/NMBIN v3 全否；贵在间接寻址非 stride。
- ★★★ 换语言（C→Rust）不是渲染性能杠杆：`-O3`/`-march=native` 实测零收益（native 已含自动向量化）⇒ 瓶颈在内存访问与每要素固定开销；Rust 走同一 LLVM 后端，1:1 移植预期 0%（±5%）。LOD 档位已是设置项 lod_area（2~12 缺省 4；settings 不能 include paint.h（分层）⇒ 缺省字面值 4 双处锚定）。
- ★★★ 分带并行已落地（d8ed879）：`band.{c,h}` + `SKEDADDLE_BANDS`（0=串行基线/N=强制/缺省核数≤8）；面/线/路+标注四阶段与串行逐字节一致，1.7~1.9×。四陷阱勿再犯：①跨带线段对画布裁剪出串行同端点再逐像素滤带（重跑 Bresenham 漂 1px）②路分桶余量只给 ±3 行（搬视口 margin(64) 会让每边被 2.3 带重画反变慢）③带早退判据须比分桶宽 4 行（相等则边界边"分进来又退掉"不画不计数）④路名锚点钳到端带按 seq 归并还原串行序。
- ★★★ 标注并行两坑（11-10）：①font_draw 的 y=基线，CJK 字形顶可超出 hhea ascent（28px 实测 22 行 vs metrics 12）⇒ 行包围盒必须用 `font_vbox()`（max(metrics,0.9×ptsize)），估小=带边界上方缺字；②字形缓存懒插入共享哈希，8 线程 glyph_get 锁热点（21→129ms）⇒ flush 前主线程 `font_warm_text` 预热、worker 走 `font_draw_a_pre` 无锁只读。
- ★★ 离屏出图必须从仓库根目录跑：POI/地名数据按 cwd 相对路径找，从 dist/ 里跑整图差 4.2%（白字全没）⇒ 假 DIFF。Windows regress 运行时 PATH：`dist/skedaddle-release` + `ucrt64/bin`（编译期仍禁 Qt bin）。
- ★ 瓦片缓存/条带复用全量回退：`mv_project` 逐要素 lround + `sk_mercator_y_fast` 与 `inv` 不互逆 ⇒ 复用区差 1px。
- 算路：rev.bin v2 / PQ 叉数≠4 / 状态表改布局 全败；`RouteOpts` 一律指名初始化。地标 >30 无收益；改地标数须连带重定 w。
- 一次 `nmbin_open` 全部 disp tile 耗尽 Windows 映射句柄 ⇒ 逐 tile 开→拼→关。浮层透明度：`QGraphicsOpacityEffect` 与"整块一起淡"已否。
- 新增 `Buf` 须同步设 `.esz`；`sqlite3_open` 不建父目录 ⇒ 开库前 `sk_mkdir_p`；本机 `rm` 是 safe-delete 改名 ⇒ 真删 `/bin/rm`。
- ★ D161 中国 POI（Geofabrik 2025-02-14 快照）不合并：比自建库旧 20 个月（beijing.pbf 副本时间戳 2026-09-11）、北京域内点少一半（2.08万 vs 3.6万）、有名点 85% 已重叠，独有大半是 toilet/artwork 噪音；补全国覆盖走 sync_maps.sh 加省 PBF。解析 shp 坑：记录 content 长度=rlen*2（不含 8 字节头），多减 8 整文件脱同步得 0 点。
- ★ `core.autocrlf=true`：Python 文本模式写文件会转 CRLF 造成假 `M` ⇒ 读写 rb/wb 或 `newline=''`；python 多行匹配 CRLF 源文件（labels.c 等）须先 normalize `\r\n`→`\n`、写回还原。

## 已达成水平（参考口径，勿跨口径相减）
- 渲染 1280×800（北京单省，11-10 分带+标注并行后）：阶段和 z9 ≈33 / z11 ≈32 / z13 ≈16 ms（1.7~1.9×）；标注并行后 z13 12→6ms、z15 11→5ms。串行口径（8 省北京中心，4 轮最小）：z9 113 / z11 51 / z13 35 / z15 16 ms。算路 50 条(30 地标 w=1.25)：中位 123 / p90 753 / 最慢 1503 ms，展开 10.43M；比无表快 1.6×、p90 4.2×、质量 −3.21%。

## Android 原生 JNI App（11-04，见 docs/android.md）
- 原生 JNI App，不动 `ui_qt`；C 核心零改动，平台差异全在 `plat.c` 的 `__ANDROID__` 分支。工程 `android/`（Gradle + NDK CMake 复用 `src/` 编 `libskedaddle.so`），JNI 包名 `com.skedaddle.android.Skedaddle`。
- ★ JNI 桥本机校验：无 NDK 时用复刻 NDK 的 `jni.h` 垫片（C++ `JNIEnv_` + inline 转发）+ 真实 `src/` 头，`g++ -std=c++17 -D__ANDROID__ -Wall -Wextra -fsyntax-only`；别用 JDK 头当 NDK 替身。
- 第三方必须 vendor `android/app/src/main/cpp/thirdparty/{sqlite3,protobuf-c}`；JNI 每帧渲染后 `px[i]|=0xFF000000`；部署图集 `adb push products/graph`。增量更新+OTA 已落地（Prefs/Downloader/Manifests/UpdateChecker 等）；进程内热换省未做。限制：无真实 GPS/NMEA、TTS 仅 DRYRUN、可出 APK 但均未真机验证。
