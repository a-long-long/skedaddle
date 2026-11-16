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
 * ★ 提交阶段全是 {@link AtomicCommit#commit}（rename(2) 原子替换），按「数据文件
 *   先、块表最后」的顺序落地：进程在任何一刻被杀，目录里都只有「旧的一套」或
 *   「部分新的数据 + 旧块表」，不会有「文件缺失」或「块表谎报数据已更新」。
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
        /* 先清上次被杀留下的临时文件：.new / .new.part / chunks.json.new 从不
         * 会被自动接管，攒着白占几百 MB；而且一个残留的旧 .new 很容易在
         * 排障时被当成"新版本已经下好了"（2026-11-16）。 */
        cleanStaleTemps(remote);

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
            // 每块都走同一条原子替换：旧代码是先 delete(cur) 再 renameTo(cur)，
            // 两步之间被杀就只剩 .new、正式文件消失，而块表还是旧的 ⇒ 下次更新
            // 按旧块表去 patch 一个不存在的文件（2026-11-16 修）。
            for (Map.Entry<File, File> en : staged.entrySet()) {
                AtomicCommit.commit(en.getValue(), en.getKey());
            }
            File tableTmp = new File(localDir, "chunks.json.new");
            writeAll(tableTmp, remoteJson);
            /* 块表**最后**提交，这是整套提交里唯一的顺序约束：它一旦更新就等于
             * 宣称「数据文件都是远端版本了」。任何更早的中断只会留下「数据新 +
             * 块表旧」——块表旧时 plan() 会多取几个块，但 apply() 拼出来的仍是
             * 远端内容且整包 sha256 校验通过，下次运行自然收敛（见 M2JavaTest
             * 的 COMMIT-RECOVER-OK）。反过来先提交块表，就会留下「块表说新、
             * 文件是旧」的组合，那才是真正的谎报。 */
            AtomicCommit.commit(tableTmp, localTable);
            r.verified = true;
            return r;
        } catch (IOException | RuntimeException e) {
            for (File tmp : staged.values()) AtomicCommit.discard(tmp);
            AtomicCommit.discard(new File(localDir, "chunks.json.new"));
            throw e;
        }
    }

    /**
     * 清掉上次中断留下的临时文件。只碰本类自己命名的三样（{@code <name>.new}、
     * {@code <name>.new.part}、{@code chunks.json.new}），绝不碰正式文件——
     * 万一命名规则漂了，最坏也只是漏删，不会删到数据。
     */
    private void cleanStaleTemps(Manifests.ChunkTable remote) {
        AtomicCommit.discard(new File(localDir, "chunks.json.new"));
        for (String name : remote.files.keySet()) {
            if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.contains("..")) continue;
            AtomicCommit.discard(new File(localDir, name + ".new"));
            AtomicCommit.discard(new File(localDir, name + ".new.part"));
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
