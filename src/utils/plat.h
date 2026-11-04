#ifndef SKEDADDLE_PLAT_H
#define SKEDADDLE_PLAT_H

/* ---------------------------------------------------------------------------
 * 平台抽象层。规则：**"换个平台就不成立"的东西只许出现在这里**。
 *
 * 收编的对象（都在这台机器之外炸过或明显会炸）：
 *   - 路径分隔符 '/' vs '\\'、盘符绝对路径
 *   - 临时目录 /tmp（Windows 没有）
 *   - 字体路径 /usr/share/fonts/...（Windows 在 %WINDIR%\Fonts）
 *   - mmap/madvise（Windows 用 CreateFileMapping）
 *   - opendir/readdir、mkdir、unlink、rmdir、lstat
 *   - /proc/self/exe（Windows 用 GetModuleFileName）
 *   - fork/execv/waitpid、kill、getpid
 *   - usleep、clock_gettime(CLOCK_MONOTONIC)
 *   - getrusage（峰值内存）
 *
 * POSIX 分支覆盖 Linux/macOS；Windows 分支覆盖 MSVC 与 MinGW。
 * ★ 平台差异 100% 收口在本抽象层：业务代码只调 sk_* 接口、零 #ifdef _WIN32，
 *   同一套源文件由编译器按 _WIN32 在编译期选分支，本地/远端无分叉。
 *   Windows 分支已在 MSYS2/UCRT64(MinGW) 实机编译验证（skedaddle_plat_selftest 全过）；
 *   POSIX 侧仍由 tools/plat_selftest.c 保证"接口契约"可断言。
 * ------------------------------------------------------------------------- */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 路径 ---------------- */

char        sk_sep(void);                    /* '/' 或 '\\' */
const char *sk_last_sep(const char *path);   /* 最后一个分隔符（没有返回 NULL） */
const char *sk_base_name(const char *path);  /* 最后一个分隔符之后 */
int         sk_is_abs(const char *path);     /* '/' 开头或盘符开头 */

/* 拼接。空串参数被当成"没有这一级"（join("", "b") == "b"）。
 * 缓冲区不够返回 -1（绝不静默截断——截断的路径会写到别的省上）。 */
int sk_path_join(char *out, size_t cap, const char *a, const char *b);

/* ---------------- 文件 / 目录 ---------------- */

int sk_is_dir(const char *path);
int sk_file_exists(const char *path);
/* 存在且可执行。Windows 没有可执行位，"存在"即算可执行
 * （能不能跑由扩展名/.exe 决定，见 sk_find_in_path）。 */
int sk_executable(const char *path);

/* 取大小（字节）与 mtime（秒）。文件不存在返回 -1。 */
int sk_stat_file(const char *path, uint64_t *size, int64_t *mtime);

int sk_mkdir(const char *path);      /* 单层 */
int sk_mkdir_p(const char *path);    /* 递归，已存在算成功 */
int sk_rmtree(const char *path);     /* 递归删除；不存在算成功 */
int sk_unlink(const char *path);
/* 独占创建并写入一小段内容（转换锁用：文件已存在即失败，不会截断别人的锁）。
 * 成功返回 0；失败（已存在/建不了/写不进）返回 -1。 */
int sk_create_exclusive(const char *path, const char *content);
/* 覆盖式改名。POSIX rename 本身就是原子替换；Windows 的 rename()
 * 在目标存在时失败，所以这里显式 MOVEFILE_REPLACE_EXISTING。 */
int sk_rename(const char *from, const char *to);

/* 只读映射整个文件，长度写入 *out_len，返回基址；失败返回 NULL。
 * 空文件返回 NULL（与旧行为一致：size<=0 不当可映射）。 */
void *sk_map_file(const char *path, size_t *out_len);
void  sk_unmap_file(void *addr, size_t len);

/* 目录枚举。返回的条目名属于 SkDir，下次 sk_dir_next 前有效；
 * "." 与 ".." 已跳过。顺序由文件系统决定，需要可复现就自己排序
 * （mapset_load / skedaddle_places_build 都先收集再 qsort）。 */
typedef struct SkDir SkDir;
SkDir      *sk_dir_open(const char *path);
const char *sk_dir_next(SkDir *d);
void        sk_dir_close(SkDir *d);

/* ---------------- 并发 ---------------- */

/* 互斥锁（跨平台）。业务代码只调 sk_mutex_*，不直接碰 pthread / CRITICAL_SECTION。
 * 单一所有权：创建者负责 sk_mutex_destroy。锁内不要做阻塞/耗时操作。 */
typedef struct SkMutex SkMutex;
SkMutex *sk_mutex_create(void);
void     sk_mutex_destroy(SkMutex *m);
void     sk_mutex_lock(SkMutex *m);
void     sk_mutex_unlock(SkMutex *m);

