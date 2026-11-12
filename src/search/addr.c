#include "search/addr.h"

#include <string.h>
#include <stdio.h>

/* ★ 刻意**不** include search.h / 不调 search_normalize_addr：
   search_normalize_addr 会反过来调 addr_split + addr_core_key，形成函数级
   递归。addr_split 的输入若是规范化串（search.c 的调用点就是），拼出来的
   street+number 天然已规范化；直接拼接即可。 */

/* 词表：顺序无关，靠"最早出现 / 最右结束"决定切点。
 * ★ 长词必须排在前面（"大道" 先于 "路" 之类），否则同一个起点会被短词抢走。 */
static const char *const k_admin_tok[] = {
    "自治州", "地区", "街道", "省", "市", "区", "县", "镇", "乡", "村", NULL
};
static const char *const k_street_tok[] = {
    "大道", "大街", "胡同", "路", "街", "巷", "弄", "里", "段", NULL
};

static void put_range(char *dst, size_t cap, const char *a, const char *b)
{
    if (cap == 0) return;
    size_t n = (b > a) ? (size_t)(b - a) : 0;
    if (n >= cap) n = cap - 1;
    memcpy(dst, a, n);
    dst[n] = '\0';
}

/* 在 s 里找最早出现的 street token，返回起点；*len 回填词长。 */
static const char *find_street(const char *s, size_t *len)
{
    const char *best = NULL;
    size_t best_len = 0;
    for (int i = 0; k_street_tok[i]; i++) {
        const char *p = strstr(s, k_street_tok[i]);
        if (!p) continue;
        size_t l = strlen(k_street_tok[i]);
        if (!best || p < best || (p == best && l > best_len)) {
            best = p; best_len = l;
        }
    }
    *len = best_len;
    return best;
}

/* 在 [s, end) 里找"结束位置最靠右"的 admin token，返回它的结束位置。 */
static const char *find_admin_end(const char *s, const char *end)
{
    const char *last = s;
    for (const char *p = s; p < end; p++) {
        for (int i = 0; k_admin_tok[i]; i++) {
            size_t l = strlen(k_admin_tok[i]);
            if ((size_t)(end - p) < l) continue;
            if (strncmp(p, k_admin_tok[i], l) == 0 && p + l > last) {
                last = p + l;
                break;
            }
        }
    }
    return last;
}

/* 号码：必须是数字打头，后面允许数字 / ASCII 字母 / '-' / '#'。 */
static const char *scan_number(const char *p, char *dst, size_t cap)
{
    size_t n = 0;
    if (*p >= '0' && *p <= '9') {
        while (*p && ((*p >= '0' && *p <= '9') ||
                      (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                      *p == '-' || *p == '#')) {
            if (n + 1 < cap) dst[n++] = *p;
            p++;
        }
    }
    if (cap) dst[n] = '\0';
    return p;
}

static const char *skip_hao(const char *p)
{
    if (strncmp(p, "號", 3) == 0 || strncmp(p, "号", 3) == 0) return p + 3;
    return p;
}

int addr_split(const char *s, AddrParts *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));

    size_t slen = 0;
    const char *st = find_street(s, &slen);
    const char *street_end = st ? st + slen : NULL;

    /* 行政区只在街道之前找；没有街道时全串都算候选前缀。 */
    const char *admin_end = find_admin_end(s, street_end ? st : s + strlen(s));
    put_range(out->admin, sizeof(out->admin), s, admin_end);

    if (st) {
        put_range(out->street, sizeof(out->street), admin_end, street_end);
        const char *p = scan_number(street_end, out->number, sizeof(out->number));
        p = skip_hao(p);
        /* 附加部分：丢掉开头的分隔空白与「号楼/院/幢/栋」前的纯数字也照收 */
        snprintf(out->extra, sizeof(out->extra), "%s", p);
    } else {
        const char *p = scan_number(admin_end, out->number, sizeof(out->number));
        p = skip_hao(p);
        snprintf(out->extra, sizeof(out->extra), "%s", p);
    }
    return 0;
}

int addr_core_key(const AddrParts *p, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!p) return 0;
    if (!*p->street && !*p->number) return 0;
    /* 字段来自 addr_split 对**已规范化串**的拆分，直接拼接就是建库端
       make_addr_name 的同款检索核。 */
    int n = snprintf(out, cap, "%s%s", p->street, p->number);
    return (n > 0 && (size_t)n < cap) ? n : (int)strlen(out);
}

int addr_join(const char *const *parts, int n, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!parts || n <= 0) return 0;
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        if (!parts[i] || !*parts[i]) continue;
        /* 分隔符 " · " = 空格(1) + U+00B7(2) + 空格(1) = 4 字节 */
        size_t need = strlen(parts[i]) + (o ? 4 : 0);
        if (o + need + 1 > cap) break;
        if (o) { memcpy(out + o, " \xc2\xb7 ", 4); o += 4; }
        size_t l = strlen(parts[i]);
        memcpy(out + o, parts[i], l);
        o += l;
        out[o] = '\0';
    }
    return (int)o;
}
