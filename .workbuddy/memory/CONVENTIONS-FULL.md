# skedaddle 项目长期约定

> 只留结论与「别再试」；推导看 PLAN.md / PROGRESS.md / `.workbuddy/memory/*.md`。★=踩过坑，★★★=反复踩。

## 定位（11-12 定稿）
- ★★★ **L1 云端飞轮型能力明确不追**（实时路况/红绿灯倒计时/高精车道级/AI 助手/用户贡献）：靠上亿用户轨迹+云端 AI，非加代码可得，别写进 TODO。依据 `CONSUMER-NAV-GAP-2026-11-12.md` + README「定位」。L2 本地可补、L3 产品化取舍。
- ★ 与商业导航「同名不同物」8 词：车道级=OSM `turn:lanes` 静态串 vs 高精地图+亚米定位；离线=本项目全功能 vs 竞品受限算路/搜索。
- 架构 = 构建期固化数据 + 单机离线内核：`sources/*.osm.pbf` → `sync_maps.sh` → `products/graph/<省>/`（`disp.idx+lanes.bin+t_*`），运行时 mmap，不依赖地图服务。

## 固定动作 / 构建 / 验收
- `sources/` 只放 `*.osm.pbf`；`products/` 全可删、`sync_maps.sh` 重建（同时用全部 PBF 重建全国共用 `places_auto.db`：地名/POI/bigram，加省自动进搜索）。
- ★★★ `skedaddle_graph_extract --out=` 必须与脚本 `OUT_ROOT`(=`products/graph`) 逐字一致；删重复前 `md5sum` + 查脚本依赖。tile 化失败致命无兜底，唯一显示源 disp tile；`--graph=` 一次一省，搜索库全国共用；`osmid.bin` 已停生成/停校验。
- ★ Linux(UOS aarch64/Wayland) Release：`source env.sh && cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/along/.local/qt5-dev/usr -DQt5_DIR=/home/along/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5 && cmake --build build_rel -j$(nproc)`；protoc-c 缺库 `export LD_LIBRARY_PATH=/home/along/.local/usr/lib/aarch64-linux-gnu`。`build_release.sh` 仅 Windows。无头 `QT_QPA_PLATFORM=offscreen`；GUI 视图由 `view_*` 定，交互 GUI 不认 `--lat/--lon`（仅离屏认）。
- ★★★ Windows/MSYS2 编译期 PATH 绝不能放 Qt bin（cc1plus 加载旧 libstdc++ ⇒ 退出码1、stderr 空）；Qt bin 仅运行期 + offscreen。Qt6 DLL `/d/Qt/6.11.2/mingw_64/bin`，`libprotobuf-c-1.dll` 在 ucrt64/bin。`build_launcher.bat`/`cc_retry.sh` 空 stderr + 重试全败 ≠ Defender：真编译错误被吞 ⇒ 绕包装器 `c++ -fsyntax-only` 看真 stderr；LTO 并行链接偶败 `-j1`。
- ★★ Windows 发布包：`build_release.sh` 的 `rm -rf` 被 safe-delete 拦 ⇒ `cmake --build build_rel --clean-first` + 旧 dist 改名 + `--no-rebuild`（77 文件/102MB）。平台差异只许在 `src/utils/plat.{h,c}`；Qt5/Qt6 用 `QT_VERSION` 条件编译（`QDesktopWidget` Qt6 移除 ⇒ `QScreen`，`1d99c92`）。
- `regress.sh`：Linux **87/0 是 11-11、109/0 是 11-12 M1 前**的历史口径；11-12 新增 17 条 L2/数据链 + 6 条 NMBIN M1 判据，**最终实跑 115/0**。★ `SKEDADDLE_BUILD_DIR=$PWD/build_rel` 指新构建（缺省 `build/` 常陈旧 ⇒ 假绿）。编译零警告；改渲染补 `bmpdiff.py 旧 新`；`--ui-test` 须隔离 ini；跑完 commit+push。
- ★ 副机全量 regress：同上变量；运行期 PATH 须含 `dist/skedaddle-release`（缺 ⇒ disp_runtime rc=127）+ Qt bin + `ucrt64/bin`；11-11 口径 67/24（24 条全因缺 7 省数据）。改 `regress.sh` 后必跑 `regen_tutorial_blocks.py`（无 --check）再 `--check`；★ Edit 改讲义翻 CRLF ⇒ --check 假红，生成器再跑一次归一化。
- ★★ 删文件/删 include 前 grep 该符号/该头全部引用（`window_sdl.c` ⇒ `app_run` 悬空；只 grep `label_` 漏 `NamePool`）。C 注释别写 `src/x/*.cpp`/`products/*.db`（`/*` ⇒ -Wcomment ⇒ -Werror）；C++ include C 头须 `extern "C"`。

