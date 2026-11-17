# 第 9 课 · 路由算法（双向加权 A* 的单体版）
> 分类：核心代码

> 📖 配套读源码讲义：[`docs/teaching/09_routing.md`](../../docs/teaching/09_routing.md)（读源码式讲解，讲清楚“为什么这么写”；与本科小程序同号对应）


**累加内容**：跨省双向加权 A* 的核心思想，用一个小图演示。
- 状态主键 = `coord_key_e7`（坐标打包），不是 `(省, 下标)`——跨省接边不需要映射表。
- 启发式 `h(u) = haversine(u, goal) * w`；`w` 是旋钮：w 越大越贪心、展开越少但可能次优。
- 本课用线性扫描的 open 集（节点少足够演示）；真实工程用 4 叉最小堆 `PQueue`。

**运行**：
```bash
gcc -std=c11 -O2 -o tut main.c && ./tut
```

**你会看到**：打印 start/goal 的 `coord_key`，以及 `w=1.0`（纯 Dijkstra，展开最多）/ `w=2.0`（更贪心，
展开更少）下的路径与代价；`out.bmp` 是蓝节点 + 红路径。

**关键概念（项目里一致）**：
1. **★ 坐标即主键 `coord_key_e7`**：学 OsmAnd `id=(x31<<31)|y31`，跨省接边不需要"本省→全局"映射表。
   初版用 `(省,节点)` 当主键让北京→天津展开 111 万、抖 560 次，换键后消失。
2. **加权 A\* 的 w 是旋钮**：默认 `ROUTE_HEUR_W_DEFAULT=1.4`（京→郑 1076→88ms，代价 +2.8%）；有 `lm.bin`
   时兜底 `1.25`；自适应两遍（撞 `adapt_try` 才换更重的 `1.8` 重跑）。**按概率分布调参，而非手挑路线**。
3. **没有 CH，而是 Landmark(ALT) 加速**：`lm.bin` 提供下界——但它不是严格下界，所以开 `--exact` 不再
   保证最优；验收对真值用 `SKEDADDLE_LM=0`。

**生产最终形态**：`src/route/route.c`（`router_route_ll`/`plan_search` 双向加权 A* 主循环）、
`src/route/st.c`（`StateTab`，主键 `coord_key_e7`）、`src/route/lmtable.c`（Landmark 下界）。

**练习**：把 `w` 在 1.0~2.0 之间多取几个值，画出"展开数 vs w"的曲线，观察贪心度与代价/最优性的权衡。

**生产补记（2026-11-15）**：算路不只要答案，还要保证**对象活得比搜索久**。

- `Router` 里保存的是 `&MapSet`；`app_reload_maps()` 现在先 `nav_stop=1` 并等待 `nav_busy` 归零，
  再销毁 `Router`，最后才 `mapset_free()`。否则下一次算路会读已释放的 `ms`。
- 后台算路期间 `paint_frame()` 不再调用 `mapset_set_view()` / `app_update_coverage()`：
  卸省会 `munmap` 正在被 A* 读的 `nodes/edges`，加载新省也会改 `loaded` 和文件指针。
- `rev.bin` 加载新增 `start[]` 单调性检查：尾哨兵只能证明最后一项没坏，不能证明中间项。
- 对 Rust 读者：这类问题是 C 的“所有权没有类型表达”；Rust 里对应的做法是让搜索持有
  `Arc<MapSet>` 或用生命周期把 `Router<'a>` 钉在 `MapSet` 上，重载时类型系统会逼你先处理借用。

**生产补记（2026-11-17）**：`mapset_free()` 本身也有生命周期坑——释放时后台解码
线程可能还在读 `srcs[].nodes`。现在解码带协作式中止钩子（每个 blob 检查退出
标志，毫秒级就停），`mapset_free()` 返回 `int`：超时没等到线程停就**什么都不
释放**并返回 -1（宁可漏，不可悬），调用方保留旧图集稍后重试。Rust 对照：共享
`Arc<MapSet>` 后想拿 `&mut` 释放就得 `try_unwrap` 等引用退场，等价于"等到
`dmap_stopped` 才释放"。

**练习**：把 `w` 在 1.0~2.0 之间多取几个值，画出"展开数 vs w"的曲线；再思考：如果 `Router` 改成
`Router<'a> { ms: &'a MapSet }`，`app_reload_maps()` 的签名需要怎样改？
