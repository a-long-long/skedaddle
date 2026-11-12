/* skedaddle_addr_selftest —— 地址层深化的确定性判据 + 负向对照
 *
 * 正常模式：
 *   A) addr_split / addr_core_key / addr_join 的纯函数逐字段比对；
 *   B) search_normalize_addr 的「普通地名恒等变换」锚点 + 新地址串能削成检索核；
 *   C) 用一份**临时 SQLite** 自造四级地名，调生产 search_reverse 合成层级地址。
 *      ★ 不依赖 products/ 里的任何产物，跨机器可重跑。
 *   ⇒ ADDR-OK，rc=0。
 *
 * --inject=no-house：临时库里故意不插 kind 14 门牌行（模拟"建库端没抽出
 *   门牌层"）。合规的判据必须抓到合成串缺一级 ⇒ ADDR-INJECT-CAUGHT / rc=0；
 *   没抓到则 rc=1 让门禁变红。
 *
 * 注意：日志里不要出现 "ASSERT-FAIL" 字样（regress.sh 的 run 会当失败）。
 */

#include "search/addr.h"
#include "search/search.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fails;

static void eq(const char *tag, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) return;
    printf("MISMATCH %-26s 得到[%s] 期望[%s]\n", tag, got, want);
    g_fails++;
}

static void eqi(const char *tag, int got, int want)
{
    if (got == want) return;
    printf("MISMATCH %-26s 得到[%d] 期望[%d]\n", tag, got, want);
    g_fails++;
}

static void check_pure(void)
{
    AddrParts p;
    char b[256];

    eqi("split rc", addr_split("北京市朝阳区建国路88号3单元", &p), 0);
    eq("split admin",  p.admin,  "北京市朝阳区");
    eq("split street", p.street, "建国路");
    eq("split number", p.number, "88");
    eq("split extra",  p.extra,  "3单元");
    eqi("core key len", addr_core_key(&p, b, sizeof(b)) > 0, 1);
    eq("core key", b, "建国路88");

    eqi("split2 rc", addr_split("建国路88号楼", &p), 0);
    eq("split2 admin",  p.admin,  "");
    eq("split2 street", p.street, "建国路");
    eq("split2 number", p.number, "88");
    eq("split2 extra",  p.extra,  "楼");
    addr_core_key(&p, b, sizeof(b));
    eq("core key2", b, "建国路88");

    /* 纯地名不该被拆出检索核（负向对照锚点） */
    eqi("split3 rc", addr_split("中关村", &p), 0);
    eq("split3 street", p.street, "");
    eqi("no core for place", addr_core_key(&p, b, sizeof(b)), 0);

    const char *parts[4] = { "北京市", "朝阳区", "建国路", "建国路88" };
    addr_join(parts, 4, b, sizeof(b));
    eq("join 4", b, "北京市 \xc2\xb7 朝阳区 \xc2\xb7 建国路 \xc2\xb7 建国路88");
    const char *parts2[4] = { "", "朝阳区", "", "建国路88" };
    addr_join(parts2, 4, b, sizeof(b));
    eq("join skip empty", b, "朝阳区 \xc2\xb7 建国路88");

    /* ★ 普通地名是恒等变换：这条一旦变红就是地址层把普通搜索弄坏了。 */
    search_normalize_addr("中关村", b, sizeof(b));   eq("norm place", b, "中关村");
    search_normalize_addr("西二旗", b, sizeof(b));   eq("norm place2", b, "西二旗");
    search_normalize_addr("学院",   b, sizeof(b));   eq("norm place3", b, "学院");
    /* 既有行为：去空格 / 全角转半角 / 去尾号 */
    search_normalize_addr("建国路 88 号", b, sizeof(b)); eq("norm legacy", b, "建国路88");
    search_normalize_addr("建国路８８号", b, sizeof(b)); eq("norm fullwidth", b, "建国路88");
    /* 新增：附加部分削掉，与建库端「街道+号码」检索核对齐 */
    search_normalize_addr("建国路88号3单元", b, sizeof(b)); eq("norm extra", b, "建国路88");
    search_normalize_addr("建国路88号楼",   b, sizeof(b)); eq("norm lou",   b, "建国路88");
    search_normalize_addr("北京市朝阳区建国路88号", b, sizeof(b)); eq("norm admin", b, "建国路88");
}

/* 造一份只含 place 表的最小库（search_open 只读它）。 */
static int make_db(const char *path, int with_house)
{
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) return -1;
    char *err = NULL;
    int rc = sqlite3_exec(db,
        "DROP TABLE IF EXISTS place;"
        "CREATE TABLE place(id INTEGER PRIMARY KEY, name TEXT, lat REAL, lon REAL,"
        "                   kind INTEGER, weight INTEGER);"
        "INSERT INTO place VALUES(1,'北京市',39.9000,116.4000,1,300);"
        "INSERT INTO place VALUES(2,'朝阳区',39.9050,116.4050,4,120);"
        "INSERT INTO place VALUES(3,'建国路',39.9010,116.4010,13,20);",
        NULL, NULL, &err);
    if (rc == SQLITE_OK && with_house)
        rc = sqlite3_exec(db,
            "INSERT INTO place VALUES(4,'建国路88',39.9005,116.4005,14,8);",
            NULL, NULL, &err);
    sqlite3_free(err);
    sqlite3_close(db);
    return rc == SQLITE_OK ? 0 : -1;
}

static void check_reverse(int with_house)
{
    const char *path = "/tmp/skedaddle_addr_selftest.db";
    if (make_db(path, with_house) != 0) {
        printf("MISMATCH 造临时库失败\n");
        g_fails++;
        return;
    }
    SearchDb *d = search_open(path);
    if (!d) {
        printf("MISMATCH search_open 失败\n");
        g_fails++;
        return;
    }
    char b[512];
    int rc = search_reverse(d, 39.9007, 116.4007, 3000.0, b, sizeof(b));
    eqi("reverse rc", rc, 0);
    eq("reverse addr", b,
       "北京市 \xc2\xb7 朝阳区 \xc2\xb7 建国路 \xc2\xb7 建国路88");
    /* 负向：半径内一点地址要素都没有时必须返回 1 且输出空串 */
    char e[512];
    eqi("reverse empty rc", search_reverse(d, 0.0, 0.0, 50.0, e, sizeof(e)), 1);
    eq("reverse empty str", e, "");
    search_close(d);
    remove(path);
}

int main(int argc, char **argv)
{
    int inject = (argc > 1 && strcmp(argv[1], "--inject=no-house") == 0);
    check_pure();
    int pure_fails = g_fails;
    check_reverse(!inject);
    int rev_fails = g_fails - pure_fails;

    if (inject) {
        if (pure_fails == 0 && rev_fails > 0) {
            printf("ADDR-INJECT-CAUGHT 缺门牌层被逆地理编码判据抓到（%d 处）\n",
                   rev_fails);
            return 0;
        }
        printf("ADDR-INJECT-MISSED 判据没有分辨力（pure=%d rev=%d）\n",
               pure_fails, rev_fails);
        return 1;
    }
    if (g_fails > 0) {
        printf("ADDR-FAIL %d 处不一致\n", g_fails);
        return 1;
    }
    printf("ADDR-OK 结构化拆分 + 检索核 + 普通地名恒等 + 逆地理编码四级\n");
    return 0;
}
