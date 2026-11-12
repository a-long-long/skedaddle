package android.util;

/** 测试替身：主机夹具没有 Android 运行时；真机用系统 android.util.Log。 */
public final class Log {
    private Log() {}

    public static int i(String tag, String msg) { System.out.println("I/" + tag + ": " + msg); return 0; }
    public static int w(String tag, String msg) { System.out.println("W/" + tag + ": " + msg); return 0; }
    public static int e(String tag, String msg) { System.out.println("E/" + tag + ": " + msg); return 0; }
}
