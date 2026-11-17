/* skedaddle_dmap_selftest：验证 PBF 显示层**后台异步**解码（不阻塞主线程）。
 *
 * 判据（数据驱动，不是"看着没卡"）：
 *   1) set_view 单次耗时远低于一省实际解码耗时（北京 ~1.6s）——证明解码在
 *      后台线程进行，主线程只做"派发 / 发布"，不进重活；
 *   2) 轮询期间观测到进度从 0 爬升到 100（pbfmap_decode 的进度回调生效）；
 *   3) 20s 内该省 has_features 置 1 且 lines 记录数 > 0（解码确实完成且产出正确）。
 *   4) 解码在途时把视口挪出该省：`loaded` 必须仍为 1、nodes 映射必须还在；
 *      解码结束后推迟的卸载必须兑现（loaded=0、nodes 已 unmap）——钉住
 *      "src_unload 提前返回但调用方照样置 loaded=0" 这条回归（2026-11-16）。
 *
 * 用法: skedaddle_dmap_selftest [<graph_dir> [<pbf_dir> [<省名>]]]
 *   默认 graph_dir=products/graph，pbf_dir=sources；省名缺省时自动挑第一个
 *   在 pbf_dir 里能找到 <省>.osm.pbf 的省。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map/mapset.h"
#include "map/pbfmap.h"   /* pbfmap_live_bytes：解码缓冲记账（泄漏判据用） */
#include "utils/plat.h"   /* sk_now_ms / sk_sleep_ms */

/* ---- 泄漏判据的辅助（判据 6）------------------------------------------
 * 思路：pbfmap.c 里有一个"已 malloc 未释放"的字节计数器（pbfmap_live_bytes），
 * 解码产物三块缓冲谁 malloc 谁记账、pbfmap_free 等额冲账。于是"有没有漏"
 * 可以写成 `== 0` 的硬条件，而不是靠 RSS 阈值瞎猜。
 *
 * 两种退出姿势都要覆盖（两种各漏一处，2026-11-16 一起修）：
 *   kind=0「解码完成后再退出」：命中 src_close —— 它原来根本不碰 s->dmap，
 *          退出一省就漏一省显示层。
 *   kind=1「解码途中退出」  ：命中 dmap_worker 的让出口 —— 那里把 free 条件
 *          写反了（`if (!ok)`），恰好漏掉成功的产物。
 *
 * 返回 0 通过 / 1 失败 / 2 跳过（数据不全、没等到窗口）。 */
static int leak_case(const char *graph, const char *pbfdir, const char *prov_name,
                     int kind, const char *tag)
{
    /* 基线：调用方可能还持着自己的 MapSet（活缓冲），判据只对本例的增量负责。 */
    long long base = pbfmap_live_bytes();
    MapSet m;
    memset(&m, 0, sizeof(m));
    if (mapset_load(&m, graph) != 0) { printf("SKIP  %s：图集加载失败\n", tag); return 2; }
    if (mapset_enable_pbf_display(&m, pbfdir, 3) != 0) {
        mapset_free(&m);
        printf("SKIP  %s：pbf_dir 不可用\n", tag);
        return 2;
    }
    int p = -1;
    for (int i = 0; i < m.n; i++)
        if (strcmp(m.srcs[i].name, prov_name) == 0) { p = i; break; }
    if (p < 0) { mapset_free(&m); printf("SKIP  %s：图集里没有 %s\n", tag, prov_name); return 2; }
    MapSrc *t = &m.srcs[p];
    int32_t vb[4] = { t->bbox[0], t->bbox[1], t->bbox[2], t->bbox[3] };

    mapset_set_view(&m, vb);            /* 进省 ⇒ 派发后台解码 */
    uint64_t t0 = sk_now_ms();
    int hit = 0, last_pct = 0;
    while (sk_now_ms() - t0 < 20000) {
        mapset_set_view(&m, vb);        /* 帧循环：publish + 派发 */
        last_pct = mapset_dmap_progress(&m, p);
        if (kind == 0) {
            if (t->has_features) { hit = 1; break; }        /* 解码已完成并挂上 */
        } else {
            /* 在途窗口：已开工（进度>0）且没结束（<100） */
            if (t->dmap_inflight && last_pct > 0 && last_pct < 100) { hit = 1; break; }
        }
        sk_sleep_ms(2);
    }
    if (!hit) {
        mapset_free(&m);
        printf("SKIP  %s：20s 内没等到%s（进度 %d%%）\n", tag,
               kind == 0 ? "解码完成" : "解码在途窗口", last_pct);
        return 2;
    }

    long long before = pbfmap_live_bytes();
    mapset_free(&m);                    /* 就是用户关窗那一刻 */
    long long after = pbfmap_live_bytes();

    if (kind == 0 && before - base <= 0) {
        /* 完成态下应有活缓冲；没增量说明这个省压根没解出东西，判据空转 */
        printf("SKIP  %s：解码完成但没有产出缓冲（data 全空）\n", tag);
        return 2;
    }
    if (after != base) {
        printf("ASSERT-FAIL  %s：退出后多出 %.1f MB 解码缓冲没释放"
               "（退出前本例占 %.1f MB，全都该在 mapset_free 里收干净）\n",
               tag, (double)(after - base) / 1048576.0,
               (double)(before - base) / 1048576.0);
        return 1;
    }
    printf("  泄漏判据通过  %-34s 本例占 %.1f MB → 退出后 0.0 MB\n",
           tag, (double)(before - base) / 1048576.0);
    return 0;
}

