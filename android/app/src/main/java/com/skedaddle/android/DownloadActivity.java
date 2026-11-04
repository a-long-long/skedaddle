package com.skedaddle.android;

import android.app.ProgressDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * 地图数据增量更新（取代 v1 桩）。
 *
 * <p>功能：
 *  · 拉取省图清单 {@code <serverBase>/map/manifest.json}（可配置；无服务器则列出内置省、需本地安装）
 *  · 扫描本地 {@code graph/} 已装省 + 版本（写在 {@code <省>/.skversion}）
 *  · 每行显示状态：未安装 / 已装 vX / 可更新 → vY，并提供下载/更新/重下按钮
 *  · 远程下载经 {@link Downloader}；本地安装用“从本地文件安装”选 zip（{@code ACTION_OPEN_DOCUMENT}）
 *
 * <p>注意：C 核心在 init 时一次性加载 graph 根，新增/更新省后需重启应用才会被加载。
 */
public class DownloadActivity extends AppCompatActivity {

    // 内置省（服务器未配置时的兜底清单；id 与 products/graph/ 目录名一致）。
    private static final String[] BUILTIN = {
            "beijing", "tianjin", "hebei", "shanxi", "shaanxi",
            "shandong", "henan", "liaoning"};

    private static final String VERSION_FILE = ".skversion";

    private Prefs mPrefs;
    private File mGraphDir;
    private ListView mList;
    private TextView mNote;
    private ProgressBar mBar;
    private TextView mProgressText;
    private final Handler mUi = new Handler(Looper.getMainLooper());

    private final List<Row> mRows = new ArrayList<>();
    private RowAdapter mAdapter;

    // 选本地 zip 安装
    private final ActivityResultLauncher<String> mPickZip =
            registerForActivityResult(new ActivityResultContracts.GetContent(),
                    this::installFromUri);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        mPrefs = new Prefs(this);
        File ext = getExternalFilesDir(null);
        String base = (ext != null) ? ext.getAbsolutePath() : getFilesDir().getAbsolutePath();
        mGraphDir = new File(base, "graph");

        androidx.appcompat.widget.LinearLayoutCompat root =
                new androidx.appcompat.widget.LinearLayoutCompat(this);
        root.setOrientation(androidx.appcompat.widget.LinearLayoutCompat.VERTICAL);
        root.setPadding(16, 16, 16, 16);

        mNote = new TextView(this);
        mNote.setPadding(0, 0, 0, 8);
        root.addView(mNote);

        Button local = new Button(this);
        local.setText(R.string.download_install_local);
        local.setOnClickListener(v -> mPickZip.launch("application/zip"));
        root.addView(local);

        mList = new ListView(this);
        mList.setLayoutParams(new androidx.appcompat.widget.LinearLayoutCompat
                .LayoutParams(androidx.appcompat.widget.LinearLayoutCompat.LayoutParams.MATCH_PARENT,
                0, 1f));
        root.addView(mList);

        mProgressText = new TextView(this);
        mProgressText.setPadding(0, 8, 0, 4);
        root.addView(mProgressText);

        mBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        mBar.setMax(100);
        mBar.setLayoutParams(new androidx.appcompat.widget.LinearLayoutCompat
                .LayoutParams(androidx.appcompat.widget.LinearLayoutCompat.LayoutParams.MATCH_PARENT,
                androidx.appcompat.widget.LinearLayoutCompat.LayoutParams.WRAP_CONTENT));
        root.addView(mBar);

        setContentView(root);

        mAdapter = new RowAdapter();
        mList.setAdapter(mAdapter);
        mList.setOnItemClickListener((p, v, pos, id) -> onRowAction(mRows.get(pos)));

