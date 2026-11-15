# 第 11 课 · 架构纪律与验收门禁
> 分类：核心代码

> 📖 配套读源码讲义：[`docs/teaching/11_gates.md`](../../docs/teaching/11_gates.md)（读源码式讲解，讲清楚“为什么这么写”；与本科小程序同号对应）


**累加内容**：前面 12 课都在“加功能”，这一课讲“为什么这个工程敢改”——因为有一整套防回归门禁。
三个核心：分层门禁 `check_layers`、回归运行器（标记 + 退出码双判据）、负向对照（判据必须能失败）。

**运行**：
```bash
gcc -std=c11 -O2 -o tut main.c && ./tut ; echo "exit=$?"
```

**你会看到**：
- 打印 `check_layers` 对“干净依赖”零违规、对“map 反向 include ui”抓出 1 处违规（带 `file:line`）。
- 一组 `ASSERT-OK`（`classify` 单调性、边界、以及负向对照）。
- 收尾 `=== REGRESS-RESULT: PASS ===` 且进程退出码 0。
- 附带 `gates.bmp`：7 条彩色横带的分层 DAG 示意图（下=地基 utils，上=ui_qt）。

**关键概念（项目里一致）**：
1. **★ 依赖是纯 DAG，反向依赖立刻被抓**：`utils ← pbf ← map ← route ← render ← ui ← ui_qt`，
   模块 i 只能 include 层号 ≤ i 的模块。真实工程里 `scripts/check_layers.sh` 解析**真实**的
   `#include`（剥离块注释与行注释——本项目注释里常出现 include 路径文本，不剥会大量假 FAIL），
   违规打印文件行号并退出非 0。链接器单趟解析（不用 `--start-group`）是第一道防线，
   `check_layers.sh` 是第二道——`--start-group` 已移除，因为它会让链接器多轮扫描悄悄补上反向依赖，边界形同虚设。
2. **★ 双判据：标记 + 退出码**：回归靠「退出码 + 标记」双判据。标记缺失会被判成假 FAIL——
   因为任何“成功”都必须显式打印 `ASSERT-OK/FAIL` 与收尾 `REGRESS-RESULT`，判据脚本只认标记不认“看起来像通过”。
3. **★★ 负向对照必须能失败**：任何判据都要注入一个“必失败”的改动验证它真的起作用。
   本课里 `classify_bug`（把 `<` 写成 `<=`）在 `classify(KNOT)==1` 上**应当 FAIL**，
   我们用 `!(classify_bug(KNOT)==1)` 这个断言确认“坏实现确实被抓”——如果连坏实现都绿，判据就是假绿。

**生产最终形态**：
- `scripts/check_layers.sh`：分层门禁（已进 `regress.sh`，用例 `layer_dirs`）。
- `scripts/regress.sh`：全量回归；基线 **78 通过 / 3 失败**（3 项环境缺失非回归）。含
  `area_lod_tier`（LOD 确定性量）、`shot/app_route/gpx_render/ovl_shot`（全走 `--shot-offscreen`）。
- `tools/blend_equiv.c` / `tools/clip_equiv.c`：自写光栅 vs SDL 软渲染逐像素等价对照体（SDL 在这里合法）。
- `scripts/bmpdiff.py`：逐字节比对两张 BMP（要素色→背景色 = 丢要素）；`scripts/ovl_px.py`：半透明比率判定。

**练习**：
1. 在 `main.c` 的 `bad[]` 里再加一条 `{"render/paint.c", 5, "render", "map"}`，重跑确认违规数变成 2。
2. 把 `classify_bug` 的 bug 去掉（改成和 `classify` 一样），重跑——你会发现负向对照那条 `ASSERT-FAIL`，
   这就证明了“负向对照本身在起作用”：判据确实能区分对错。
3. （进阶）读 `scripts/check_layers.sh`，找一条真实工程里曾因反向依赖被它拦下的改动。

**2026-11-15 生产化修复批次**：这一批不是新功能，而是把“考试题”从“正常数据能跑”推进到
“坏数据、线程退出、Windows 路径、下载目录名都不能骗过”。和门禁相关的落点：

| 修复 | C 教程落点 | 验收方式 |
|---|---|---|
| `names.bin` / `tiles.bin` / `lm.bin` / `rev.bin` 语义校验 | 第 3 课、第 9 课 | 注入坏 count / 坏 offset / 截断 / 非单调 start，必须拒绝 |
| `Router` / `MapSet` 生命周期 | 第 9 课、第 16 课 | `app_reload_maps()` 后 `router==NULL`，重新算路仍 `ROUTE-OK` |
| 算路期间冻结 `mapset_set_view()` | 第 9 课 | 算路中把视口移出省，省仍保持 loaded；算路结束后才卸载 |
| Windows 路径 / `-latest` 目录 | 第 15 课、第 16 课 | 反斜杠 graph 路径、`macau-latest` 反查 |
| `maxspeed` mph/knots | 第 4 课 | `50 mph → 80`、`30 knots → 56` |
| SDL2 可选 | 第 15 课 | 无 SDL2 配置成功，仅跳过两个对照目标 |

**最终产物验收**：`scripts/build_release.sh` 退出 0，`dist/skedaddle-release` 78 个文件、约 107 MB；
`build_rel` 控制台版与 dist GUI 版都跑过 `--shot-offscreen`，路由回归仍 `ROUTE-OK`。

**练习**：把本表任一条改成“只跑正常路径、不做负向对照”，观察它是否还能算门禁；
然后用 `check_teaching_sync.py` / `check_tutorial_tracks.py` 检查讲义是否仍与 H1/分类一致。
