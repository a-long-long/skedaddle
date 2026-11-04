# skedaddle Android 前端

原生 JNI App：复用 C 核心（地图渲染 / 搜索 / 算路），Android 端只写 UI 与 JNI 桥，
**不碰** 桌面端的 Qt 前端（`src/ui_qt/`）与离屏路径。

> ✅ **本机（ARM64 Linux）已可完整出 APK**（2026-11-04 验证），见 §5.1。
> x86_64 宿主（Windows/Linux）走标准 externalNativeBuild 流程即可。

---

## 1. 架构

```
Java (Android)                         C 核心 (复用 src/)
─────────────                         ──────────────────
MainActivity / MapView  ──JNI──▶       skedaddle_jni.cpp
  SurfaceView 渲染线程                   app_init / app_render_to_buf
  手势(平移/缩放/双击/点选)              app_nav_* / app_search_query
  搜索面板 / 设置 / 下载桩               mv_*
                                        plat.c (sk_android_set_*)
```

- **渲染**：`MapView` 自持渲染线程，每帧 `Skedaddle.render(int[], w, h)` 把地图画进
  ARGB_8888 缓冲，再贴到 SurfaceView 的 Canvas。复刻桌面/Wayland 教训：C 光栅器
  `raster_rgb` 不写 alpha，JNI 桥在每帧把每个像素 `|= 0xFF000000` 强制不透明，否则
  Bitmap 把 alpha=0 当全透明 → 一片花。
- **平台差异**：全部收在 `src/utils/plat.c` 的 `__ANDROID__` 分支（目录注入、temp/config、
  TTS 桩）。Java 侧通过 `getFilesDir()/getCacheDir()` 取得私有目录，在 `init` 时经
  `sk_android_set_paths` 传给 C 核心。
- **UI 状态在 Java 侧**：C 核心零窗口系统依赖（与 Qt 前端同语义），`qt_ui=1` 让 C 核心
  跳过自绘 chrome（搜索框/面板由 Java 真控件承担），但地图、结果图钉、夜间蒙版照常。

## 2. 目录布局

```
android/
  settings.gradle
  build.gradle                顶层（apply false 占位插件）
  gradle.properties           SKEDADDLE_SRC_ROOT 等
  app/
    build.gradle              externalNativeBuild → cpp/CMakeLists.txt
    src/main/
      AndroidManifest.xml
      java/com/skedaddle/android/
        Skedaddle.java          native 桥（方法名决定 JNI 函数名）
        MapView.java            SurfaceView + 渲染线程 + 手势
        MainActivity.java       顶栏/搜索/导航/设置入口/自动更新检查
        DownloadActivity.java   地图数据增量更新（拉清单+扫描本地+逐省下载/本地安装）
        SettingsActivity.java   设置中心（服务器/自动更新/检查更新/显示开关/关于）
        Prefs.java             配置中枢（SharedPreferences；新增开关从这里加）
        Downloader.java        通用 HTTP 下载 + zip 解包（地图与 APK 共用）
        Manifests.java         省图清单 / 应用更新清单解析（org.json）
        UpdateChecker.java     App 自更新（OTA）：拉清单+下载 APK+FileProvider 安装
      res/xml/file_paths.xml   FileProvider 暴露 updates/ 与 graph/ 给系统安装器
      res/                      布局、字符串、主题、自适应图标
      cpp/
        CMakeLists.txt          复用 src/ 全部核心源码
        skedaddle_jni.cpp       JNI 桥
        thirdparty/             ★ 需自行 vendor（见 §4）
          sqlite3/sqlite3.c
          protobuf-c/protobuf-c.c
      assets/fonts/
        DroidSansFallback.ttf   已内嵌（CJK 兜底字体）
```

## 3. 前置（构建机）

- **JDK 17（必需）**：AGP 8.1.4 在 JDK 22 下会在 Groovy 语义分析阶段直接报
  `Unsupported class file major version 66`。Gradle 8.7 本身支持 Java 22，但 AGP 不支持。
