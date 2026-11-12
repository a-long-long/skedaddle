package org.json;

/** 测试替身：M2 主机夹具只需要这个异常类型；真机用 Android 自带 org.json。 */
public class JSONException extends Exception {
    public JSONException(String message) { super(message); }
}
