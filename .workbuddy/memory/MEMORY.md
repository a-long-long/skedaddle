# skedaddle 项目长期约定（精要）

> 完整细则（推导/参数/来源/11-12 L2 实现细节）见同目录 `CONVENTIONS-FULL.md`；逐日过程看 `2026-*.md`、PLAN.md、PROGRESS.md。★=踩过坑，★★★=反复踩。

## 定位
- ★★★ **L1 云端飞轮型明确不追**（实时路况/红绿灯倒计时/高精车道级/AI 助手/用户贡献），别写进 TODO；依据 `CONSUMER-NAV-GAP-2026-11-12.md` + README「定位」。L2 本地可补、L3 按定位取舍。架构 = 构建期固化数据 + 单机离线内核，运行时 mmap。竞品「同名不同物」8 词别误读。

## 构建 / 验收
- `sources/` 只放 `*.osm.pbf`；`products/` 全可删、`sync_maps.sh` 重建（同时重建全国共用 `places_auto.db`）。★★★ `skedaddle_graph_extract --out=` 必须与脚本 `OUT_ROOT`(=`products/graph`) 逐字一致；删重复前 `md5sum`+查脚本依赖。tile 化失败致命无兜底，唯一显示源 disp tile；`--graph=` 一次一省；`osmid.bin` 停生成/停校验。
- ★ Linux(UOS aarch64/Wayland) Release：`source env.sh && cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/along/.local/qt5-dev/usr -DQt5_DIR=/home/along/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5 && cmake --build build_rel -j$(nproc)`；protoc-c 缺库 `export LD_LIBRARY_PATH=/home/along/.local/usr/lib/aarch64-linux-gnu`。`build_release.sh` 仅 Windows。无头 `QT_QPA_PLATFORM=offscreen`；交互 GUI 不认 `--lat/--lon`（仅离屏认）。
- ★★★ Windows/MSYS2 编译期 PATH 绝不能放 Qt bin（cc1plus 加载旧 libstdc++ ⇒ 退出码1、stderr 空）；Qt bin 仅运行期。Qt6 DLL `/d/Qt/6.11.2/mingw_64/bin`，`libprotobuf-c-1.dll` 在 ucrt64/bin。包装器空 stderr + 重试全败 ≠ Defender，绕开用 `c++ -fsyntax-only`；LTO 并行链接偶败 `-j1`。发布包 `rm -rf` 被拦 ⇒ `--clean-first` + 旧 dist 改名 + `--no-rebuild`。
- 平台差异只许在 `src/utils/plat.{h,c}`；Qt5/Qt6 用 `QT_VERSION`（`QDesktopWidget`→`QScreen`）。★★ 删文件/删 include 前 grep 全部引用。C 注释别写 `src/x/*.cpp`/`products/*.db`（⇒ -Wcomment ⇒ -Werror）；C++ include C 头须 `extern "C"`。★★★ 新设置项四处都改：`SETTINGS_SCHEMA`+`app_init`+`app_persist_settings`+`app_apply_settings`。
- `regress.sh`：**2026-11-12 实跑 109/0**（87/0 是 11-11 旧口径）。★ `SKEDADDLE_BUILD_DIR=$PWD/build_rel` 指新构建（缺省 `build/` 陈旧 ⇒ 假绿）；编译零警告；`--ui-test` 隔离 ini；完整回归会写脏 `view_*` ⇒ 跑前备份 `~/.config/skedaddle/settings.ini`、跑完恢复；跑完 commit+push。改 `regress.sh` 后必跑 `regen_tutorial_blocks.py`（无 --check）→ 再 `--check`。★ Edit 改讲义翻 CRLF ⇒ --check 假红，生成器再跑一次归一化。
- 副机全量还需运行期 PATH 含 `dist/skedaddle-release`（缺 ⇒ rc=127）+ Qt bin + `ucrt64/bin`；缺 7 省数据的 FAIL 非回归。

## 判据纪律（最高价值）
- ★★★ 负向对照**必须能失败**；判成功看退出码不看输出行数；手挑样本下结论必翻车。判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。「绘制数变了」先切加载路径再谈数据（整省 nmbin < PBF 解码 < disp tile 跨界重复 +2.3%）。
- ★★ 修前二进制别拷出 `build_rel/`（字体按 exe 相对 `../fonts` 解析）；基线/新图一律用发布包 exe。浮层半透明后精确配色计数失效 ⇒ 数混合后颜色；UI 改版让硬编码坐标失效 ⇒ 坐标是活资产（`--rect=740,0,60,430`）；视觉模型看 BMP 不可靠；手写 BMP 裁剪注意 BGR 序。
- ★ 傍晚后（11 月北京 ~17:10 日落）用全新 ini 固定 `day_night=0`（缺省=2 自动判夜 ⇒ 全屏发灰）。★★★ 实验参数与 `--ui-test` 必须隔离 `SKEDADDLE_SETTINGS` 全新 ini。
- ★★★ 教程行号审计：报的「漂移」回源按真实函数 span 核验，别盲改；历史补记别写 `file:N`。离屏出图必须从仓库根跑（POI/地名按 cwd 相对找 ⇒ 从 dist/ 跑白字全没、假 DIFF）。

