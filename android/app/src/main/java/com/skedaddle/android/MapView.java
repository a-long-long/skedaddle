package com.skedaddle.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * 地图画布：自持一个渲染线程，每帧通过 JNI 调 C 核心把地图画进 int[] 缓冲，
 * 再贴到 SurfaceView 的 Canvas 上（ARGB_8888 Bitmap）。
 *
 * <p>交互手势全部在这里处理，与桌面前端同源：
 *   · 单指拖动 → Skedaddle.pan(dx,dy)
 *   · 双指捏合 → Skedaddle.zoomAt(距离比, 中点x, 中点y)
 *   · 双击      → Skedaddle.zoomAt(2.0, 落点x, 落点y)
 *   · 单击（导航模式下）→ 反投影成经纬度，回调给 MainActivity 设点
 *
 * <p>★ 与 Wayland 教训一致：C 光栅器不写 alpha，native 渲染后每像素已强制
 *   0xFF；这里 Bitmap 用 ARGB_8888，绘制时即不透明，不会花屏。
 */
public class MapView extends SurfaceView implements SurfaceHolder.Callback {

    private static final String TAG = "skedaddle.MapView";
    private static final long FRAME_MS = 33;           // ~30fps
    private static final float TAP_MOVE_PX = 10f;      // 小于此位移算“点”
    private static final long TAP_TIME_MS = 300;       // 单击/双击判定窗口

    // ---- 初始化参数（prepare 时存，surface 就绪后 init）----
    private String mGraphDir, mFilesDir, mCacheDir, mFontDir, mFontPath;
    private int mFontSize = 14;
    private boolean mPrepared = false;

    // ---- 渲染缓冲 ----
    private int mW = 0, mH = 0;
    private int[] mPixels = null;
    private Bitmap mBitmap = null;
    private boolean mInited = false;

    // ---- 渲染线程 ----
    private RenderThread mThread;
    private final Object mInitLock = new Object();

    // ---- 手势状态 ----
    private static final int NONE = 0, PAN = 1, ZOOM = 2;
    private int mMode = NONE;
    private float mLastX, mLastY;
    private float mLastDist;
    private float mDownX, mDownY;
    private long mDownTime;
    private long mLastTapTime;
    private float mLastTapX, mLastTapY;
    private boolean mNavMode = false;

    private OnMapTapListener mTapListener;

    public interface OnMapTapListener {
        /** 导航模式下单击地图，回传该点的经纬度。 */
        void onMapTap(double lat, double lon);
    }

    public MapView(Context context) { super(context); initHolder(); }
    public MapView(Context context, AttributeSet attrs) { super(context, attrs); initHolder(); }