- **Android SDK** + **NDK**（建议 25.x 或 21.x，版本号要与 `app/build.gradle` 的
  `ndk { }` 一致；在 SDK Manager 安装）。
- **CMake** ≥ 3.22（Gradle 的 `externalNativeBuild` 会用）。
- **host 端 protobuf-c 代码生成器**：图集解码依赖 protobuf-c 的 C binding，由 host
  （非交叉）生成 `osmformat.pb-c.c / fileformat.pb-c.c`。Linux 用独立 `protoc-c`；
  Windows/MSYS2 的 `protoc-c.exe` 实为 `protoc-gen-c` 插件二进制（与
  `protoc-gen-c.exe` 同一份），需把 `protoc.exe` + `protoc-gen-c.exe` 放进 PATH，
  `cpp/CMakeLists.txt` 的 `CMAKE_HOST_WIN32` 分支会自动改走
  `protoc --plugin=protoc-gen-c=...`（Android 属交叉编译，`WIN32` 描述 target，
  恒为 false，不能用来判宿主）。
  桌面构建机在 `env.sh` 里已就绪；纯 Android 构建机需另装 `protobuf-c` 工具。
- **可选但推荐**：`gradle` 8.x（或用 Android Studio 打开后它自动生成 `gradlew` 与 wrapper）。

## 4. 第三方库 vendor（必须，否则链接失败）

NDK 不自带 `sqlite3` 与 `protobuf-c` 运行时。把源码 vendor 进
`app/src/main/cpp/thirdparty/`（**该目录已被 `android/.gitignore` 排除，不进 git**；
仓库保持精简，克隆后用下面脚本一键补齐）：

```bash
bash android/scripts/vendor-thirdparty.sh
```

脚本会拉取 sqlite3 amalgamation（默认 3.43.1）与 protobuf-c 运行时（`.c` + `.h`），
直连不通时 protobuf-c 自动走 `ghfast.top` 镜像。可用环境变量 `SK_SQLITE_VER` /
`SK_SQLITE_ZIP` / `SK_PROTOBUFC_REPO` 覆盖源。

> 缺失时 `cpp/CMakeLists.txt` 只给 `WARNING`，链接阶段才会真正因 `sqlite3_*` /
> `protobuf_c_*` 未定义而失败——这是预期，跑一次上面的脚本即可。

## 5. 构建

构建模式由 `app/build.gradle` 按宿主架构自动选择，无需手工配置：

| 宿主 | 模式 | 说明 |
|---|---|---|
| x86_64（Windows/Linux） | `externalNativeBuild` 标准流程 | 装 Android Studio 的 SDK+NDK 即可 |
| aarch64 Linux（ARM64 机） | 预编译 so | 先跑 §5.1 脚本出 so，Gradle 只打包 |
| 任意 → 强制标准流程 | `-Pskedaddle.forceNdk=true` | 需可运行的 NDK 宿主工具 |

```bash
cd android
./gradlew :app:assembleDebug
# 或 Android Studio：打开 android/ 目录 → 等待同步 → Run 'app'
```

产物：`app/build/outputs/apk/debug/app-debug.apk`，安装到 arm64 设备。

### 5.1 ARM64 Linux 宿主：预编译 so 模式

官方 NDK 只发布 x86_64 宿主二进制，ARM64 Linux 机上其 clang/ninja/ndk-build 无法
运行。本仓库的方案是**只用 NDK 包中架构无关的资产，编译链接全用宿主原生 clang**：

- **NDK sysroot**（Bionic 头文件/桩库）+ `libclang_rt.builtins.a`（目标代码）：
  从 `android-ndk-r27b-linux.zip` 提取（一次）→ `~/tools/android/ndk-sysroot`。
  **`android/scripts/setup-local-toolchain.sh <ndk-zip> [api]` 一键完成提取**
  （含 clang-res 的 include 符号链接 + builtins 布局），无需手工摆路径。
- **clang 运行时**：本机 UOS 仓库版 clang 24 的 resource-dir 无运行时库，自建
  `~/tools/android/clang-res`（include 符号链接 + builtins 摆成 clang 期望布局，
  由上面脚本生成）
