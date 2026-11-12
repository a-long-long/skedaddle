#!/usr/bin/env python3
"""扩省不变量门禁：sources/*.osm.pbf ⇄ products/graph/<省>/ 三件套 + 搜索库。

判据（全部是「文件存在 / 数量」这类确定性量，不看时间、不看体积）：
  1) 每个 sources 里的 PBF 都有同名省目录；
  2) 省目录里 disp.idx / lanes.bin 齐，且至少一个 t_* 瓦片；
  3) 全图共享 places_auto.db 存在、place 表非空；
  4) 没有「有目录却没 PBF」的孤产物（sync_maps.sh 不会自动清，手工改名会留）；
  5) M2 / O3：每省 chunks.json 齐全且**覆盖所有数据文件**、块数 = ceil(size/4096)。
     ★ 与生成端共用 gen_chunks.data_files() / expected_chunk_count()，避免名单漂移；
       这里只做「齐全」的轻量检查，逐块哈希重算由 gen_chunks.py --verify 承担。

--self-test：在临时目录造一份**合成夹具**（2 个省 + 假三件套 + 真实生成的块表
  + 假 SQLite），先证明正常夹具能过，再分别删掉一个 disp.idx、删掉块表、
  删掉块表里的一个块条目，证明三条判据都会 FAIL。
  ★ 不碰仓库里的 products/ —— 负向对照改的是被检查的夹具数据，不是条件。

正常打印 COVERAGE-OK / 自检打印 COVERAGE-SELFTEST-OK；任何不一致 rc=1。
"""

import glob
import json
import os
import sqlite3
import sys
import tempfile
import shutil

import gen_chunks          # 同目录：块表名单 / 块数口径与生成端共用

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

        # 5) M2 / O3 块表齐全：覆盖每个数据文件、块数 = ceil(size/4096)。
        #    逐块哈希重算不在这里做（那是 gen_chunks.py --verify 的活），
        #    这条门禁只守「有没有、齐不齐」，成本与文件体积无关。
        ct = os.path.join(d, gen_chunks.CHUNKS_NAME)
        if not os.path.isfile(ct):
            fails.append("%s 缺 chunks.json（M2 块表）" % name)
        else:
            try:
                with open(ct, "r", encoding="utf-8") as f:
                    table = json.load(f)
                if table.get("version") != gen_chunks.VERSION or \
                   table.get("chunk_size") != gen_chunks.CHUNK_SIZE or \
                   table.get("algo") != gen_chunks.ALGO:
                    fails.append("%s chunks.json 版本/口径不对" % name)
                files = table.get("files", {})
                actual = gen_chunks.data_files(d)
                for n in sorted(set(actual) - set(files)):
                    fails.append("%s 块表缺文件 %s" % (name, n))
                for n in sorted(set(files) - set(actual)):
                    fails.append("%s 块表多出文件 %s" % (name, n))
                for n in actual:
                    e = files.get(n)
                    if not isinstance(e, dict):
                        continue
                    sz = os.path.getsize(os.path.join(d, n))
                    if e.get("size") != sz:
                        fails.append("%s/%s 块表 size 不符" % (name, n))
                    want = gen_chunks.expected_chunk_count(sz)
                    ch = e.get("chunks")
                    if not isinstance(ch, list) or len(ch) != want:
                        fails.append("%s/%s 块表块数不符（应为 %d）" % (name, n, want))
            except Exception as ex:
                fails.append("%s chunks.json 打不开: %s" % (name, ex))

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
        gen_chunks.write_chunks(d)      # 用真实生成端造块表，不手写一份会漂移的假 JSON
    con = sqlite3.connect(os.path.join(root, "products", "places_auto.db"))
    con.execute("CREATE TABLE place (id INTEGER PRIMARY KEY, name TEXT)")
    con.execute("INSERT INTO place (name) VALUES ('甲地')")
    con.commit()
    con.close()


def _fresh():
    d = tempfile.mkdtemp(prefix="sk_coverage_")
    make_fixture(d)
    return d


def main():
    if "--self-test" in sys.argv[1:]:
        roots = []
        try:
            tmp = _fresh(); roots.append(tmp)
            good, _ = check(tmp)
            if good:
                print("COVERAGE-SELFTEST-FAIL 正常夹具就不该 FAIL: %s" % good)
                return 1

            # 负向 1：删掉一个 disp.idx（改被检查的数据，不改判据条件）
            os.remove(os.path.join(tmp, "products", "graph", "beta", "disp.idx"))
            bad, _ = check(tmp)
            if not any("disp.idx" in x for x in bad):
                print("COVERAGE-SELFTEST-FAIL 删了 disp.idx 判据却没抓到")
                return 1

            # 负向 2：删掉整个块表（M2 新增不变量必须能失败）
            d2 = _fresh(); roots.append(d2)
            os.remove(os.path.join(d2, "products", "graph", "alpha",
                                   gen_chunks.CHUNKS_NAME))
            bad2, _ = check(d2)
            if not any("chunks.json" in x for x in bad2):
                print("COVERAGE-SELFTEST-FAIL 删了块表判据却没抓到")
                return 1

            # 负向 3：块表里删掉一个块条目（块数不变量必须能失败）
            d3 = _fresh(); roots.append(d3)
            ct = os.path.join(d3, "products", "graph", "alpha",
                              gen_chunks.CHUNKS_NAME)
            with open(ct, "r", encoding="utf-8") as f:
                table = json.load(f)
            table["files"]["disp.idx"]["chunks"].pop()
            with open(ct, "w", encoding="utf-8", newline="") as f:
                json.dump(table, f)
            bad3, _ = check(d3)
            if not any("块数不符" in x for x in bad3):
                print("COVERAGE-SELFTEST-FAIL 删了块条目判据却没抓到")
                return 1

            print("COVERAGE-SELFTEST-OK 正常夹具通过 / 缺 disp.idx、缺块表、删块条目均被抓到")
            return 0
        finally:
            for d in roots:
                shutil.rmtree(d, ignore_errors=True)

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