    private void initHolder() {
        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    /** 存初始化参数；surface 第一次就绪时真正调 native init。 */
    public void prepare(String graphDir, String filesDir, String cacheDir,
                        String fontDir, String fontPath, int fontSize) {
        mGraphDir = graphDir; mFilesDir = filesDir; mCacheDir = cacheDir;
        mFontDir = fontDir; mFontPath = fontPath; mFontSize = fontSize;
        mPrepared = true;
    }

    public void setNavMode(boolean on) { mNavMode = on; }
    public boolean isNavMode() { return mNavMode; }
    public void setOnMapTapListener(OnMapTapListener l) { mTapListener = l; }
    public boolean isInited() { return mInited; }

    // ------------------------------------------------------------------
    // Surface 生命周期
    // ------------------------------------------------------------------
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        startThread();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (width <= 0 || height <= 0) return;
        synchronized (mInitLock) {
            mW = width; mH = height;
            mPixels = new int[mW * mH];
            mBitmap = Bitmap.createBitmap(mW, mH, Bitmap.Config.ARGB_8888);
            if (!mInited && mPrepared) {
                boolean ok = Skedaddle.init(mGraphDir, mFilesDir, mCacheDir,
                        mFontDir, mW, mH, mFontPath, mFontSize);
                if (ok) {
                    mInited = true;
                    Log.i(TAG, "native init OK " + mW + "x" + mH);
                } else {
                    Log.e(TAG, "native init FAILED —— 检查 graphDir 路径与图集");
                }
            }
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        stopThread();
    }

    private void startThread() {
        if (mThread == null || !mThread.isAlive()) {
            mThread = new RenderThread();
            mThread.start();
        }
    }

    private void stopThread() {
        if (mThread != null) { mThread.requestStop(); mThread = null; }
    }

    // ------------------------------------------------------------------
    // 渲染线程
    // ------------------------------------------------------------------
    private class RenderThread extends Thread {
        private volatile boolean mStop = false;
        void requestStop() { mStop = true; }

        @Override
        public void run() {
            while (!mStop) {
                SurfaceHolder holder = getHolder();
                Canvas c = null;
                try {
                    c = holder.lockCanvas();
                    if (c != null) {
                        synchronized (mInitLock) {
                            if (mInited && mPixels != null && mBitmap != null) {
                                Skedaddle.render(mPixels, mW, mH);
                                mBitmap.setPixels(mPixels, 0, mW, 0, 0, mW, mH);
                                c.drawBitmap(mBitmap, 0, 0, null);
                            } else {
                                drawInitializing(c);
                            }
                        }
                    }
                } finally {
                    if (c != null) holder.unlockCanvasAndPost(c);
                }
                try { Thread.sleep(FRAME_MS); } catch (InterruptedException e) { break; }
            }
        }

        private void drawInitializing(Canvas c) {
            c.drawColor(Color.BLACK);
            Paint p = new Paint();
            p.setColor(Color.WHITE);
            p.setTextSize(24);
            p.setTextAlign(Paint.Align.CENTER);
            String msg = mPrepared ? "正在加载图集…" : "未初始化（缺少图集路径）";
            c.drawText(msg, c.getWidth() / 2f, c.getHeight() / 2f, p);
        }
    }

    // ------------------------------------------------------------------
    // 手势
    // ------------------------------------------------------------------
    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        int action = ev.getActionMasked();
        int count = ev.getPointerCount();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
                mMode = PAN;
                mLastX = ev.getX(); mLastY = ev.getY();
                mDownX = ev.getX(); mDownY = ev.getY();
                mDownTime = System.currentTimeMillis();
                return true;

            case MotionEvent.ACTION_POINTER_DOWN:
                if (count >= 2) {
                    mMode = ZOOM;
                    mLastDist = pinchDistance(ev);
                }
                return true;

            case MotionEvent.ACTION_MOVE:
                if (mMode == PAN && count == 1) {
                    float x = ev.getX(), y = ev.getY();
                    int dx = (int) (x - mLastX), dy = (int) (y - mLastY);
                    if (dx != 0 || dy != 0) Skedaddle.pan(dx, dy);
                    mLastX = x; mLastY = y;
                } else if (mMode == ZOOM && count >= 2) {
                    float d = pinchDistance(ev);
                    if (mLastDist > 0 && d > 0) {
                        float ratio = d / mLastDist;
                        float mx = (ev.getX(0) + ev.getX(1)) / 2f;
                        float my = (ev.getY(0) + ev.getY(1)) / 2f;
                        Skedaddle.zoomAt(ratio, (int) mx, (int) my);
                    }
                    mLastDist = d;
                }
                return true;

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (mMode == PAN && count == 1) {
                    float moved = (float) Math.hypot(ev.getX() - mDownX, ev.getY() - mDownY);
                    long dt = System.currentTimeMillis() - mDownTime;
                    if (moved < TAP_MOVE_PX && dt < TAP_TIME_MS) handleTap(ev.getX(), ev.getY());
                }
                mMode = NONE;
                return true;

            default:
                return true;
        }
    }

    private void handleTap(float x, float y) {
        long now = System.currentTimeMillis();
        boolean doubleTap = (now - mLastTapTime < TAP_TIME_MS)
                && (float) Math.hypot(x - mLastTapX, y - mLastTapY) < TAP_MOVE_PX * 2;
        mLastTapTime = now; mLastTapX = x; mLastTapY = y;

        if (doubleTap) {
            Skedaddle.zoomAt(2.0f, (int) x, (int) y);
            mLastTapTime = 0; // 消费掉，避免三连击
            return;
        }
        if (mNavMode && mTapListener != null) {
            double[] ll = Skedaddle.unproject((int) x, (int) y);
            if (ll != null && ll.length == 2) mTapListener.onMapTap(ll[0], ll[1]);
        }
    }

    private static float pinchDistance(MotionEvent ev) {
        if (ev.getPointerCount() < 2) return 0f;
        float dx = ev.getX(0) - ev.getX(1);
        float dy = ev.getY(0) - ev.getY(1);
        return (float) Math.hypot(dx, dy);
    }

    /** Activity 销毁时释放原生资源。 */
    public void release() {
        synchronized (mInitLock) {
            if (mInited) { Skedaddle.shutdown(); mInited = false; }
        }
    }
}