/* 条件变量（跨平台）。用来做"生产者-消费者"队列：等待方必须已持有
 * sk_mutex_lock，sk_cond_wait 返回时**重新持有**该锁。
 * 单一所有权：创建者负责 sk_cond_destroy。 */
typedef struct SkCond SkCond;
SkCond  *sk_cond_create(void);
void     sk_cond_destroy(SkCond *c);
void     sk_cond_wait(SkCond *c, SkMutex *m);   /* 释放锁 → 等信号 → 重获锁 */
void     sk_cond_signal(SkCond *c);              /* 唤醒一个等待者 */
void     sk_cond_broadcast(SkCond *c);           /* 唤醒全部等待者 */

/* 起一个脱离式（detached）线程跑 f(arg)。成功返回 0，失败返回 -1。
 * 线程自行结束，调用方不 join；f 内部自行处理自己的资源。 */
int sk_thread_spawn(void (*f)(void *), void *arg);

/* ---------------- 进程 / 时间 ---------------- */

/* 当前可执行文件路径（Linux /proc/self/exe，macOS _NSGetExecutablePath，
 * Windows GetModuleFileName）。失败返回 -1，调用方应退回 PATH 查找。 */
int sk_exe_path(char *out, size_t cap);
int sk_get_pid(void);
int sk_pid_alive(int pid);            /* pid<=0 一律返回 0 */

/* 在 PATH 里找可执行文件。Windows 上会补 ".exe" 再试一次。
 * 找不到返回 -1。 */
int sk_find_in_path(const char *name, char *out, size_t cap);

/* 在指定目录里找可执行文件（Windows 上补 ".exe"）。
 * find_tool 的"与本程序同目录"分支用它。找不到返回 -1。 */
int sk_which(const char *dir, const char *name, char *out, size_t cap);

/* 起一个进程并等它结束。exit_code 可传 NULL。
 * 返回 0 = 正常退出（码在 *exit_code）；-1 = 起不来/被信号杀。 */
int sk_spawn_wait(const char *exe, char *const argv[], int *exit_code);

/* ---------------------------------------------------------------------------
 * 常驻子进程 + 管道（跨平台）
 *
 * 用途：TTS 后端要**一次启动、多次喂文本**。每句都起一个进程的话，
 * Windows 上 PowerShell 启动要 0.5~1.5 秒 —— 导航播报会因此错过时机
 * （车都开过路口了声音才出来）。所以起一个常驻子进程，把它的 stdin
 * 接一条管道，逐行喂文本。
 *
 * 用法：spawn → 反复 write → close（关管道让子进程看到 EOF 并退出）。
 * 单一所有权：创建者负责 sk_proc_close。
 * ------------------------------------------------------------------------- */
typedef struct SkProc SkProc;
/* 起常驻子进程并把它的 stdin 接上管道。成功返回句柄，失败返回 NULL。
 * argv 必须以 NULL 结尾；argv[0] 是程序路径。 */
SkProc *sk_proc_spawn(const char *exe, char *const argv[]);
/* 往子进程 stdin 写一段文本（不追加换行，调用方自己决定）。
 * 返回写入字节数；失败返回 -1。 */
int     sk_proc_write(SkProc *p, const char *text);
/* 关闭 stdin 并等子进程退出，释放句柄。p 为 NULL 时什么都不做。
 * 返回子进程退出码；取不到返回 -1。 */
int     sk_proc_close(SkProc *p);
/* 子进程是否还活着。p 为 NULL 或已退出返回 0。 */
int     sk_proc_alive(SkProc *p);

uint64_t sk_now_ms(void);             /* 单调时钟，毫秒 */
uint64_t sk_now_us(void);             /* 单调时钟，微秒（基准测试要亚毫秒精度） */
void     sk_sleep_ms(unsigned ms);
double   sk_peak_rss_mb(void);        /* 峰值驻留内存（MB） */

/* ---------------- 语音播报（离线 TTS） ----------------
 * 跨平台离线发声：Windows 走 PowerShell + System.Speech（SAPI），
 * 其它平台按 PATH 探测 espeak-ng / espeak / say。真正的发声在分离线程里
 * 做（sk_thread_spawn），调用方（导航每帧推进）绝不被阻塞。
 * 设 SKEDADDLE_TTS_DRYRUN=文件路径 时只把文本写进该文件、不真正发声，
 * 用于无头自测（本机没有音频设备 / 没有 TTS 引擎时也能断言集成是否正确）。 */
