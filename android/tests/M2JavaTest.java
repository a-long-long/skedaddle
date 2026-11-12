package com.skedaddle.android;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import org.json.JSONException;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.InetSocketAddress;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * NMBIN M2 主机夹具（纯 JDK + 测试替身，无 Android 运行时 / 无真机）。
 *
 * <p>测的是**真实生产类**：Manifests / Downloader / ChunkDelta / ChunkUpdater；
 * HTTP 用本机临时 HttpServer，Range 请求头与传输字节数都从服务端侧记录，
 * 不是客户端自己报数。负向注入改的是「被检查的数据」（清单哈希 / 块内容）。
 *
 * <p>输出标记：MANIFEST-OK / MANIFEST-INJECT-CAUGHT / DELTA-OK /
 * DELTA-ZERO-OK / DELTA-INJECT-CAUGHT / M2-JAVA-OK。
 */
public final class M2JavaTest {

    private static final int CHUNK = 4096;
    private static final String FILE = "nodes.bin";

    public static void main(String[] args) {
        try {
            testManifest();
            testDownloadVerify();
            testDelta(false, false, "DELTA-OK 改 1 块只发 1 个 Range 请求 / 传 4096 B / 重组整包哈希一致");
            testDelta(true, false, "DELTA-ZERO-OK 全等 0 块 / 0 字节 / 0 个 Range 请求");
            testDeltaInject();
            System.out.println("M2-JAVA-OK 清单解析 + 下载校验 + 按块差分 + 三类负向注入全通过");
        } catch (Throwable t) {
            System.out.println("ASSERT-FAIL " + t);
            t.printStackTrace(System.out);
            System.exit(1);
        }
    }

    // ------------------------------------------------------------------
    // O4：清单字段解析 + 老服务端兼容
    // ------------------------------------------------------------------
    private static void testManifest() throws JSONException {
        String good = rep('a', 64);
        String json = "{\"updated\":\"2026-11-12\",\"provinces\":["
                + "{\"id\":\"beijing\",\"name\":\"北京\",\"version\":4,\"size\":12345,"
                + "\"url\":\"/map/beijing.zip\",\"sha256\":\"" + good + "\","
                + "\"chunksUrl\":\"https://cdn.example.com/map/beijing/chunks.json\","
                + "\"chunkSize\":4096},"
                + "{\"id\":\"legacy\",\"name\":\"老省\",\"version\":1,"
                + "\"url\":\"/map/legacy.zip\"}]}";
        Manifests.ProvinceManifest m = Manifests.parseProvinces(json);
        check(m.entries.size() == 2, "清单应有 2 个省");
        Manifests.ProvinceEntry e = m.entries.get(0);
        check(e.hasDigest(), "带哈希条目应标记已校验");
        check(good.equals(e.sha256), "sha256 解析错");
        check(e.hasChunks(), "chunksUrl 应解析出来");
        check(e.chunkSize == CHUNK, "chunkSize 解析错");
        Manifests.ProvinceEntry old = m.entries.get(1);
        check(!old.hasDigest() && !old.hasChunks(), "老服务端条目应标记未校验");
        check(old.chunkSize == CHUNK, "老条目 chunkSize 应取缺省 4096");

        // 块表解析（与 scripts/gen_chunks.py 的 snake_case 键名一致）
        Manifests.ChunkTable t = Manifests.parseChunks(chunksJson(pattern(CHUNK + 17, 3)));
        check(t.version == 1 && t.chunkSize == CHUNK && "sha256".equals(t.algo), "块表口径解析错");
        check(t.files.containsKey(FILE), "块表应含 nodes.bin");
        check(t.files.get(FILE).chunks.size() == 2, "4096+17 字节应为 2 块");
        System.out.println("MANIFEST-OK 带哈希条目解析正确 / 缺哈希条目向后兼容 / 块表字段口径一致");
    }

