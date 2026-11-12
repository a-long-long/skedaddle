#ifndef SKEDADDLE_SEARCH_ADDR_H
#define SKEDADDLE_SEARCH_ADDR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 地址层（2026-11-12，L2 本地能力）
 *
 * 项目原先只有「门牌 kind 14」一个扁平串（addr:street + addr:housenumber
 * 拼起来做 bigram）。这里补三件纯本地、不依赖任何在线服务的能力：
 *   1) addr_split      把中文地址拆成 行政区 / 街道 / 号码 / 附加部分
 *   2) addr_core_key   生成「街道+号码」检索核（丢掉行政区与楼层单元）
 *   3) addr_join       把若干级地名合成可读的层级地址（逆地理编码显示用）
 *
 * ★ 全是纯函数、零 I/O、零全局状态 ⇒ 可在无地图产物的机器上做确定性判据。
 * ★ 规则是**词表驱动**而非"分词理解"：中文地址没有分隔符，这里只认
 *   常见后缀（省/市/区/县/镇/乡/村、路/街/巷/大道…），不认得就整段
 *   留给调用方。写清楚边界比假装能解析所有地址更重要。 */

typedef struct {
    char admin[96];    /* 行政区前缀，如「北京市朝阳区」；可为空 */
    char street[96];   /* 含后缀的街道名，如「建国路」；可为空 */
    char number[48];   /* 门牌号 / 数字串，如「88」「12-3」；可为空 */
    char extra[96];    /* 号楼 / 单元 / 室 等附加部分；可为空 */
} AddrParts;

/* 拆分。返回 0 成功（哪怕一个字段都没认出来也返回 0，字段为空串）。
 * s==NULL / out==NULL 返回 -1。 */
int addr_split(const char *s, AddrParts *out);

/* 检索核 = 街道 + 号码，经 search_normalize_addr 规范化。
 * ★ 与建库端 make_addr_name 同口径：库里存的是「建国路88」，用户在搜索框
 *   输入「北京市朝阳区建国路88号3单元」时用它做二次召回。
 * 返回写入字节数；无街道无号码时返回 0（out[0]='\0'）。 */
int addr_core_key(const AddrParts *p, char *out, size_t cap);

/* 用「 · 」连接非空字段（逆地理编码的显示串）。返回写入字节数。
 * n<=0 / parts==NULL 时输出空串。 */
int addr_join(const char *const *parts, int n, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SKEDADDLE_SEARCH_ADDR_H */