- **交叉编译**：`android/scripts/local-clang-android.toolchain.cmake` +
  `build-so-local.sh`，即 `clang --target=aarch64-linux-android26 --sysroot=...`
  （`-unwindlib=none`：arm64 Android 的 unwinder 在 libc 里，API 21+）
- **产物**：`app/src/main/jniLibs/arm64-v8a/libskedaddle.so`（约 1.8M，
  NEEDED 仅 liblog/libz/libm/libdl/libc，JNI 桥零 STL → 无 libc++_shared 依赖）

```bash
# 本机（aarch64 Linux）一条链：
source android/scripts/env-android.sh    # JDK17 / SDK / GRADLE_USER_HOME / protoc-c
bash android/scripts/build-so-local.sh Release   # 出 so（脚本会自动 source 上一行）
cd android && ./gradlew :app:assembleDebug        # 出 APK
```

> `env-android.sh` 是本机（aarch64 Linux）专用环境脚本：显式设 `JAVA_HOME`、
> `ANDROID_HOME`、`GRADLE_USER_HOME`（避开全局 `~/.gradle/init.gradle` 的
> mavenLocal 冲突）并带上宿主 protoc-c/ninja。Windows / x86_64 Linux 机器
> 走标准流程，不需要它。

> JDK 17 + CMake 3.22 + ninja（`~/.local/bin`）+ 宿主 `protoc-c`（`~/.local/usr/bin`，
> 需 `LD_LIBRARY_PATH=~/.local/usr/lib/aarch64-linux-gnu`）均为本机已有环境。
> SDK/Gradle 布局：`~/tools/android/{sdk, gradle/8.7, ndk-sysroot, clang-res}`，
> 与系统盘/系统包零接触，删目录即卸载。gradle wrapper 的 distributionUrl 已指向
> 腾讯镜像；`settings.gradle` 内置阿里云 maven 镜像，Windows 机器同样受益。
> 本机注意：全局 `~/.gradle/init.gradle` 会注入 mavenLocal 与本项目
> `FAIL_ON_PROJECT_REPOS` 冲突，构建时须 `GRADLE_USER_HOME=~/tools/android/gradle-home`。

**aapt2（唯一残留的 x86_64 环节）**：AGP 打包必须用 maven 里的 aapt2 x86_64 二进制。
方案：nix 装 box64（`nix profile install nixpkgs#box64`，0.4.4，走清华缓存），
`~/tools/android/bin/aapt2` 是两行 wrapper（`exec box64 aapt2-x64/aapt2 "$@"`），
x86_64 真身已从 AGP maven jar 解到 `~/tools/android/aapt2-x64/`；
`gradle.properties` 的 `android.aapt2FromMavenOverride` 指向该 wrapper。
⚠ x86_64 宿主（Windows）**必须**用 `-Pandroid.aapt2FromMavenOverride=<本机 aapt2>`
覆盖这一行：实测 AGP 8.1.4 遇到不存在的 override 路径**不会**回退 maven aapt2，
而是报 `Custom AAPT2 location does not point to an AAPT2 executable` 并中止配置。
详见 §5.2。

### 5.2 x86_64 Windows 宿主：标准流程（2026-11-04 实测）

官方 NDK 带 Windows 宿主 clang/ninja，直接走 `externalNativeBuild`，无需预编译 so。
前置：JDK 17、Android SDK（`platforms;android-34` / `build-tools;34.0.0` /
`platform-tools`）、NDK 25.1.8937393、CMake 3.22.1、MSYS2 的 `protoc.exe` +
`protoc-gen-c.exe`（放进 PATH），以及 §4 的 vendor 第三方源码。

```bash
export JAVA_HOME=<JDK17 目录>
export ANDROID_HOME=<Android SDK 目录>
export GRADLE_USER_HOME=<独立目录，避开全局 init.gradle>
export PATH="$JAVA_HOME/bin:$PATH:<MSYS2>/ucrt64/bin"
cd android
./gradlew :app:assembleDebug --no-daemon \
  -Pandroid.aapt2FromMavenOverride=<SDK>/build-tools/34.0.0/aapt2.exe
```