## 判据纪律（最高价值）
- 判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。★★ 修前二进制别拷出 `build_rel/`（字体按 exe 相对 `../fonts` 解析）；基线/新图一律用发布包 exe。
- ★★★ 手挑样本下结论必翻车；负向对照必须能失败；判成功看退出码不是输出行数。「绘制数变了」先切加载路径再谈数据（整省 nmbin < PBF 解码 < disp tile 跨界重复 +2.3%）。
- ★★ 浮层半透明后精确配色计数失效 ⇒ 数混合后颜色；判前景没淡须逐像素相同。UI 改版让硬编码坐标失效 ⇒ 坐标是活资产。视觉模型看 BMP 不可靠，判据用像素统计；手写 BMP 裁剪注意 BGR 序。
- ★ 傍晚后（北京 11 月 ~17:10 日落）用全新 ini 固定 `day_night=0`（缺省=2 自动判夜 ⇒ 夜幕 RGB(8,14,34)@96 发灰，判颜色先排夜幕）。
- ★★★ 实验参数与 `--ui-test` 必须隔离 `SKEDADDLE_SETTINGS` 全新 ini，否则污染真实 settings.ini ⇒ 断言假红；完整 regress 会把 `view_*` 写默认 ⇒ 跑前备份、跑完恢复。
- ★★★ 教程行号审计：报的「漂移」回源按真实函数 span 核验，别盲改；历史补记别写 `file:N`。离屏出图必须从仓库根跑（POI/地名按 cwd 相对找，从 dist/ 跑整图差 4.2% 白字全没 ⇒ 假 DIFF）。

## 字体 / Qt GUI / 渲染边界
- 字体链 `sk_font_candidates()`：内置 `fonts/` Noto + DroidSansFallback(CJK 兜底) → 系统；`SKEDADDLE_FONT_DIR` 优先。`notocjk.otf` 是 OTTO/CFF，stbtt 读不了。★ `paint_frame` 的 `if(a->font)` 是文字门控：加载失败 ⇒ 文字静默丢弃、几何照常（`skedaddle_font_probe`）。
- ★★★ 画布缓冲 alpha 必置 0xFF（`raster_rgb` 只写 RGB）：Wayland ARGB8888 上传 ⇒ alpha=0「一片花」。修在 `map_canvas renderNow` 后 OR 0xFF000000；Android JNI 每帧同理。
- ★★ `setChecked()` 发 `toggled` ⇒ 递归 SIGSEGV，同步勾选用 `QSignalBlocker`；`isVisible()` 对未 show() 恒 false ⇒ `!isHidden()`。自绘子类用 `dynamic_cast`；改 `.hpp`/`cp -p` 后 `touch` 对应 `.cpp`；C++ 用 C 数学须 `<cmath>`+`std::floor`。
- ★ 无边框（11-06）：`FramelessWindowHint` 必须在 show() 前，`SKEDADDLE_FRAMELESS=0` 回退；退出 Ctrl+Q/Alt+F4；只留顶部 10px 透明拖动带（`xprop _NET_FRAME_EXTENTS`）。拖动 eventFilter 不吞 MouseButtonPress（吞了收不到 Move），只吞 dragging 中 move；xdotool 点击分步+sleep。
- ★★ `showMaximized()` 在 UOS/Deepin 对 frameless ⇒ HIDDEN ⇒ 自记 `geometry()` → `setGeometry(可用区)` → 还原状态机；Wayland 顶层 `move()` 无效。不要 QToolBar（切高移画布），按钮一律画布子浮层；`QGraphicsOpacityEffect` 连文字一起淡 ⇒ 弃用。截屏：Wayland KWin gdbus / xcb `ffmpeg -f x11grab` / Win `gdigrab`。★ `qt_ovl_alpha` 矩形是活资产：按钮删除后贴顶 ⇒ `--rect=740,0,60,430`。
- 交互唯一 `skedaddle`（SDL 已移除）；离屏唯一 `src/ui/offscreen_shot.c`；审计走 C 核心缓冲 ⇒ 不可删 `chrome.c`/`paint.c`/`navdraw.c`（共享 `render/audit.{c,h}`）。★ `--shot=` 不调 `win.start()` ⇒ 只有 chrome 空白底（非 bug）；验地图用 `--shot-offscreen`。销毁 renderer 前 `font_drop_textures()`，不能每帧调。
- ★★ 放大丢字（11-05 修）：bbox 跨度比 <0.8/>1.25 重查 + 缓存 zoom；只在同进程连续缩放复现。中低 zoom 区名大字仅 z<14（`font_open_default(font_size*2)`+白描边；别复用 `a->font_big` 无 CJK ⇒ 方框）；路名 z12 主干/z14 次干/z16 支路；z17 稀是视口要素少。负向锚 `SKEDADDLE_LABEL_HALO=0`/`FONT_SHARP=1.0`/`POI_R=4`。无 area/boundary label 链路。
- ★ POI 四件套：①`show_poi` 关 ⇒ kind 11 图标名全隐、不占避让格（SQL `AND (?7 OR kind<>11)`）；②`sub` 0 蓝圆/1 紫方交通/2 褐圆教育/3 红十字医疗/4 绿圆旅游/5 琥珀生活，旧库回退 `0 AS sub`，不参与避让；③半径 z<15→4 / z15~16→5 / z≥17→6，随 `LCMD_POI.R` 下发；④`poi_source` 0 自带/1 `products/poi_ext.csv`（`SKEDADDLE_POI_EXT`，`名称,纬度,经度[,权重[,子类]]`，init 载一次）。★★ 外部 POI 原本只进标注不进 bigram ⇒ 搜不到，11-11 `search_add_external()` 补运行时索引。

