#include "render/band.h"
#include "utils/plat.h"

#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* 配置                                                                */
/* ------------------------------------------------------------------ */

int band_requested(void)
{
    /* -2 = 还没读过；-1 = 未设置（自动）；0 = 关闭；>0 = 指定带数。
       缓存是因为这个函数会被每帧每阶段调用，getenv 不是零成本的。 */
    static int v = -2;
    if (v != -2) return v;
    const char *e = getenv("SKEDADDLE_BANDS");
    if (!e || !*e) v = -1;
    else v = atoi(e);
    if (v < 0 && v != -1) v = 0;
    return v;
}

int band_auto_count(void)
{
    int n = sk_cpu_cores();
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    return n;
}

int band_pick_count(void)
{
    int r = band_requested();
    if (r == 0) return 1;              /* 显式关闭 */
    if (r > 0) {
        if (r > SK_BAND_MAX) r = SK_BAND_MAX;
        return r;
    }
    return band_auto_count();
}

/* ------------------------------------------------------------------ */
/* 切带                                                                */
/* ------------------------------------------------------------------ */

int band_init(BandView *bv, const Raster *rs, int n)
{
    if (!bv || !rs) return 0;
    if (n < 1) n = 1;
    if (n > SK_BAND_MAX) n = SK_BAND_MAX;

    bv->n = n;
    bv->base = *rs;

    /* 带边界按像素行均分；最后一条 edge 对齐到 clip 的下边界，
       避免除不尽的余数把最后几行漏掉（漏行是"最下面一条缝"的经典来源）。 */
    int y0 = rs->clip.y, h = rs->clip.h;
    for (int t = 0; t <= n; t++) {
        long long y = (long long)y0 + (long long)h * t / n;
        bv->edge[t] = (int)y;
    }
    bv->edge[n] = y0 + h;

    for (int t = 0; t < n; t++) {
        bv->band[t] = *rs;
        bv->band[t].clip.y = bv->edge[t];
        bv->band[t].clip.h = bv->edge[t + 1] - bv->edge[t];
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* 阻塞式并行                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    void     (*f)(void *, int);
    void      *arg;
    int        tid;
    SkMutex   *m;
    SkCond    *c;
    int       *pending;
} BandJob;

static void band_job(void *p)
{
    BandJob *j = (BandJob *)p;
    j->f(j->arg, j->tid);
    /* 完成计数与唤醒在同一个临界区里做：主线程只要看到 pending 归零，
       就说明该 worker 已 unlock、之后不会再碰 m/c（否则 destroy 会竞态）。 */
    sk_mutex_lock(j->m);
    (*j->pending)--;
    sk_cond_signal(j->c);
    sk_mutex_unlock(j->m);
}

int band_run(BandView *bv, void (*f)(void *, int), void *arg)
{
    if (!bv || !f) return -1;
    if (bv->n <= 1) { f(arg, 0); return 0; }   /* 单带：直接串行，零开销 */

    SkMutex *m = sk_mutex_create();
    SkCond  *c = sk_cond_create();
    if (!m || !c) {
        /* 线程资源都拿不到（极少见）：退回串行，结果不变，只是慢。 */
        if (m) sk_mutex_destroy(m);
        if (c) sk_cond_destroy(c);
        for (int t = 0; t < bv->n; t++) f(arg, t);
        return 0;
    }

    /* pending 先按"每条带都要跑"设；起线程失败的带由主线程补跑并手工扣账。
       这样无论 worker 多快结束，计数都不会落空（不会死等）。 */
    int pending = bv->n - 1;
    unsigned failed = 0;
    BandJob jobs[SK_BAND_MAX];
    for (int t = 1; t < bv->n; t++) {
        jobs[t].f = f;
        jobs[t].arg = arg;
        jobs[t].tid = t;
        jobs[t].m = m;
        jobs[t].c = c;
        jobs[t].pending = &pending;
        if (sk_thread_spawn(band_job, &jobs[t]) != 0) failed |= (1u << t);
    }

    f(arg, 0);                                    /* 主线程跑第 0 带 */

    int nfailed = 0;
    if (failed) {
        for (int t = 1; t < bv->n; t++) {
            if (failed & (1u << t)) { f(arg, t); nfailed++; }
        }
        sk_mutex_lock(m);
        pending -= nfailed;
        sk_mutex_unlock(m);
    }

    /* 检查与等待同一把锁 ⇒ 不会丢唤醒（worker 若已全部结束，这里直接跳过）。 */
    sk_mutex_lock(m);
    while (pending > 0) sk_cond_wait(c, m);
    sk_mutex_unlock(m);

    sk_cond_destroy(c);
    sk_mutex_destroy(m);
    return 0;
}
