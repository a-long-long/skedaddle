#ifndef SKEDADDLE_I18N_H
#define SKEDADDLE_I18N_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * 极简多语言：以中文为原文（msgid）。
 *   - zh 模式：i18n_tr(zh) 直接返回传入的 msgid（即中文本身）。
 *   - 其它语言：先查该语言的外部 XML 文件（lang/<code>.xml）；查不到则回退
 *     内置的英文 TR[]；再查不到才回退中文（绝不返回空串）。
 * 因此：默认中文下所有调用都是"原样返回"，不会引入任何字符串变化；
 * 既有的像素/UI 自检（断言中文串）在 zh 模式下行为与改前完全一致。
 *
 * 新增语言只需：① 在 I18nLang 加一个枚举值（= settings.ini 的 lang 键，向后兼容）；
 * ② 在 i18n.c 的 g_langs 表里加一行（code + 自称）；③ 提供 lang/<code>.xml。
 * ------------------------------------------------------------------------- */

typedef enum {
    I18N_ZH = 0,   /* 中文（默认） */
    I18N_EN = 1,   /* English */
    I18N_FR = 2,   /* Français */
    I18N_DE = 3,   /* Deutsch */
    I18N_ES = 4,   /* Español */
    I18N_PT = 5,   /* Português */
    I18N_IT = 6,   /* Italiano */
    I18N_JA = 7,   /* 日本語 */
    I18N_COUNT      /* 语言总数（= g_langs 表长度；不要在此之后再加非语言值） */
} I18nLang;

/* 语言元数据：i18n.c 提供实现，Qt 端用它动态生成语言菜单与设置面板下拉。 */
typedef struct {
    I18nLang   id;
    const char *code;   /* ISO 639-1：对应 lang/<code>.xml 及其内部翻译标签；中文为 NULL（无文件） */
    const char *name;   /* 该语言的自称（endonym），用于语言菜单/设置下拉 */
} I18nInfo;

void      i18n_set_lang(I18nLang l);
/* 设置语言文件所在目录（通常是 exe 同目录下的 lang/）。
 * 由 Qt 侧在启动时用 QCoreApplication::applicationDirPath() 传入；
 * 未设置时回退到相对路径 lang/ 或工作目录，再失败则用内置 TR[] 兜底。 */
void      i18n_set_lang_dir(const char *dir);
I18nLang  i18n_lang(void);
/* 以中文串为 key：返回当前语言的对应串（缺译回退 内置英文 → 中文）。 */
const char *i18n_tr(const char *zh);

/* 语言元数据查询（Qt 端动态生成语言菜单用）。无效 id 返回 NULL / "?" / 0。 */
const I18nInfo *i18n_lang_info(I18nLang l);   /* 语言元数据（NULL=无效） */
const char *i18n_lang_code(I18nLang l);        /* ISO 代码，中文返回 NULL */
const char *i18n_lang_name(I18nLang l);        /* 自称（endonym） */
int         i18n_lang_count(void);             /* == I18N_COUNT */

#ifdef __cplusplus
}
#endif

#endif /* SKEDADDLE_I18N_H */
