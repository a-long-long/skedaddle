package com.skedaddle.android;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Scanner;

/**
 * App 自更新（OTA）。
 *
 * <p>流程：{@link #fetchUpdate} 拉 {@code <serverBase>/app/update.json}，比较 versionCode；
 * 有新版本则 {@link #downloadApk} 下载到应用私有 {@code updates/}，再 {@link #installApk}
 * 经 FileProvider 调起系统安装器（Android 7+ 必须走 content://，且需
 * {@code REQUEST_INSTALL_PACKAGES} 权限）。
 */
public final class UpdateChecker {

    private static final String TAG = "skedaddle.Update";

    /**
     * 拉取并判断是否有更新。无服务器 / 网络错 / 已最新 都返回 null。
     */
    public static Manifests.AppUpdate fetchUpdate(Prefs prefs, int currentVersionCode)
            throws IOException, org.json.JSONException {
        String base = prefs.getServerBase();
        if (base.isEmpty()) return null;
        String json = httpGet(base + "/app/update.json");
        Manifests.AppUpdate u = Manifests.parseAppUpdate(json);
        return (u.versionCode > currentVersionCode) ? u : null;
    }

    /** 下载 APK 到 <external-files>/updates/skedaddle-update.apk（旧接口，无哈希）。 */
    public static File downloadApk(Context ctx, String apkUrl) throws IOException {
        return downloadApk(ctx, apkUrl, "");
    }

    /** 下载 APK；清单给了 sha256 时必须整包校验一致才落盘。 */
    public static File downloadApk(Context ctx, String apkUrl, String expectedSha256)
            throws IOException {
        File dir = new File(ctx.getExternalFilesDir(null), "updates");
        dir.mkdirs();
        File apk = new File(dir, "skedaddle-update.apk");
        boolean verified = new Downloader().downloadVerified(apkUrl, apk,
                expectedSha256, null);
        if (!verified) {
            // 老服务端没有 sha256；仍可安装，但记录“未校验”，别伪装成安全。
            Log.w(TAG, "APK 下载未提供 sha256，依赖 Android 安装器签名校验");
        }
        return apk;
    }

    /** 经 FileProvider 调起系统安装器。 */
    public static void installApk(Context ctx, File apk) {
        Uri uri = FileProvider.getUriForFile(ctx,
                ctx.getPackageName() + ".fileprovider", apk);
        Intent i = new Intent(Intent.ACTION_VIEW);
        i.setDataAndType(uri, "application/vnd.android.package-archive");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_ACTIVITY_NEW_TASK);
        ctx.startActivity(i);
        Log.i(TAG, "install intent for " + apk.getAbsolutePath());
    }

    private static String httpGet(String url) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setRequestMethod("GET");
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        try (InputStream in = c.getInputStream();
             Scanner s = new Scanner(in, StandardCharsets.UTF_8.name())) {
            s.useDelimiter("\\A");
            return s.hasNext() ? s.next() : "";
        } finally {
            c.disconnect();
        }
    }
}
