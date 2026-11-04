# 第 17 课 · Android 原生 JNI App：把同一个 C 核心搬进手机

> 模块：`android/`（Gradle + NDK）、`src/utils/plat.{c,h}` 的 `__ANDROID__` 分支
> 配套：**`android/` 真实工程本身**——本机 ARM64 Linux 已实测编出 `libskedaddle.so`（1.86 MB）
> 与 `app-debug.apk`（11.4 MB）；本课是**第一门不配 `tutorial/NN_xxx` 小程序**的课，因为
> 它要跑的正是产品工程，而不是一个自包含玩具。构建/部署/自托管命令见 [`../android.md`](../android.md)，
> 本课只讲「为什么这么设计」。
> 前置：第 2 课平台抽象层（尤其 3.5 Android 分支）、第 8 课渲染管线（`app_render_to_buf`）、
> 第 12 课 Qt 前端（alpha 教训）、第 13 课分层门禁。

## ① 教学目的

桌面端的 `ui_qt` 用的是 Qt，Android 没有 Qt 运行时，也没有 X11/Wayland。一个自然的坏做法是
「给 Android 重写一套渲染 + 算路」——那会立刻产生第二份地图逻辑，从此两边行为分叉。
本工程选的是另一条路：**C 核心一行不改，只换壳**。学完本课应能说清三件事：

1. **边界在哪**：Java 与 C 之间只有一个类（`Skedaddle`，13 个 `native` 方法）和一个文件
   （`skedaddle_jni.cpp`，13 个 `JNIEXPORT`）；核心仍走第 8 课已经暴露好的 `app_*` 缓冲渲染 API。
2. **平台差异怎么进来的**：Android 没有 `$HOME` / `XDG` / `TMP`，也没有「可执行文件」，
   全部靠 JNI 在 `app_init` **之前**注入目录（第 2 课 3.5）。顺序错了，症状不是崩溃，而是
   「有图无线无字」——静默失败最难查。
3. **为什么不破坏分层**：`android/` 是 `ui_qt` 的**同级替代前端**，不是新的一层；
   `utils ← pbf ← map ← route ← render ← ui` 这条 DAG 一个箭头都没改。

## ② 功能构成

| 文件 / 类 | 职责 | 关键位置 |
|---|---|---|
| `java/.../Skedaddle.java` | 唯一的 native 门面；`static { System.loadLibrary("skedaddle"); }` | `Skedaddle.java:10-75`（声明 30-74） |
| `cpp/skedaddle_jni.cpp` | JNI 桥：13 个 `Java_com_skedaddle_android_Skedaddle_*` 函数，持有全局 `App g_app` + `SkMutex` | `skedaddle_jni.cpp:32-259` |
| `java/.../MapView.java` | `SurfaceView` + 30 fps 渲染线程 + 平移/双指缩放/双击/点选反投影 | `MapView.java:27,136-162` |
| `java/.../MainActivity.java` | 抽 `assets/fonts/` 字体到 files 目录、搜索面板、导航按钮 | `MainActivity.java:42,77-85,263` |
| `cpp/CMakeLists.txt` | 复用仓库 `src/` 全部核心源码：`skedaddle_core STATIC` → `skedaddle SHARED` | `CMakeLists.txt:101-114` |
| `src/utils/plat.c` | Android 目录注入 + TTS/exe/temp/config/font 平台分支 | `plat.c:1205-1244`（详见第 2 课 3.5） |
| `java/.../Downloader.java` | HTTP 下载 + zip 解包（地图包与 APK 共用） | `Downloader.java:20,34,80` |
| `java/.../Manifests.java` | 解析 `map/manifest.json` 与 `app/update.json` | `Manifests.java:18,34,63` |
| `java/.../DownloadActivity.java` | 地图增量更新：`.skversion` 比对、下载/更新/本地 zip 安装 | `DownloadActivity.java:41,48,267` |
| `java/.../UpdateChecker.java` + `SettingsActivity.java` | App OTA：比 `versionCode` → 下 APK → `FileProvider` 调系统安装器 | `UpdateChecker.java:33,43,52`；`SettingsActivity.java:25,76` |
| `AndroidManifest.xml` | `INTERNET`、`REQUEST_INSTALL_PACKAGES`、`FileProvider` | `AndroidManifest.xml:5,8,44-52` |

## ③ 怎么实现（核心教学点）

### 3.1 一条边界：13 对 13

Java 侧只声明，不做任何地图逻辑：

```java
// Skedaddle.java:30-35（节选）
public static native boolean init(String graphDir, String filesDir, String cacheDir,
                                  String fontDir, int w, int h, String fontPath, int fontSize);
public static native void render(int[] pixels, int w, int h);
```

C++ 侧的每个函数都由**包名 + 类名 + 方法名**机械决定：
`Java_com_skedaddle_android_Skedaddle_init` ↔ `Skedaddle.init(...)`。
所以改包名 / 类名必须同步改 `skedaddle_jni.cpp` 里全部函数前缀，否则运行时抛
`UnsatisfiedLinkError`（编译期一个错都不会报）。这是 JNI 最典型的「拼写即契约」。

