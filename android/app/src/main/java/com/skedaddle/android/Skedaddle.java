package com.skedaddle.android;

/**
 * JNI 桥：对应 cpp/skedaddle_jni.cpp 里 Java_com_skedaddle_android_Skedaddle_*。
 * 类全名 com.skedaddle.android.Skedaddle 决定 JNI 函数名，改包名/类名必须同步改 cpp。
 *
 * <p>所有方法都是 static native：C 核心状态是一个进程级的全局 App（g_app），
 * 不随 Java 对象走，故用类方法最省事。调用前必须先 init 成功。
 */
public final class Skedaddle {

    static {
        // libskedaddle.so（CMakeLists OUTPUT_NAME=skedaddle）。
        System.loadLibrary("skedaddle");
    }

    private Skedaddle() {}

    /**
     * 初始化 C 核心。
     * @param graphDir 图集根（products/graph 或单省目录），部署到设备后的绝对路径
     * @param filesDir context.getFilesDir() 绝对路径（持久私有）
     * @param cacheDir context.getCacheDir() 绝对路径（临时）
     * @param fontDir  已从 assets 抽出的字体目录（通常是 filesDir/fonts）
     * @param w,h      初始画布尺寸（像素）；Surface 变化后每帧会再同步
     * @param fontPath 生效字体文件绝对路径（传 "" / null 用候选链兜底）
     * @param fontSize 字号 pt（<=0 用默认 14）
     * @return 是否成功（rc==0）
     */
    public static native boolean init(String graphDir, String filesDir, String cacheDir,
                                      String fontDir, int w, int h,
                                      String fontPath, int fontSize);

    /** 把一帧画进 pixels（长度必须 == w*h，ARGB_8888）。每帧调用。 */
    public static native void render(int[] pixels, int w, int h);

    /** 单指拖动：屏幕位移（像素），正方向=内容跟随手指。 */
    public static native void pan(int dx, int dy);

    /** 以 (px,py) 为锚点缩放；dzoom>1 放大，<1 缩小（双指捏合传距离比）。 */
    public static native void zoomAt(float dzoom, int px, int py);

    /** 直接设定视口中心与缩放（double：lat, lon, zoom）。 */
    public static native void setView(double lat, double lon, double zoom);

    /** 取当前视口 [lat, lon, zoom]。 */
    public static native double[] getView();

    /** 屏幕像素 → 经纬度 [lat, lon]（点选起/终点用）。 */
    public static native double[] unproject(int px, int py);

    /**
     * 同步搜索，返回多行 "名称|纬度|经度\n…"，最多 16 条；无命中返回空串。
     */
    public static native String search(String q);

    /** 点选一个导航点：第一次=起点，第二次=终点（触发后台算路），第三次=新起点。 */
    public static native void setStart(double lat, double lon);

    /** 清除当前路线与起终点。 */
    public static native void clearRoute();

    /**
     * 同步规划一条路线，返回 [ok(1/0), total_m, total_s]；ok==0 表示不可达。
     * 结果由 C 核心记录，渲染自动画出。
     */
    public static native double[] planRoute(double alat, double alon,
                                            double blat, double blon);

    /** 显示开关：poi/labels/contours 各 0/1（设置对话框用）。 */
    public static native void setShowFlags(int poi, int labels, int contours);

    /** 释放资源（Activity onDestroy 调）。 */
    public static native void shutdown();
}
