/* skedaddle_autobuild_sidecar_selftest —— 图集级旁路产物自愈验收
 *
 * 只调 autobuild_repair()，不加载 mapset，所以跑得很快；按 AutoBuildStat
 * 里新增的旁路产物计数做断言。判据全部是「计数」这类确定性量，不看时间、
 * 不看体积。scripts/check_autobuild_sidecars.py 用它在临时零拷贝夹具上做
 * 故障注入，regress.sh 也用它跑正常态。
 *
 * 用法：
 *   --graph=<dir>            图集目录或单省目录
 *   --sources=<dir>          源 pbf 目录（NULL 语义用 --sources= 省略）
 *   --tool=<path>            注入 skedaddle_graph_extract 路径
 *   --py=<path>              注入 python3 路径（可给不存在的路径造降级）
 *   --chunks-script=<path>   注入 gen_chunks.py 路径（夹具没有 scripts/ 时用）
 *   --lm-tool=<path>         注入地标表构建器（夹具用桩工具，见 autobuild.h）
 *   --no-build               enable=0，只诊断不动手
 *   --expect-checked=<n>     省检查数
 *   --expect-rebuilt=<n>     省重建数
 *   --expect-sc-checked=<n>  是否触发旁路检查（0/1）
 *   --expect-sc-rebuilt=<n>  成功补齐项数
 *   --expect-sc-failed=<n>   补齐失败项数
 *   --expect-sc-skipped=<n>  被 --no-build 关掉的项数
 *   --expect-sc-degraded=<n> 块表因缺工具降级（0/1）
 *
 * 任何断言不符打印 ASSERT-FAIL 并返回 1；全过打印 AUTOBUILD-SIDECAR-OK。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map/autobuild.h"

static const char *arg_val(const char *a, const char *p)
{
    size_t n = strlen(p);
    return strncmp(a, p, n) == 0 ? a + n : NULL;
}

int main(int argc, char **argv)
{
    const char *graph = "products/graph";
    const char *src = NULL, *tool = NULL, *py = NULL, *script = NULL;
    const char *lm_tool = NULL;
    int enable = 1, fails = 0;
    int want_checked = -1, want_rebuilt = -1;
    int want_sc_checked = -1, want_sc_rebuilt = -1, want_sc_failed = -1;
    int want_sc_skipped = -1, want_sc_degraded = -1;

    for (int i = 1; i < argc; i++) {
        const char *v;
        if      ((v = arg_val(argv[i], "--graph=")))          graph = v;
        else if ((v = arg_val(argv[i], "--sources=")))        src = v;
        else if ((v = arg_val(argv[i], "--tool=")))           tool = v;
        else if ((v = arg_val(argv[i], "--py=")))             py = v;
        else if ((v = arg_val(argv[i], "--chunks-script=")))  script = v;
        else if ((v = arg_val(argv[i], "--lm-tool=")))         lm_tool = v;
        else if ((v = arg_val(argv[i], "--expect-checked="))) want_checked = atoi(v);
        else if ((v = arg_val(argv[i], "--expect-rebuilt="))) want_rebuilt = atoi(v);
        else if ((v = arg_val(argv[i], "--expect-sc-checked=")))  want_sc_checked = atoi(v);
        else if ((v = arg_val(argv[i], "--expect-sc-rebuilt=")))  want_sc_rebuilt = atoi(v);
        else if ((v = arg_val(argv[i], "--expect-sc-failed=")))   want_sc_failed = atoi(v);
        else if ((v = arg_val(argv[i], "--expect-sc-skipped=")))  want_sc_skipped = atoi(v);
        else if ((v = arg_val(argv[i], "--expect-sc-degraded="))) want_sc_degraded = atoi(v);
        else if (!strcmp(argv[i], "--no-build")) enable = 0;
        else { fprintf(stderr, "未知参数 %s\n", argv[i]); return 2; }
    }

    AutoBuildOpt o;
    memset(&o, 0, sizeof(o));
    o.enable        = enable;
    o.src_dir       = src;
    o.tool          = tool;
    o.py            = py;
    o.chunks_script = script;
    o.lm_tool       = lm_tool;

    AutoBuildStat st;
    memset(&st, 0, sizeof(st));
    int rc = autobuild_repair(graph, &o, &st);

    printf("AUTOBUILD-STAT rc=%d checked=%d ok=%d rebuilt=%d failed=%d nosrc=%d "
           "skipped=%d sc_checked=%d sc_rebuilt=%d sc_failed=%d "
           "sc_skipped=%d sc_degraded=%d\n",
           rc, st.checked, st.ok, st.rebuilt, st.failed, st.nosrc, st.skipped,
           st.sidecars_checked, st.sidecars_rebuilt, st.sidecars_failed,
           st.sidecars_skipped, st.sidecars_degraded);

#define WANT(w, f, label) do {                                          \
        if ((w) >= 0) {                                                 \
            if (st.f != (w)) {                                          \
                printf("ASSERT-FAIL %s 期望 %d，实际 %d\n",             \
                       (label), (w), st.f);                            \
                fails++;                                                \
            } else {                                                    \
                printf("ASSERT-OK   %s = %d\n", (label), (w));          \
            }                                                           \
        }                                                               \
    } while (0)

    WANT(want_checked,      checked,            "省检查数");
    WANT(want_rebuilt,      rebuilt,            "省重建数");
    WANT(want_sc_checked,   sidecars_checked,   "旁路检查触发");
    WANT(want_sc_rebuilt,   sidecars_rebuilt,   "旁路补齐数");
    WANT(want_sc_failed,    sidecars_failed,    "旁路失败数");
    WANT(want_sc_skipped,   sidecars_skipped,   "旁路跳过数");
    WANT(want_sc_degraded,  sidecars_degraded,  "块表降级");
#undef WANT

    if (rc != 0) { printf("ASSERT-FAIL autobuild_repair 返回 %d\n", rc); fails++; }
    if (fails) {
        printf("AUTOBUILD-SIDECAR-FAIL %d 项断言失败\n", fails);
        return 1;
    }
    printf("AUTOBUILD-SIDECAR-OK\n");
    return 0;
}
