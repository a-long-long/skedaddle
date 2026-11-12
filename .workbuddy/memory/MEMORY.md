# skedaddle 项目长期约定（精要 v2）

> 细则/推导/参数/来源一律看同目录 `CONVENTIONS-FULL.md`（97 行，按主题分节，含 L2 与 NMBIN M1 全文）；
> 逐日过程看 `2026-*.md`、PLAN.md、PROGRESS.md；NMBIN 施工看仓库根 `NMBIN-OPTIMIZATION-PLAN-2026-11-12.md`。
> ★=踩过坑，★★★=反复踩。本文件只留「每次动手前必须想起」的条目。

## 定位 / 架构
- ★★★ **L1 云端飞轮型能力不追**（实时路况/红绿灯倒计时/高精车道级/AI 助手/用户贡献），别写进 TODO；L2 本地可补、L3 按定位取舍。依据 `CONSUMER-NAV-GAP-2026-11-12.md` + README。竞品「同名不同物」8 词别误读。
- 构建期固化 + 单机离线：`sources/*.osm.pbf` → `sync_maps.sh` → `products/graph/<省>/`（`disp.idx+lanes.bin+t_*`），运行时按需 mmap；搜索库 `places_auto.db` 全国共用。

## 固定动作（构建 / 产物 / 回归）
- ★ Linux UOS aarch64 Release：`source env.sh && cmake -S . -B build_rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/along/.local/qt5-dev/usr -DQt5_DIR=/home/along/.local/qt5-dev/usr/lib/aarch64-linux-gnu/cmake/Qt5 && cmake --build build_rel -j$(nproc)`；protoc-c 缺库 `export LD_LIBRARY_PATH=/home/along/.local/usr/lib/aarch64-linux-gnu`。`build_release.sh` 仅 Windows；无头 `QT_QPA_PLATFORM=offscreen`。
- ★★★ `skedaddle_graph_extract --out=` 必须与脚本 `OUT_ROOT`(=`products/graph`) 逐字一致；`products/` 全可删由 `sync_maps.sh` 重建；tile 化失败致命无兜底，唯一显示源 disp tile；`--graph=` 一次一省；`osmid.bin` 已停生成/停校验。
- ★★★ Windows/MSYS2 编译期 PATH 绝不能放 Qt bin（空 stderr + 退出码 1）；Qt bin 仅运行期。包装器空 stderr ≠ Defender ⇒ `c++ -fsyntax-only` 看真错误。发布包 `rm -rf` 被 safe-delete 拦 ⇒ `--clean-first` + 旧 dist 改名 + `--no-rebuild`。
- `regress.sh`：**2026-11-12 M2 后实跑 125/0**（87/0、109/0、115/0 均为历史口径，每加判据记新总数）。★ `SKEDADDLE_BUILD_DIR=$PWD/build_rel`（缺省 `build/` 陈旧 ⇒ 假绿）；编译零警告（`-Werror`）；完整回归写脏 `view_*` ⇒ 跑前备份 `~/.config/skedaddle/settings.ini`、跑完恢复；跑完 commit+push。改 `regress.sh` 后必跑 `regen_tutorial_blocks.py`（无 --check）→ 再 `--check`（Edit 改讲义翻 CRLF ⇒ 生成器再跑一次归一化）。
- 副机全量还需运行期 PATH 含 `dist/skedaddle-release`（缺 ⇒ rc=127）+ Qt bin + `ucrt64/bin`；缺 7 省数据的 FAIL 非回归。

## 改动纪律（最容易翻车）
- ★★ 删文件/删 include 前 grep 该符号/该头**全部引用**；C 注释别写 `src/x/*.cpp`、`products/*.db`、`NF_*/EF_*`（`*/` 提前闭合块注释 ⇒ -Werror，本轮曾 1641 error）；C++ include C 头须 `extern "C"`。平台差异只许在 `plat.{h,c}`，Qt5/Qt6 用 `QT_VERSION`。
- ★★★ 新设置项四处都改：`SETTINGS_SCHEMA` + `app_init` + `app_persist_settings` + `app_apply_settings`。
- DAG 零环 `utils←pbf←map←route←render←ui←ui_qt`（search/settings 横切）；`--start-group` 已移除 ⇒ 反向依赖立刻 undefined-reference；`ui_qt` 禁直连 `route/`/`search/`，走 `ui/window.h` 门面。`check_layers.sh --self-test` 可验门禁能失败。
- `core.autocrlf=true` ⇒ Python 写用 rb/wb 或 `newline=''`；`rm` 是 safe-delete ⇒ 真删用 `/bin/rm`。

