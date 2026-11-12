package com.skedaddle.android;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * 服务端清单解析（org.json，Android 自带，零依赖）。
 *
 * <p>两类清单：
 *  · 省图清单  {@code <serverBase>/map/manifest.json}
 *  · 应用更新  {@code <serverBase>/app/update.json}
 * 见 docs/android.md §11 的格式说明。
 */
public final class Manifests {

    /** 单个省的可用版本（url 可为完整 http(s) 或相对 serverBase 的路径）。 */
    public static class ProvinceEntry {
        public String id;       // 目录名，如 beijing
        public String name;     // 展示名
        public long version;    // 清单里的版本戳（>0）
        public long size;       // 字节（可选，仅展示）
        public String url;      // 完整或相对地址
        // M2 / O4：整包 sha256（省 zip 的全文件哈希）。空串 = 老服务端，允许解析但标记未校验。
        public String sha256 = "";
        // M2 / O5：该省 chunks.json 的 URL；为空则回落整包下载。
        public String chunksUrl = "";
        public long chunkSize = 4096;

        public boolean hasDigest() { return sha256 != null && !sha256.isEmpty(); }
        public boolean hasChunks() { return chunksUrl != null && !chunksUrl.isEmpty(); }
    }

    public static class ProvinceManifest {
        public String updated = "";
        public List<ProvinceEntry> entries = new ArrayList<>();
    }

    public static ProvinceManifest parseProvinces(String json)
            throws JSONException {
        ProvinceManifest m = new ProvinceManifest();
        JSONObject o = new JSONObject(json);
        m.updated = o.optString("updated", "");
        JSONArray a = o.optJSONArray("provinces");
        if (a != null) {
            for (int i = 0; i < a.length(); i++) {
                JSONObject e = a.getJSONObject(i);
                ProvinceEntry pe = new ProvinceEntry();
                pe.id = e.getString("id");
                pe.name = e.optString("name", pe.id);
                pe.version = e.optLong("version", 1);
                pe.size = e.optLong("size", 0);
                pe.url = e.getString("url");
                pe.sha256 = e.optString("sha256", "");
                pe.chunksUrl = e.optString("chunksUrl", "");
                pe.chunkSize = e.optLong("chunkSize", 4096);
                m.entries.add(pe);
            }
        }
        return m;
    }

    // ---- M2 / O3：块表 chunks.json（格式由 scripts/gen_chunks.py 定义） --------
    // 服务端把每个省目录里的 chunks.json 原样发布即可；客户端只用它算差分，
    // 运行时地图加载路径不读这张表。

    public static class ChunkFile {
        public String name = "";
        public long size;
        public String sha256 = "";        // 全文件 sha256（O5 重组后校验）
        public String recordSha256 = "";  // NMBIN 记录区 sha256（与 O2 头内摘要闭环）
        public String nmbinDigest = "";   // 头内 O2 摘要；旧格式产物为空
        public List<String> chunks = new ArrayList<>();
    }

    public static class ChunkTable {
        public int version;
        public int chunkSize = 4096;
        public String algo = "";
        public Map<String, ChunkFile> files = new LinkedHashMap<>();
    }

    public static ChunkTable parseChunks(String json) throws JSONException {
        ChunkTable t = new ChunkTable();
        JSONObject o = new JSONObject(json);
        t.version = o.optInt("version", 0);
        // chunks.json 由 scripts/gen_chunks.py 生成，键名是 snake_case；
        // 省图清单 manifest.json 才是 camelCase（chunksUrl / chunkSize），别串台。
        t.chunkSize = o.optInt("chunk_size", 4096);
        t.algo = o.optString("algo", "");
        JSONObject fs = o.optJSONObject("files");
        if (fs == null) return t;
        Iterator<String> it = fs.keys();
        while (it.hasNext()) {
            String name = it.next();
            JSONObject e = fs.getJSONObject(name);
            ChunkFile cf = new ChunkFile();
            cf.name = name;
            cf.size = e.optLong("size", 0);
            cf.sha256 = e.optString("sha256", "");
            cf.recordSha256 = e.optString("record_sha256", "");
            cf.nmbinDigest = e.optString("nmbin_digest", "");
            JSONArray a = e.optJSONArray("chunks");
            if (a != null) {
                for (int i = 0; i < a.length(); i++) cf.chunks.add(a.getString(i));
            }
            t.files.put(name, cf);
        }
        return t;
    }

    /** 应用更新信息；versionCode 大于已装版本才需要更新。 */
    public static class AppUpdate {
        public int versionCode;
        public String versionName = "";
        public String apkUrl = "";
        public String notes = "";
    }

    public static AppUpdate parseAppUpdate(String json) throws JSONException {
        AppUpdate u = new AppUpdate();
        JSONObject o = new JSONObject(json);
        u.versionCode = o.getInt("versionCode");
        u.versionName = o.optString("versionName", "");
        u.apkUrl = o.getString("apkUrl");
        u.notes = o.optString("notes", "");
        return u;
    }
}
