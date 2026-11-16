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
            testSizeValidation();
            testDelta(false, false, "DELTA-OK 改 1 块只发 1 个 Range 请求 / 传 4096 B / 重组整包哈希一致");
            testDelta(true, false, "DELTA-ZERO-OK 全等 0 块 / 0 字节 / 0 个 Range 请求");
            testDeltaInject();
            testCommitMissingTmp();
            testCommitReplace();
            testCommitRecover();
            System.out.println("M2-JAVA-OK 清单解析 + 下载校验 + 按块差分 + 三类负向注入 + 原子提交全通过");
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
    // 下载器必须校验**实收字节数**（2026-11-16 修）。
    //
    // 旧代码只比 sha256：清单没给哈希的老服务端路径下 `verified=false`，截断包
    // 照样落到 dest —— 装上才在解包/加载时炸，或者得到一个缺文件的省。
    // 三种"字节数不对"各一条负向 + 一条"如实声明必须放行"的正向（防判据过严）。
    // ------------------------------------------------------------------
    private static void testSizeValidation() throws Exception {
        byte[] full = pattern(7500, 11);
        File dir = Files.createTempDirectory("sk_m2_size_").toFile();
        try {
            // ① 清单声明 7500，服务端只给了 5000（Content-Length 诚实 = 5000）：
            //    只有"拿清单 size 核对实收"这条路抓得住它。
            byte[] half = Arrays.copyOf(full, 5000);
            SizeServer honest = new SizeServer(half.length, half, half.length);
            try {
                File dest = new File(dir, "short.zip");
                boolean caught = false;
                try {
                    new Downloader().downloadVerified(honest.base + "pkg.zip", dest,
                            "", full.length, null);
                } catch (IOException expected) {
                    caught = true;
                }
                check(caught, "实收 5000 != 清单 7500 必须抛 IOException");
                check(!dest.exists(), "字节数不符绝不能落盘");
                check(!new File(dir, "short.zip.part").exists(), "字节数不符必须删掉 .part 半包");

                // 正向对照：同一份 5000 字节、清单如实声明 5000 ⇒ 必须放行
                //（否则"一律拒绝"也能让上面三条通过，判据就成了摆设）
                File okDest = new File(dir, "ok.zip");
                boolean verified = new Downloader().downloadVerified(honest.base + "pkg.zip",
                        okDest, "", half.length, null);
                check(!verified, "无哈希应返回 verified=false（未校验）");
                check(okDest.length() == half.length, "字节数如实时应正常落盘");
                check(Arrays.equals(half, Files.readAllBytes(okDest.toPath())),
                        "落盘内容应与服务端一致");
            } finally {
                honest.close();
            }

            // ② 空包：200 + 0 字节（无哈希）。旧代码会落一个 0 字节的 zip。
            SizeServer empty = new SizeServer(-1, new byte[0], 0);
            try {
                File dest = new File(dir, "empty.zip");
                boolean caught = false;
                try {
                    new Downloader().downloadVerified(empty.base + "pkg.zip", dest, "", -1, null);
                } catch (IOException expected) {
                    caught = true;
                }
                check(caught, "0 字节的包必须拒绝");
                check(!dest.exists(), "0 字节的包绝不能落盘");
                check(!new File(dir, "empty.zip.part").exists(), "0 字节的包必须删掉 .part");
            } finally {
                empty.close();
            }

            // ③ 服务端谎报：声明 7500、只发 5000 就断。客户端可能自己抛
            //    "premature EOF"，也可能读到短包返回 —— 两条路都必须落到
            //    "抛异常 + 不落盘 + 不留半包"。
            SizeServer liar = new SizeServer(full.length, half, half.length);
            try {
                File dest = new File(dir, "liar.zip");
                boolean caught = false;
                try {
                    new Downloader().downloadVerified(liar.base + "pkg.zip", dest, "", -1, null);
                } catch (IOException expected) {
                    caught = true;
                }
                check(caught, "声明 7500 实发 5000 必须抛 IOException");
                check(!dest.exists(), "谎报长度的截断包绝不能落盘");
                check(!new File(dir, "liar.zip.part").exists(), "谎报长度时必须删掉 .part");
            } finally {
                liar.close();
            }
            System.out.println("SIZE-INJECT-CAUGHT 实收字节数与清单/Content-Length 不符被拒"
                    + "（含 0 字节空包），如实声明仍放行");
        } finally {
            deleteTree(dir);
        }
    }

    // ------------------------------------------------------------------
    // 提交原子性（2026-11-16 修）：临时文件缺失时绝不能先把正式文件删掉。
    //
    // 旧代码的提交是两步：`dest.delete()` → `tmp.renameTo(dest)`。第二步失败
    // （半包被清理掉了 / 目标目录不可写 / 命名漂了）时，第一步已经把旧文件删了
    // ⇒ 目录里既没有旧文件也没有新文件。这条判据就是钉住"提交前不删旧"：
    // 同一个调用在旧代码下必挂（dest 消失），在新代码下旧文件原样保留。
    // ------------------------------------------------------------------
    private static void testCommitMissingTmp() throws Exception {
        File dir = Files.createTempDirectory("sk_m2_commit_").toFile();
        try {
            File dest = new File(dir, "nodes.bin");
            byte[] old = pattern(9000, 3);
            write(dest, old);
            File missing = new File(dir, "nodes.bin.part");   // 故意不存在：模拟半包没了
            boolean caught = false;
            try {
                ChunkDelta.verifyAndCommit(missing, dest,
                        ChunkDelta.sha256Hex(old), ChunkDelta.sha256Hex(old));
            } catch (IOException expected) {
                caught = true;
            }
            check(caught, "临时文件缺失必须抛 IOException");
            check(dest.isFile() && Arrays.equals(old, Files.readAllBytes(dest.toPath())),
                    "临时文件缺失时旧文件必须原样保留（旧代码会先 delete 掉它）");
            System.out.println("COMMIT-INJECT-CAUGHT 半包缺失被拒且旧文件逐字节未变（提交不再先删目标）");
        } finally {
            deleteTree(dir);
        }
    }

    /** 正常路径：一次替换整块内容、覆盖已存在的目标、不留临时文件。 */
    private static void testCommitReplace() throws Exception {
        File dir = Files.createTempDirectory("sk_m2_repl_").toFile();
        try {
            File dest = new File(dir, "nodes.bin");
            byte[] old = pattern(5000, 4);
            byte[] neu = pattern(5000, 5);
            write(dest, old);

            File part = new File(dir, "nodes.bin.part");
            write(part, neu);
            ChunkDelta.verifyAndCommit(part, dest,
                    ChunkDelta.sha256Hex(neu), ChunkDelta.sha256Hex(neu));
            check(Arrays.equals(neu, Files.readAllBytes(dest.toPath())), "提交后内容应为新版本");
            check(!part.exists(), "提交后临时文件不应残留");

            // 再提交一次（目标已存在）：靠 rename 的替换语义覆盖，不需要先删
            File part2 = new File(dir, "nodes.bin.part");
            write(part2, old);
            ChunkDelta.verifyAndCommit(part2, dest,
                    ChunkDelta.sha256Hex(old), ChunkDelta.sha256Hex(old));
            check(Arrays.equals(old, Files.readAllBytes(dest.toPath())), "二次提交应能覆盖已存在的目标");
            check(!part2.exists(), "二次提交后临时文件同样不应残留");
            System.out.println("COMMIT-ATOMIC-OK 临时文件一次替换正式文件 / 覆盖已存在目标 / 无临时残留");
        } finally {
            deleteTree(dir);
        }
    }

    /**
     * 提交顺序：数据文件先、块表最后。中途死在块表这一步，必须留下
     * 「数据已是新版 + 块表还是旧版」的可自愈中间态，而不是「文件没了」或
     * 「块表说新、文件是旧」。二次运行必须收敛（不回落整包、不报错）。
     */
    private static void testCommitRecover() throws Exception {
        byte[] local = pattern(CHUNK * 3, 1);
        byte[] remote = local.clone();
        remote[CHUNK + 7] ^= 0x5A;
        Server srv = new Server(remote, chunksJson(remote), false, null);
        File dir = Files.createTempDirectory("sk_m2_rec_").toFile();
        try {
            write(new File(dir, FILE), local);
            String oldTable = chunksJson(local);
            write(new File(dir, "chunks.json"), oldTable);

            /* 造一次"数据已提交、块表没提交"的中断：把 chunks.json.new 做成
             * 非空目录 ⇒ writeAll 打不开文件 ⇒ update 在块表这一步抛异常。
             * 顺序保证它发生在数据提交之后（否则测的就不是这件事）。 */
            File block = new File(dir, "chunks.json.new");
            check(block.mkdirs(), "应能建占位目录");
            write(new File(block, "keep"), "keep");
            boolean caught = false;
            try {
                new ChunkUpdater(dir, srv.base + "chunks.json", M2JavaTest::httpGet).update();
            } catch (IOException expected) {
                caught = true;
            }
            check(caught, "块表写入失败必须抛 IOException");
            check(Arrays.equals(remote, Files.readAllBytes(new File(dir, FILE).toPath())),
                    "中断前数据文件应已提交为远端版本");
            check(oldTable.equals(new String(Files.readAllBytes(
                    new File(dir, "chunks.json").toPath()), StandardCharsets.UTF_8)),
                    "块表必须还是旧版（块表最后提交，不许抢先）");

            deleteTree(block);
            ChunkUpdater.Result r = new ChunkUpdater(dir, srv.base + "chunks.json",
                    M2JavaTest::httpGet).update();
            check(r.verified, "恢复运行应 verified");
            check(Arrays.equals(remote, Files.readAllBytes(new File(dir, FILE).toPath())),
                    "恢复后内容应为远端版本");
            check(ChunkDelta.sha256Hex(remote).equals(ChunkDelta.sha256Hex(new File(dir, FILE))),
                    "恢复后整包 sha256 应一致");
            Manifests.ChunkTable now = Manifests.parseChunks(
                    new String(Files.readAllBytes(new File(dir, "chunks.json").toPath()),
                            StandardCharsets.UTF_8));
            check(ChunkDelta.sha256Hex(remote).equals(now.files.get(FILE).sha256),
                    "恢复后块表应更新为远端版本");
            System.out.println("COMMIT-RECOVER-OK 数据先提交/块表最后提交：中断后二次运行收敛到远端");
        } finally {
            srv.close();
            deleteTree(dir);
        }
    }

    // ------------------------------------------------------------------
    // 工具
    // ------------------------------------------------------------------
    /**
     * 只服务一个"字节数不对"的包：declared = 声明的 Content-Length（-1 = 无体），
     * sent = 实际写出的字节数。声明 &gt; 实发时 HttpServer 关流会报"没写满"，
     * 客户端随即看到截断 —— 这正是判据要制造的现场。
     */
    private static final class SizeServer implements AutoCloseable {
        final HttpServer hs;
        final String base;

        SizeServer(long declared, byte[] body, int sent) throws IOException {
            hs = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
            hs.createContext("/pkg.zip", ex -> {
                try {
                    ex.sendResponseHeaders(200, declared);
                    OutputStream os = ex.getResponseBody();
                    if (sent > 0) os.write(body, 0, Math.min(sent, body.length));
                    os.flush();
                    os.close();
                } catch (IOException ignore) {
                    /* 声明 > 实发：关流时"没写满"，就是要的截断 */
                } finally {
                    ex.close();          /* HttpExchange.close() 不抛受检异常 */
                }
            });
            hs.setExecutor(null);
            hs.start();
            base = "http://127.0.0.1:" + hs.getAddress().getPort() + "/";
        }

        @Override public void close() { hs.stop(0); }
    }

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