/* 挑 pbf 文件最大的省：解码最久 ⇒ "在途退出"的窗口最宽（mongolia 62MB ≈ 4s）。 */
static const char *pick_biggest_pbf(const MapSet *ms, const char *pbfdir)
{
    const char *best = NULL;
    long long bestsz = -1;
    for (int i = 0; i < ms->n; i++) {
        char pb[4200];
        snprintf(pb, sizeof(pb), "%s/%s.osm.pbf", pbfdir, ms->srcs[i].name);
        FILE *f = fopen(pb, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long long sz = ftell(f);
        fclose(f);
        if (sz > bestsz) { bestsz = sz; best = ms->srcs[i].name; }
    }
    return best;
}

int main(int argc, char **argv)
{
    const char *graph  = (argc > 1) ? argv[1] : "products/graph";
    const char *pbfdir = (argc > 2) ? argv[2] : "sources";

    MapSet ms;
    memset(&ms, 0, sizeof(ms));
    if (mapset_load(&ms, graph) != 0) {
        fprintf(stderr, "load fail: %s\n", graph);
        return 2;
    }
    if (ms.n == 0) { fprintf(stderr, "无省份\n"); return 2; }

    int prov = -1;
    /* 省名可给（argv[3]）；默认挑**第一个真能在 pbf_dir 找到 <省>.osm.pbf 的省**。
     * 原来写死 beijing：数据不全的环境（sources 里没有 beijing 的 pbf）会让
     * 本自检直接跑成"解码失败"，把数据缺失误报成代码问题（2026-11-16）。 */
    const char *want = (argc > 3) ? argv[3] : NULL;
    if (want) {
        for (int i = 0; i < ms.n; i++)
            if (strcmp(ms.srcs[i].name, want) == 0) { prov = i; break; }
        if (prov < 0) { fprintf(stderr, "图集里没有省 %s\n", want); return 2; }
    } else {
        for (int i = 0; i < ms.n; i++) {
            char pb[4200];
            snprintf(pb, sizeof(pb), "%s/%s.osm.pbf", pbfdir, ms.srcs[i].name);
            FILE *f = fopen(pb, "rb");
            if (f) { fclose(f); prov = i; break; }
        }
        if (prov < 0) {
            fprintf(stderr, "pbf_dir=%s 下没有任何 <省>.osm.pbf，无法测 PBF 解码\n", pbfdir);
            return 2;
        }
    }
    MapSrc *s = &ms.srcs[prov];

    if (mapset_enable_pbf_display(&ms, pbfdir, 3) != 0) {
        fprintf(stderr, "enable_pbf_display 失败：%s 下需有 <省>.osm.pbf\n", pbfdir);
        return 2;
    }

    /* 本自检专测 PBF 后台解码这一条路。历史上需在此手动把 has_disp_tiles 清 0
     * 才能关掉 disp tile 路径：enable_pbf_display 只清 nmbin 显示层、set_view 的
     * tile 加载不看 pbf_display_mode，导致双路赛跑、判据 flaky（2026-11-02，连跑
     * 两次一次「进度爬升=1」一次「=0」）。该 bug 已在 mapset.c:set_view 修复——
     * pbf_display_mode=1 时 tile 加载被 !pbf_display_mode 互斥抑制——故此处不再
     * 需要手动清。保留注释以便追溯；若进度爬升又变 flaky，先查 set_view 的互斥
     * 条件是否被改坏。 */

    /* 放大视口边距，确保该省被判定为"视口内"并触发异步派发 */
    int32_t m  = (s->bbox[2] - s->bbox[0]) / 2;
    int32_t mn = (s->bbox[3] - s->bbox[1]) / 2;
    int32_t eb[4] = { s->bbox[0] - m, s->bbox[1] - mn,
                      s->bbox[2] + m, s->bbox[3] + mn };

    /* 第一次 set_view：派发后台解码任务 */
    mapset_set_view(&ms, eb);

    double   max_dt      = 0;
    int      saw_prog    = 0;
    int      reached_100 = 0;
    uint64_t t0 = sk_now_ms();
    int      done = 0;

    while (sk_now_ms() - t0 < 20000) {
        uint64_t ta = sk_now_ms();
        mapset_set_view(&ms, eb);                /* 主线程只派发/发布，不解码 */
        double dt = (double)(sk_now_ms() - ta);
        if (dt > max_dt) max_dt = dt;

        int p = mapset_dmap_progress(&ms, prov);
        if (getenv("DMAP_TRACE")) {
            fprintf(stderr, "  [trace] t=%llums p=%d has_features=%d loaded=%d "
                    "disp_tiles=%d pbf_mode=%d\n",
                    (unsigned long long)(sk_now_ms() - t0), p, s->has_features,
                    s->loaded, s->has_disp_tiles, s->pbf_display_mode);
        }
        if (p > 0 && p < 100) saw_prog = 1;
        if (p >= 100) reached_100 = 1;
        if (s->has_features) { done = 1; break; }
        sk_sleep_ms(2);     /* 让后台线程跑 */
    }
    if (s->has_features) done = 1;

    printf("dmap: prov=%s loaded=%d has_features=%d 进度爬升=%d 到100=%d "
           "max_set_view=%.1f ms\n",
           s->name, s->loaded, s->has_features, saw_prog, reached_100, max_dt);

    int bad = 0;
    if (max_dt > 250) {
        printf("ASSERT-FAIL  set_view 单次 %.1f ms 过长（应远小于 1.6s 解码耗时，证明未阻塞主线程）\n",
               max_dt);
        bad++;
    }
    if (!saw_prog) { printf("ASSERT-FAIL  未观测到进度爬升（pbfmap_decode 进度回调未生效？）\n"); bad++; }
    if (!done)     { printf("ASSERT-FAIL  20s 内未解码完成\n"); bad++; }
    if (s->has_features && s->lines.hdr.rec_count == 0) {
        printf("ASSERT-FAIL  解码完成但 lines 记录数为 0\n"); bad++;
    }

    /* ---- 判据 5（2026-11-16 修）：解码在途时视口离开，不得谎报 loaded=0 ----
     * 旧 bug：src_unload() 因 dmap_inflight 提前返回（worker 正在读 nodes，
     * 不能 munmap），调用方却照样 `loaded = 0`。后果不是"多个标记"这么轻：
     *   ① 文件还映射着却报"已卸载" ⇒ 下次视口进省又 src_open_files 一遍，
     *      在已打开的映射上重开（旧映射泄漏、base 变）；
     *   ② 更致命的是 src_open_files 里那句无条件的 `dmap_inflight = 0` 把 pin
     *      拆了，此后任意一次 src_unload 就会在 worker 仍在读 nodes 时 munmap。
     * 这里两步钉死：在途+离视口 ⇒ loaded 仍为 1、nodes 仍映射；解码结束（publish）
     * ⇒ 推迟的卸载兑现：loaded=0、nodes 已 unmap。 */
    {
        int32_t far_vb[4] = { 800000000, 800000000, 800000100, 800000100 };  /* 空海域，与省无关 */

        /* ① 正常离省：此刻没有在途解码 ⇒ 必须真卸载。 */
        mapset_set_view(&ms, far_vb);
        if (s->loaded || s->nodes.data) {
            printf("ASSERT-FAIL  离开视口后未卸载（loaded=%d nodes=%p）\n",
                   s->loaded, (void *)s->nodes.data);
            bad++;
        }

        /* ② 回省：重新打开 + 派发异步解码（1.6s 级，几毫秒内必在途）。 */
        mapset_set_view(&ms, eb);
        if (!s->loaded) {
            printf("ASSERT-FAIL  视口回省后未重新打开（loaded=0）\n"); bad++;
        }
        if (!s->dmap_inflight) {
            /* 机器快到解码已在本次 set_view 内完成（无锁同步路径）：
             * 本条依赖"在途"窗口，跳过而不误判。 */
            printf("SKIP  解码未在途（已同步完成），跳过「在途卸载」判据\n");
        } else {
            /* ③ 在途时立刻把视口挪走：src_unload 必须**推迟**，loaded 保持 1。 */
            mapset_set_view(&ms, far_vb);
            if (!s->loaded) {
                printf("ASSERT-FAIL  解码在途时离省：loaded 被置 0（文件其实还映射着——本次修的 bug）\n");
                bad++;
            }
            if (!s->nodes.data) {
                printf("ASSERT-FAIL  解码在途时 nodes 映射被提前 unmap（worker 会 UAF）\n");
                bad++;
            }
            /* ④ 等解码结束；publish 在 set_view 顶部，继续喂 far_vb 即可结算。 */
            uint64_t t1 = sk_now_ms();
            while (sk_now_ms() - t1 < 20000 && s->dmap_inflight) {
                mapset_set_view(&ms, far_vb);
                sk_sleep_ms(2);
            }
            mapset_set_view(&ms, far_vb);        /* 让 publish 把这笔账结掉 */
            if (s->dmap_inflight) {
                printf("ASSERT-FAIL  20s 内解码未结束\n"); bad++;
            }
            if (s->loaded || s->nodes.data) {
                printf("ASSERT-FAIL  解码结束后推迟的卸载未兑现（loaded=%d nodes=%p）\n",
                       s->loaded, (void *)s->nodes.data);
                bad++;
            }
        }
        /* ★ 收尾**不回省**：回省又会派发一次解码，判据 6 的"退出后应为 0"
         *   就会把这个外层 MapSet 的活缓冲算进去（实测凭空多出 1.1 MB）。
         *   这里保持"视口在省外、本省已卸载"的干净状态。 */
    }

    /* ---- 判据 6（2026-11-16）：退出时不得泄漏已解码的成功结果 ----
     * 一省显示层是十几~几十 MB 的 hand-malloc 缓冲：它既不在 mmap 里，也不归
     * nmbin_close 管，漏掉一处 free 只是"内存慢慢涨"，进程照跑、别的断言全绿。
     * 这里两种退出姿势各跑一遍（见 leak_case 注释），判据是硬条件
     * `pbfmap_live_bytes() == 0`（解码产物记账见 pbfmap.c）。 */
    {
        const char *big = pick_biggest_pbf(&ms, pbfdir);
        int rc = leak_case(graph, pbfdir, s->name, 0, "退出前解码已完成（src_close）");
        if (rc == 1) bad++;
        if (big) {
            rc = leak_case(graph, pbfdir, big, 1, "解码途中退出（dmap_worker 让出）");
            if (rc == 1) bad++;
        } else {
            printf("SKIP  找不到任何 <省>.osm.pbf，跳过「解码途中退出」判据\n");
        }
    }

    printf(bad ? "DMAP-FAIL\n" : "DMAP-OK\n");

    /* ★ 必须先收尾再 return：`ms` 是 main 的**栈变量**，worker 线程此刻可能
     *   还在 pbfmap_decode 里，若直接 return，栈帧销毁后它回调
     *   dmap_progress_cb(ud=&ms) 就会去锁已释放的 dmap_mtx ⇒ 退出期 SIGSEGV。
     *   实测：日志里 DMAP-OK 齐全、进程 rc=139（一度被误当成"通过"，因为判据
     *   只 grep 了标记行）。mapset_free 会置 dmap_quit 并等 worker 停下——
     *   解码带协作式中止钩子后毫秒级就停；返回 -1（理论走不到）就再等再试，
     *   绝不带着仍在跑的 worker 返回。 */
    fflush(stdout);
    while (mapset_free(&ms) != 0)
        sk_sleep_ms(50);

    return bad ? 1 : 0;
}
