package org.json;

import java.util.ArrayList;

/** 测试替身：M2 夹具用到的 org.json 子集（真机仍是 Android 自带实现）。 */
public class JSONArray {
    private final ArrayList<Object> a = new ArrayList<Object>();

    void addRaw(Object v) { a.add(v); }

    public int length() { return a.size(); }

    public Object opt(int i) { return (i >= 0 && i < a.size()) ? a.get(i) : null; }

    public String getString(int i) throws JSONException {
        Object v = opt(i);
        if (!(v instanceof String)) throw new JSONException("array getString: " + i);
        return (String) v;
    }

    public JSONObject getJSONObject(int i) throws JSONException {
        Object v = opt(i);
        if (!(v instanceof JSONObject)) throw new JSONException("array getJSONObject: " + i);
        return (JSONObject) v;
    }
}
