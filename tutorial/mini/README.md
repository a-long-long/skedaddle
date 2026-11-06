# `tutorial/mini/` — 从最小框架长到最终代码的演化链

> 这条链回答一个问题：**教程是不是「从最小框架开始，逐步完善，而后到最终代码」？**
> 2026-11-06 之前的答案是「只沾一半」：讲义从第 1 课起就逐字贴最终 `src/` 源码，
> `tutorial/NN_*` 是 17 个各自独立自包含的小程序，末课也没有 `mini/ → src/` 的合流点。
> 本目录把缺的那一半补上：**一份工程、18 个连续版本、17 个相邻 diff、末课一张合流对照表**。

## 一、三份资料各管什么（不要混）

| 资料 | 形态 | 回答的问题 | 是不是同一份代码在长大 |
|---|---|---|---|
| `docs/teaching/NN_*.md` | 18 篇源码导读讲义 | 最终 `src/` 为什么这么写 | 否，第 1 课起就是成品源码 |
| `tutorial/NN_*/` | 17 个独立小程序 | 单个概念怎么跑起来 | 否，各目录自包含，只共享 `tutorial/common.h` |
| **`tutorial/mini/steps/`** | **18 个连续快照** | **同一份工程怎么逐课长大** | **是，且由门禁逐行验收** |

三者**并存互补**，不是互相替换：讲义讲"为什么"，独立小程序练"单点动手"，
演化链看"怎么从一行画布长成一帧地图 + 两个前端"。

## 二、硬规则：只增不减

```
第 0 步  main.c + step00.h                         → 一块像素缓冲、画线、BMP
第 1 步  上面全部文件（逐字不变） + step01.h      → 追加坐标系
 ...
第 17 步 上面全部文件（逐字不变） + step17.h      → 追加 JNI 外壳
```

- 每步的 `main.c` **只在** `@@INCLUDE@@` / `@@CALL@@` 两个锚点前各插入一行；
  任何旧行都不许删改。
- 除 `main.c` 外，上一步的每个文件在下一步里**逐字相同**。
- 每步恰好新增一个 `stepNN.h`（header-only 模块），因此文件数从 2 单调长到 20。
- `scripts/check_mini_chain.py` 用「旧文件的行序列是新文件行序列的**子序列**」
  独立验收这条不变量，并用 5 类注入破坏做负向对照 —— 只跑通正常路径不算验收。

## 三、怎么走

```bash
# ① 从最小框架开始
cd tutorial/mini/steps/step00_frame
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini

# ② 看这一课相对上一课真正改了什么
cat ../../diffs/00_to_01.diff

# ③ 进入下一步，重复
cd ../step01_coords && gcc ... && ./mini
```

每一步都会：

1. 跑 **0..N 全部课程**的函数（不是只跑当前课），printf 出 `MINI-STEP-00-OK` …
   `MINI-STEP-NN-OK` —— 所以第 17 步的输出里前 17 课都还在链上；
2. 写出 `mini_out.bmp`（320×240，纯内存缓冲 + 内建 24 位 BMP，零 SDL/Qt 依赖）；
3. 末行打印 `MINI-CHAIN-OK steps=N+1 ink=… gates=…/0`。
   `ink` 是非背景像素数、`gates` 是内置判据通过/失败数，两者都是确定性量，
   **不用时间当判据**（项目既有纪律）。

第 17 步额外打印 `MINI-FINAL-OK`，指向合流点 [`MAP.md`](MAP.md)。

## 四、课程映射（编号与讲义 / 小程序同号）