> **练习**：在 `Skedaddle.java` 里加一个 `native int nodeCount()`，按同样规则在
> `skedaddle_jni.cpp` 补上 `Java_..._nodeCount`，让它返回 `g_app` 里的节点数 ——
> 体会「加一个 API 只动这两个文件，核心源码零改动」。

### 3.2 `init` 的顺序：注入目录必须早于 `app_init`

```cpp
// skedaddle_jni.cpp:51-57（节选）
sk_android_set_paths(fdir, cdir);              // ① files/cache 先注入
if (fodir) sk_android_set_font_dir(fodir);    // ② 字体目录
memset(&g_app, 0, sizeof(g_app));
if (!g_mu) g_mu = sk_mutex_create();
int rc = app_init(&g_app, gdir ? gdir : "", (int)w, (int)h, fp, fs, nullptr);  // ③ 才初始化
```

调用链是 `MainActivity`（`MainActivity.java:77-85` 取 `getFilesDir()` / `getCacheDir()`、
`263` 抽字体）→ `MapView.java:105` 的 `Skedaddle.init(...)` → 上面这段。
**为什么顺序是硬约束**：`app_init` 内部会读配置目录、建字体，`plat.c` 的
`sk_config_dir` / `get_font_dir` 在 Android 上唯一的数据来源就是这两个全局量；
注入晚了，`get_font_dir` 会静默失败 ⇒ `font_open_default()` 返回 NULL ⇒
`paint_frame` 的 `if(a->font)` 把文字全丢掉。第 12 课记过这条门控，这里就是它的触发路径。

### 3.3 每帧：30 fps 渲染线程 + 强制不透明

```java
// MapView.java:146-159（节选，RenderThread.run）
c = holder.lockCanvas();
if (c != null) {
    synchronized (mInitLock) {
        if (mInited && mPixels != null && mBitmap != null) {
            Skedaddle.render(mPixels, mW, mH);
            mBitmap.setPixels(mPixels, 0, mW, 0, 0, mW, mH);
            c.drawBitmap(mBitmap, 0, 0, null);
        }
    }
}
...
if (c != null) holder.unlockCanvasAndPost(c);
Thread.sleep(FRAME_MS);   // FRAME_MS = 33 → ~30fps
```

JNI 的 `render`（`skedaddle_jni.cpp:82-110`）同步视口尺寸、收割后台算路结果，然后调
`app_render_to_buf` 直接写进 Java 的 `int[]`。**关键的一行在 C++ 侧**：

```cpp
/* skedaddle_jni.cpp:105-107 */
long n = (long)w * (long)h;
for (long i = 0; i < n; i++) p[i] |= 0xFF000000u;
```

C 软件光栅 `raster_rgb` 只写 RGB，缓冲恒为 `0x00RRGGBB`；`Bitmap` 按 ARGB_8888 上传时
alpha=0 ⇒ **整屏全透明花屏**。这与第 12 课 Wayland 下 `map_canvas::renderNow` 必须
`OR 0xFF000000` 是同一个坑，只是从 `QImage` 换成了 `Bitmap`。凡是把 `app_render_to_buf`
接到新的显示后端，这一行都是必带项。

### 3.4 CMake：整核心进 STATIC，只把 JNI 桥做成 SHARED

```cmake
# android/app/src/main/cpp/CMakeLists.txt:101-114（节选）
add_library(skedaddle_core STATIC
  ${PBF_SRCS} ${MAP_SRCS} ${ROUTE_SRCS} ${SEARCH_SRCS} ${RENDER_SRCS}
  ${UI_SRCS} ${UTILS_SRCS} ${EXTRA_TP})
target_link_libraries(skedaddle_core PUBLIC log z m)

add_library(skedaddle SHARED skedaddle_jni.cpp)
target_link_libraries(skedaddle PRIVATE skedaddle_core log)
```

`SK_SRC` 指向仓库根（`CMakeLists.txt:4`，从 cpp 目录向上 5 级），所以**核心源码只有一份**，
Android 工程里没有任何 `src/` 的拷贝。编成单个 STATIC 再链进 SHARED，是为了避开桌面端
多库链接顺序问题（第 13 课记过 `-Wl,--start-group` 被移除的历史）。

> **第三方必须 vendor**：NDK 不自带 `sqlite3` 与 `protobuf-c` 运行时，缺了会直接链接失败
> （`CMakeLists.txt:43-67` 有 WARNING）。`android/scripts/vendor-thirdparty.sh` 负责取源，
> `thirdparty/` 被 `.gitignore` 排除——**克隆后不跑这个脚本必失败**。
> 这也是「构建」与「教程」分工的原因：命令在 `docs/android.md` §4，本课只讲依赖为什么存在。

### 3.5 增量更新 / OTA：两种清单，一条 `Downloader`

