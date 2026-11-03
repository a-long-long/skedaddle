/* tutorial/16_i18n/main.c — 进阶课 · 极简多语言（i18n）
 *
 * 生产实现见 src/i18n/i18n.c（库 skedaddle_i18n）。本课把它的核心思想
 * 重新实现一遍，让你亲手看到：
 *   1) 为什么"以中文为原文（msgid）"是安全的——中文模式下 i18n_tr 直接返回
 *      传入的中文字串，不引入任何字符串变化，因此既有的像素/UI 自检在 zh 模式
 *      下行为与改前完全一致；
 *   2) 缺译回退链：zh → 外部 lang/<code>.xml → 内置英文 TR[] → 中文原文
 *      （任何情况下都不返回空串）；
 *   3) 怎么"加一门语言"：枚举 + 元数据表 + 一个 lang/<code>.xml；
 *   4) 运行时文件加载如何"覆盖"内置翻译（手改 lang 文件即时生效）。
 *
 * 编译： gcc -std=c11 -O2 -Wall -o tut main.c && ./tut
 * （不依赖项目 src/，纯自包含；真实实现额外支持运行时解析 lang/<code>.xml）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 1. 语言枚举（与 i18n.c 的 I18nLang 同构，这里只演示 zh/en/fr）---- */
typedef enum { LANG_ZH = 0, LANG_EN = 1, LANG_FR = 2, LANG_COUNT } Lang;

/* 语言元数据：code 为 NULL 表示"源语言（中文），无外部文件"。 */
typedef struct { Lang id; const char *code; const char *name; } LangInfo;
static const LangInfo g_langs[] = {
    { LANG_ZH, NULL,    "中文" },
    { LANG_EN, "en",    "English" },
    { LANG_FR, "fr",    "Français" },
};

/* ---- 2. 内置兜底翻译表（生产里是 i18n.c 的 TR[]，全条目都有英文）---- */
typedef struct { const char *zh; const char *tr; } TrPair;   /* tr = 当前语言的译文 */
static const TrPair g_tr[] = {   /* zh 是 key（msgid），tr 是"当前语言"的译文 */
    { "跑路了",        "Skedaddle" },
    { "设置",          "Settings" },
    { "语言",          "Language" },
    { "昼夜模式",      "Day/Night" },
    { "搜索",          "Search" },
    { "退出",          "Quit" },
};
static const size_t g_tr_n = sizeof(g_tr) / sizeof(g_tr[0]);

/* ---- 3. 运行时从 lang/<code>.xml 加载的覆盖表（动态）---- */
static TrPair *g_pairs = NULL;
static size_t  g_pairs_n = 0;

static const char *find_tr(const TrPair *tab, size_t n, const char *zh)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(tab[i].zh, zh) == 0) return tab[i].tr;
    return NULL;
}

/* 极简 XML 字段提取：在 [s,e) 内找 <tag>...</tag>，去掉首尾空白后返回
 * 新 malloc 串（调用方负责 free）。找不到返回 NULL。
 * 注：真实 i18n.c 还还原 &amp; &lt; &gt; 等实体，这里只演示纯文本，便于阅读。 */
static char *extract_tag(const char *s, const char *e, const char *tag)
{
    size_t tlen = strlen(tag);
    for (const char *p = s; p + 1 + tlen < e; p++) {
        if (p[0] == '<' && strncmp(p + 1, tag, tlen) == 0 &&
            (p[1 + tlen] == '>' || p[1 + tlen] == ' ')) {
            const char *cs = p + 1 + tlen;
            while (*cs && *cs != '>') cs++;
            if (!*cs) return NULL;
            cs++;
            char close[64];
            int cn = snprintf(close, sizeof close, "</%s>", tag);
            const char *ce = cs;
            while (ce + cn <= e) {
                if (memcmp(ce, close, (size_t)cn) == 0) break;
                ce++;
            }
            while (cs < ce && (*cs == ' ' || *cs == '\t' || *cs == '\n' || *cs == '\r')) cs++;
            while (ce > cs && (ce[-1] == ' ' || ce[-1] == '\t' || ce[-1] == '\n' || ce[-1] == '\r')) ce--;
            size_t len = (size_t)(ce - cs);
            char *out = (char *)malloc(len + 1);
            if (!out) return NULL;
            memcpy(out, cs, len);
            out[len] = 0;
            return out;
        }
    }
    return NULL;
}

/* 加载 lang/<code>.xml：解析每个 <item><zh>..</zh><code>..</code></item>。
 * 这里把"当前语言标签"也写成 code（en.xml 用 <en>，fr.xml 用 <fr>），与生产一致。
 * 失败（文件缺失）则保持 g_pairs 为空——调用方回退内置 TR[]。 */