## 字体 / Qt GUI / 渲染边界
- 字体链 `sk_font_candidates()`：内置 `fonts/` Noto + DroidSansFallback(CJK 兜底) → 系统；`SKEDADDLE_FONT_DIR` 优先。`notocjk.otf` 是 OTTO/CFF，stbtt 读不了。★ `paint_frame` 的 `if(a->font)` 是文字门控：加载失败 ⇒ 文字静默丢弃、几何照常。
- ★★★ 画布缓冲 alpha 必置 0xFF（`raster_rgb` 只写 RGB）：Wayland ARGB8888 上传 ⇒ alpha=0「一片花」。修在 `map_canvas renderNow` 后 OR 0xFF000000；Android JNI 每帧同理。
- ★★ `setChecked()` 发 `toggled` ⇒ 递归 SIGSEGV，同步勾选用 `QSignalBlocker`；`isVisible()` 对未 show() 恒 false ⇒ `!isHidden()`。自绘子类用 `dynamic_cast`；改 `.hpp`/`cp -p` 后 `touch` 对应 `.cpp`；C++ 用 C 数学须 `<cmath>`+`std::floor`。
- ★ 无边框：`FramelessWindowHint` 必须在 show() 前，`SKEDADDLE_FRAMELESS=0` 回退；退出 Ctrl+Q/Alt+F4；只留顶部 10px 透明拖动带。拖动 eventFilter 不吞 MouseButtonPress、只吞 dragging 中 move；xdotool 点击分步+sleep。★★ `showMaximized()` 对 frameless ⇒ HIDDEN ⇒ 自记 `geometry()` → `setGeometry(可用区)` → 还原状态机；Wayland 顶层 `move()` 无效。不要 QToolBar，按钮一律画布子浮层；`QGraphicsOpacityEffect` 连文字一起淡 ⇒ 弃用。
- 交互唯一 `skedaddle`（SDL 已移除）；离屏唯一 `src/ui/offscreen_shot.c`；审计走 C 核心缓冲 ⇒ 不可删 `chrome.c`/`paint.c`/`navdraw.c`（共享 `render/audit.{c,h}`）。★ `--shot=` 不调 `win.start()` ⇒ 只有 chrome 空白底（非 bug）；验地图用 `--shot-offscreen`。销毁 renderer 前 `font_drop_textures()`，不能每帧调。
- ★★ 放大丢字：bbox 跨度比 <0.8/>1.25 重查 + 缓存 zoom；只在同进程连续缩放复现。中低 zoom 区名大字仅 z<14（`font_open_default(font_size*2)`+白描边；别复用 `a->font_big` 无 CJK ⇒ 方框）；路名 z12 主干/z14 次干/z16 支路。负向锚 `SKEDADDLE_LABEL_HALO=0`/`FONT_SHARP=1.0`/`POI_R=4`。
- ★ POI 四件套：`show_poi` 关 ⇒ kind 11 全隐不占避让格；`sub` 0~5 分类（旧库回退 `0 AS sub`）；半径 z<15→4 / z15~16→5 / z≥17→6 随 `LCMD_POI.R` 下发；`poi_source` 0 自带/1 `products/poi_ext.csv`（`SKEDADDLE_POI_EXT`，init 载一次）。★★ 外部 POI 原本不进 bigram ⇒ 搜不到，`search_add_external()` 补运行时索引。

## 算路 / 导航 / 语音
- ★ `pref_*` 三档只改搜索代价 `g`（`edge_cost_pref()`，软惩罚 0.6），不碰 `total_s`/ETA；`h` 物理时间下界 ⇒ admissible。ETA 只留 `navsim_set_eta_calibrator()` 挂 `navsim_remaining_s()`，缺省 NULL 恒等。门牌 `kind 14`「只进搜索永不画」，查询含数字才召回；MSYS2 中文转 GBK ⇒ `search_cli --q-file=<UTF-8>`。
- ★★ 测速告警：建库端独立 `alarm` 表；查询端 `label_cache_query_alarms()` 复用 `LabelCache.db`（60m 吸附/1.5km 上限），旧库无表静默 0；语音端点状+边标志位共用 `alarm_priority()`。★ `inst=1000000+id`；`NavVoiceCtx` 构造一律 `= {0}`。
- ★★★ 车头朝上四接口一起修：①`mv_visible_bbox` 反投影**四角** ②`mv_pan_pixels`/`mv_zoom_at` 位移经 `R(+rot)` ③`mv_fit_bbox` 四角按 `R(-rot)` ④`paint_nav_car` 屏幕角 `heading-rot`。`*_banded` 加 `rot!=0→return -1`；指北针北向 `(-R·sin rot,-R·cos rot)`；点指北针/旋转同时清 `heading_up`。判据 `mapview_selftest`+`rot45/90_shot`；`--rot=` 须同时进 `offscreen_shot.c` 与 `qt_main.cpp` 白名单。
- `voice.c` 说什么、`plat.c:sk_tts_speak()` 发声；接线唯一 `voice_wire.c:nav_voice_frame()`；`say()` 唯一出口（15s 冷却）；到达帧先播报后置 `sim_paused`；判发声 `SKEDADDLE_TTS_DRYRUN=<文件>`。`datum.c`：CGCS2000 平移 0；北京1954=EPSG:4214 默认 TOWGS84；西安1980=全国 3 参数(−104.57,−45.94,+5.30)；分区参数已否决。