## 算路 / 导航 / 语音 / 设置
- ★ `pref_*` 三档只改搜索代价 `g`（`edge_cost_pref()`，软惩罚 0.6），不碰 `total_s`/ETA；`h` 仍是物理时间下界 ⇒ admissible；硬排除 =2 与旧 `avoid_*` 合并。ETA 只留 `navsim_set_eta_calibrator()` 挂 `navsim_remaining_s()`，缺省 NULL 恒等。门牌 `kind 14` 复用 place 表「只进搜索永不画」，`search_query()` 仅查询含数字时召回；MSYS2 中文转 GBK ⇒ `search_cli --q-file=<UTF-8>`。
- ★★ 测速告警：建库端独立 `alarm` 表（不复用 place，免污染 bigram/Top-N），北京 35 点；查询端 `label_cache_query_alarms()` 复用 `LabelCache.db`（60m 吸附/1.5km 上限），旧库无表静默返回 0；语音端 `voice_*_ex()` 把点状与边标志位放同一候选表共用 `alarm_priority()`，薄封装无数据时逐位不变。★ `inst=1000000+id` 错开段下标；`NavVoiceCtx` 构造点一律 `= {0}`；e2e 用内存 SQLite，注入 `n_al=0` rc=1 有分辨力。
- ★★★ 车头朝上：四个接口一起修（原先仅 `mv_proj`/`mv_unproject` 认 rot）：①`mv_visible_bbox` 反投影**四角**取 min/max（两角 ⇒ 整块要素被裁剪）②`mv_pan_pixels`/`mv_zoom_at` 位移经 `R(+rot)`（`screen_to_world_vec`）③`mv_fit_bbox` 四角按 `R(-rot)`（45° 差 √2）④`paint_nav_car` 屏幕角 `heading-rot`。`*_banded` 加 `rot!=0→return -1` 落回串行；指北针北向 `(-R·sin rot,-R·cos rot)`（原号反）；点指北针/旋转同时清 `heading_up`。判据 `mapview_selftest`+`rot45/90_shot`、改前 exe vs `--rot=0` md5 相同（bmpdiff 0/1024000）；`--rot=` 须同时进 `offscreen_shot.c` 与 `qt_main.cpp` 白名单。
- `voice.c` 说什么、`plat.c:sk_tts_speak()` 发声；接线唯一 `voice_wire.c:nav_voice_frame()`；`say()` 唯一出口（15s 冷却）；到达帧先播报后置 `sim_paused`；判发声 `SKEDADDLE_TTS_DRYRUN=<文件>`。
- ★★★ 新设置项四处都改：`SETTINGS_SCHEMA`+`app_init`+`app_persist_settings`+`app_apply_settings`（`app_init` 逐字段读，不走后者；后者是 C 面板「应用/恢复默认」）。`datum.c`：CGCS2000 平移 0；北京1954=EPSG:4214 默认 TOWGS84；西安1980=全国 3 参数(−104.57,−45.94,+5.30)；分区参数已否决。