`-Pandroid.aapt2FromMavenOverride` 不能省（原因见 §5.1 末）。本机实测：
`BUILD SUCCESSFUL`，产物 `app/build/outputs/apk/debug/app-debug.apk` 12 MB，
含 `lib/arm64-v8a/libskedaddle.so` 1.50 MB（ELF64 / AArch64）、3 个 dex、
DroidSansFallback.ttf；`aapt2 dump badging` = `com.skedaddle.android`
versionCode 1 / `0.2-android` / minSdk 26 / targetSdk 33；`apksigner verify`
v2 签名通过。注意这只是**构建产物验证**，真机运行仍需 §6 推图集。

## 6. 部署图集（必须，否则 init 失败）

C 核心的 `graphDir` 默认指 `/sdcard/Android/data/com.skedaddle.android/files/graph`
（应用私有外部存储，无需权限）。把桌面端 build 好的图集整个推过去：

```bash
adb push products/graph /sdcard/Android/data/com.skedaddle.android/files/
# 重启应用，init 会读 <graph>/<省>/... ；多省可直接放 products/graph
```

可用省（与 `products/graph/` 一致）：beijing, tianjin, hebei, shanxi(山西),
shaanxi(陕西), shandong, henan, liaoning。

### 6.1 首次装机 → 真正跑起来的清单（静态核对，未真机验证）

**能不能装** —— 对 `app-debug.apk` 做 `aapt2 dump badging` / `apksigner verify` 的结果：

- 只带 `lib/arm64-v8a/`，`native-code: 'arm64-v8a'` ⇒ 必须是 64 位 ARM 手机；
  32 位 ARM 或 x86 模拟器会 `INSTALL_FAILED_NO_MATCHING_ABIS`。
- `minSdk 26`（Android 8.0+）、`targetSdk 33`；`application-debuggable` + debug
  签名（`CN=Android Debug`），v2 签名 `Verifies`。系统需允许「安装未知来源应用」。
- APK 13,622,241 B（≈13.0 MB，debug 包，体积随构建机略有差异），共 842 个条目：
  `classes.dex` 9,763,712 B + `classes2.dex` 515,944 B + `classes3.dex` 65,732 B、
  `lib/arm64-v8a/libskedaddle.so` 1,504,736 B、
  `lib/arm64-v8a/libc++_shared.so` 1,026,616 B、
  `assets/fonts/DroidSansFallback.ttf` 3,939,852 B、`resources.arsc`。
  CJK 字体与 `libc++_shared.so` 已打包，so 的 `NEEDED` 只有
  `liblog / libz / libm / libdl / libc / libc++_shared`，13 个 JNI 符号全部导出，
  `MainActivity` / `MapView` / `Skedaddle` 等类都在 dex 里。
- 权限只有 `INTERNET` 与 `REQUEST_INSTALL_PACKAGES`（**无定位权限**，与 §9
  的「未接真实 GPS」一致）。

**装完直接打开会弹引导** —— `MainActivity` 把 `graphDir` 固定为
`getExternalFilesDir(null)/graph`。`app_init → mapset_load` 在目录不存在、或没有
含 `manifest.txt` 的省目录时直接返回 -1；JNI `Skedaddle.init` 于是返回 false。
此时 `MapView` 经 `OnInitListener` 回调 `MainActivity`，弹出「未检测到地图数据」
对话框（下载省图 / 设置服务器 / 稍后），画布同时显示提示文案。13 个 JNI 入口
都判 `g_inited`，所以不会闪退。按顺序装机：

1. 装 APK 并启动一次（抽字体、建私有目录）。
2. `adb push products/graph /sdcard/Android/data/com.skedaddle.android/files/`
   —— 落到 `files/graph/<省>/...`；只想试跑可只推单省
   `products/graph/beijing`（约 337 MB，含 `disp.idx` / `lanes.bin` / `t_*`）。