## 判据纪律（最高价值）
- ★★★ 负向对照**必须能失败**（改「被检查的数据」，别把条件改常量 0）；判「成功」看退出码不是输出行数；手挑样本下结论必翻车。判据用展开数/里程/像素，时间只兜底取多轮最小；渲染取第二帧、算路丢第 1 次。
- ★★★ 实验参数与 `--ui-test` 必须隔离 `SKEDADDLE_SETTINGS` 全新 ini；傍晚后用 `day_night=0`（缺省自动判夜 ⇒ 全屏发灰）。
- ★★ 修前二进制别拷出 `build_rel/`（字体按 exe 相对 `../fonts` 解析）；基线/新图一律用发布包 exe；离屏出图必须从仓库根跑。零回归对拍用 `git archive HEAD` 独立编译 + `bmpdiff.py`。
- 「绘制数变了」先切加载路径再谈数据（整省 nmbin < PBF 解码 < disp tile 跨界重复 +2.3%）；浮层半透明后精确配色计数失效；UI 改版让硬编码像素坐标失效（坐标是活资产）；视觉模型看 BMP 不可靠。
- ★★★ 教程行号审计报「漂移」必须回源按真实函数 span 核验，别盲改；历史补记别写 `file:N`。

## 关键实现锚点（细节看 FULL）
- 画布缓冲 alpha 必置 0xFF（Wayland ARGB8888 上传）；`if(a->font)` 是文字门控；`setChecked()` 发 `toggled` ⇒ 用 `QSignalBlocker`；`showMaximized()` 对 frameless ⇒ HIDDEN ⇒ 自记 geometry 状态机。
- 交互唯一 `skedaddle`、离屏唯一 `offscreen_shot.c`；`--shot=` 不调 `win.start()` ⇒ 空白底非 bug，验地图用 `--shot-offscreen`。
- 车头朝上四接口一起修（`mv_visible_bbox` 四角、`mv_pan_pixels`/`mv_zoom_at` 反旋转、`mv_fit_bbox` 四角、`paint_nav_car` 角 `heading-rot`）；指北针北向 `(-R·sin rot,-R·cos rot)`。
- POI 四件套（`show_poi` / `sub` 0~5 / 半径按 zoom 分档 / `poi_source` 外部 CSV）；外部 POI 须 `search_add_external()` 才进搜索。
- NMBIN M1（11-12，`f16cf8a`）：14 种 `(kind,rec_size)` 期望 schema 加载期比对；`flags` bit0 + 手工头 `@76` 的 `sha256(记录区)`；**全量哈希绝不进加载期**（破坏按需 mmap），只放 `nmbin_verify_digest()` 边界；旧 8 省 flags=0 跳过 ⇒ 不顶版本、不重抽。C 注释 `NF_*/EF_*` 的 `*/` 坑见上。
- NMBIN M2（11-12，O3+O4+O5）：`gen_chunks.py` 生成 `<省>/chunks.json`（4 KiB 块哈希 + 全文件 + 记录区 + 头内 O2 摘要），`sync_maps.sh` 在 `graph_check` 后生成，coverage 门禁守「块表齐全」；纯 JDK 的 `ChunkDelta`/`ChunkUpdater` 实现 HTTP Range 差分，`Downloader.downloadVerified()` 做整包哈希；`java_gate.sh` 用 javac + 本地 HttpServer 夹具实测（服务端侧计数：改 1 块只传 4096 B / 全等 0 B / 脏块被拒）。★★ 三个哈希口径别串台（zip 整包 / 单文件全哈希 / O2 记录区）；块表 8 省 **71 MB** ⇒ 需 gzip+ETag，M2 不宣称净流量收益。旧 8 省 flags=0 ⇒ `nmbin_digest` 空。Android 真机未验证。
- 11-12 L2：三档 profile / 单位制 / 地址层 / 历史 / DEM 等高线 / 扩省流程已落地；graph 只收 `drivable=1` ⇒ 完整步行骑行需重抽；本机**无真实 DEM / 新省 PBF**，只交付流程与门禁。
- 教程三轨并存（docs/teaching、tutorial/NN、tutorial/mini），**不是**同一份代码长大；`mini/_parts/` 唯一人写源；`regen_mini_chain.py --check`、`check_mini_chain.py`、`check_tutorial_tracks.py`、`check_teaching_sync.py`、`check_doc_numbers.sh`（644 ⇒ 手动跑）。

## 别再试（verdicts only，理由看 FULL）
- 渲染：简化几何/去描边/面 tile 索引/顶点内联/软件预取/合并折线/NMBIN v3/瓦片缓存条带复用；贵在间接寻址非 stride。★★★ 换语言 C→Rust 不是杠杆（`-O3`/`-march=native` 零收益）。
- 算路：rev.bin v2 / PQ 叉数≠4 / 状态表改布局；地标 >30 无收益。分带并行与标注并行已落地（两处共享缓存坑看 FULL）。
- D161 中国 POI 不合并；补全国走 `sync_maps.sh` 加省 PBF。性能/覆盖/Android 口径勿跨口径相减。
