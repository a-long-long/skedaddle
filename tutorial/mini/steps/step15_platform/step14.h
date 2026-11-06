/* tutorial/mini · 第 14 课新增：极简 i18n（中文 msgid 是安全的）
 *
 * 对照 src/i18n/i18n.c、lang/en.xml。真实工程从 XML 加载词条并做 XML 转义；
 * mini 只保留「查表 + 未登记回退原串 + 转义解码」三件事。
 */
#ifndef MINI_STEP14_H
#define MINI_STEP14_H

#include <stdio.h>
#include <string.h>

typedef struct { const char *id; const char *en; } MiniTr;

static const MiniTr g_mini_tr[] = {
    { "退出", "Quit" },
    { "搜索", "Search" },
    { "设置", "Settings" },
    { "起点", "Start" },
    { "终点", "End" }
};
#define MINI_TR_N ((int)(sizeof g_mini_tr / sizeof g_mini_tr[0]))

static inline const char *mini_tr(const char *id) {
    for (int i = 0; i < MINI_TR_N; i++)
        if (strcmp(g_mini_tr[i].id, id) == 0) return g_mini_tr[i].en;
    return id;   /* 未登记一律回退原串，绝不返回空指针 */
}

static inline void mini_xml_unescape(const char *in, char *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; in[i] && k + 1 < cap; ) {
        if (strncmp(in + i, "&amp;", 5) == 0) { out[k++] = '&'; i += 5; }
        else if (strncmp(in + i, "&lt;", 4) == 0) { out[k++] = '<'; i += 4; }
        else out[k++] = in[i++];
    }
    out[k] = '\0';
}

static void mini_step_14(MiniCtx *c) {
    char dec[32];
    mini_xml_unescape("A&amp;B", dec, sizeof dec);
    MINI_ASSERT(c, strcmp(mini_tr("退出"), "Quit") == 0,
                "step14: 中文 msgid 能查到英文");
    MINI_ASSERT(c, strcmp(mini_tr("未登记"), "未登记") == 0,
                "step14: 未登记词条回退原串");
    MINI_ASSERT(c, strcmp(dec, "A&B") == 0, "step14: XML 实体解码");
    /* 追加：画一个语言切换小框（不用字体，纯几何示意） */
    mini_fill_rect(c, 286, 12, 28, 12, 0x00F8F8F8u);
    mini_line(c, 286, 12, 313, 12, 0x003A4652u);
    mini_line(c, 286, 23, 313, 23, 0x003A4652u);
    printf("MINI-STEP-14-OK  entries=%d ink=%ld\n", MINI_TR_N, mini_ink(c));
    c->step_count++;
}

#endif /* MINI_STEP14_H */
