/* skedaddle_history_selftest —— 最近地点历史的确定性判据 + 负向对照
 *
 * 正常模式：MRU 顺序 / 同名同坐标去重 / 容量淘汰 / 落盘回读 / 坏行跳过 /
 *           格式化，全部用固定时间戳 ⇒ 跨机器完全一致。⇒ HISTORY-OK，rc=0。
 *
 * --inject=stale：故意在「最后一条点选」**之前**就落盘（模拟丢了一次写入），
 *   回读后仍按完整历史断言 ⇒ 判据必须抓到队首不对。抓到打印
 *   HISTORY-INJECT-CAUGHT / rc=0；没抓到 rc=1 让门禁变红。
 *
 * 注意：日志里不要出现 "ASSERT-FAIL" 字样（regress.sh 的 run 会当失败）。
 */

#include "search/history.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_fails;

static void eq(const char *tag, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) return;
    printf("MISMATCH %-26s 得到[%s] 期望[%s]\n", tag, got, want);
    g_fails++;
}

static void eqi(const char *tag, int got, int want)
{
    if (got == want) return;
    printf("MISMATCH %-26s 得到[%d] 期望[%d]\n", tag, got, want);
    g_fails++;
}

static const char *PATH = "/tmp/skedaddle_history_selftest.txt";

static void check_basic(void)
{
    History h;
    history_init(&h, PATH);
    eqi("n empty", h.n, 0);
    eqi("add A", history_add(&h, "中关村", 39.98, 116.31, 11, 100), 0);
    eqi("add B", history_add(&h, "西二旗", 40.05, 116.30, 11, 101), 0);
    eqi("add C", history_add(&h, "国贸",   39.91, 116.46, 11, 102), 0);
    eqi("n 3", h.n, 3);
    eq("MRU 0", h.v[0].name, "国贸");
    eq("MRU 1", h.v[1].name, "西二旗");
    eq("MRU 2", h.v[2].name, "中关村");

    /* 同名同坐标 ⇒ 提到队首，不新增；时间戳更新 */
    eqi("dedup add", history_add(&h, "中关村", 39.98, 116.31, 11, 200), 0);
    eqi("dedup n", h.n, 3);
    eq("dedup front", h.v[0].name, "中关村");
    eqi("dedup ts", (int)h.v[0].ts, 200);
    /* 同名不同坐标 ⇒ 算两个地点 */
    eqi("diff coord", history_add(&h, "中关村", 39.99, 116.32, 11, 201), 0);
    eqi("diff coord n", h.n, 4);

    char b[256];
    eqi("format rc", history_format(&h, 0, b, sizeof(b)), 0);
    printf("formatted    %s\n", b);
    eqi("format bad idx", history_format(&h, 99, b, sizeof(b)) == -1, 1);
    eqi("format null", history_format(&h, 0, b, 0) == -1, 1);

    /* 容量淘汰：加满后始终只留 HISTORY_MAX 条，最新的在队首 */
    for (int i = 0; i < HISTORY_MAX + 8; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "P%02d", i);
        history_add(&h, nm, 39.0 + i * 0.001, 116.0, 11, (uint64_t)(300 + i));
    }
    eqi("cap n", h.n, HISTORY_MAX);
    eq("cap front", h.v[0].name, "P39");
}

static void check_roundtrip(int inject)
{
    remove(PATH);
    History a;
    history_init(&a, PATH);
    history_add(&a, "甲地", 39.90, 116.40, 1, 1000);
    history_add(&a, "乙地", 39.91, 116.41, 2, 1001);
    if (inject) {
        /* 注入：在最后一次点选之前就落盘 ⇒ 文件里没有「最近点」。 */
        history_save(&a);
        history_add(&a, "最近点", 39.92, 116.42, 3, 1002);
    } else {
        history_add(&a, "最近点", 39.92, 116.42, 3, 1002);
        history_save(&a);
    }

    History b;
    history_init(&b, PATH);
    eqi("load rc", history_load(&b), 0);
    eqi("load n", b.n, 3);
    eq("load front", b.v[0].name, "最近点");
    eq("load 1", b.v[1].name, "乙地");
    eq("load 2", b.v[2].name, "甲地");
    eqi("load ts", (int)b.v[0].ts, 1002);

    /* 坏行必须跳过、好行还在（写一行缺字段的垃圾） */
    FILE *f = fopen(PATH, "ab");
    if (f) { fputs("这不是一条历史记录\n", f); fclose(f); }
    History c;
    history_init(&c, PATH);
    eqi("bad line rc", history_load(&c), -1);
    eqi("bad line n", c.n, 3);
    eq("bad line front", c.v[0].name, "最近点");

    history_clear(&c);
    eqi("clear n", c.n, 0);
    eqi("save after clear", history_save(&c), 0);
    History d;
    history_init(&d, PATH);
    eqi("reload empty rc", history_load(&d), 0);
    eqi("reload empty n", d.n, 0);
    remove(PATH);
}

int main(int argc, char **argv)
{
    int inject = (argc > 1 && strcmp(argv[1], "--inject=stale") == 0);
    check_basic();
    int basic_fails = g_fails;
    check_roundtrip(inject);
    int rt_fails = g_fails - basic_fails;

    if (inject) {
        if (basic_fails == 0 && rt_fails > 0) {
            printf("HISTORY-INJECT-CAUGHT 丢写入被回读判据抓到（%d 处）\n", rt_fails);
            return 0;
        }
        printf("HISTORY-INJECT-MISSED 判据没有分辨力（basic=%d rt=%d）\n",
               basic_fails, rt_fails);
        return 1;
    }
    if (g_fails > 0) {
        printf("HISTORY-FAIL %d 处不一致\n", g_fails);
        return 1;
    }
    printf("HISTORY-OK MRU/去重/淘汰/落盘回读/坏行跳过/清空\n");
    return 0;
}
