#!/usr/bin/env python3
"""扩省不变量门禁：sources/*.osm.pbf ⇄ products/graph/<省>/ 三件套 + 搜索库。

判据（全部是「文件存在」这类确定性量，不看时间、不看体积）：
  1) 每个 sources 里的 PBF 都有同名省目录；
  2) 省目录里 disp.idx / lanes.bin 齐，且至少一个 t_* 瓦片；
  3) 全图共享 places_auto.db 存在、place 表非空；
  4) 没有「有目录却没 PBF」的孤产物（sync_maps.sh 不会自动清，手工改名会留）。

--self-test：在临时目录造一份**合成夹具**（2 个省 + 假三件套 + 假 SQLite），
  先证明正常夹具能过，再故意删掉一个 disp.idx，证明判据会 FAIL。
  ★ 不碰仓库里的 products/ —— 负向对照改的是被检查的夹具数据，不是条件。

正常打印 COVERAGE-OK / 自检打印 COVERAGE-SELFTEST-OK；任何不一致 rc=1。
"""

import glob
import os
import sqlite3
import sys
import tempfile
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def check(root):
    fails = []
    sources = sorted(glob.glob(os.path.join(root, "sources", "*.osm.pbf")))
    names = [os.path.basename(p)[:-len(".osm.pbf")] for p in sources]
    if not names:
        fails.append("sources/ 里没有任何 *.osm.pbf")

    for name in names:
        d = os.path.join(root, "products", "graph", name)
        if not os.path.isdir(d):
            fails.append("缺省目录 %s" % name)
            continue
        for f in ("disp.idx", "lanes.bin"):
            if not os.path.isfile(os.path.join(d, f)):
                fails.append("%s 缺 %s" % (name, f))
        if not glob.glob(os.path.join(d, "t_*")):
            fails.append("%s 没有 t_* 瓦片" % name)

    # 孤产物：目录名不像 PBF 派生的都要报（lm.bin 是文件，不会被当目录）
    gdir = os.path.join(root, "products", "graph")
    if os.path.isdir(gdir):
        for d in sorted(os.listdir(gdir)):
            p = os.path.join(gdir, d)
            if os.path.isdir(p) and d not in names:
                fails.append("孤产物目录 %s（sources 里没有同名 PBF）" % d)

    db = os.path.join(root, "products", "places_auto.db")
    if not os.path.isfile(db):
        fails.append("缺全国共用搜索库 places_auto.db")
    else:
        try:
            con = sqlite3.connect("file:%s?mode=ro" % db, uri=True)
            n = con.execute("SELECT COUNT(*) FROM place").fetchone()[0]
            con.close()
            if n <= 0:
                fails.append("place 表为空")
            print("搜索库        place %d 行" % n)
        except Exception as e:
            fails.append("搜索库打不开/表不对: %s" % e)

    print("省份覆盖      %d 省：%s" % (len(names), ", ".join(names)))
    return fails, names


def make_fixture(root):
    os.makedirs(os.path.join(root, "sources"), exist_ok=True)
    graph = os.path.join(root, "products", "graph")
    os.makedirs(graph, exist_ok=True)
    for n in ("alpha", "beta"):
        open(os.path.join(root, "sources", n + ".osm.pbf"), "wb").write(b"x" * 2048)
        d = os.path.join(graph, n)
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, "disp.idx"), "wb").write(b"idx")
        open(os.path.join(d, "lanes.bin"), "wb").write(b"bin")
        open(os.path.join(d, "t_0.bin"), "wb").write(b"tile")
    con = sqlite3.connect(os.path.join(root, "products", "places_auto.db"))
    con.execute("CREATE TABLE place (id INTEGER PRIMARY KEY, name TEXT)")
    con.execute("INSERT INTO place (name) VALUES ('甲地')")
    con.commit()
    con.close()


def main():
    if "--self-test" in sys.argv[1:]:
        tmp = tempfile.mkdtemp(prefix="sk_coverage_")
        try:
            make_fixture(tmp)
            good, _ = check(tmp)
            if good:
                print("COVERAGE-SELFTEST-FAIL 正常夹具就不该 FAIL: %s" % good)
                return 1
            # 负向：删掉一个 disp.idx（改被检查的数据，不改判据条件）
            os.remove(os.path.join(tmp, "products", "graph", "beta", "disp.idx"))
            bad, _ = check(tmp)
            if not any("disp.idx" in x for x in bad):
                print("COVERAGE-SELFTEST-FAIL 删了 disp.idx 判据却没抓到")
                return 1
            print("COVERAGE-SELFTEST-OK 正常夹具通过 / 缺 disp.idx 被抓到")
            return 0
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    fails, _ = check(ROOT)
    if fails:
        for f in fails:
            print("MISMATCH " + f)
        print("COVERAGE-FAIL %d 处不一致" % len(fails))
        return 1
    print("COVERAGE-OK sources ⇄ graph 三件套 ⇄ places_auto.db 自洽")
    return 0


if __name__ == "__main__":
    sys.exit(main())
