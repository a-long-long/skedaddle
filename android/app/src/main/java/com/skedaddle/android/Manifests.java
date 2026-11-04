package com.skedaddle.android;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

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
                m.entries.add(pe);
            }
        }
        return m;
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