| | 地图数据 | App 自身 |
|---|---|---|
| 入口 | `DownloadActivity.java:41` | `SettingsActivity.java:76` → `UpdateChecker.java:33` |
| 清单 | `<serverBase>/map/manifest.json`（`Manifests.java:34`） | `<serverBase>/app/update.json`（`Manifests.java:63`） |
| 版本判据 | 省目录里的 `.skversion` 文件（`DownloadActivity.java:48`） | `versionCode`（当前 `build.gradle:28` = 1） |
| 落盘 | 解包到 `graph/<id>/` 并写 `.skversion`（`:267`） | 下 APK 到 `updates/`，经 `FileProvider` 调系统安装器（`UpdateChecker.java:52`） |
| 权限 | `INTERNET` | `REQUEST_INSTALL_PACKAGES`（`AndroidManifest.xml:8`） |

两者共用 `Downloader.download` + `Downloader.extractZip`（`Downloader.java:34,80`）。
判据用的是**纯十进制整数版本号**，不是时间戳也不是文件大小——与第 13 课「布尔判据用
展开数/里程/像素，时间只兜底」同一条纪律。发新版必须递增 `versionCode`，否则设备永远
判定「无需更新」（`build.gradle:29` 的注释专门提醒了下个版本用 2）。

### 3.6 分层纪律复核（第 13 课）

`android/` 与 `ui_qt/` 是**并列的两种前端**，都只调 `ui/` 暴露的 `app_*` API：
`app_init` / `app_render_to_buf` / `app_nav_*` / `app_search_query` / `mv_*`。
平台差异全在 `plat.c` 的 `__ANDROID__` 分支里（第 2 课 3.5），业务代码
**零 `#ifdef __ANDROID__`**。所以 `./scripts/check_layers.sh` 的依赖图完全不受影响——
新增一个前端并没有往 DAG 里加边，只是换了个调用者。

## ④ 与其他功能的联系

- **上游 · 第 2 课 / 第 8 课**：`plat.c` 的目录注入（3.5）是 Android 能启动的前提；
  `app_render_to_buf`（第 8 课为离屏截图加的缓冲渲染入口）在这里被复用成主渲染路径——
  当初为审计做的接口，如今直接撑起了第二个前端。
- **横切 · 第 11 课语音**：`voice_wire → say_dup → sk_tts_speak` 的接线不变，但
  `plat.c:1137` 的 Android 分支让它**只走 DRYRUN、不发声**（SELinux 禁 fork/exec、
  无 espeak）。所以 `SKEDADDLE_TTS_DRYRUN` 仍是验证接线通不通的可用判据，只是听不到声音。
- **横切 · 第 12 课字体 / alpha**：`if(a->font)` 文字门控、Wayland alpha 教训都在本课
  以 Android 形态复现了一次（3.2、3.3）。同一条根因在两个平台上换皮出现，值得记成判据。
- **下游 · 第 13 课门禁**：本工程未改 `src/`，regress 判据完全不受影响；这也解释了为什么
  Android 的验收要另立一套（APK 能否出、符号是否齐、JNI 契约是否对），见下方演示。

## 演示、练习与验收

- **演示**（需 ARM64 Linux 构建机，命令见 `docs/android.md` §5.1）：
  1. `android/scripts/vendor-thirdparty.sh` 补齐 sqlite3 / protobuf-c → 跑
     `android/scripts/build-so-local.sh Release`；
  2. 检查产物：`libskedaddle.so` 为 aarch64 ELF（本机实测 1.86 MB），`readelf -d` 的
     NEEDED 只有 `liblog/libz/libm/libdl/libc`、**零 libc++**，`nm -D` 能列出全部 JNI 符号；
  3. `app-debug.apk`（实测 11.4 MB）→ `adb push products/graph` 到
     `/sdcard/Android/data/com.skedaddle.android/files/`，重启看首帧地图与中文标注。
- **练习**：
  ① **负向对照**：注释掉 `skedaddle_jni.cpp:51` 的 `sk_android_set_paths`，重跑——
     应出现「有图无字」或 `settings.ini` 落到错误目录，用可见现象证明注入承重；
  ② 去掉 `skedaddle_jni.cpp:107` 的 `|= 0xFF000000u`，观察整屏花屏，与第 12 课 Wayland
     截图对照（同一根因，两个后端）；
  ③ **契约自检**：本机无 NDK 时，用忠实复刻 NDK 的 `jni.h` 垫片（C++ `JNIEnv_` struct +
     inline 转发 `functions->X(this,...)`）配真实 `src/` 头，跑
     `g++ -std=c++17 -D__ANDROID__ -Wall -Wextra -fsyntax-only skedaddle_jni.cpp` 零告警。
     注意**真实 JDK 的 `jni.h` 在 C++ 模式下没有 inline 包装**，不足以校验 `env->Method()`
     写法，别拿它当 NDK 替身。
- **验收**：能画出「Java → JNI → `app_*` → C 核心」这条链，说清
  `sk_android_set_paths` 为什么必须早于 `app_init`、alpha 为什么必须补、thirdparty 为什么
  必须 vendor、以及为什么新增一个前端没有改动分层 DAG。

## 已知限制（v1，见 `docs/android.md` §9）

未接真实 GPS / NMEA（起终点靠屏幕点选）、离线 TTS 仅 DRYRUN、顶底栏为简易浮层、
minSdk 26、真机跑通尚未验证（本机只做到完整编译出 so + APK）。
