package com.skedaddle.android;

import org.json.JSONException;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * NMBIN M2 / O5：按块差分更新的编排层（纯 JDK，可在本机夹具里端到端实测）。
 *
 * <p>流程：拉远端 chunks.json → 读本地 chunks.json → 逐文件调 ChunkDelta.plan()
 * 求变化块 → 只对变化块发 HTTP Range → ChunkDelta.apply() 按偏移重组并校验整包
 * sha256 → 全部成功后才提交（先数据文件、最后块表）。
 *
 * <p>★ 任何一步失败都删掉 .new 半包、保留旧图不动，由调用方回落整包下载。
 * ★ 本地没有 chunks.json、远端新增文件、服务端不支持 206：都直接回落，不硬撑。
 */
public final class ChunkUpdater {

    public static class Result {
        public int files;             // 实际重组的文件数
        public int changedBlocks;    // 变化块总数
        public long bytes;           // 真实传输字节（HttpRangeSource 统计）
        public boolean verified;     // 全部文件整包哈希通过
    }

    /** 拉文本清单；Android 侧接 DownloadActivity.httpGet，夹具直接接本地 HTTP 服务。 */
    public interface Fetcher {
        String getText(String url) throws IOException;
    }

    private final File localDir;
    private final String chunksUrl;
    private final Fetcher fetch;

    public ChunkUpdater(File localDir, String chunksUrl, Fetcher fetch) {
        this.localDir = localDir;
        this.chunksUrl = chunksUrl;
        this.fetch = fetch;
    }

    public Result update() throws IOException, JSONException {
        String remoteJson = fetch.getText(chunksUrl);
        Manifests.ChunkTable remote = Manifests.parseChunks(remoteJson);
        if (remote.version != 1 || remote.chunkSize != ChunkDelta.CHUNK_SIZE
                || !"sha256".equals(remote.algo)) {
            throw new IOException("块表版本/口径不支持（需 v1 / 4096 / sha256）");
        }
        File localTable = new File(localDir, "chunks.json");
        if (!localTable.isFile()) {
            throw new IOException("本地无 chunks.json ⇒ 回落整包下载");
        }
        Manifests.ChunkTable local = Manifests.parseChunks(readAll(localTable));

        int slash = chunksUrl.lastIndexOf('/');
        String base = (slash >= 0) ? chunksUrl.substring(0, slash + 1) : "";
        Result r = new Result();
        LinkedHashMap<File, File> staged = new LinkedHashMap<>();
        try {
            for (Map.Entry<String, Manifests.ChunkFile> en : remote.files.entrySet()) {
                String name = en.getKey();
                if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.contains("..")) {
                    throw new IOException("块表文件名非法: " + name);
                }
                Manifests.ChunkFile rf = en.getValue();
                Manifests.ChunkFile lf = local.files.get(name);
                File cur = new File(localDir, name);
                int[] plan = ChunkDelta.plan(
                        lf != null ? lf.chunks : Collections.<String>emptyList(), rf.chunks);

                // 全等：连文件都不重写，网络 0 字节。
                if (plan.length == 0 && cur.isFile() && cur.length() == rf.size
                        && ChunkDelta.sha256Hex(cur).equalsIgnoreCase(rf.sha256)) {
                    continue;
                }
                if (!cur.isFile()) {
                    throw new IOException("新增/缺失文件需整包下载: " + name);
                }
                File tmp = new File(localDir, name + ".new");
                ChunkDelta.HttpRangeSource src = new ChunkDelta.HttpRangeSource(
                        base + name, rf.size, remote.chunkSize);
                ChunkDelta.apply(cur, tmp, src, plan, remote.chunkSize, rf.size, rf.sha256);
                r.changedBlocks += plan.length;
                r.bytes += src.transferredBytes();
                r.files++;
                staged.put(cur, tmp);
            }

            // 提交阶段：只有全部文件重组 + 整包哈希都通过才会走到这里。
            for (Map.Entry<File, File> en : staged.entrySet()) {
                File cur = en.getKey();
                File tmp = en.getValue();
                if (cur.exists() && !cur.delete()) {
                    throw new IOException("无法替换 " + cur);
                }
                if (!tmp.renameTo(cur)) {
                    throw new IOException("rename failed: " + cur);
                }
            }
            File tableTmp = new File(localDir, "chunks.json.new");
            writeAll(tableTmp, remoteJson);
            if (localTable.exists() && !localTable.delete()) {
                throw new IOException("无法替换块表");
            }
            if (!tableTmp.renameTo(localTable)) {
                throw new IOException("rename failed: " + localTable);
            }
            r.verified = true;
            return r;
        } catch (IOException | RuntimeException e) {
            for (File tmp : staged.values()) tmp.delete();
            new File(localDir, "chunks.json.new").delete();
            throw e;
        }
    }

    public static String readAll(File f) throws IOException {
        byte[] b = new byte[(int) f.length()];
        try (FileInputStream in = new FileInputStream(f)) {
            int off = 0;
            while (off < b.length) {
                int n = in.read(b, off, b.length - off);
                if (n <= 0) break;
                off += n;
            }
        }
        return new String(b, 0, b.length, java.nio.charset.StandardCharsets.UTF_8);
    }

    public static void writeAll(File f, String s) throws IOException {
        try (OutputStream os = new FileOutputStream(f)) {
            os.write(s.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        }
    }
}
