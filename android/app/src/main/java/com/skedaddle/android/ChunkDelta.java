package com.skedaddle.android;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;

/**
 * NMBIN M2 的纯 JDK 内核（不 import android.* / org.json，主机可 javac + 夹具实测）。
 *
 * <p>O5：按 4 KiB 块差分 —— plan() 比对本地/远端块哈希求出变化块下标，
 * apply() 只从 Source 取变化块、其余块从本地旧文件复制，按偏移原地重组，
 * 最后用整包 sha256 校验（写错一块必然被抓）。
 * <p>O4：verifyAndCommit() 是「下载完先算整包哈希再落盘」的唯一收敛点。
 *
 * <p>★ 块表只是清单，NMBIN 运行时读取路径一个字节都不改；本类只在下载/安装边界跑。
 */
public final class ChunkDelta {

    public static final int CHUNK_SIZE = 4096;

    private ChunkDelta() {}

    /** 远端块源；实现方负责统计真实传输量（夹具用内存源，Android 用 HTTP Range）。 */
    public interface Source {
        byte[] fetch(int index) throws IOException;
    }

    /** HTTP Range 块源：一次请求只取一个 4 KiB 块；服务端不支持 206 时直接失败并回落整包。 */
    public static final class HttpRangeSource implements Source {
        private final String url;
        private final long size;
        private final int chunkSize;
        private long bytes;
        private int requests;

        public HttpRangeSource(String url, long size) { this(url, size, CHUNK_SIZE); }

        public HttpRangeSource(String url, long size, int chunkSize) {
            this.url = url;
            this.size = size;
            this.chunkSize = chunkSize;
        }

        @Override
        public byte[] fetch(int index) throws IOException {
            long from = (long) index * chunkSize;
            long to = Math.min(from + chunkSize, size) - 1;
            if (from >= size || to < from) {
                throw new IOException("块下标越界: " + index);
            }
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod("GET");
            c.setConnectTimeout(15000);
            c.setReadTimeout(30000);
            c.setRequestProperty("Range", "bytes=" + from + "-" + to);
            int code;
            try {
                code = c.getResponseCode();
                if (code != HttpURLConnection.HTTP_PARTIAL) {
                    throw new IOException("服务端未返回 206 Partial Content（code=" + code
                            + "），不支持按块差分");
                }
                ByteArrayOutputStream bo = new ByteArrayOutputStream();
                try (InputStream in = c.getInputStream()) {
                    byte[] b = new byte[8192];
                    int n;
                    while ((n = in.read(b)) > 0) bo.write(b, 0, n);
                }
                byte[] out = bo.toByteArray();
                requests++;
                bytes += out.length;
                return out;
            } finally {
                c.disconnect();
            }
        }

        public long transferredBytes() { return bytes; }
        public int requests() { return requests; }
    }

    /** 变化块下标（升序）。远端比本地长时，多出来的块全部算变化。 */
    public static int[] plan(List<String> local, List<String> remote) {
        ArrayList<Integer> idx = new ArrayList<>();
        for (int i = 0; i < remote.size(); i++) {
            String l = (i < local.size()) ? local.get(i) : null;
            String r = remote.get(i);
            if (l == null || r == null || !l.equalsIgnoreCase(r)) idx.add(i);
        }
        int[] out = new int[idx.size()];
        for (int i = 0; i < out.length; i++) out[i] = idx.get(i);
        return out;
    }

    /**
     * 以 base 为底重组到 out：changed 里的块从 src 取，其余从 base 复制。
     * 先写 .part、整包 sha256 通过后才改名；任何失败都删掉半包。
     */
    public static void apply(File base, File out, Source src, int[] changed,
                             int chunkSize, long remoteSize, String expectedSha256)
            throws IOException {
        if (remoteSize < 0 || chunkSize <= 0) {
            throw new IOException("差分参数非法");
        }
        File parent = out.getAbsoluteFile().getParentFile();
        if (parent != null) parent.mkdirs();
        File part = new File(parent, out.getName() + ".part");
        long count = (remoteSize + chunkSize - 1) / chunkSize;
        MessageDigest md = newSha256();
        byte[] buf = new byte[chunkSize];
        int ci = 0;
        try (RandomAccessFile in = new RandomAccessFile(base, "r");
             RandomAccessFile raf = new RandomAccessFile(part, "rw")) {
            raf.setLength(0);
            for (long i = 0; i < count; i++) {
                long off = i * chunkSize;
                int want = (int) Math.min((long) chunkSize, remoteSize - off);
                boolean isChanged = (ci < changed.length && changed[ci] == i);
                if (isChanged) ci++;
                if (isChanged) {
                    byte[] b = src.fetch((int) i);
                    if (b.length != want) {
                        throw new IOException("块 " + i + " 长度 " + b.length + " != " + want);
                    }
                    System.arraycopy(b, 0, buf, 0, want);
                } else {
                    in.seek(off);
                    in.readFully(buf, 0, want);
                }
                raf.seek(off);
                raf.write(buf, 0, want);
                md.update(buf, 0, want);
            }
        } catch (IOException e) {
            part.delete();
            throw e;
        }
        if (part.length() != remoteSize) {
            part.delete();
            throw new IOException("重组长度 " + part.length() + " != " + remoteSize + "（半包）");
        }
        String got = hex(md.digest());
        if (expectedSha256 != null && !expectedSha256.isEmpty()
                && !expectedSha256.equalsIgnoreCase(got)) {
            part.delete();
            throw new IOException("重组后整包 sha256 不符（写错块/篡改）：期望 "
                    + expectedSha256 + "，实得 " + got);
        }
        if (out.exists() && !out.delete()) {
            part.delete();
            throw new IOException("无法覆盖 " + out);
        }
        if (!part.renameTo(out)) {
            part.delete();
            throw new IOException("rename failed: " + out);
        }
    }

    /** O4：下载完成后校验并落盘。expected 为空 = 老服务端「未校验」，行为与旧版一致。 */
    public static boolean verifyAndCommit(File part, File dest,
                                          String expectedSha256, String computedSha256)
            throws IOException {
        boolean verified = expectedSha256 != null && !expectedSha256.isEmpty();
        if (verified && !expectedSha256.equalsIgnoreCase(computedSha256)) {
            part.delete();                    // 错包必须删半包，不能留着被下次误装
            throw new IOException("sha256 不符：期望 " + expectedSha256
                    + "，实得 " + computedSha256);
        }
        if (dest.exists() && !dest.delete()) {
            part.delete();
            throw new IOException("无法覆盖 " + dest);
        }
        if (!part.renameTo(dest)) {
            part.delete();
            throw new IOException("rename failed: " + dest);
        }
        return verified;
    }

    public static String sha256Hex(File f) throws IOException {
        MessageDigest md = newSha256();
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = new byte[16384];
            int n;
            while ((n = in.read(b)) > 0) md.update(b, 0, n);
        }
        return hex(md.digest());
    }

    public static String sha256Hex(byte[] data) {
        return hex(newSha256().digest(data));
    }

    private static MessageDigest newSha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("JDK 缺 SHA-256", e);
        }
    }

    public static String hex(byte[] b) {
        StringBuilder s = new StringBuilder(b.length * 2);
        for (byte x : b) {
            s.append(Character.forDigit((x >> 4) & 0xF, 16));
            s.append(Character.forDigit(x & 0xF, 16));
        }
        return s.toString();
    }
}