3. **完全退出再启动**：C 核心只在 init 时扫一次 graph 根，`adb push` 热推不会重载。
   若走应用内「下载省图 / 从本地 zip 安装」，从下载页返回 `onResume` 会自动
   `retryInit()` 重新加载图集，**无需手动杀进程重启**（`MainActivity.java:299-307`、
   `MapView.java:143-158`）。

个别 Android 11+ ROM 会拒绝 `adb push` 进 `Android/data`；替代做法是把省目录推到
`/sdcard/`，用「下载省图」里的「从本地文件安装」选 zip，或改
`MainActivity.onCreate` 的 `graphDir` 指过去。

**当前数据下哪些功能能跑**（本机 `products/graph/` 只有 beijing，全仓没有
`places_auto.db` / `lm.bin`）：

| 功能 | 状态 |
|---|---|
| 地图显示 / 拖动 / 缩放 | 可用：`disp.idx` + `t_*.areas/lines/polyrefs` 完整 |
| 算路 | 可用但偏慢：有 `rev.bin`，缺可选 `lm.bin` ⇒ 退回纯直线启发式 |
| 地名搜索 | **不可用**：`app_init` 用相对路径 `products/places_auto.db` 开搜索库，Android 进程 CWD 下没有；`a->sdb == NULL` 时搜索恒返回空串 |
| 语音播报 | 不发声（Android 分支只有 DRYRUN，见 §9） |
| 真实定位 | 未接，点选起终点做模拟导航 |
| 省内下载 / OTA | `server_base` 默认为空，需在设置里填服务器地址；否则只能本地安装 |

## 7. 字体

`DroidSansFallback.ttf`（CJK 兜底）已内嵌在 `assets/fonts/`，首次运行自动抽到
`getFilesDir()/fonts/`，`init` 时作为 `fontPath` 传给 C 核心。要换/补字体，往
`assets/fonts/` 加文件并在 `MainActivity.extractFont()` 里同步抽取即可。

## 8. JNI 契约（`Skedaddle` ↔ `skedaddle_jni.cpp`）

| Java 方法 | 原生符号 | 说明 |
|---|---|---|
| `init(...)` | `Java_com_skedaddle_android_Skedaddle_init` | 注入 Android 目录 + `app_init` + `qt_ui=1` |
| `render(int[],w,h)` | `..._render` | 每帧渲染 + 补 alpha |
| `pan(dx,dy)` | `..._pan` | `mv_pan_pixels` |
| `zoomAt(dz,px,py)` | `..._zoomAt` | `mv_zoom_at` |
| `setView(lat,lon,zoom)` | `..._setView` | 直接设视口 |
| `getView()` → `double[3]` | `..._getView` | 视口回读 |
| `unproject(px,py)` → `double[2]` | `..._unproject` | `mv_unproject`（点选用） |
| `search(q)` → `String` | `..._search` | `app_search_query`，多行 `名称\|lat\|lon` |
| `setStart(lat,lon)` | `..._setStart` | `app_nav_click`（起/终点） |
| `clearRoute()` | `..._clearRoute` | `app_nav_clear` |
| `planRoute(a,b)` → `double[3]` | `..._planRoute` | `app_nav_plan_sync` → `[ok,m,s]` |
| `setShowFlags(poi,labels,contours)` | `..._setShowFlags` | 显示开关 |
| `shutdown()` | `..._shutdown` | `app_shutdown` |

> 改包名/类名必须同步改 `skedaddle_jni.cpp` 里的 `Java_com_skedaddle_android_Skedaddle_*`
> 前缀（JNI 函数名由此决定）。

## 9. 已知限制（v1）

- **未接真实 GPS / NMEA**：导航是“屏幕点选起终点 + 模拟”，不接定位源（桌面端有模拟
  导航，本端暂未暴露对应 JNI；后续可加 `sim_*` 桥）。
- **离线 TTS 桩**：Android 上 `sk_tts_speak` 仅保留 DRYRUN 写盘分支（无离线语音引擎），
  语音播报不发声。