    // ------------------------------------------------------------------
    // O4：下载后校验。错哈希必须拒绝安装并删掉半包。
    // ------------------------------------------------------------------
    private static void testDownloadVerify() throws Exception {
        byte[] pkg = pattern(7500, 9);
        String correct = ChunkDelta.sha256Hex(pkg);
        Server srv = new Server(null, "", false, pkg);
        File dir = Files.createTempDirectory("sk_m2_dl_").toFile();
        try {
            File dest = new File(dir, "beijing.zip");
            boolean rejected = false;
            try {
                new Downloader().downloadVerified(srv.base + "pkg.zip", dest,
                        rep('0', 64), null);
            } catch (IOException expected) {
                rejected = true;
            }
            check(rejected, "错哈希必须抛 IOException");
            check(!dest.exists(), "错哈希绝不能落盘");
            check(!new File(dir, "beijing.zip.part").exists(), "错哈希必须删掉 .part 半包");

            boolean verified = new Downloader().downloadVerified(
                    srv.base + "pkg.zip", dest, correct, null);
            check(verified, "正确哈希应返回 verified=true");
            check(Arrays.equals(pkg, Files.readAllBytes(dest.toPath())), "落盘字节应与服务端一致");

            File legacy = new File(dir, "legacy.zip");
            boolean legacyOk = new Downloader().downloadVerified(
                    srv.base + "pkg.zip", legacy, "", null);
            check(!legacyOk, "缺哈希应返回 verified=false（未校验）");
            check(legacy.exists(), "老服务端无哈希仍应允许安装");
            System.out.println("MANIFEST-INJECT-CAUGHT 错哈希被拒且半包删除 / 正确哈希落盘 / 缺哈希兼容");
        } finally {
            srv.close();
            deleteTree(dir);
        }
    }

    // ------------------------------------------------------------------
    // O5：按块差分。remote 与 local 只差 1 块（或全等），服务端记录 Range 请求。
    // ------------------------------------------------------------------
    private static void testDelta(boolean identical, boolean corrupt, String okMark)
            throws Exception {
        byte[] local = pattern(CHUNK * 3, 1);
        byte[] remote = local.clone();
        if (!identical) remote[CHUNK + 7] ^= 0x5A;      // 只改第 1 块
        String remoteTable = chunksJson(remote);
        Server srv = new Server(remote, remoteTable, corrupt, null);
        File dir = Files.createTempDirectory("sk_m2_delta_").toFile();
        try {
            write(new File(dir, FILE), local);
            write(new File(dir, "chunks.json"), chunksJson(local));

            ChunkUpdater upd = new ChunkUpdater(dir, srv.base + "chunks.json",
                    M2JavaTest::httpGet);
            ChunkUpdater.Result r = upd.update();
            check(r.verified, "差分结果应为 verified");
            check(r.changedBlocks == (identical ? 0 : 1),
                    "变化块数应为 " + (identical ? 0 : 1) + "，实际 " + r.changedBlocks);
            check(r.bytes == (identical ? 0 : CHUNK),
                    "传输字节应为 " + (identical ? 0 : CHUNK) + "，实际 " + r.bytes);
            check(srv.ranges.size() == (identical ? 0 : 1),
                    "Range 请求数应为 " + (identical ? 0 : 1) + "，实际 " + srv.ranges.size());
            if (!identical) {
                check("bytes=4096-8191".equals(srv.ranges.get(0)),
                        "Range 头应只取第 1 块，实际 " + srv.ranges.get(0));
                check(Arrays.equals(remote, Files.readAllBytes(new File(dir, FILE).toPath())),
                        "重组结果应与远端逐字节一致");
                check(ChunkDelta.sha256Hex(remote).equals(
                        ChunkDelta.sha256Hex(new File(dir, FILE))), "重组整包 sha256 不一致");
                Manifests.ChunkTable now = Manifests.parseChunks(
                        new String(Files.readAllBytes(new File(dir, "chunks.json").toPath()),
                                StandardCharsets.UTF_8));
                check(ChunkDelta.sha256Hex(remote).equals(now.files.get(FILE).sha256),
                        "本地块表应更新为远端版本");
            }
            System.out.println(okMark);
        } finally {
            srv.close();
            deleteTree(dir);
        }
    }

    /** 负向：服务端返回被篡改的块，重组必须被整包 sha256 抓到且旧图不被替换。 */
    private static void testDeltaInject() throws Exception {
        byte[] local = pattern(CHUNK * 3, 1);
        byte[] remote = local.clone();
        remote[CHUNK + 7] ^= 0x5A;
        Server srv = new Server(remote, chunksJson(remote), true, null);
        File dir = Files.createTempDirectory("sk_m2_neg_").toFile();
        try {
            write(new File(dir, FILE), local);
            String localTable = chunksJson(local);
            write(new File(dir, "chunks.json"), localTable);
            boolean caught = false;
            try {
                new ChunkUpdater(dir, srv.base + "chunks.json", M2JavaTest::httpGet).update();
            } catch (IOException expected) {
                caught = true;
            }
            check(caught, "写错一块必须被整包 sha256 抓到");
            check(Arrays.equals(local, Files.readAllBytes(new File(dir, FILE).toPath())),
                    "失败后旧数据文件必须原样保留");
            check(localTable.equals(new String(Files.readAllBytes(
                    new File(dir, "chunks.json").toPath()), StandardCharsets.UTF_8)),
                    "失败后旧块表必须原样保留");
            check(!new File(dir, FILE + ".new").exists()
                            && !new File(dir, FILE + ".new.part").exists(),
                    "失败后 .new 半包必须被删掉");
            System.out.println("DELTA-INJECT-CAUGHT 重组写错一块被整包 sha256 抓到，旧图与旧块表未被替换");
        } finally {
            srv.close();
            deleteTree(dir);
        }
    }

