package com.skedaddle.android;

import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

/**
 * skedaddle Android 前端主 Activity。
 *
 * <p>职责（UI 状态全在 Java 侧，C 核心只管地图/搜索/算路）：
 *   · 抽取字体 assets → files 目录，给 native init 用
 *   · 顶栏按钮：搜索 / 导航 / 清除 / 设置 / 下载
 *   · 搜索面板：调 Skedaddle.search，点结果定位
 *   · 导航模式：点地图设起/终点 → Skedaddle.setStart + planRoute
 *   · 设置对话框：POI / 地名 / 等高线开关 → Skedaddle.setShowFlags
 *   · 底栏：实时显示视口中心与缩放
 */
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "skedaddle.Main";
    private static final String FONT_ASSET = "fonts/DroidSansFallback.ttf";

    private MapView mMap;
    private EditText mQuery;
    private ListView mResults;
    private TextView mInfo;
    private LinearLayout mSearchPanel;

    private boolean mNavMode = false;
    private boolean mHasStart = false;
    private double mStartLat, mStartLon;

    private Prefs mPrefs;
    private File mGraphDir;
    private AlertDialog mNoMapDialog;
    /** 去了下载/设置页，返回时 onResume 自动重试 init。 */
    private boolean mExpectMapChange = false;
    private boolean mRetrying = false;

    private final ArrayList<String> mResultNames = new ArrayList<>();
    private final ArrayList<double[]> mResultCoords = new ArrayList<>();
    private ArrayAdapter<String> mResultAdapter;

    private final Handler mUi = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mMap = findViewById(R.id.map);
        mQuery = findViewById(R.id.query);
        mResults = findViewById(R.id.results);
        mInfo = findViewById(R.id.infobar);
        mSearchPanel = findViewById(R.id.searchPanel);

        // 字体：从 assets 抽到 files 目录（C 核心在 Android 上靠它找字体）。
        String filesDir = getFilesDir().getAbsolutePath();
        String cacheDir = getCacheDir().getAbsolutePath();
        String fontDir = filesDir + "/fonts";
        String fontPath = extractFont(fontDir) ? (fontDir + "/" + "DroidSansFallback.ttf") : "";

        // 图集根：默认放到应用私有外部存储 graph/（adb push 进去即可）。
        // 真机可改这里或读取 SharedPreferences。
        File ext = getExternalFilesDir(null);
        String base = (ext != null) ? ext.getAbsolutePath() : getFilesDir().getAbsolutePath();
        mGraphDir = new File(base, "graph");
        mMap.prepare(mGraphDir.getAbsolutePath(), filesDir, cacheDir, fontDir, fontPath, 14);
        mMap.setOnMapTapListener(this::onMapTap);
        mMap.setOnInitListener(new MapView.OnInitListener() {
            @Override public void onInitOk()      { onMapInitOk(); }
            @Override public void onInitFailed()  { onMapInitFailed(); }
        });

        // 配置中枢：启动时应用已保存的显示开关，并决定是否后台查更新。
        mPrefs = new Prefs(this);
        Skedaddle.setShowFlags(mPrefs.getFlagPoi() ? 1 : 0,
                mPrefs.getFlagLabels() ? 1 : 0,
                mPrefs.getFlagContours() ? 1 : 0);

        mResultAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_list_item_1, android.R.id.text1, mResultNames);
        mResults.setAdapter(mResultAdapter);

        wireButtons();
        wireSearch();

        // 底栏刷新：每 500ms 读一次视口。
        mUi.postDelayed(mViewTick, 500);

        if (mPrefs.getAutoUpdate()) maybeCheckUpdate();
    }

    // ------------------------------------------------------------------
    // 按钮
    // ------------------------------------------------------------------
    private void wireButtons() {
        Button bSearch = findViewById(R.id.btnSearch);
        Button bNav = findViewById(R.id.btnNav);
        Button bClear = findViewById(R.id.btnClear);
        Button bSettings = findViewById(R.id.btnSettings);
        Button bDownload = findViewById(R.id.btnDownload);

        bSearch.setOnClickListener(v -> {
            mSearchPanel.setVisibility(
                    mSearchPanel.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE);
            if (mSearchPanel.getVisibility() == View.VISIBLE) mQuery.requestFocus();
        });

        bNav.setOnClickListener(v -> {
            mNavMode = !mNavMode;
            mMap.setNavMode(mNavMode);
            bNav.setText(mNavMode ? R.string.btn_nav_off : R.string.btn_nav_on);
            mHasStart = false;
            Toast.makeText(this, R.string.enter_nav, Toast.LENGTH_SHORT).show();
        });

        bClear.setOnClickListener(v -> {
            Skedaddle.clearRoute();
            mHasStart = false;
            mInfo.setText(R.string.infobar_default);
        });

        bSettings.setOnClickListener(v -> {
            mExpectMapChange = true;
            startActivity(new Intent(this, SettingsActivity.class));
        });

        bDownload.setOnClickListener(v -> {
            mExpectMapChange = true;
            startActivity(new Intent(this, DownloadActivity.class));
        });
    }

    // ------------------------------------------------------------------
    // 搜索面板
    // ------------------------------------------------------------------
    private void wireSearch() {
        mQuery.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == android.view.inputmethod.EditorInfo.IME_NULL
                    || actionId == android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH
                    || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                doSearch(mQuery.getText().toString());
                return true;
            }
            return false;
        });
        // 输入即搜（防抖 250ms）
        mQuery.addTextChangedListener(new TextWatcher() {
            final android.os.Handler h = new android.os.Handler(Looper.getMainLooper());
            Runnable r;
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(Editable s) {
                if (r != null) h.removeCallbacks(r);
                r = () -> doSearch(s.toString());
                h.postDelayed(r, 250);
            }
        });

        mResults.setOnItemClickListener((parent, view, pos, id) -> {
            double[] ll = mResultCoords.get(pos);
            if (ll != null) Skedaddle.setView(ll[0], ll[1], 15.0);
            mSearchPanel.setVisibility(View.GONE);
        });
    }

    private void doSearch(String q) {
        if (q == null || q.trim().isEmpty()) return;
        String out = Skedaddle.search(q.trim());
        mResultNames.clear();
        mResultCoords.clear();
        if (out != null && !out.isEmpty()) {
            for (String line : out.split("\n")) {
                String[] f = line.split("\\|");
                if (f.length >= 3) {
                    try {
                        double lat = Double.parseDouble(f[1]);
                        double lon = Double.parseDouble(f[2]);
                        mResultNames.add(f[0]);
                        mResultCoords.add(new double[]{lat, lon});
                    } catch (NumberFormatException ignored) {}
                }
            }
        }
        if (mResultNames.isEmpty()) mResultNames.add(getString(R.string.search_empty));
        mResultAdapter.notifyDataSetChanged();
    }

    // ------------------------------------------------------------------
    // 导航：点地图设起/终点
    // ------------------------------------------------------------------
    private void onMapTap(double lat, double lon) {
        if (!mHasStart) {
            mHasStart = true;
            mStartLat = lat; mStartLon = lon;
            Skedaddle.setStart(lat, lon);            // pick_a 引脚
            Toast.makeText(this, R.string.start_set, Toast.LENGTH_SHORT).show();
        } else {
            Skedaddle.setStart(lat, lon);            // pick_b 引脚（第 2 次点击）
            double[] r = Skedaddle.planRoute(mStartLat, mStartLon, lat, lon);
            if (r != null && r.length == 3 && r[0] == 1.0) {
                mInfo.setText(String.format(java.util.Locale.US,
                        getString(R.string.route_ok), r[1] / 1000.0, r[2] / 60.0));
            } else {
                Toast.makeText(this, R.string.route_fail, Toast.LENGTH_SHORT).show();
            }
            mHasStart = false; // 下一次点击开始新路线
        }
    }

    // ------------------------------------------------------------------
    // 自动检查应用更新（最多每 24h 一次，发现新版本仅轻提示，不自动下载）
    // ------------------------------------------------------------------
    private void maybeCheckUpdate() {
        long now = System.currentTimeMillis();
        if (now - mPrefs.getLastUpdateCheck() < 24L * 3600 * 1000) return;
        mPrefs.setLastUpdateCheck(now);
        new Thread(() -> {
            try {
                int cur = getPackageManager()
                        .getPackageInfo(getPackageName(), 0).versionCode;
                Manifests.AppUpdate u = UpdateChecker.fetchUpdate(mPrefs, cur);
                if (u != null) {
                    runOnUiThread(() -> Toast.makeText(this,
                            getString(R.string.update_available_fmt, u.versionName),
                            Toast.LENGTH_LONG).show());
                }
            } catch (Exception ignored) {
                // 静默失败（无服务器/网络错）；用户可在设置手动查
            }
        }).start();
    }

    // ------------------------------------------------------------------
    // 底栏：视口刷新
    // ------------------------------------------------------------------
    private final Runnable mViewTick = new Runnable() {
        @Override public void run() {
            double[] v = Skedaddle.getView();
            if (v != null && v.length == 3 && mMap.isInited()) {
                mInfo.setText(String.format(java.util.Locale.US,
                        "%.5f, %.5f  z=%.1f", v[0], v[1], v[2]));
            }
            mUi.postDelayed(this, 500);
        }
    };

    // ------------------------------------------------------------------
    // 字体抽取
    // ------------------------------------------------------------------
    private boolean extractFont(String fontDir) {
        File dir = new File(fontDir);
        if (!dir.exists() && !dir.mkdirs()) return false;
        File out = new File(dir, "DroidSansFallback.ttf");
        if (out.exists() && out.length() > 0) return true;  // 已抽过
        AssetManager am = getAssets();
        try (InputStream in = am.open(FONT_ASSET);
             OutputStream os = new FileOutputStream(out)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            Log.i(TAG, "字体已抽取: " + out.getAbsolutePath());
            return true;
        } catch (IOException e) {
            Log.w(TAG, "字体抽取失败（将回退候选链）：" + e.getMessage());
            return false;
        }
    }

    // ------------------------------------------------------------------
    // 无地图检测 → 引导下载 → 返回自动重载
    // ------------------------------------------------------------------
    @Override
    protected void onResume() {
        super.onResume();
        if (mExpectMapChange) {
            mExpectMapChange = false;
            if (mMap != null && !mMap.isInited()) {
                mRetrying = true;
                mMap.retryInit();
            }
        }
    }

    private void onMapInitOk() {
        if (isFinishing() || isDestroyed()) return;
        if (mNoMapDialog != null && mNoMapDialog.isShowing()) mNoMapDialog.dismiss();
        if (mRetrying) {
            mRetrying = false;
            Toast.makeText(getApplicationContext(),
                    R.string.map_retry_ok, Toast.LENGTH_SHORT).show();
        }
    }

    private void onMapInitFailed() {
        if (isFinishing() || isDestroyed()) return;
        if (mRetrying) {                 // 重试仍失败：别反复弹窗，只轻提示
            mRetrying = false;
            Toast.makeText(getApplicationContext(),
                    R.string.map_retry_fail, Toast.LENGTH_LONG).show();
            return;
        }
        showNoMapDialog();
    }

    private void showNoMapDialog() {
        if (isFinishing() || isDestroyed()) return;
        if (mNoMapDialog != null && mNoMapDialog.isShowing()) return;
        String server = (mPrefs != null && mPrefs.hasServer())
                ? mPrefs.getServerBase() : getString(R.string.server_not_set);
        String msg = getString(R.string.map_missing_msg,
                mGraphDir.getAbsolutePath(), server);
        mNoMapDialog = new AlertDialog.Builder(this)
                .setTitle(R.string.map_missing_title)
                .setMessage(msg)
                .setPositiveButton(R.string.map_missing_download, (d, w) -> {
                    mExpectMapChange = true;
                    startActivity(new Intent(this, DownloadActivity.class));
                })
                .setNeutralButton(R.string.map_missing_settings, (d, w) -> {
                    mExpectMapChange = true;
                    startActivity(new Intent(this, SettingsActivity.class));
                })
                .setNegativeButton(R.string.map_missing_later, null)
                .setCancelable(true)
                .create();
        mNoMapDialog.show();
    }

    @Override
    protected void onDestroy() {
        mUi.removeCallbacks(mViewTick);
        if (mNoMapDialog != null && mNoMapDialog.isShowing()) mNoMapDialog.dismiss();
        mMap.release();
        super.onDestroy();
    }
}