- **视觉/样式**：顶底栏是简易浮层，未做 Material 精细布局；浮层透明度等桌面端特性未接。
- **真机验证待做**：ARM64 Linux（§5.1）与 x86_64 Windows（§5.2）都已编出 so + APK，
  Java/JNI 语法、符号链接与签名全通过，但**两者都没有在真机上跑过** init/渲染/算路。
- **minSdk 26**：仅 Android 8.0+，故图标用 XML 自适应（无需打包 PNG）。
- **无地图只引导、不自动下载**：`server_base` 默认为空，没有配置服务器就无从下载；
  未配置时只能「从本地文件安装」选 zip 或 `adb push` 推入图集（见 §6.1）。
- **地名搜索在 Android 上默认不可用**：`app_init` 里
  `search_open("products/places_auto.db")` 是相对路径，Android 进程没有这个目录，
  且 v1 的 `assets/` 里也没带该库；`a->sdb == NULL` 时 `app_search_query` 直接返回 0
  （不崩，只是搜不到）。要启用需另外部署该库并用 `SKEDADDLE_PLACES` 指绝对路径。

### 与桌面 Qt 版的功能对照（2026-11-04 逐项核查）

Android 端只暴露 13 个 JNI 方法（`Skedaddle.java`）：`init / render / pan / zoomAt /
setView / getView / unproject / search / setStart / clearRoute / planRoute /
setShowFlags / shutdown`。对照 `src/ui_qt/main_window.cpp:996-1200` 的菜单，下列
缺失项是**没接线**，不是 C 核心不具备：

| 能力 | 桌面 Qt | Android |
|---|---|---|
| 地图显示 / 拖动缩放 / 点选起终点算路 | ✅ | ✅ |
| 地名搜索 | ✅ `places_auto.db` | ⚠️ JNI 在，库未部署 ⇒ 恒返回空串 |
| POI / 地名 / 等高线开关 | ✅ | ✅（`setShowFlags`） |
| 真实 GPS/NMEA、卫星模型 / 天空图 | ✅ `nmea_*` / `satellite_*` / `skyplot_*` | ❌ 无定位权限、无 JNI 桥 |
| 模拟导航 / 跟随车头 / 偏航重算 / 途经点 / 缩放到路线 | ✅ `app_nav_sim_*` / `app_nav_via` / `app_nav_fit` | ❌ 未暴露 JNI |
| 语音播报 | ✅ | ❌ 仅 DRYRUN（见上） |
| GPX 导入 / 导出 / 收藏 / 轨迹面板 | ✅ | ❌ |
| 坐标系 / 经纬网 / 网格类型与细分 | ✅（`datum.c`） | ❌ |
| 道路 LOD 0–4 / 字号增减 / 浮层透明度 | ✅ | ❌（字号在 init 固定 14） |
| 夜间模式（手动 + 日出日落自动）/ 多语言 | ✅ | ❌ |
| 无地图检测引导 | ❌ | ✅（`OnInitListener` + `retryInit`） |
| 地图获取 | Geofabrik PBF 下载 + `sync_maps.sh` 转换 | 省图 zip 增量下载 / 本地安装 + APK OTA |

> 补齐方向：给 `skedaddle_jni.cpp` 加 `app_nav_sim_*` / `datum` / `day_night` 等桥并在
> `MainActivity` 挂 UI 即可，C 核心零改动（与 §1 的分层纪律一致）。

### 已落地的“增量更新 / OTA”能力（见 §11）

- **地图数据增量更新**：`DownloadActivity` 已不再是桩——可拉省图清单、扫描本地已装省与
  版本、逐省下载/更新，或从本地 zip 安装。C 核心在 `init` 时一次性加载 graph 根，
  但从下载页返回 `onResume` 会自动 `retryInit()` 重载，**无需杀进程重启**；只有运行中
  `adb push` 热推或后台改目录才需要重启应用（真正的进程内热换省仍未做）。
