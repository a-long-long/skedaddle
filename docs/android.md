# skedaddle Android 前端

原生 JNI App：复用 C 核心（地图渲染 / 搜索 / 算路），Android 端只写 UI 与 JNI 桥，
**不碰** 桌面端的 Qt 前端（`src/ui_qt/`）与离屏路径。

> ⚠️ **本机（开发机）无 Android SDK / NDK，以下代码按规范编写、未经真机编译。**
> 必须在装有 Android SDK + NDK 的构建机（或 Android Studio）上验证。
> 已知风险点已在「已知限制」一节列出。

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
        MainActivity.java       顶栏/搜索/导航/设置/下载
        DownloadActivity.java   下载桩（v1 未接服务）
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

- **Android SDK** + **NDK**（建议 25.x 或 21.x，版本号要与 `app/build.gradle` 的
  `ndk { }` 一致；在 SDK Manager 安装）。
- **CMake** ≥ 3.22（Gradle 的 `externalNativeBuild` 会用）。
- **host 端 `protoc-c`**：图集解码依赖 protobuf-c 的 C binding，由 host（非交叉）生成
  `osmformat.pb-c.c / fileformat.pb-c.c`。桌面构建机在 `env.sh` 里已就绪；纯 Android
  构建机需另装 `protobuf-c` 工具。
- **可选但推荐**：`gradle` 8.x（或用 Android Studio 打开后它自动生成 `gradlew` 与 wrapper）。

## 4. 第三方库 vendor（必须，否则链接失败）

NDK 不自带 `sqlite3` 与 `protobuf-c` 运行时。把源码 vendor 进
`app/src/main/cpp/thirdparty/`：

```bash
TP=android/app/src/main/cpp/thirdparty
# sqlite3 amalgamation
mkdir -p $TP/sqlite3
curl -L https://www.sqlite.org/2023/sqlite-amalgamation-3430100.zip -o /tmp/sqlite.zip
unzip -o /tmp/sqlite.zip -d /tmp/sqlite
cp /tmp/sqlite/*/sqlite3.c /tmp/sqlite/*/sqlite3.h $TP/sqlite3/

# protobuf-c 运行时（仅 protobuf-c.c + protobuf-c.h）
mkdir -p $TP/protobuf-c
git clone --depth 1 https://github.com/protobuf-c/protobuf-c.git /tmp/pbc
cp /tmp/pbc/protobuf-c/protobuf-c.c /tmp/pbc/protobuf-c/protobuf-c.h $TP/protobuf-c/
```

> 缺失时 `cpp/CMakeLists.txt` 只给 `WARNING`，链接阶段才会真正因 `sqlite3_*` /
> `protobuf_c_*` 未定义而失败——这是预期，按上面补上即可。

## 5. 构建

```bash
cd android
# 若有 gradlew：
./gradlew :app:assembleDebug
# 或 Android Studio：打开 android/ 目录 → 等待同步 → Run 'app'
```

产物：`app/build/outputs/apk/debug/app-debug.apk`，安装到 arm64 设备。

## 6. 部署图集（必须，否则 init 失败）

C 核心的 `graphDir` 默认指 `/sdcard/Android/data/com.skedaddle.android/files/graph`
（应用私有外部存储，无需权限）。把桌面端 build 好的图集整个推过去：

```bash
adb push products/graph /sdcard/Android/data/com.skedaddle.android/files/
# 重启应用，init 会读 <graph>/<省>/... ；多省可直接放 products/graph
```

可用省（与 `products/graph/` 一致）：beijing, tianjin, hebei, shanxi(山西),
shaanxi(陕西), shandong, henan, liaoning。

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
- **下载是桩**：`DownloadActivity` 仅列出省并说明 adb 推送方式，真实 HTTP 下载未接。
- **离线 TTS 桩**：Android 上 `sk_tts_speak` 仅保留 DRYRUN 写盘分支（无离线语音引擎），
  语音播报不发声。
- **视觉/样式**：顶底栏是简易浮层，未做 Material 精细布局；浮层透明度等桌面端特性未接。
- **本机未编译**：开发机无 SDK/NDK，全部按 NDK 规范编写 + host 用真实 JDK `jni.h`
  （NDK 同源于 Oracle 规范）做了 JNI 桥的 `-fsyntax-only` 校验；真机/构建机验证待做。
- **minSdk 26**：仅 Android 8.0+，故图标用 XML 自适应（无需打包 PNG）。

## 10. 后续可加（非 v1）

- `app_nav_sim_*` 桥（模拟导航 + 车头跟随）。
- `app_nav_via` / 途经点。
- 真实下载服务 + 解包 + 进度。
- 浮层/昼夜/网格开关（已有 `App` 字段，加 JNI 即暴露）。
- 离线 TTS 引擎接入（如 Vosk / 系统 TTS）。
