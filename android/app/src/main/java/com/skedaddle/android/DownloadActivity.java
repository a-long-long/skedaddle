package com.skedaddle.android;

import android.os.Bundle;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import java.util.Arrays;
import java.util.List;

/**
 * 下载省图（v1 桩）。
 *
 * <p>真实下载服务未接入：本 Activity 仅列出可选省，并说明如何把桌面端已 build 的
 * 图集部署到设备。后续接下载服务时，这里替换为真实的 HTTP 拉取 + 解包 + 进度条。
 */
public class DownloadActivity extends AppCompatActivity {

    // 与桌面端 products/graph/ 下的省目录一致（shaanxi=陕西, shanxi=山西）。
    private static final List<String> PROVINCES = Arrays.asList(
            "beijing", "tianjin", "hebei", "shanxi", "shaanxi",
            "shandong", "henan", "liaoning");

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        TextView note = new TextView(this);
        note.setPadding(24, 24, 24, 12);
        note.setText("v1 未接入下载服务。\n请在桌面端构建图集后执行：\n"
                + "adb push products/graph /sdcard/Android/data/"
                + getPackageName() + "/files/\n"
                + "然后重启应用即可加载。");

        ListView list = new ListView(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                android.R.layout.simple_list_item_1, android.R.id.text1,
                PROVINCES);
        list.setAdapter(adapter);
        list.setOnItemClickListener((parent, view, pos, id) ->
                Toast.makeText(this, "未接入下载：" + PROVINCES.get(pos)
                        + "（请用 adb 推送）", Toast.LENGTH_LONG).show());

        androidx.appcompat.widget.LinearLayoutCompat root =
                new androidx.appcompat.widget.LinearLayoutCompat(this);
        root.setOrientation(androidx.appcompat.widget.LinearLayoutCompat.VERTICAL);
        root.addView(note);
        root.addView(list, new androidx.appcompat.widget.LinearLayoutCompat.LayoutParams(
                androidx.appcompat.widget.LinearLayoutCompat.LayoutParams.MATCH_PARENT,
                androidx.appcompat.widget.LinearLayoutCompat.LayoutParams.MATCH_PARENT));
        setContentView(root);
    }
}
