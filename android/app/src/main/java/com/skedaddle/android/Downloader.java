package com.skedaddle.android;

import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * 通用 HTTP 下载（HttpURLConnection，零第三方依赖）+ zip 解包。
 * 地图省图与 App 自更新（APK）共用，避免两套下载逻辑。
 */
public final class Downloader {

    /** 进度回调：bytesRead 已读字节，total 总字节（未知为 -1），done 是否完成。 */
    public interface Progress {
        void on(long bytesRead, long total, boolean done);
    }

    private static final String TAG = "skedaddle.Downloader";
    private static final int BUF = 16384;
    private volatile boolean mCancelled = false;

    public void cancel() { mCancelled = true; }

    /** 下载 url 到 dest（先写 .part，成功才改名）。total 未知时回调 -1。 */
    public void download(String url, File dest, Progress p) throws IOException {
        mCancelled = false;
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setRequestMethod("GET");
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setInstanceFollowRedirects(true);
        int code = c.getResponseCode();
        if (code != HttpURLConnection.HTTP_OK) {
            c.disconnect();
            throw new IOException("HTTP " + code + " for " + url);
        }
        long total = c.getContentLength();
        File parent = dest.getParentFile();
        if (parent != null) parent.mkdirs();
        File part = new File(dest.getParentFile(), dest.getName() + ".part");
        try (InputStream in = c.getInputStream();
             OutputStream os = new FileOutputStream(part)) {
            byte[] b = new byte[BUF];
            int n;
            long read = 0;
            while ((n = in.read(b)) > 0) {
                if (mCancelled) {
                    os.close();
                    part.delete();
                    throw new IOException("cancelled");
                }
                os.write(b, 0, n);
                read += n;
                if (p != null) p.on(read, total, false);
            }
        } finally {
            c.disconnect();
        }
        if (!part.renameTo(dest)) {
            part.delete();
            throw new IOException("rename failed: " + dest);
        }
        if (p != null) p.on(dest.length(), total, true);
        Log.i(TAG, "downloaded " + dest.getAbsolutePath() + " (" + dest.length() + "B)");
    }

    /**
     * 解压 zip 到 outDir（java.util.zip，Android 自带）。
     * 防 zip-slip：跳过逸出目标目录的条目。
     */
    public static void extractZip(File zip, File outDir) throws IOException {
        outDir.mkdirs();
        String base = outDir.getCanonicalPath() + File.separator;
        try (ZipInputStream zis = new ZipInputStream(new FileInputStream(zip))) {
            byte[] buf = new byte[8192];
            ZipEntry e;
            while ((e = zis.getNextEntry()) != null) {
                File f = new File(outDir, e.getName());
                String canon = f.getCanonicalPath();
                if (!canon.equals(base) && !canon.startsWith(base)) {
                    zis.closeEntry();           // 防 zip-slip
                    continue;
                }
                if (e.isDirectory()) {
                    f.mkdirs();
                } else {
                    File parent = f.getParentFile();
                    if (parent != null) parent.mkdirs();
                    try (OutputStream os = new FileOutputStream(f)) {
                        int n;
                        while ((n = zis.read(buf)) > 0) os.write(buf, 0, n);
                    }
                }
                zis.closeEntry();
            }
        }
    }
}
