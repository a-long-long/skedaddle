# 第 4 课 · 建图：PBF → 路网（CSR）
> 分类：核心代码

> 📖 配套读源码讲义：[`docs/teaching/04_graph.md`](../../docs/teaching/04_graph.md)（读源码式讲解，讲清楚“为什么这么写”；与本科小程序同号对应）


**累加内容**：从"原始 OSM 要素"到"可导航图"。本课用一小撮手写节点 + way 演示建图全过程：
① 节点原始 id → 0..n-1 下标映射；② 每条 way 相邻节点对 → 双向边；③ 边代价 = haversine 米。
最终存成 CSR（压缩稀疏行：offset + edge list），这是 A* 高效遍历的内存布局。

**运行**：
```bash
gcc -std=c11 -O2 -o tut main.c && ./tut
```

**你会看到**：stdout 打出 CSR 邻接表（每个节点的出边 + 代价），`out.bmp` 是 5 节点路网
（蓝方块=节点，红线=边）。

**关键概念（项目里一致）**：
1. **最小导航集**：真实建图只抽 `highway=*`、`railway`、通行标志等，不抽全部要素。
2. **CSR 存储**：邻接表用 `offset[] + edge[]` 压缩，A* 遍历只需 `for(e=off[u];e<off[u+1];e++)`，
   缓存友好、零指针跳跃。
3. **限制（restriction）**：`relation` 里的 `type=restriction` 在建图阶段就抽成 `restrictions.bin`，
   路由时查 `restr_blocked` 判定禁转。

**生产最终形态**：`src/map/road_tags.c`（`road_tags_parse` 把 way 的 key/val 变成 `road_class`/
`drivable`/`oneway`/`speed_kmh`/`lanes`…），`src/map/graph_build.c`（两遍扫描：挑可通行 way →
节点下标映射 → 建 CSR 边 + 限制）。

**练习**：把 `way[][4]` 再加一条路（如 `{7002,7005,7003}`），重跑，确认邻接表与图同步更新。

**生产补记（2026-11-15）**：建图不只看 `highway=*`，还要把 tag 值翻译对。

- `maxspeed` 现在识别单位：`50 mph → 80 km/h`、`30 knots → 56 km/h`、`50 km/h → 50 km/h`；
  旧实现用 `atoi("50 mph")` 会把 50 当 km/h，全球数据下会低估速度。
- 省目录 slug 统一为 Geofabrik 当前值：西藏 `xizang`、香港 `hongkong`；
  旧目录 `tibet` / `hong-kong` 由 `coverage.c` 做别名归一，`*-latest` 后缀也会在查表前剥掉。
- 下载窗的“已下载”判定同时识别 `<leaf>` 与 `<leaf>-latest` 产品目录，避免 `macau-latest` 这类目录漏认。

**练习**：给 `road_tags_parse()` 加一个 `maxspeed="65 mph"` 断言，期望 `104~105 km/h`；再把单位去掉，确认回落到道路 class 的缺省速度。