## 教程三轨 / 门禁
- ★ 三轨并存：`docs/teaching/NN_*.md`（最终 src 导读）、`tutorial/NN_*/`（各自独立小程序）、`tutorial/mini/`（同一工程 18 快照+17 diff）；前两轨不是同一代码长大。核心 00–14 / 跨平台 15–17，`check_tutorial_tracks.py` 守分类+两表递增；按排序取首个目录 ⇒ 重编号残留旧目录会报假 FAIL。
- `mini/_parts/` 唯一人写源；`regen_mini_chain.py` 生成 steps/diffs（`--check` 过期 rc=1）；`check_mini_chain.py` 守编译+gates+5 类负向对照（未接 regress）；末课 MAP.md 是 mini ↔ src 18 行对照。★ C 注释里 `NN_*/` 提前闭合块注释炸 -Werror；`steps/stepNN_slug/` 回 `docs/` 是 4 层 `../../../../`；负向测 RUN 要改被检查的数据（改条件为常量 0 会先被 -Werror 挡在 BUILD）。
- ★ 讲义逐字块由 `regen_tutorial_blocks.py` 生成（16_qt+11_gates），改 `main_window.*`/`regress.sh` 后跑再 `--check`；11-12 新增 `check_teaching_sync.py`（未接 regress）守第 7 课 2 个逐字块+功能构成表区间+演进补记/index 索引；源码改致区间平移会红 = 回源重贴信号，别直接改数字。`check_doc_numbers.sh` 是 644 ⇒ regress 跳过需手动跑。

## 分层 / 别再试（verdicts only）
- DAG 零环 `utils←pbf←map←route←render←ui←ui_qt`（search/settings 横切）；`-Wl,--start-group` 已移除 ⇒ 反向依赖立刻 undefined-reference；`check_layers.sh` 只扫 `src/`、解析 include 须剥注释。查符号归属别只 grep 前缀（`mapset.h` 的 `render/labels.h` 承重 NamePool）。
- 渲染否决：简化几何/去描边/面 tile 索引/顶点内联/软件预取/合并折线/NMBIN v3/瓦片缓存条带复用（`sk_mercator_y_fast` 与 `inv` 不互逆 ⇒ 差 1px）；贵在间接寻址非 stride。★★★ 换语言 C→Rust 不是杠杆：`-O3`/`-march=native` 零收益 ⇒ 瓶颈在内存访问与每要素固定开销，1:1 移植预期 0%（±5%）。LOD 已是设置项 `lod_area`(2~12 缺省 4；settings 不能 include paint.h ⇒ 字面值 4 双处锚定)。
- ★★★ 分带并行已落地（d8ed879）：`band.{c,h}`+`SKEDADDLE_BANDS`(0=串行/N=强制/缺省≤8)，四阶段与串行逐字节一致 1.7~1.9×。四陷阱：跨带线段须对画布裁剪再逐像素滤带（重跑 Bresenham 漂 1px）；路分桶余量只 ±3 行（搬 margin 64 ⇒ 每边 2.3 带重画反变慢）；带早退判据须比分桶宽 4 行；路名锚点钳到端带按 seq 归并。
- ★★★ 标注并行两坑：①`font_draw` y=基线、CJK 顶超 hhea ascent（28px 实测 22 行 vs metrics 12）⇒ 行盒用 `font_vbox()`=max(metrics,0.9×ptsize)；②字形缓存懒插入共享哈希，8 线程 `glyph_get` 锁热点 21→129ms ⇒ flush 前主线程 `font_warm_text` 预热、worker 走 `font_draw_a_pre` 无锁只读。11-12 删 `font_draw_halo` 死函数（唯一 -Wunused-function 源）。
- ★★ 算路否决 rev.bin v2 / PQ 叉数≠4 / 状态表改布局；`RouteOpts` 指名初始化，地标 >30 无收益且改数须重定 w。一次 `nmbin_open` 全部 disp tile 耗尽 Win 映射句柄 ⇒ 逐 tile 开→拼→关。新增 `Buf` 设 `.esz`；`sqlite3_open` 前 `sk_mkdir_p`；`rm` 是 safe-delete ⇒ 真删 `/bin/rm`；`core.autocrlf=true` ⇒ Python 写用 rb/wb 或 `newline=''`。
- ★ D161 中国 POI 不合并：比自建库旧 20 个月、北京点少一半（2.08万 vs 3.6万）、有名点 85% 重叠；补全国走 sync_maps.sh 加省 PBF。SHP 记录 content 长度=rlen*2（不含 8 字节头）。