    // ------------------------------------------------------------------
    // 工具
    // ------------------------------------------------------------------
    private static final class Server implements AutoCloseable {
        final HttpServer hs;
        final List<String> ranges = new ArrayList<String>();
        final String base;
        private final byte[] data;
        private final String table;
        private final boolean corrupt;
        private final byte[] pkg;

        Server(byte[] data, String table, boolean corrupt, byte[] pkg) throws IOException {
            this.data = data;
            this.table = table;
            this.corrupt = corrupt;
            this.pkg = pkg;
            hs = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
            hs.createContext("/chunks.json", ex ->
                    respond(ex, 200, table.getBytes(StandardCharsets.UTF_8), null));
            hs.createContext("/nodes.bin", ex -> {
                String r = ex.getRequestHeaders().getFirst("Range");
                if (r == null) {
                    respond(ex, 200, data, null);
                    return;
                }
                ranges.add(r);
                String[] ab = r.substring("bytes=".length()).split("-");
                int from = Integer.parseInt(ab[0]);
                int to = Integer.parseInt(ab[1]);
                byte[] part = Arrays.copyOfRange(data, from, to + 1);
                if (corrupt) part[0] ^= 0x33;      // 改「被检查的数据」：块内容
                String cr = "bytes " + from + "-" + to + "/" + data.length;
                respond(ex, 206, part, cr);
            });
            if (pkg != null) {
                hs.createContext("/pkg.zip", ex -> respond(ex, 200, pkg, null));
            }
            hs.setExecutor(null);
            hs.start();
            base = "http://127.0.0.1:" + hs.getAddress().getPort() + "/";
        }

        private static void respond(HttpExchange ex, int code, byte[] body, String contentRange)
                throws IOException {
            if (contentRange != null) {
                ex.getResponseHeaders().add("Content-Range", contentRange);
            }
            ex.sendResponseHeaders(code, body.length);
            try (OutputStream os = ex.getResponseBody()) {
                os.write(body);
            } finally {
                ex.close();
            }
        }

        @Override public void close() { hs.stop(0); }
    }

    private static String httpGet(String url) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setRequestMethod("GET");
        c.setConnectTimeout(5000);
        c.setReadTimeout(10000);
        try (InputStream in = c.getInputStream()) {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] b = new byte[4096];
            int n;
            while ((n = in.read(b)) > 0) bo.write(b, 0, n);
            return new String(bo.toByteArray(), StandardCharsets.UTF_8);
        } finally {
            c.disconnect();
        }
    }

    /** 与 scripts/gen_chunks.py 输出同构的紧凑块表（snake_case 键）。 */
    private static String chunksJson(byte[] data) {
        StringBuilder s = new StringBuilder();
        s.append("{\"version\":1,\"chunk_size\":4096,\"algo\":\"sha256\",\"files\":{");
        s.append("\"").append(FILE).append("\":{\"size\":").append(data.length)
                .append(",\"sha256\":\"").append(ChunkDelta.sha256Hex(data))
                .append("\",\"record_sha256\":\"\",\"nmbin_digest\":\"\",\"chunks\":[");
        for (int i = 0; i < data.length; i += CHUNK) {
            if (i > 0) s.append(',');
            int n = Math.min(CHUNK, data.length - i);
            s.append('"').append(ChunkDelta.sha256Hex(
                    Arrays.copyOfRange(data, i, i + n))).append('"');
        }
        s.append("]}}}");
        return s.toString();
    }

    private static byte[] pattern(int n, int seed) {
        byte[] b = new byte[n];
        for (int i = 0; i < n; i++) b[i] = (byte) ((i * 31 + seed * 17 + (i >> 8)) & 0xFF);
        return b;
    }

    private static String rep(char c, int n) {
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < n; i++) s.append(c);
        return s.toString();
    }

    private static void write(File f, byte[] b) throws IOException {
        try (FileOutputStream os = new FileOutputStream(f)) { os.write(b); }
    }

    private static void write(File f, String s) throws IOException {
        write(f, s.getBytes(StandardCharsets.UTF_8));
    }

    private static void check(boolean cond, String msg) {
        if (!cond) throw new AssertionError(msg);
    }

    private static void deleteTree(File f) {
        File[] sub = f.listFiles();
        if (sub != null) for (File x : sub) deleteTree(x);
        f.delete();
    }
}