static void load_pack(const char *code)
{
    char path[1024];
    snprintf(path, sizeof path, "lang/%s.xml", code);   /* 相对路径，从本目录 lang/ 取 */
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  （未找到 %s，回退内置英文 TR[]）\n", path); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0; fclose(f);

    const char *p = buf, *end = buf + rd;
    while (p < end) {
        const char *it = strstr(p, "<item>");
        if (!it) break;
        const char *ite = strstr(it, "</item>");
        if (!ite) break;
        char *zh = extract_tag(it, ite, "zh");
        char *tr = extract_tag(it, ite, code);   /* 该语言对应的翻译标签 */
        if (zh && tr) {
            TrPair *tmp = (TrPair *)realloc(g_pairs, (g_pairs_n + 1) * sizeof(TrPair));
            if (tmp) { g_pairs = tmp; g_pairs[g_pairs_n].zh = zh; g_pairs[g_pairs_n].tr = tr; g_pairs_n++; }
            else { free(zh); free(tr); }
        } else { free(zh); free(tr); }
        p = ite + strlen("</item>");
    }
    free(buf);
}

/* ---- 4. 核心查找：模拟 i18n_tr(zh) ---- */
static const char *tr(const char *zh, Lang lang)
{
    if (!zh) return zh;
    if (lang == LANG_ZH) return zh;            /* 中文模式：原样返回（零开销、零变化）*/
    const char *r = find_tr(g_pairs, g_pairs_n, zh);   /* 文件优先 */
    if (r) return r;
    r = find_tr(g_tr, g_tr_n, zh);                    /* 内置英文兜底 */
    if (r) return r;
    return zh;                                         /* 缺译：回退中文原文（绝不空串）*/
}

static const char *lang_name(Lang l)
{
    return (l >= 0 && l < LANG_COUNT) ? g_langs[l].name : "?";
}

int main(void)
{
    printf("=== 极简多语言 i18n 演示（当前语言可切换）===\n");

    /* 场景 A：中文模式——i18n_tr 原样返回，不改任何串 */
    Lang cur = LANG_ZH;
    printf("\n[A] 中文模式（lang=%s）：\n", lang_name(cur));
    printf("  i18n_tr(\"设置\") = \"%s\"  (原样返回，零变化)\n", tr("设置", cur));
    printf("  i18n_tr(\"退出\") = \"%s\"  (原样返回，零变化)\n", tr("退出", cur));

    /* 场景 B：英文模式，内置 TR[] 兜底（未加载文件） */
    cur = LANG_EN;
    printf("\n[B] 英文模式，未加载 lang/en.xml（靠内置 TR[]）：\n");
    printf("  i18n_tr(\"设置\") = \"%s\"\n", tr("设置", cur));
    printf("  i18n_tr(\"昼夜模式\") = \"%s\"\n", tr("昼夜模式", cur));

    /* 场景 C：英文模式 + 运行时加载 lang/en.xml（手改文件可覆盖内置） */
    printf("\n[C] 英文模式，加载 lang/en.xml 覆盖内置：\n");
    load_pack(g_langs[LANG_EN].code);
    printf("  i18n_tr(\"设置\") = \"%s\"  (文件把\"设置\"译成 Preferences → 覆盖 Settings)\n", tr("设置", cur));
    printf("  i18n_tr(\"昼夜模式\") = \"%s\"  (文件无此条 → 回退内置英文)\n", tr("昼夜模式", cur));

    /* 场景 D：缺译回退链——内置也没有的串，回退中文原文 */
    printf("\n[D] 缺译回退：内置 TR[] 里没有的串\n");
    printf("  i18n_tr(\"这个按钮没有译文\") = \"%s\"  (回退中文原文，绝不空串)\n", tr("这个按钮没有译文", cur));

    /* 场景 E：加一门语言（fr）——只需枚举 + 元数据 + 一份 lang/fr.xml */
    cur = LANG_FR;
    printf("\n[E] 法语模式（新增语言：g_langs 加一行 + 提供 lang/fr.xml）：\n");
    load_pack(g_langs[LANG_FR].code);
    printf("  i18n_tr(\"搜索\") = \"%s\"  (fr.xml 有 → 法语)\n", tr("搜索", cur));
    printf("  i18n_tr(\"退出\") = \"%s\"  (fr.xml 缺 → 回退内置英文 Quit)\n", tr("退出", cur));

    /* 清理动态分配的覆盖表 */
    for (size_t i = 0; i < g_pairs_n; i++) { free((void *)g_pairs[i].zh); free((void *)g_pairs[i].tr); }
    free(g_pairs);

    printf("\n=== 结论：zh 是 msgid，中文模式零变化；缺译永远回退到原文而不是空串 ===\n");
    return 0;
}
