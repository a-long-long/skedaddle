/* tutorial/mini · 第 4 课新增：建图（PBF 节点/边 → 前向星邻接表）
 *
 * 对照 src/map/graph_build.c、src/map/road_tags.c。mini 用 4 点 5 边的玩具图，
 * 但「按起点挂链」的数据结构与真实 rev.bin / lanes.bin 的构建思路一致。
 */
#ifndef MINI_STEP04_H
#define MINI_STEP04_H

#define MINI_GN 4
#define MINI_GE 5

static const int g_mini_eu[MINI_GE] = { 0, 1, 2, 0, 1 };
static const int g_mini_ev[MINI_GE] = { 1, 2, 3, 3, 3 };
static const int g_mini_ew[MINI_GE] = { 10, 20, 15, 50, 10 };
static const int g_mini_gx[MINI_GN] = { 60, 130, 205, 270 };
static const int g_mini_gy[MINI_GN] = { 190, 165, 205, 120 };

static inline void mini_graph_build(int *head, int *to, int *w, int *next) {
    for (int v = 0; v < MINI_GN; v++) head[v] = -1;
    for (int e = 0; e < MINI_GE; e++) {
        int u = g_mini_eu[e];
        to[e] = g_mini_ev[e];
        w[e] = g_mini_ew[e];
        next[e] = head[u];
        head[u] = e;
    }
}

static inline int mini_graph_deg(const int *head, const int *next, int v) {
    int d = 0;
    for (int e = head[v]; e >= 0; e = next[e]) d++;
    return d;
}

/* 从 s 出发可达的节点数（显式栈 DFS） */
static inline int mini_graph_reach(const int *head, const int *to, const int *next, int s) {
    int seen[MINI_GN];
    int st[MINI_GN], top = 0, cnt = 0;
    for (int i = 0; i < MINI_GN; i++) seen[i] = 0;
    st[top++] = s; seen[s] = 1;
    while (top) {
        int v = st[--top]; cnt++;
        for (int e = head[v]; e >= 0; e = next[e]) {
            int u = to[e];
            if (!seen[u]) { seen[u] = 1; st[top++] = u; }
        }
    }
    return cnt;
}

static void mini_step_04(MiniCtx *c) {
    int head[MINI_GN], to[MINI_GE], w[MINI_GE], next[MINI_GE];
    mini_graph_build(head, to, w, next);
    int reach = mini_graph_reach(head, to, next, 0);
    MINI_ASSERT(c, mini_graph_deg(head, next, 0) == 2, "step04: 前向星挂链度数");
    MINI_ASSERT(c, reach == MINI_GN, "step04: 从 0 号点可达全图");
    MINI_ASSERT(c, w[0] == 10 && w[4] == 10, "step04: 边权随链保存");
    /* 追加：把图按屏幕坐标画成路段（真正往缓冲上累加内容） */
    for (int e = 0; e < MINI_GE; e++) {
        int u = g_mini_eu[e], v = g_mini_ev[e];
        mini_line(c, g_mini_gx[u], g_mini_gy[u], g_mini_gx[v], g_mini_gy[v], 0x008A8A8Au);
    }
    for (int v = 0; v < MINI_GN; v++)
        mini_fill_rect(c, g_mini_gx[v] - 2, g_mini_gy[v] - 2, 5, 5, 0x002A2A2Au);
    printf("MINI-STEP-04-OK  nodes=%d edges=%d ink=%ld\n", MINI_GN, MINI_GE, mini_ink(c));
    c->step_count++;
}

#endif /* MINI_STEP04_H */
