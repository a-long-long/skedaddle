package com.skedaddle.android;

import android.app.ProgressDialog;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;

import java.io.File;

/**
 * 设置中心（取代 v1 的显示对话框）。
 *
 * <p>聚合：更新服务器地址、自动检查应用更新、手动“检查更新”（OTA 下载+安装）、
 * 显示开关（POI/地名/等高线）、关于。所有持久化走 {@link Prefs}。
 */
public class SettingsActivity extends AppCompatActivity {

    private Prefs mPrefs;
    private EditText mEditServer;
    private Switch mSwAuto, mSwPoi, mSwLabels, mSwContours;
    private Button mBtnCheck, mBtnApply;
    private TextView mAbout;
    private final Handler mUi = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_settings);

        mPrefs = new Prefs(this);
        mEditServer = findViewById(R.id.editServer);
        mSwAuto = findViewById(R.id.swAuto);
        mSwPoi = findViewById(R.id.swPoi);
        mSwLabels = findViewById(R.id.swLabels);
        mSwContours = findViewById(R.id.swContours);
        mBtnCheck = findViewById(R.id.btnCheck);
        mBtnApply = findViewById(R.id.btnApplyDisplay);
        mAbout = findViewById(R.id.txtAbout);

        // 初始值
        mEditServer.setText(mPrefs.getServerBase());
        mSwAuto.setChecked(mPrefs.getAutoUpdate());
        mSwPoi.setChecked(mPrefs.getFlagPoi());
        mSwLabels.setChecked(mPrefs.getFlagLabels());
        mSwContours.setChecked(mPrefs.getFlagContours());
        mAbout.setText(aboutText());

        mBtnApply.setOnClickListener(v -> applyDisplay());
        mBtnCheck.setOnClickListener(v -> checkUpdate());
    }

    // ------------------------------------------------------------------
    // 显示开关
    // ------------------------------------------------------------------
    private void applyDisplay() {
        boolean poi = mSwPoi.isChecked();
        boolean labels = mSwLabels.isChecked();
        boolean contours = mSwContours.isChecked();
        mPrefs.setFlags(poi, labels, contours);
        Skedaddle.setShowFlags(poi ? 1 : 0, labels ? 1 : 0, contours ? 1 : 0);
        Toast.makeText(this, R.string.settings_apply_display, Toast.LENGTH_SHORT).show();
    }

    // ------------------------------------------------------------------
    // 应用更新（OTA）
    // ------------------------------------------------------------------
    private void checkUpdate() {
        // 保存服务器地址（检查前）
        mPrefs.setServerBase(mEditServer.getText().toString());
        mPrefs.setAutoUpdate(mSwAuto.isChecked());

        if (!mPrefs.hasServer()) {
            Toast.makeText(this, R.string.update_no_server, Toast.LENGTH_LONG).show();
            return;
        }
        Toast.makeText(this, R.string.update_checking, Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try {
                int cur = getPackageManager()
                        .getPackageInfo(getPackageName(), 0).versionCode;
                Manifests.AppUpdate u = UpdateChecker.fetchUpdate(mPrefs, cur);
                mUi.post(() -> {
                    if (u == null) {
                        Toast.makeText(this, R.string.update_none, Toast.LENGTH_SHORT).show();
                    } else {
                        offerInstall(u);
                    }
                });
            } catch (Exception e) {
                mUi.post(() -> Toast.makeText(this,
                        getString(R.string.update_fail, e.getMessage()), Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    private void offerInstall(Manifests.AppUpdate u) {
        AlertDialog.Builder b = new AlertDialog.Builder(this);
        b.setTitle(getString(R.string.update_available_fmt, u.versionName));
        b.setMessage(u.notes.isEmpty() ? ("v" + u.versionName) : u.notes);
        b.setPositiveButton(android.R.string.ok, (d, w) -> downloadAndInstall(u));
        b.setNegativeButton(android.R.string.cancel, null);
        b.show();
    }

    private void downloadAndInstall(Manifests.AppUpdate u) {
        ProgressDialog dlg = new ProgressDialog(this);
        dlg.setMessage(getString(R.string.update_downloading));
        dlg.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
        dlg.setMax(100);
        dlg.show();
        new Thread(() -> {
            try {
                File apk = UpdateChecker.downloadApk(this, u.apkUrl);
                mUi.post(() -> {
                    dlg.dismiss();
                    Toast.makeText(this, R.string.update_downloaded, Toast.LENGTH_SHORT).show();
                    UpdateChecker.installApk(this, apk);
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

    private String aboutText() {
        String ver = "?";
        int code = 0;
        try {
            PackageManager pm = getPackageManager();
            android.content.pm.PackageInfo pi = pm.getPackageInfo(getPackageName(), 0);
            ver = pi.versionName;
            code = pi.versionCode;
        } catch (PackageManager.NameNotFoundException ignored) {}
        return "skedaddle " + ver + " (code " + code + ")\n"
                + "更新服务器：" + (mPrefs.hasServer() ? mPrefs.getServerBase() : "未配置");
    }
}