        refresh();
    }

    // ------------------------------------------------------------------
    // 数据加载：远程清单 + 本地扫描
    // ------------------------------------------------------------------
    private void refresh() {
        mNote.setText(R.string.map_loading);
        new Thread(() -> {
            // 1) 远程清单（可选）
            java.util.Map<String, Manifests.ProvinceEntry> remote = new java.util.HashMap<>();
            if (mPrefs.hasServer()) {
                try {
                    String json = httpGet(mPrefs.getServerBase() + "/map/manifest.json");
                    Manifests.ProvinceManifest m = Manifests.parseProvinces(json);
                    for (Manifests.ProvinceEntry e : m.entries) remote.put(e.id, e);
                } catch (Exception e) {
                    // 拉不到就用内置列表兜底
                }
            }
            // 2) 本地已装
            Set<String> localIds = new HashSet<>();
            File[] sub = mGraphDir.listFiles(File::isDirectory);
            if (sub != null) for (File d : sub) localIds.add(d.getName());

            // 3) 合并成行
            mRows.clear();
            Set<String> seen = new HashSet<>();
            for (Manifests.ProvinceEntry e : remote.values()) {
                seen.add(e.id);
                long localV = readVersion(new File(mGraphDir, e.id));
                Row r = new Row();
                r.id = e.id; r.name = e.name; r.remoteVersion = e.version;
                r.url = e.url; r.localVersion = localV;
                mRows.add(r);
            }
            // 内置省里没有出现在远程的（服务器没配或拉失败）
            for (String id : BUILTIN) {
                if (seen.contains(id)) continue;
                long localV = readVersion(new File(mGraphDir, id));
                Row r = new Row();
                r.id = id; r.name = id; r.remoteVersion = 0; r.url = "";
                r.localVersion = localV;
                mRows.add(r);
            }
            // 本地有但内置/远程都没有的（例如手动 adb 推的）
            for (String id : localIds) {
                if (seen.contains(id)) continue;
                Row r = new Row();
                r.id = id; r.name = id; r.remoteVersion = 0; r.url = "";
                r.localVersion = readVersion(new File(mGraphDir, id));
                mRows.add(r);
            }

            mUi.post(() -> {
                mAdapter.notifyDataSetChanged();
                String srv = mPrefs.hasServer()
                        ? "服务器：" + mPrefs.getServerBase() : "未配置服务器（仅本地安装）";
                mNote.setText(srv + "  ·  本地 " + mGraphDir.getAbsolutePath());
            });
        }).start();
    }

    // ------------------------------------------------------------------
    // 行操作：下载 / 更新 / 重下
    // ------------------------------------------------------------------
    private void onRowAction(Row r) {
        if (r.url != null && !r.url.isEmpty()) {
            String full = r.url.startsWith("http") ? r.url
                    : (mPrefs.getServerBase() + (r.url.startsWith("/") ? "" : "/") + r.url);
            downloadProvince(r, full);
        } else {
            // 无服务器/无 url：引导本地安装
            Toast.makeText(this, R.string.download_from_server_first, Toast.LENGTH_LONG).show();
            mPickZip.launch("application/zip");
        }
    }

    private void downloadProvince(Row r, String url) {
        File zip = new File(getCacheDir(), r.id + ".zip");
        ProgressDialog dlg = new ProgressDialog(this);
        dlg.setMessage(getString(R.string.download_start, r.name));
        dlg.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
        dlg.setMax(100);
        dlg.show();

        new Thread(() -> {
            try {
                new Downloader().download(url, zip, (read, total, done) -> mUi.post(() -> {
                    if (total > 0) {
                        int pct = (int) (read * 100 / total);
                        dlg.setProgress(pct);
                        mProgressText.setText(getString(R.string.download_progress,
                                read / 1024, total / 1024));
                    } else {
                        mProgressText.setText(getString(R.string.download_progress, read / 1024, 0));
                    }
                }));
                applyZip(r.id, zip, r.remoteVersion);
                mUi.post(() -> {
                    dlg.dismiss();
                    Toast.makeText(this, getString(R.string.download_done, r.name),
                            Toast.LENGTH_LONG).show();
                    refresh();
                });
            } catch (Exception e) {
                mUi.post(() -> {
                    dlg.dismiss();
                    Toast.makeText(this, getString(R.string.download_fail, e.getMessage()),
                            Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    // 本地 zip 安装（从 content URI 拷到缓存再解包）
    private void installFromUri(Uri uri) {
        if (uri == null) return;
        ProgressDialog dlg = new ProgressDialog(this);
        dlg.setMessage(getString(R.string.download_install_local));
        dlg.show();
        new Thread(() -> {
            try {
                File zip = new File(getCacheDir(), "local-install.zip");
                try (InputStream in = getContentResolver().openInputStream(uri);
                     OutputStream os = new FileOutputStream(zip)) {
                    byte[] b = new byte[16384];
                    int n;
                    while ((n = in.read(b)) > 0) os.write(b, 0, n);
                }
                // 记录解包前已有的省目录，便于把 .skversion 写到新目录
                Set<String> before = currentProvinceDirs();
                Downloader.extractZip(zip, mGraphDir);
                Set<String> after = currentProvinceDirs();
                for (String id : after) {
                    if (!before.contains(id)) {
                        writeVersion(new File(mGraphDir, id), 0); // 本地安装：版本未知
                    }
                }
                mUi.post(() -> {
                    dlg.dismiss();
                    Toast.makeText(this, R.string.download_done, Toast.LENGTH_LONG).show();
                    refresh();
                });
            } catch (Exception e) {
                mUi.post(() -> {
                    dlg.dismiss();
                    Toast.makeText(this, getString(R.string.download_fail, e.getMessage()),
                            Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    /** 把 zip 解包到 graph/<id>/ 并写版本文件。 */
    private void applyZip(String id, File zip, long version) throws Exception {
        Downloader.extractZip(zip, mGraphDir); // zip 顶层即 <id>/...
        writeVersion(new File(mGraphDir, id), version);
    }

    private Set<String> currentProvinceDirs() {
        Set<String> s = new HashSet<>();
        File[] sub = mGraphDir.listFiles(File::isDirectory);
        if (sub != null) for (File d : sub) s.add(d.getName());
        return s;
    }

    private long readVersion(File provinceDir) {
        File f = new File(provinceDir, VERSION_FILE);
        if (!f.exists()) return -1; // 已装但未知版本
        try {
            String s = new String(java.nio.file.Files.readAllBytes(f.toPath())).trim();
            return Long.parseLong(s);
        } catch (Exception e) {
            return -1;
        }
    }

    private void writeVersion(File provinceDir, long version) {
        if (!provinceDir.isDirectory()) provinceDir.mkdirs();
        try (FileOutputStream os = new FileOutputStream(new File(provinceDir, VERSION_FILE))) {
            os.write(Long.toString(version).getBytes("UTF-8"));
        } catch (Exception ignored) {}
    }

    private String statusText(Row r) {
        if (r.localVersion < 0) return getString(R.string.map_status_unknown);
        if (r.localVersion == 0) return getString(R.string.map_status_none);
        if (r.remoteVersion > r.localVersion)
            return getString(R.string.map_status_update, r.remoteVersion);
        return getString(R.string.map_status_installed, r.localVersion);
    }

    private String actionText(Row r) {
        if (r.localVersion > 0) return getString(R.string.map_action_update);
        return getString(R.string.map_action_install);
    }

    private String httpGet(String url) throws Exception {
        java.net.HttpURLConnection c =
                (java.net.HttpURLConnection) new java.net.URL(url).openConnection();
        c.setRequestMethod("GET");
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        try (java.util.Scanner s = new java.util.Scanner(c.getInputStream(), "UTF-8")) {
            s.useDelimiter("\\A");
            return s.hasNext() ? s.next() : "";
        } finally {
            c.disconnect();
        }
    }

    // ------------------------------------------------------------------
    // 行模型 + 适配器
    // ------------------------------------------------------------------
    private static class Row {
        String id, name, url;
        long remoteVersion; // 0=无远程
        long localVersion;  // 0=未装, -1=已装未知
    }

    private class RowAdapter extends ArrayAdapter<Row> {
        RowAdapter() { super(DownloadActivity.this, 0, mRows); }

        @Override
        public android.view.View getView(int pos, android.view.View cv, android.view.ViewGroup parent) {
            Row r = mRows.get(pos);
            androidx.appcompat.widget.LinearLayoutCompat row =
                    (cv instanceof androidx.appcompat.widget.LinearLayoutCompat)
                            ? (androidx.appcompat.widget.LinearLayoutCompat) cv
                            : new androidx.appcompat.widget.LinearLayoutCompat(DownloadActivity.this);
            row.setOrientation(androidx.appcompat.widget.LinearLayoutCompat.HORIZONTAL);
            row.setPadding(0, 12, 0, 12);

            TextView tv = new TextView(DownloadActivity.this);
            tv.setText(r.name + "\n" + statusText(r));
            tv.setTextColor(getResources().getColor(android.R.color.white));
            tv.setLayoutParams(new androidx.appcompat.widget.LinearLayoutCompat
                    .LayoutParams(0, androidx.appcompat.widget.LinearLayoutCompat.LayoutParams.WRAP_CONTENT, 1f));

            Button btn = new Button(DownloadActivity.this);
            btn.setText(actionText(r));
            btn.setOnClickListener(v -> onRowAction(r));

            row.removeAllViews();
            row.addView(tv);
            row.addView(btn);
            return row;
        }
    }
}
