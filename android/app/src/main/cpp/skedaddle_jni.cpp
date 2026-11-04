/* skedaddle JNI 桥：Android 原生前端入口（对应 desktop 的 ui_qt / offscreen_shot）。
 *
 * 设计要点（与项目纪律一致）：
 *   - 不碰业务代码：本文件只是"前端入口"，只调 App / MapView 的公开 C API；
 *     平台差异全部在 src/utils/plat.c（sk_android_set_* 注入 Android 目录）。
 *   - 渲染复用 app_render_to_buf（与 Qt 前端同一条 C 光栅路径）；不写任何
 *     图形代码。
 *   - ★ 复刻 Wayland 教训：C 光栅器 raster_rgb 不写 alpha 字节 ⇒ 缓冲恒
 *     0x00RRGGBB；Android Bitmap(ARGB_8888) 把 alpha=0 当全透明。故每帧渲染后
 *     强制 px[i] |= 0xFF000000，否则地图会被当成全透明（一片花）。
 */
#include <jni.h>
#include <android/log.h>
#include <string.h>
#include <stdint.h>

extern "C" {
#include "ui/window.h"      /* App / app_init / app_render_to_buf / 导航 / 搜索 */
#include "map/map_view.h"   /* MapView / mv_pan_pixels / mv_zoom_at */
}

#define LOG_TAG "skedaddle"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static App    g_app;
static int    g_inited = 0;
static SkMutex *g_mu = nullptr;

/* 包名路径：Java_com_skedaddle_android_Skedaddle_<method> */

