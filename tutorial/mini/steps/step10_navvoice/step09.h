/* tutorial/mini · 第 9 课新增：最短路（Dijkstra on 前向星）
 *
 * 对照 src/route/plan_search.c、src/route/astar.c。真实工程是跨省分层 +
 * 地标 A* + rev.bin；mini 把「权重 → 松弛 → 回溯路径」这条主干留下来。
 */
#ifndef MINI_STEP09_H
#define MINI_STEP09_H

static inline int mini_route(int s, int t, int *path, int cap, int *total) {
    int head[MINI_GN], to[MINI_GE], w[MINI_GE], next[MINI_GE];
    int dist[MINI_GN], prev[MINI_GN], used[MINI_GN];
    mini_graph_build(head, to, w, next);
    for (int v = 0; v < MINI_GN; v++) {
        dist[v] = 0x3FFFFFFF; prev[v] = -1; used[v] = 0;
    }
    dist[s] = 0;
    for (int it = 0; it < MINI_GN; it++) {
        int best = -1;
        for (int v = 0; v < MINI_GN; v++)
            if (!used[v] && (best < 0 || dist[v] < dist[best])) best = v;
        if (best < 0 || dist[best] == 0x3FFFFFFF) break;
        used[best] = 1;
        for (int e = head[best]; e >= 0; e = next[e]) {
            int u = to[e], nd = dist[best] + w[e];
            if (nd < dist[u]) { dist[u] = nd; prev[u] = best; }
        }
    }
    if (dist[t] == 0x3FFFFFFF) { *total = -1; return 0; }
    int tmp[MINI_GN], k = 0, v = t;
    while (v >= 0 && k < MINI_GN) { tmp[k++] = v; v = prev[v]; }
    int n = 0;
    for (int i = k - 1; i >= 0 && n < cap; i--) path[n++] = tmp[i];
    *total = dist[t];
    return n;
}

static void mini_step_09(MiniCtx *c) {
    int path[MINI_GN], total = -1;
    int n = mini_route(0, 3, path, MINI_GN, &total);
    MINI_ASSERT(c, total == 20, "step09: 0→3 最短路代价 = 20（非直达 50）");
    MINI_ASSERT(c, n == 3 && path[0] == 0 && path[2] == 3,
                "step09: 路径回溯端点正确");
    MINI_ASSERT(c, path[1] == 1, "step09: 途经 1 号点（10+10 < 10+20+15）");
    /* 追加：把最短路画成亮蓝折线，压在灰色路网之上 */
    for (int i = 0; i + 1 < n; i++) {
        int a = path[i], b = path[i + 1];
        mini_line(c, g_mini_gx[a], g_mini_gy[a], g_mini_gx[b], g_mini_gy[b], 0x001F6FB0u);
    }
    printf("MINI-STEP-09-OK  cost=%d hops=%d ink=%ld\n", total, n - 1, mini_ink(c));
    c->step_count++;
}

#endif /* MINI_STEP09_H */
