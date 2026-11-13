#ifndef SKEDADDLE_AUTOBUILD_H
#define SKEDADDLE_AUTOBUILD_H

#include "map/mapset.h"

/* ---------------------------------------------------------------------------
 * 启动自愈：源 pbf 还在，产物却没了/坏了 —— 那就当场重建，别让人去记命令。
 *
 * 为什么需要它：src/ 里从来没有 system/popen/exec，程序是纯加载。实测把
 * products/graph/tianjin 删掉后，离屏通道只打印一句"既不是省目录也没有
 * nodes.bin"就空跑，skedaddle_graph_check 直接 ASSERT-FAIL —— 没有任何自愈能力。
 * 而转换本身是确定性的（删掉重建后 nodes.bin 的 md5 与删前完全一致），
 * 所以"检测到缺失就重转"是安全的，不存在"重建出不一样的东西"的风险。
 *
 * 判据刻意与 scripts/sync_maps.sh 完全一致（同一套 manifest 指纹），
 * 避免出现"脚本说该转、程序说不转"的分歧。五条：
 *   1. 产物目录不存在
 *   2. 缺必需文件（manifest.txt / nodes.bin / edges.bin）
 *   3. nodes.bin 或 edges.bin 打不开、或 nmbin_check 不过（截断/损坏）
 *   4. manifest 里的 binfmt_version 与当前二进制格式版本不符
 *   5. manifest 里的 src_bytes / src_mtime 与源 pbf 实际值不符（源被换过了）
 *
 * 安全性：只起项目自带的可执行文件（同目录 → build/ → PATH 依次探测），
 * 参数固定、不经 shell，省名先过白名单（不允许 '/' 和 '.'/'..'），
 * 所以不存在命令注入或路径逃逸。进程派生/等待走平台层 sk_spawn_wait
 * （POSIX fork+execv，Windows CreateProcess）。清理旧产物用平台层的
 * sk_unlink / sk_rmtree 递归删除，不依赖外部 rm 命令。
 *
 * 图集级旁路产物（2026-11-13 补）：省产物之外还有三样共享产物，缺了会静默
 * 退化而不是报错 —— 搜索恒空、算路展开数多一个数量级、Android 差分拿不到清单：
 *   · products/places_auto.db（+ .meta）  地名库，搜索与地名标注用
 *   · products/graph/lm.bin（+ .meta）     A* 地标下界表，可选但退化明显
 *   · products/graph/<省>/chunks.json（+ gz） M2 块表，桌面运行时零读取
 * 旧版 rebuild_one() 只调 graph_extract，这三样一概不管；而 graph_extract 本身
 * 也不产块表（那是 sync_maps.sh 的活）⇒ 自动重建出来的省一定是"半套产物"。
 *
 * ★ 判据刻意**不复刻** sync_maps.sh 的 md5 指纹、lmfp、meta 下标那套算法：
 *   两份实现必然漂移，会出现"脚本说该转、程序说不转"。autobuild 只用两个
 *   确定性事件 —— (a) 本次有任何省被重建 ⇒ 三样必然过期，无条件全建；
 *   (b) 产物自身缺失/打不开/为空 ⇒ 建。这样程序只会在更保守的方向动作，
 *   且 sync_maps.sh 仍是构建期唯一可信的指纹源。LM_NLM / cell 这类常量由
 *   scripts/check_autobuild_sidecars.py 守与脚本一致，防止改一边忘另一边。
 *
 * ★ chunks 是可选 sidecar：桌面发布包不带 python3 与 scripts/，缺工具时
 *   明确警告并降级（不计失败）；桌面运行时一个字节都不读它，Android 差分
 *   另行生成。找得到 python3 + gen_chunks.py 时则硬保证成套。
 * ------------------------------------------------------------------------- */

typedef struct {
    int         enable;         /* 1 = 检测到就重建（默认）；0 = 只诊断不动手 */
    const char *src_dir;        /* 源 pbf 目录。NULL = 自动探测（graph_dir 往上找 sources/） */
    const char *tool;           /* skedaddle_graph_extract 路径。NULL = 自动探测 */
    const char *py;             /* python3 路径。NULL = 自动探测 PATH；测试可注入 */
    const char *chunks_script;  /* gen_chunks.py 路径。NULL = 从 src_dir 推仓库根；测试可注入 */
    const char *lm_tool;        /* skedaddle_lmbuild_cli 路径。NULL = 自动探测；测试可注入。
                                 * 全图地标表要 8 省路网 + 全图 Dijkstra，本机内存
                                 * 跑不动；夹具里用桩工具验证编排，真实工具由
                                 * sync_maps.sh 承担（生产路径永远自动探测）。 */
} AutoBuildOpt;

typedef struct {
    int    checked;   /* 检查了几个省 */
    int    ok;        /* 本来就完好的 */
    int    rebuilt;   /* 重建成功的 */
    int    failed;    /* 重建失败的（源损坏或转换器报错） */
    int    nosrc;     /* 产物不可用但源 pbf 也没有，想建也建不了 */
    int    skipped;   /* 需要重建但被 --no-autobuild 关掉了 */
    /* 图集级旁路产物 */
    int    sidecars_checked;   /* 本次是否真的跑了旁路产物检查（0/1） */
    int    sidecars_rebuilt;   /* 成功补齐的项数（places / lm / chunks 各算 1） */
    int    sidecars_failed;    /* 补齐失败的项数 */
    int    sidecars_skipped;   /* 需要补齐但被 --no-autobuild 关掉的项数 */
    int    sidecars_degraded;  /* chunks 因缺 python3 / 脚本而降级（桌面运行时零读取） */
    double elapsed_ms;
} AutoBuildStat;

/* 体检并在需要时重建。返回 0 = 过程本身没出错（不保证每个省都可用，
 * 看 stat->failed / stat->nosrc）。opt 传 NULL 表示"开启 + 全部自动探测"。 */
int  autobuild_repair(const char *graph_dir, const AutoBuildOpt *opt,
                      AutoBuildStat *st_out);

/* 便捷包装：先自愈再加载。st_out 可为 NULL。 */
int  mapset_load_repair(MapSet *ms, const char *graph_dir,
                        const AutoBuildOpt *opt, AutoBuildStat *st_out);

#endif
