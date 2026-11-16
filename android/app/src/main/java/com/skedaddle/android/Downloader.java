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
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
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
        downloadVerified(url, dest, null, p);   // 老路径：无清单哈希 ⇒ 未校验但行为不变
    }

    /**
     * M2 / O4：下载完先算整包 sha256，与清单 expectedSha256 一致才落盘。
     * expectedSha256 为空 = 老服务端（允许安装，返回 false 标记「未校验」）。
     * ★ 错哈希必须删掉 .part 半包并抛异常，绝不 rename 到 dest。
     */
    public boolean downloadVerified(String url, File dest, String expectedSha256, Progress p)
            throws IOException {
        return downloadVerified(url, dest, expectedSha256, -1L, p);
    }

    /**
     * 同上，并校验**实际收到的字节数**（2026-11-16 修）。
     *
     * <p>之前只比 sha256：清单没给哈希的老服务端路径下，连接被中途掐断
     * （移动网切换、代理超时、服务端 content-length 与实际不符）会得到一个
     * 截断的 zip/APK，`verified=false` 却照样落到 dest —— 解包到一半才报错，
     * 或者装上一个缺文件的省。
     *
     * @param expectedSize 清单里声明的字节数（<=0 = 清单没给）。**优先于
     *                     Content-Length**：它和 sha256 出自同一份清单；服务端
     *                     用 chunked 不发 Content-Length 时，它是唯一判据。
     */
    public boolean downloadVerified(String url, File dest, String expectedSha256,
                                    long expectedSize, Progress p) throws IOException {
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
        long declared = c.getContentLengthLong();      // -1 = 服务端没声明
        long expect = expectedSize > 0 ? expectedSize : declared;
        File parent = dest.getParentFile();
        if (parent != null) parent.mkdirs();
        File part = new File(dest.getParentFile(), dest.getName() + ".part");
        MessageDigest md;
        try {
            md = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            c.disconnect();
            throw new IOException(e);
        }
        long read = 0;                                // 提到 try 外：字节数要参与判定
        long actual;
        try {
            try (InputStream in = c.getInputStream();
                 OutputStream os = new FileOutputStream(part)) {
                byte[] b = new byte[BUF];
                int n;
                while ((n = in.read(b)) > 0) {
                    if (mCancelled) {
                        os.close();
                        part.delete();
                        throw new IOException("cancelled");
                    }
                    os.write(b, 0, n);
                    md.update(b, 0, n);      // 边下边算，零额外 IO
                    read += n;
                    if (p != null) p.on(read, expect > 0 ? expect : declared, false);
                }
            }
            /* ★ 字节数三道闸，任何一道不过都删半包、绝不落盘：
             *   ① 空包（0 字节）—— 200 + 空体"看起来成功了"，最容易被放过；
             *   ② 实收 != 落盘长度 —— FileOutputStream 无缓冲时恒等，但一旦谁
             *      换成 BufferedOutputStream 又忘了 flush，这条是唯一抓手；
             *   ③ 实收 != 期望（清单 size 优先，其次 Content-Length）—— 截断下载。
             * 期望值未知（清单没 size 且服务端 chunked）时只剩 sha256 兜底，
             * 这条要如实记日志，不能假装校验过。 */
            actual = part.length();
            if (read <= 0) {
                throw new IOException("下载到 0 字节：" + url);
            }
            if (actual != read) {
                throw new IOException("落盘字节 " + actual + " != 实收 " + read + "：" + dest);
            }
            if (expect >= 0 && read != expect) {
                long d = expect - read;
                throw new IOException("下载不完整：" + dest.getName() + " 期望 " + expect
                        + " 字节，实收 " + read + " 字节（"
                        + (d > 0 ? "少 " : "多 ") + Math.abs(d) + "）");
            }
        } catch (IOException | RuntimeException e) {
            /* 传输中途断（服务端谎报长度、连接被掐）也会走到这里：读循环直接
             * 抛 premature EOF。半包一律删掉，别留着被下次误装。 */
            part.delete();
            c.disconnect();
            throw e;
        }
        c.disconnect();
        if (expect < 0) {
            Log.w(TAG, "服务端与清单都没给字节数，只能靠 sha256 兜底：" + dest.getName());
        }
        boolean verified = ChunkDelta.verifyAndCommit(
                part, dest, expectedSha256, ChunkDelta.hex(md.digest()));
        if (p != null) p.on(actual, expect > 0 ? expect : actual, true);
        Log.i(TAG, "downloaded " + dest.getAbsolutePath() + " (" + actual
                + "B, " + (verified ? "sha256 verified" : "unverified/legacy") + ")");
        return verified;
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