## 11-12 L2 能力（细则见 FULL）
- 三档 profile / 全局单位制 / 结构化地址+逆地理编码 / 最近地点历史 / 等高线全链 / 扩省流程已落地，正向判据各配可失败的负向注入；`SKEDADDLE_BUILD_DIR` 已贯穿 Python 判据。★ graph 只含 `drivable=1` ⇒ 完整步行/骑行需重抽 PBF；本机**未下载新省 PBF / 真实 DEM**，只交付流程与门禁。★ NMBIN 头手工序列化：`rec_count` 在 `@20`。

## 教程三轨 / 门禁
- ★ 三轨并存：`docs/teaching/NN_*.md`（最终 src 导读）、`tutorial/NN_*/`（独立小程序）、`tutorial/mini/`（同一工程 18 快照+17 diff）；前两轨不是同一代码长大。核心 00–14 / 跨平台 15–17。`check_tutorial_tracks.py` 按排序取首个目录 ⇒ 重编号残留旧目录会假 FAIL。
- `mini/_parts/` 唯一人写源；`regen_mini_chain.py`（`--check` 过期 rc=1）+ `check_mini_chain.py`（未接 regress）守编译+5 类负向对照。★ C 注释里 `NN_*/` 炸 -Werror；`steps/stepNN_slug/` 回 `docs/` 是 4 层 `../../../../`；负向测 RUN 要改被检查的数据。
- ★ 讲义逐字块由 `regen_tutorial_blocks.py` 生成（16_qt+11_gates）；`check_teaching_sync.py`（未接 regress）守第 7 课 2 块+区间+演进补记/index 索引；源码改致区间平移会红 = 回源重贴信号，别直接改数字。`check_doc_numbers.sh` 是 644 ⇒ 手动跑；`check_layers.sh --self-test` 用夹具注入 map→render 验门禁。11-12 删 `font_draw_halo` 后 labels.c 行号 -18、window.c +18，讲义已回源重贴。

## 分层 / 别再试（verdicts only）
- DAG 零环 `utils←pbf←map←route←render←ui←ui_qt`（search/settings 横切）；`--start-group` 已移除 ⇒ 反向依赖立刻 undefined-reference。★ `ui_qt` 禁止直接 include `route/`/`search/`，走 `ui/window.h` 门面。查符号归属别只 grep 前缀（`mapset.h` 的 `render/labels.h` 承重 NamePool）。
- 渲染否决：简化几何/去描边/面 tile 索引/顶点内联/软件预取/合并折线/NMBIN v3/瓦片缓存条带复用；贵在间接寻址非 stride。★★★ 换语言 C→Rust 不是杠杆：`-O3`/`-march=native` 零收益 ⇒ 瓶颈在内存访问与每要素固定开销。LOD 设置项 `lod_area`(2~12 缺省 4；字面值 4 双处锚定)。
- ★★★ 分带并行（d8ed879）：`band.{c,h}`+`SKEDADDLE_BANDS`，四阶段与串行逐字节一致 1.7~1.9×。四陷阱：跨带线段先对画布裁剪再逐像素滤带（重跑 Bresenham 漂 1px）；路分桶余量只 ±3 行；带早退判据须比分桶宽 4 行；路名锚点钳到端带按 seq 归并。
- ★★★ 标注并行两坑：①CJK 字形顶超 hhea ascent ⇒ 行盒用 `font_vbox()`=max(metrics,0.9×ptsize)；②共享字形哈希 8 线程 `glyph_get` 锁热点 21→129ms ⇒ flush 前主线程 `font_warm_text` 预热、worker 走 `font_draw_a_pre` 无锁只读。
- ★★ 算路否决 rev.bin v2 / PQ 叉数≠4 / 状态表改布局；`RouteOpts` 指名初始化，地标 >30 无收益。一次 `nmbin_open` 全部 disp tile 耗尽 Win 映射句柄 ⇒ 逐 tile 开→拼→关。新增 `Buf` 设 `.esz`；`sqlite3_open` 前 `sk_mkdir_p`；`rm` 是 safe-delete ⇒ 真删 `/bin/rm`；`core.autocrlf=true` ⇒ Python 写用 rb/wb 或 `newline=''`。
- ★ D161 中国 POI 不合并；补全国走 `sync_maps.sh` 加省 PBF。SHP content 长度=rlen*2。性能/覆盖/Android 等口径（勿跨口径相减）见 `CONVENTIONS-FULL.md`。
