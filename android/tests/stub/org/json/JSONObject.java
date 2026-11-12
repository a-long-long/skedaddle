package org.json;

import java.util.Iterator;
import java.util.LinkedHashMap;

/**
 * 测试替身：只实现 M2 夹具用到的 org.json 子集 + 一个最小 JSON 解析器。
 *
 * <p>★ 它**不是** Android 自带 org.json 的完整实现：主机夹具靠它编译并解析
 * 清单/块表，真机跑的是系统实现。夹具只断言「解析出的字段值」，不宣称
 * 替身与系统库在所有边界输入上等价。
 */
public class JSONObject {
    private final LinkedHashMap<String, Object> m = new LinkedHashMap<String, Object>();

    public JSONObject() {}

    public JSONObject(String json) throws JSONException {
        new Parser(json).parseInto(this);
    }

    void putRaw(String k, Object v) { m.put(k, v); }

    public Iterator<String> keys() { return m.keySet().iterator(); }
    public int length() { return m.size(); }
    public boolean has(String k) { return m.containsKey(k); }

    public String optString(String k, String def) {
        Object v = m.get(k);
        if (v instanceof String) return (String) v;
        return (v == null) ? def : String.valueOf(v);
    }

    public String getString(String k) throws JSONException {
        Object v = m.get(k);
        if (!(v instanceof String)) throw new JSONException("getString: " + k);
        return (String) v;
    }

    public long optLong(String k, long def) {
        Object v = m.get(k);
        if (v instanceof Number) return ((Number) v).longValue();
        if (v instanceof String) {
            try { return Long.parseLong((String) v); } catch (NumberFormatException e) { return def; }
        }
        return def;
    }

    public int optInt(String k, int def) { return (int) optLong(k, def); }

    public int getInt(String k) throws JSONException {
        if (!m.containsKey(k)) throw new JSONException("getInt: " + k);
        return (int) optLong(k, 0);
    }

    public JSONObject optJSONObject(String k) {
        Object v = m.get(k);
        return (v instanceof JSONObject) ? (JSONObject) v : null;
    }

    public JSONObject getJSONObject(String k) throws JSONException {
        JSONObject o = optJSONObject(k);
        if (o == null) throw new JSONException("getJSONObject: " + k);
        return o;
    }

    public JSONArray optJSONArray(String k) {
        Object v = m.get(k);
        return (v instanceof JSONArray) ? (JSONArray) v : null;
    }

    public JSONArray getJSONArray(String k) throws JSONException {
        JSONArray a = optJSONArray(k);
        if (a == null) throw new JSONException("getJSONArray: " + k);
        return a;
    }

    private static final class Parser {
        private final String s;
        private int i;

        Parser(String s) { this.s = s; }

        void parseInto(JSONObject o) throws JSONException {
            ws();
            if (cur() != '{') throw err("期望 {");
            i++;
            ws();
            if (cur() == '}') { i++; return; }
            while (true) {
                ws();
                String k = string();
                ws();
                if (cur() != ':') throw err("期望 :");
                i++;
                ws();
                o.putRaw(k, value());
                ws();
                char c = cur();
                if (c == '}') { i++; return; }
                if (c != ',') throw err("期望 , 或 }");
                i++;
            }
        }

        private Object value() throws JSONException {
            ws();
            char c = cur();
            if (c == '{') { JSONObject o = new JSONObject(); parseInto(o); return o; }
            if (c == '[') return array();
            if (c == '"') return string();
            if (c == 't') { lit("true"); return Boolean.TRUE; }
            if (c == 'f') { lit("false"); return Boolean.FALSE; }
            if (c == 'n') { lit("null"); return null; }
            return number();
        }

        private JSONArray array() throws JSONException {
            i++;
            JSONArray a = new JSONArray();
            ws();
            if (cur() == ']') { i++; return a; }
            while (true) {
                a.addRaw(value());
                ws();
                char c = cur();
                if (c == ']') { i++; return a; }
                if (c != ',') throw err("期望 , 或 ]");
                i++;
                ws();
            }
        }

        private String string() throws JSONException {
            if (cur() != '"') throw err("期望字符串");
            i++;
            StringBuilder b = new StringBuilder();
            while (i < s.length()) {
                char c = s.charAt(i++);
                if (c == '"') return b.toString();
                if (c == '\\') {
                    if (i >= s.length()) break;
                    char e = s.charAt(i++);
                    switch (e) {
                        case '"': b.append('"'); break;
                        case '\\': b.append('\\'); break;
                        case '/': b.append('/'); break;
                        case 'b': b.append('\b'); break;
                        case 'f': b.append('\f'); break;
                        case 'n': b.append('\n'); break;
                        case 'r': b.append('\r'); break;
                        case 't': b.append('\t'); break;
                        case 'u':
                            if (i + 4 > s.length()) throw err("坏的 unicode 转义");
                            b.append((char) Integer.parseInt(s.substring(i, i + 4), 16));
                            i += 4;
                            break;
                        default: throw err("坏的转义");
                    }
                } else {
                    b.append(c);
                }
            }
            throw err("字符串没闭合");
        }

        private Number number() throws JSONException {
            int st = i;
            while (i < s.length() && "-+.eE0123456789".indexOf(s.charAt(i)) >= 0) i++;
            if (st == i) throw err("期望值");
            String n = s.substring(st, i);
            try {
                if (n.indexOf('.') < 0 && n.indexOf('e') < 0 && n.indexOf('E') < 0) {
                    return Long.valueOf(n);
                }
                return Double.valueOf(n);
            } catch (NumberFormatException e) {
                throw err("坏数字 " + n);
            }
        }

        private void lit(String w) throws JSONException {
            if (!s.startsWith(w, i)) throw err("期望 " + w);
            i += w.length();
        }

        private char cur() { return i < s.length() ? s.charAt(i) : '\0'; }

        private void ws() {
            while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++;
        }

        private JSONException err(String msg) { return new JSONException(msg + " @" + i); }
    }
}
