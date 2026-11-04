package com.skedaddle.android;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * 运行时配置中枢（SharedPreferences 兜底，不依赖任何服务端）。
 *
 * <p>这是“架构可扩展”的落点：以后新增功能开关、服务器地址、用户偏好都从这里读写，
 * 不往 Activity 里塞散落的常量。所有 key 集中在此，新增开关只需加一对 getter/setter
 * 并在 {@link SettingsActivity} 接上 UI。
 */
public final class Prefs {

    private static final String NAME = "skedaddle_prefs";
    private static final String K_SERVER_BASE = "server_base";
    private static final String K_AUTO_UPDATE = "auto_update";
    private static final String K_LAST_UPDATE_CHECK = "last_update_check";
    private static final String K_FLAG_POI = "flag_poi";
    private static final String K_FLAG_LABELS = "flag_labels";
    private static final String K_FLAG_CONTOURS = "flag_contours";

    private final SharedPreferences mSp;

    public Prefs(Context ctx) {
        mSp = ctx.getApplicationContext()
                .getSharedPreferences(NAME, Context.MODE_PRIVATE);
    }

    /** 更新服务器基址（去掉结尾斜杠）。空串 = 仅本地/手动获取。 */
    public String getServerBase() {
        String s = mSp.getString(K_SERVER_BASE, "");
        return stripSlash(s);
    }
    public void setServerBase(String s) {
        mSp.edit().putString(K_SERVER_BASE, stripSlash(s)).apply();
    }

    public boolean getAutoUpdate() {
        return mSp.getBoolean(K_AUTO_UPDATE, false);
    }
    public void setAutoUpdate(boolean v) {
        mSp.edit().putBoolean(K_AUTO_UPDATE, v).apply();
    }

    public long getLastUpdateCheck() {
        return mSp.getLong(K_LAST_UPDATE_CHECK, 0);
    }
    public void setLastUpdateCheck(long t) {
        mSp.edit().putLong(K_LAST_UPDATE_CHECK, t).apply();
    }

    public boolean getFlagPoi()     { return mSp.getBoolean(K_FLAG_POI, true); }
    public boolean getFlagLabels()  { return mSp.getBoolean(K_FLAG_LABELS, true); }
    public boolean getFlagContours(){ return mSp.getBoolean(K_FLAG_CONTOURS, true); }
    public void setFlags(boolean poi, boolean labels, boolean contours) {
        mSp.edit()
                .putBoolean(K_FLAG_POI, poi)
                .putBoolean(K_FLAG_LABELS, labels)
                .putBoolean(K_FLAG_CONTOURS, contours)
                .apply();
    }

    /** 服务器是否已配置（为空=仅本地/手动获取）。 */
    public boolean hasServer() {
        return !getServerBase().isEmpty();
    }

    private static String stripSlash(String s) {
        if (s == null) return "";
        s = s.trim();
        while (s.endsWith("/")) s = s.substring(0, s.length() - 1);
        return s;
    }
}