## 已达成水平 / Android（参考口径，勿跨口径相减）
- 渲染 1280×800 北京单省、11-10 并行后阶段和：z9≈33 / z11≈32 / z13≈16 ms（1.7~1.9×），标注 z13 12→6、z15 11→5 ms；串行 8 省口径 z9 113 / z11 51 / z13 35 / z15 16 ms。算路 50 条(30 地标 w=1.25) 中位 123 / p90 753 / 最慢 1503 ms，展开 10.43M。
- Android 原生 JNI App（11-04，`docs/android.md`）：不动 `ui_qt`，差异全在 `plat.c` 的 `__ANDROID__`；NDK CMake 复用 `src/` 编 `libskedaddle.so`，vendor `thirdparty/{sqlite3,protobuf-c}`，每帧 `px[i]|=0xFF000000`；无 NDK 校验用复刻 `jni.h` 垫片 + `-fsyntax-only`（别用 JDK 头）。增量更新+OTA 已落地、进程内热换省未做；无真实 GPS/NMEA、TTS 仅 DRYRUN、APK 未真机验证。

## 11-12 L2 能力细则（精要版只留指针，推导与边界在此）
- `route/profile.{h,c}` 三档 car/bike/foot：`cost.c:edge_speed_kmh()` 是唯一速度入口
  （`edge_cost` / `seg_ds` / `route_check` 共用）；只放大 `g`，`h` 仍以 130km/h 物理下界
  ⇒ admissible；`RP_CAR` 逐字节等价旧 `(double)sp`。★ 边界：graph 只收 `drivable=1`，
  `footway/cycleway/steps` 只进 lines.bin 渲染、不进路网 ⇒ 完整步行/骑行导航需重抽 8 省 PBF；
  `lm.bin` 是构建期共享表，不按 profile 重建。
- `utils/units.{h,c}` 全局单位制：公制逐字节兼容旧 `fmt_distance()`/`voice_spoken_distance()`；
  英制 ft/mi/mph；Qt 用 `units_big_parts()`/`units_fmt_short()`。设置 `route_profile`/`unit_system`
  缺省 0；`SETTINGS_SCHEMA_N` 19→21 后 `chrome.c:settings_row_h()` 自适应行高，绘制与命中共用。
- `search/addr.{h,c}` 结构化拆分 + `addr_core_key()`（普通地名恒等变换 ⇒ 存量库无需重建）；
  `search_reverse()` 四级合成；真实库 kind14=0 行但 kind13 有 75998 行 ⇒ 仍能出层级地址。
  `search/history.{h,c}` 固定 32 条 MRU 写 `history.txt`，**不复用 tracks.db**（避免清空历史误删收藏）。
- DEM/等高线：SRTM S3 直连超时 ⇒ 不伪造真实地形，改 `contour_chain_check.py` 用合成
  圆丘/平地 `.hgt` 验「生成器→三件套→`nmbin_open`→`paint_contours` 像素」全链 + 平地负向。
  ★ NMBIN 头是手工序列化：`rec_count` 在 `@20`（不是 struct 对齐的 `@24`）。
- 数据扩省：本机只有 8 省 PBF，**没有下载新省数据/真实 DEM**；交付
  `add_province.sh` + `check_province_coverage.py`（--self-test 删 disp.idx 证明能失败），
  正常模式由 add_province.sh 在真实扩省后调用，不进 regress（副机缺 7 省会假红）。
- 分层：`ui_qt` 禁止直接 include `route/` / `search/`；Qt 菜单出行方式走
  `ui/window.h` 的 `app_route_profile_count/clamp()` 门面（实现放在 window.c 末尾，
  避免插在 paint_frame 前让讲义逐字块区间反复平移）。`check_layers.sh --self-test`
  在临时夹具注入 map→render 证明门禁能失败。
- 讲义：删 `font_draw_halo` 18 行 ⇒ labels.c 996→978、行号 -18；window.c 因新增设置项
  paint_frame 1291→1309 / app_render_to_buf 1557→1575。两个逐字块与全篇 51 处行号回源重贴，
  `check_teaching_sync.py` 改守「已删除」口径。