| 课 | 分类 | 快照目录 | 讲义 | 这一课往工程里加了什么 |
|---|---|---|---|---|
| 0 | 核心代码 | `step00_frame` | `00_overview` | 像素缓冲、Bresenham、24 位 BMP、判据宏 |
| 1 | 核心代码 | `step01_coords` | `01_coords` | 1e7 度量化、墨卡托投影、经纬网 |
| 2 | 核心代码 | `step02_pbf` | `02_pbf` | varint / 字段号解析、节点锚点 |
| 3 | 核心代码 | `step03_binary` | `03_binary` | magic + section 表、按 tag 定位 |
| 4 | 核心代码 | `step04_graph` | `04_graph` | 前向星建图、度数与可达性、路网 |
| 5 | 核心代码 | `step05_viewport` | `05_viewport` | 可见 bbox、屏幕投影、瓦片键 |
| 6 | 核心代码 | `step06_raster` | `06_raster` | 线段裁剪、扫描线填充、alpha 混合 |
| 7 | 核心代码 | `step07_pipeline` | `07_pipeline` | 一帧的 9 阶段管线编排 |
| 8 | 核心代码 | `step08_search` | `08_search` | 地名子串检索与命中锚点 |
| 9 | 核心代码 | `step09_routing` | `09_routing` | Dijkstra、路径回溯、最短路 |
| 10 | 核心代码 | `step10_navvoice` | `10_navvoice` | 机动指令、15s 同文本冷却、车头 |
| 11 | 核心代码 | `step11_gates` | `11_gates` | 判据框架 + 负向对照能失败 |
| 12 | 核心代码 | `step12_capstone` | `12_capstone` | 改一个点 → 加判据 → 负向对照闭环 |
| 13 | 核心代码 | `step13_datum` | `13_datum` | 四个大地基准与三参数平移 |
| 14 | 核心代码 | `step14_i18n` | `14_i18n` | 中文 msgid 查表回退、XML 转义 |
| 15 | 跨平台代码 | `step15_platform` | `15_platform` | 平台抽象层（全链唯一允许平台宏处） |
| 16 | 跨平台代码 | `step16_qt` | `16_qt` | Qt 外壳 + ARGB8888 的 alpha=0xFF 修正 |
| 17 | 跨平台代码 | `step17_android` | `17_android` | JNI 外壳（第二个前端）/ 演化链终点 |

**分类与顺序沿用两段式重排**：先核心代码 00–14，最后跨平台代码 15–17。
`mini/` 不改讲义编号、不改 `tutorial/NN_*` 小程序、不改课程表 —— 见
[`docs/teaching/index.md`](../../docs/teaching/index.md) 与
[`docs/teaching-plan.md`](../../docs/teaching-plan.md)。

## 五、源、生成物与门禁

```
tutorial/mini/
├── _parts/          【源，人写】main.c.tpl + 18 个 stepNN.h + chain.json
├── steps/           【生成物】18 个完整可编译快照 + NOTE.md
├── diffs/           【生成物】17 个相邻源码 diff
├── README.md        本页
└── MAP.md           末课合流点：mini 模块 ↔ src/ 模块逐项对照
```

```bash
python3 scripts/regen_mini_chain.py --check    # 生成物是否过期（过期 rc=1）
python3 scripts/check_mini_chain.py            # 全链验收
python3 scripts/check_mini_chain.py --self-test # 5 类负向对照，证明门禁能失败
```

`check_mini_chain.py` 与 `check_tutorial_tracks.py` 一样**未接进** `scripts/regress.sh`：
避免改变 87/0 基线与 Windows 副机行为，改完 `mini/` 手动跑一次。

## 六、与真实工程的关系（先把话说准）

`mini/` **不是** `src/` 的祖先、子集或分支，而是**同一套概念骨架的教学投影**：
第 0 课那块缓冲、第 7 课那 9 个阶段、第 16 课那次 alpha 修正，都对应真实工程里
确实存在的落点；但真实代码吃的是 8 省 PBF / disp tile、跑 Qt 事件循环、过 87 条回归。

**「最后到最终代码」这一步由 [`MAP.md`](MAP.md) 完成**：18 行逐模块对照 +
差距清单 + 对照阅读方法。那里明确写了 mini 少了什么、真实版多做了什么，
不含糊地说成「mini 编译出来就是 skedaddle」。
