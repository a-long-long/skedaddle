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
        File graphDir = new File(base, "graph");
        mMap.prepare(graphDir.getAbsolutePath(), filesDir, cacheDir, fontDir, fontPath, 14);
        mMap.setOnMapTapListener(this::onMapTap);

        mResultAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_list_item_1, android.R.id.text1, mResultNames);
        mResults.setAdapter(mResultAdapter);

        wireButtons();
        wireSearch();

        // 底栏刷新：每 500ms 读一次视口。
        mUi.postDelayed(mViewTick, 500);
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

        bSettings.setOnClickListener(v -> openSettings());

        bDownload.setOnClickListener(v ->
                startActivity(new Intent(this, DownloadActivity.class)));
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
    // 设置对话框
    // ------------------------------------------------------------------
    private void openSettings() {
        boolean[] states = {true, true, true};
        String[] labels = {
                getString(R.string.opt_poi),
                getString(R.string.opt_labels),
                getString(R.string.opt_contours)};
        AlertDialog.Builder b = new AlertDialog.Builder(this);
        b.setTitle(R.string.settings_title);
        b.setMultiChoiceItems(labels, states, (d, which, checked) -> states[which] = checked);
        b.setPositiveButton(android.R.string.ok, (d, w) -> {
            Skedaddle.setShowFlags(states[0] ? 1 : 0, states[1] ? 1 : 0, states[2] ? 1 : 0);
            Toast.makeText(this, "已应用", Toast.LENGTH_SHORT).show();
        });
        b.setNegativeButton(android.R.string.cancel, null);
        b.show();
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

    @Override
    protected void onDestroy() {
        mUi.removeCallbacks(mViewTick);
        mMap.release();
        super.onDestroy();
    }
}