## 11-12 NMBIN M1 细则（精要版只留指针，推导与判据在此）
- 评估报告 `NMBIN-OPTIMIZATION-2026-11-12.md` 的结论：帧时间/算路没有空间；体积有空间但不该改 NMBIN 本体；**格式健壮性与增量更新地基是真缺口且零性能代价**。施工计划与执行记录见 `NMBIN-OPTIMIZATION-PLAN-2026-11-12.md` §8。
- **O1**：期望 schema 表按 `(kind, rec_size)` 索引共 14 项，逐条复制 `graph_build.c` / `gen_contours.c` / `pbfmap.c` 的 `nmbin_add_field()` 序列（**不是从 `*_BYTES` 推导**）。回源发现两个陷阱：①`NMBIN_KIND_LANES` 同时用于 `lanes.bin`(rs=4, `lane_sid`) 与 `lane_names.bin`(rs=1, `pool`)；②`restrictions.bin` 的 `RF_PREV_EDGE`(@16) 根本没写进字段表。8 省真实产物反推出的 schema 与源码交叉验证完全一致。`nmbin_check_schema()` 只在加载期比一次，未登记组合返回 NULL 跳过（历史 v1 / 未来新 kind 不误伤）；**绝不**把按名查找放进每记录循环。
- **O2**：`src/utils/sha256.{h,c}` 最小流式 SHA-256（FIPS 180-4，零依赖，`extern "C"`），三条标准向量先与 Python `hashlib` 独立核对再写进判据。`NMBinHeader.reserved[44]` → `digest[32]+digest_algo+digest_ver+reserved[4]`（struct 仍 128 B）；手工字节流 `digest@76 / algo@108 / ver@112`，既有字段一个没挪（`rec_count` 仍在 `@20`）。`NMBinWriter` 带 `Sha256`，`nmbin_write_records()` 流式喂、`nmbin_finalize()` 定稿并置 `flags |= NMBIN_FLAG_DIGEST`。`nmbin_check()` **只验摘要区自洽**（算法/版本/非全 0），不做哈希；`nmbin_verify_digest()` 在边界重算。
- ★★ 报告 §5.2 原写「加载期多一次全文件哈希，冷启动一次性」，回源后**纠偏**：`mapset_set_view` 是按需 mmap + LRU，加载期算全文件哈希会强制读满 3.89 GiB ⇒ 全量校验只放边界。这是 M1 零帧时间影响的保证，也是计划与实际实现唯一偏离处。
- **兼容性**：`flags` bit0=0 的旧 8 省产物跳过摘要、如实统计为 legacy（实测 sealed=0 / legacy=64 / mismatch=0）；**不顶 `NMBIN_VERSION`、不逼 8 省重抽**。回源确认 `autobuild.c:prov_check()` 只看源 PBF 指纹 + `binfmt_version`，摘要字节变化不触发重转。
- **判据 6 条**：`SCHEMA-OK`（14 组合夹具 + 64 个真实文件）、`SCHEMA-INJECT-CAUGHT`（offset/name/width/missing 四种界内错误）、`NMBIN-HASH-OK`（3 条 FIPS 向量含百万字节流式 + 写入/校验全链）、`NMBIN-HASH-INJECT-CAUGHT`（flip/pad/truncate/wronghash）、`NMBIN-LEGACY-OK`、`graph_check --verify-schema`。负向模式先验证干净夹具 PASS 再验证注入 FAIL（双向判据）。
- **零回归实测**：`git archive HEAD`（M1 前 `b021e9f`）导出到 `/tmp` 独立编译基线 exe，与 M1 后 exe 用同一份隔离 ini（`day_night=0`）与绝对路径 `--graph`，z12/z14/z17 三档 `bmpdiff.py` 均 **0 / 1024000 像素**，审计计数 `diff` 无差异。★ 这比用 `build/skedaddle`（11-03 陈旧二进制）对拍可靠，且两个 exe 的 `../fonts` 都指向各自仓库根的同一份 git 归档字体。
- **讲义**：`docs/teaching/03_binary.md` 的 5 个 C 逐字块按当前源码区间（`binfmt.h:18-106 / 145-163`、`binfmt.c:19-106 / 361-431 / 272-356`）回源重贴并逐字校验一致，6 处行号引用更新，加 M1 演进补记；`docs/teaching/11_gates.md` 由 `regen_tutorial_blocks.py` 重生成（regress.sh 926→940 行）。
- ★ 坑：C 注释里 `NF_*/EF_*/XL2_*` 的 `*/` 提前闭合块注释 ⇒ 首轮编译 1641 error。统一改写成「NF、EF、XL2 系列」，并用脚本扫全部改动文件 `*/` 后是否还有残留代码。