- **App 自更新（OTA）**：`SettingsActivity` 的“检查更新”可拉 `app/update.json`、比较
  versionCode、下载 APK 并经 FileProvider 调起系统安装器（需
  `REQUEST_INSTALL_PACKAGES` 权限，Android 8+）。
- **配置中枢 `Prefs`**：所有运行时偏好（服务器地址、自动更新、显示开关）集中于
  `Prefs.java`，新增功能开关不必散落 Activity。

## 10. 后续可加（非 v1）

- `app_nav_sim_*` 桥（模拟导航 + 车头跟随）。
- `app_nav_via` / 途经点。
- 浮层/昼夜/网格开关（已有 `App` 字段，加 JNI 即暴露）。
- 离线 TTS 引擎接入（如 Vosk / 系统 TTS）。
- 真正的地图省图**热重载**（进程内换省不重跑 init）：目前只有「从下载页返回时
  `retryInit()` 一次」，运行中 `adb push` 新省仍需重启应用。

## 11. 增量更新 / OTA（已落地）

两类更新共用 `Downloader`（HTTP + zip 解包）与 `Prefs`（服务器地址等配置）。

### 11.1 地图数据增量更新

入口：`DownloadActivity`（顶栏“下载”按钮）。行为：

- 若已在 `SettingsActivity` 配置**更新服务器**，拉 `<serverBase>/map/manifest.json`；
  否则列出内置省、仅支持“从本地文件安装”。
- 扫描本地 `graph/` 子目录，读取 `<省>/.skversion` 得到已装版本（无文件=未知版本）。
- 每行显示状态：未安装 / 已装 vX / 可更新 → vY，并提供 下载 / 更新 / 重新下载 按钮。
- 远程下载：`<serverBase>` + 清单 `url` → 解包到 `graph/<id>/` 并写 `.skversion`。
- 本地安装：`ACTION_OPEN_DOCUMENT` 选 zip（顶层即 `<id>/…`）→ 解包到 `graph/` 并对新目录写 `.skversion`。

**省图清单格式**（`map/manifest.json`）：

```json
{
  "updated": "2026-11-04",
  "provinces": [
    {"id": "beijing", "name": "北京", "version": 3, "size": 12345678,
     "url": "/map/beijing.zip"},
    {"id": "shaanxi", "name": "陕西", "version": 1, "size": 9876543,
     "url": "https://cdn.example.com/map/shaanxi.zip"}
  ]
}
```

- `url` 可为相对（`serverBase` 拼接）或完整 http(s)。
- `version` 仅用于判断“可更新”（> 已装版本）；`size` 仅展示。

### 11.2 App 自更新（OTA）

入口：`SettingsActivity` → “检查更新”（勾选“自动检查应用更新”后，启动每天最多查一次）。

- 拉 `<serverBase>/app/update.json`，比较 `versionCode`；大于已装才提示。
- 下载 APK 到 `<external-files>/updates/skedaddle-update.apk`，经 `FileProvider`
  （`authorities = <applicationId>.fileprovider`，见 `res/xml/file_paths.xml`）调起系统
  安装器。**需 `REQUEST_INSTALL_PACKAGES` 权限**（Android 8+）。

**应用更新清单格式**（`app/update.json`）：

```json
{
  "versionCode": 2,
  "versionName": "0.3-android",
  "apkUrl": "/app/skedaddle-0.3-android.apk",
  "notes": "新增 XX、修复 YY"
}
```

> 发新版时把 `versionCode` 递增（当前 `app/build.gradle` 是 1），否则设备不会判定为“可更新”。

### 11.3 如何自托管

任意静态文件服务（`nginx` / 对象存储 / GitHub Pages）即可，目录结构：

```
<serverBase>/
  map/manifest.json
  map/<省>.zip          # 顶层为 <省>/…（与 products/graph/<省> 一致）
  app/update.json
  app/skedaddle-<ver>.apk
```

`serverBase` 在 App 内“设置”填写（持久化到 `Prefs`）；留空则只用本地安装、无 OTA。