int  sk_tts_speak(const char *text);          /* 返回 1=已提交(或 dryrun 已写) / 0=无引擎 */
int  sk_tts_available(void);                  /* 本机有可用引擎？（不含 dryrun） */
/* 累计被**受理**的播报句数（只读观测口，不改变任何状态）。
 * ★ 计数点在入队**之前**、调用线程内、无竞争 —— 不能读队列长度：后台
 *   朗读线程会异步喝空队列，"入队后立刻读 size()"是与活消费者赛跑
 *   （OsmAnd 的 Speech.OFFERED 踩过同一个坑）。 */
int  sk_tts_offered_count(void);
/* 累计已**交付引擎**的句数（计数点在交给引擎之后、后台线程内）。
 * 与受理数配对可断言"队列真的被消费了" —— 只看受理数证明不了 worker 在跑。
 * ★ 不等于"已播完"：常驻会话下写入管道就返回，合成还在子进程那边进行中。
 *   要等真正播完用 sk_tts_flush()。 */
int  sk_tts_delivered_count(void);
/* 等待"已受理的句子全部播完"。实现：关掉常驻会话的写端 ⇒ 子进程读到 EOF
 * ⇒ 循环退出 ⇒ 退出前会把当前句说完 ⇒ 子进程退出即精确的"播完"边界。
 * 返回 0 = 已排空并播完；-1 = 没有会话可关（无引擎）。 */
int  sk_tts_flush(void);
/* 距下一条指令 to_go_m 米时该播报的层级：0 不播 / 1 接近层(<=300m) / 2 立即层(<=60m)。
 * ★ **已弃用**：导航播报改走 route/voice.c 的"速度 × 秒数"模型
 *   （抄 OsmAnd 的 AnnounceTimeDistances），固定 300 m 在高速上来不及。
 *   保留仅为兼容外部调用方与既有自测，新代码不要用。 */
int  sk_tts_tier_for_distance(double to_go_m);

/* ---------------- 其它 ---------------- */

int        sk_strcasecmp(const char *a, const char *b);
char      *sk_strdup(const char *s);  /* 失败返回 NULL（与 strdup 一致） */
/* 临时目录：TMPDIR/TEMP/TMP 里第一个存在的，退回 /tmp，再退回 "." */
const char *sk_temp_dir(void);
/* 用户级配置目录（跨平台）：POSIX 取 $XDG_CONFIG_HOME/skedaddle 或
   $HOME/.config/skedaddle；Windows 取 %APPDATA%/skedaddle。返回的字符串属于
   ﻿本函数（静态缓冲），调用方不应 free，也无需手动建目录（settings 写盘时
   会用 sk_mkdir_p 确保存在）。统一返回正斜杠形式。 */
const char *sk_config_dir(void);
/* 环境变量覆盖式设置（setenv / _putenv_s） */
int sk_setenv(const char *key, const char *val);
/* 加可执行位。Windows 无可执行位，直接成功（存在即可执行）。 */
int sk_chmod_x(const char *path);

/* 逐平台字体候选链（宋体优先，理由见 render/font.c 的注释）。
 * 返回的数组与字符串都是静态存储，勿 free。 */
const char *const *sk_font_candidates(size_t *n);

/* 运行环境是否检测到 GPU / 图形栈。
 *   - Windows：能加载 opengl32.dll（即装了 GL 驱动）即认为有；
 *   - Linux/macOS：存在 DRI 渲染节点（/dev/dri/renderD* 或 card*）即认为有。
 * 仅供参考（本项目是软件光栅，无 GPU 渲染路径）；返回 1=有 / 0=无。 */
int sk_has_gpu(void);

/* ---------------- Android ----------------
 * Android 没有 $HOME / XDG / TMP 那套目录，也没有"可执行文件"概念；app 私有、
 * 可写、随卸载清除的目录由 Java 侧通过 context.getFilesDir()/getCacheDir()
 * 取得，并在 native 初始化时经 JNI 传进来（sk_android_set_paths）。
 * 字体随 APK 打包在 assets/fonts/，首次运行抽到 files 目录后由
 * sk_android_set_font_dir 告诉 C 核心去哪找（否则 get_font_dir 在 Android 上
 * 找不到 /usr/share/fonts 与 exe 目录，会静默落进"字体缺失"分支）。 */
#ifdef __ANDROID__
/* files_dir：context.getFilesDir()（持久、私有）；cache_dir：getCacheDir()（临时）。 */
void  sk_android_set_paths(const char *files_dir, const char *cache_dir);
/* font_dir：已从 assets 抽出的字体目录（通常是 <files_dir>/fonts）。 */
void  sk_android_set_font_dir(const char *font_dir);
/* 已设置的 files 目录（NULL = 未设置）。JNI/字体探测可用。 */
const char *sk_android_files_dir(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SKEDADDLE_PLAT_H */