extern "C" JNIEXPORT jboolean JNICALL
Java_com_skedaddle_android_Skedaddle_init(JNIEnv *env, jobject,
                                         jstring graphDir,
                                         jstring filesDir,
                                         jstring cacheDir,
                                         jstring fontDir,
                                         jint w, jint h,
                                         jstring fontPath,
                                         jint fontSize)
{
    if (g_inited) app_shutdown(&g_app);

    const char *gdir  = graphDir  ? env->GetStringUTFChars(graphDir, nullptr)  : nullptr;
    const char *fdir  = filesDir  ? env->GetStringUTFChars(filesDir, nullptr)  : nullptr;
    const char *cdir  = cacheDir  ? env->GetStringUTFChars(cacheDir, nullptr)  : nullptr;
    const char *fodir = fontDir   ? env->GetStringUTFChars(fontDir, nullptr)   : nullptr;
    const char *fpath = fontPath  ? env->GetStringUTFChars(fontPath, nullptr)  : nullptr;

    /* 注入 Android 私有目录（plat.c 的 config/temp/font 都靠它）。 */
    sk_android_set_paths(fdir, cdir);
    if (fodir) sk_android_set_font_dir(fodir);

    memset(&g_app, 0, sizeof(g_app));
    if (!g_mu) g_mu = sk_mutex_create();

    const char *fp = (fpath && *fpath) ? fpath : nullptr;
    int fs = fontSize > 0 ? (int)fontSize : 14;
    int rc = app_init(&g_app, gdir ? gdir : "", (int)w, (int)h, fp, fs, nullptr);

    if (gdir)  env->ReleaseStringUTFChars(graphDir, gdir);
    if (fdir)  env->ReleaseStringUTFChars(filesDir, fdir);
    if (cdir)  env->ReleaseStringUTFChars(cacheDir, cdir);
    if (fodir) env->ReleaseStringUTFChars(fontDir, fodir);
    if (fpath) env->ReleaseStringUTFChars(fontPath, fpath);

    if (rc != 0) {
        LOGE("app_init 失败（rc=%d），图集目录是否正确？", rc);
        g_inited = 0;
        return JNI_FALSE;
    }
    /* Android 用 Java 画 UI chrome/面板 ⇒ 让 C 核心跳过自绘 chrome（与 Qt 前端同语义）。 */
    g_app.qt_ui = 1;
    g_app.show_poi = 1;
    g_app.show_labels = 1;
    g_app.show_contours = 1;
    g_inited = 1;
    LOGI("skedaddle init OK (graph=%s, %dx%d)", gdir ? gdir : "", (int)w, (int)h);
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_skedaddle_android_Skedaddle_render(JNIEnv *env, jobject,
                                            jintArray pixels, jint w, jint h)
{
    if (!g_inited) return;
    sk_mutex_lock(g_mu);
    /* 视口尺寸随 Surface 变化，每帧同步；pan/zoom 只动 lat/lon/zoom 不动 w/h。 */
    g_app.view.w = (int)w;
    g_app.view.h = (int)h;
    /* 收割后台算路结果（若有），让路线显示刷新。 */
    app_nav_poll(&g_app);
    sk_mutex_unlock(g_mu);

    jint *p = env->GetIntArrayElements(pixels, nullptr);
    if (!p) return;

    PaintStats st;
    memset(&st, 0, sizeof(st));
    /* ARGB_8888 内存布局：rmask=0x00FF0000 gmask=0x0000FF00 bmask=0x000000FF。
     * amask 传 0xFF000000 作占位，但 raster_rgb 不写 alpha，故下方手动补 0xFF。 */
    app_render_to_buf(&g_app, p, (int)w, (int)h, (size_t)w,
                      0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u, &st);

    /* ★ 强制不透明（复刻 Wayland 教训）：否则 Bitmap alpha=0 ⇒ 全透明花屏。 */
    long n = (long)w * (long)h;
    for (long i = 0; i < n; i++) p[i] |= 0xFF000000u;

    env->ReleaseIntArrayElements(pixels, p, 0);
}

extern "C" JNIEXPORT void JNICALL
Java_com_skedaddle_android_Skedaddle_pan(JNIEnv *, jobject, jint dx, jint dy)
{
    if (!g_inited) return;
    sk_mutex_lock(g_mu);
    mv_pan_pixels(&g_app.view, (int)dx, (int)dy);
    sk_mutex_unlock(g_mu);
}

extern "C" JNIEXPORT void JNICALL
Java_com_skedaddle_android_Skedaddle_zoomAt(JNIEnv *, jobject,
                                            jfloat dzoom, jint px, jint py)
{
    if (!g_inited) return;
    sk_mutex_lock(g_mu);
    mv_zoom_at(&g_app.view, (double)dzoom, (int)px, (int)py);
    sk_mutex_unlock(g_mu);
}

extern "C" JNIEXPORT void JNICALL
Java_com_skedaddle_android_Skedaddle_setView(JNIEnv *, jobject,
                                             jdouble lat, jdouble lon, jdouble zoom)
{
    if (!g_inited) return;
    sk_mutex_lock(g_mu);
    g_app.view.lat = (double)lat;
    g_app.view.lon = (double)lon;
    g_app.view.zoom = (double)zoom;
    sk_mutex_unlock(g_mu);
}

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_skedaddle_android_Skedaddle_getView(JNIEnv *env, jobject)
{
    jdouble out[3] = {0, 0, 0};
    if (g_inited) { out[0]=g_app.view.lat; out[1]=g_app.view.lon; out[2]=g_app.view.zoom; }
    jdoubleArray a = env->NewDoubleArray(3);
    if (a) env->SetDoubleArrayRegion(a, 0, 3, out);
    return a;
}

/* 屏幕像素 → 经纬度（用于点选起/终点、点选地名）。 */
extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_skedaddle_android_Skedaddle_unproject(JNIEnv *env, jobject,
                                              jint px, jint py)
{
    jdouble out[2] = {0, 0};
    if (g_inited) {
        sk_mutex_lock(g_mu);
        mv_unproject(&g_app.view, (int)px, (int)py, &out[0], &out[1]);
        sk_mutex_unlock(g_mu);
    }
    jdoubleArray a = env->NewDoubleArray(2);
    if (a) env->SetDoubleArrayRegion(a, 0, 2, out);
    return a;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_skedaddle_android_Skedaddle_search(JNIEnv *env, jobject, jstring q)
{
    if (!g_inited) return nullptr;
    const char *qs = q ? env->GetStringUTFChars(q, nullptr) : nullptr;
    int n = 0;
    {
        sk_mutex_lock(g_mu);
        n = app_search_query(&g_app, qs ? qs : "");
        sk_mutex_unlock(g_mu);
    }
    if (qs) env->ReleaseStringUTFChars(q, qs);
    if (n <= 0) return env->NewStringUTF("");

    /* 拼成 "name|lat|lon\n" 多行串，Java 端按行拆；避免跨 JNI 返复杂结构。 */
    char buf[16 * 96];  /* 16 命中 * (64 名 + 数字余量) */
    buf[0] = '\0';
    sk_mutex_lock(g_mu);
    for (int i = 0; i < n && i < 16; i++) {
        const SearchHit *h = &g_app.su.hits[i];
        char line[96];
        int k = snprintf(line, sizeof(line), "%.60s|%.6f|%.6f\n",
                         h->name, h->lat, h->lon);
        if (k > 0 && (size_t)k < sizeof(line))
            strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
    }
    sk_mutex_unlock(g_mu);
    return env->NewStringUTF(buf);
}

extern "C" JNIEXPORT void JNICALL
Java_com_skedaddle_android_Skedaddle_setStart(JNIEnv *, jobject, jdouble lat, jdouble lon)
{
    if (!g_inited) return;
    sk_mutex_lock(g_mu);
    app_nav_click(&g_app, (double)lat, (double)lon);  /* 第一次=起点，第二次=终点并触发算路 */
    sk_mutex_unlock(g_mu);
}

extern "C" JNIEXPORT void JNICALL
Java_com_skedaddle_android_Skedaddle_clearRoute(JNIEnv *, jobject)
{
    if (!g_inited) return;
    sk_mutex_lock(g_mu);
    app_nav_clear(&g_app);
    sk_mutex_unlock(g_mu);
}

/* 同步规划（无人值守式，与离屏审计同路径）：直接填 a->rr，渲染自动显示路线。 */
extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_skedaddle_android_Skedaddle_planRoute(JNIEnv *env, jobject,
                                               jdouble alat, jdouble alon,
                                               jdouble blat, jdouble blon)
{
    jdouble out[3] = {0, 0, 0};  /* ok, total_m, total_s */
    if (!g_inited) return env->NewDoubleArray(3);
    sk_mutex_lock(g_mu);
    int rc = app_nav_plan_sync(&g_app, (double)alat, (double)alon,
                                         (double)blat, (double)blon);
    if (rc == 0 && g_app.have_rr) {
        out[0] = 1;
        out[1] = g_app.rr.total_m;
        out[2] = g_app.rr.total_s;
    }
    sk_mutex_unlock(g_mu);
    jdoubleArray a = env->NewDoubleArray(3);
    if (a) env->SetDoubleArrayRegion(a, 0, 3, out);
    return a;
}

extern "C" JNIEXPORT void JNICALL
Java_com_skedaddle_android_Skedaddle_setShowFlags(JNIEnv *, jobject,
                                                 jint poi, jint labels, jint contours)
{
    if (!g_inited) return;
    sk_mutex_lock(g_mu);
    g_app.show_poi = poi ? 1 : 0;
    g_app.show_labels = labels ? 1 : 0;
    g_app.show_contours = contours ? 1 : 0;
    sk_mutex_unlock(g_mu);
}

extern "C" JNIEXPORT void JNICALL
Java_com_skedaddle_android_Skedaddle_shutdown(JNIEnv *, jobject)
{
    if (!g_inited) return;
    sk_mutex_lock(g_mu);
    app_shutdown(&g_app);
    sk_mutex_unlock(g_mu);
    g_inited = 0;
}
