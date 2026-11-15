# 第 15 课 · 平台抽象层
> 分类：跨平台代码

> 📖 配套读源码讲义：[`docs/teaching/15_platform.md`](../../docs/teaching/15_platform.md)（读源码式讲解，讲清楚“为什么这么写”；与本科小程序同号对应）


**累加内容**：把"换个平台就不成立"的调用（休眠、时钟）100% 收口到一个 `plat.c` 里。
本课给出两个文件：`plat.h`/`plat.c`（承载 `#ifdef`）和 `main.c`（业务代码，**零 `#ifdef`**）。

**运行**：
```bash
gcc -std=c11 -O2 -D_DEFAULT_SOURCE -o tut main.c plat.c && ./tut
```
> ★ `-D_DEFAULT_SOURCE` 不能省：`-std=c11` 会关掉 glibc 的默认特性宏，
> 否则 `usleep` / `useconds_t` / `CLOCK_MONOTONIC` 全部未声明（本机实测 rc=1）。
> 加它之后 `-Wall` 零警告。这也是"平台差异收口"的一课：真正跨平台的构建参数
> 得跟着平台抽象层一起给，而不是指望编译器默认。

**你会看到**：stdout 打印画线耗时（用了 `sk_now_ms`），随后生成 `out.bmp`。关键观察是
`main.c` 里**没有任何 `#ifdef _WIN32`**——平台差异被完全隔离了。

**为什么重要（项目铁律）**：
1. **业务代码零 `#ifdef _WIN32`**：所有"换平台就不成立"的调用只在 `plat.c` 出现。这是
   可移植性 / 依赖倒置的范本——你在 Windows 和 Linux 上编同一份业务代码。
2. **链接纪律**：真实项目里 `plat.c` 的线程走 pthread，所以 `-pthread` 由 `skedaddle_plat` 库
   统一承载；漏挂会在链接时报 `pthread_create` 未定义。本课的休眠/时钟不依赖 pthread，故无需。

**生产最终形态**：`src/utils/plat.c` 里的 `SkMutex`/`SkCond`/`sk_thread_spawn`/`sk_tts_speak`/
`sk_font_candidates` 都遵循同一收口原则。

**练习**：在 `plat.c` 里加一个 `sk_mkdir_p(const char*)`（Windows 用 `_mkdir`、POSIX 用 `mkdir`），
并在 `main.c` 里调用它创建一个目录——体会"只改 plat.c、业务侧零改动"。

**生产补记（2026-11-15）**：平台层不是“编过就算”，要覆盖真实路径形态。

- `graph_root_dir()` 改用 `sk_last_sep()`，Windows 的 `C:\beijing` / `products\graph\beijing` 也能找对父目录；
  旧实现只 `strrchr('/')`，反斜杠路径会返回 `"."`，`lm.bin` 被静默跳过。
- `app_gpx_import_dir()` 从直接 `opendir/readdir/closedir` 改为 `sk_dir_open/sk_dir_next/sk_dir_close`，
  MSVC 不再需要 `dirent.h`。
- CMake 把 SDL2 改为**可选**：只有 `skedaddle_blend_equiv` / `skedaddle_clip_equiv` 需要它；
  核心/Qt 在没有 SDL2 的机器上照常构建。

**练习**：在 Windows 上分别用 `--graph=products/graph/beijing` 和 `--graph=products\graph\beijing` 启动，
确认两条路径都能加载 `lm.bin`（没有表时日志应明确说“未加载”而不是悄悄退回）。
