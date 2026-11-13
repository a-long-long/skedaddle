#!/usr/bin/env python3
"""主机侧 8 省 disp tile 覆盖门禁（2026-11-13 新增）。

为什么单独一个脚本、不进 regress.sh：regress.sh 的 disp 判据只测北京
（副机缺另外 7 省数据），而 2026-11-13 抓到的分格维度错位恰恰只在 bbox
不接近正方形的省发作 —— 北京/天津 ncols==nrows 永远看不出问题。把 8 省
全量判据塞进 regress.sh 会让副机假红，所以主机侧单独跑。

判据（两个工具交叉，都是确定性计数/集合，不看时间、不看体积）：
  1) skedaddle_pbfmap_check <省> <pbf>  → rc=0 且输出 IDENTICAL
     （PBF 解码 ↔ disp tile 双向集合等价，能抓 tile 里多出来的伪造要素）
  2) skedaddle_disp_tile_selftest --graph=<省> --pbf=<pbf> → rc=0 且 DISP-TILE-OK
     （整省要素全覆盖，单向）
  两者都要过；任一省的任一判据失败即 rc=1。

--self-test（负向对照，改的是**被检查的数据**，不是把判据条件改成常量）：
  A) 正常夹具（真实 8 省）必须全绿；
  B) 临时目录造「tile 列表截断到 1 个」的假省：disp.idx 只列 1 个 tile，
     其余文件用符号链接指回真实产物（零拷贝），PBF 基准仍用真 nodes.bin
     ⇒ 要素集合必然不等，门禁必须 FAIL；
  C) pbfmap_check --self-test（北京）分别注入「改一条」「删一条」，要求
     PBFMAP-SELFTEST-OK ⇒ 证明双向注入都能被抓。
  三者都成立才打印 DISP-COVERAGE-SELFTEST-OK。
"""

import glob
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRAPH = os.path.join(ROOT, "products", "graph")
SRC = os.path.join(ROOT, "sources")


def pick_build():
    """优先环境变量；否则挑一个真正含新版 pbfmap_check 的构建目录。

    ★ 不能无脑用 build/：本机 build/ 里的 pbfmap_check 还是 2026-11-03
    那个「打开整省 areas.bin」的死判据二进制，会全量假红。
    """
    env = os.environ.get("SKEDADDLE_BUILD_DIR")
    if env:
        return env
    for d in ("build_rel", "build"):
        if os.path.exists(os.path.join(ROOT, d, "skedaddle_pbfmap_check")):
            return os.path.join(ROOT, d)
    return os.path.join(ROOT, "build")


BUILD = pick_build()


def provinces():
    return sorted(os.path.basename(p)[:-len(".osm.pbf")]
                  for p in glob.glob(os.path.join(SRC, "*.osm.pbf")))


def check(graph_of=None, names=None):
    """返回失败说明列表；空列表 = 全绿。"""
    fails = []
    for n in (names or provinces()):
        d = graph_of(n) if graph_of else os.path.join(GRAPH, n)
        p = os.path.join(SRC, n + ".osm.pbf")
        r1 = subprocess.run(
            [os.path.join(BUILD, "skedaddle_pbfmap_check"), d, p],
            capture_output=True, text=True)
        ok1 = r1.returncode == 0 and "IDENTICAL" in r1.stdout
        r2 = subprocess.run(
            [os.path.join(BUILD, "skedaddle_disp_tile_selftest"),
             "--graph=" + d, "--pbf=" + p],
            capture_output=True, text=True)
        ok2 = r2.returncode == 0 and "DISP-TILE-OK" in r2.stdout
        if not (ok1 and ok2):
            fails.append("%s: pbfmap_check=%s(rc=%d) disp_tile_selftest=%s(rc=%d)" % (
                n, "OK" if ok1 else "FAIL", r1.returncode,
                "OK" if ok2 else "FAIL", r2.returncode))
    return fails


def make_truncated_fixture(tmp, name):
    """造一个「tile 列表只剩 1 个」的假省目录，真实文件一律符号链接零拷贝。"""
    real = os.path.join(GRAPH, name)
    fake = os.path.join(tmp, "graph", name)
    os.makedirs(fake)
    # pbfmap_decode 需要 nodes.bin 做 osm_id → 内部下标映射
    os.symlink(os.path.join(real, "nodes.bin"), os.path.join(fake, "nodes.bin"))
    with open(os.path.join(real, "disp.idx"), "r") as f:
        lines = f.read().splitlines()
    head = [l for l in lines if l.split(" ")[0] in
            ("tile_e7", "lat0", "lon0", "ncols", "nrows")]
    tiles = [l for l in lines if l and l[0].isdigit()]
    c, r = tiles[0].split()[0], tiles[0].split()[1]
    for ext in ("areas", "lines", "polyrefs"):
        src = os.path.join(real, "t_%s_%s.%s" % (c, r, ext))
        if os.path.exists(src):
            os.symlink(src, os.path.join(fake, "t_%s_%s.%s" % (c, r, ext)))
    # core.autocrlf=true：显式 newline="" 写 LF，避免生成器反复归一化
    with open(os.path.join(fake, "disp.idx"), "w", newline="") as f:
        for l in head:
            f.write(l + "\n")
        f.write("ntiles 1\n%s %s\n" % (c, r))
    return fake


def self_test():
    base = check()
    if base:
        print("DISP-COVERAGE-SELFTEST-FAIL 正常夹具却 FAIL:")
        for x in base:
            print("  " + x)
        return 1
    with tempfile.TemporaryDirectory(prefix="skdisp.") as tmp:
        fake = make_truncated_fixture(tmp, "beijing")
        bad = check(graph_of=lambda n: fake, names=["beijing"])
        if not bad:
            print("DISP-COVERAGE-SELFTEST-FAIL 截断 tile 列表却没抓到")
            return 1
        print("  负向 B：截断 tile 列表 → 门禁 FAIL（%s）" % bad[0])
    r = subprocess.run(
        [os.path.join(BUILD, "skedaddle_pbfmap_check"),
         os.path.join(GRAPH, "beijing"),
         os.path.join(SRC, "beijing.osm.pbf"), "--self-test"],
        capture_output=True, text=True)
    if r.returncode != 0 or "PBFMAP-SELFTEST-OK" not in r.stdout:
        print("DISP-COVERAGE-SELFTEST-FAIL 工具级双向注入自检未过 (rc=%d)" % r.returncode)
        return 1
    print("  负向 C：pbfmap_check 改一条/删一条均被抓 → PBFMAP-SELFTEST-OK")
    print("DISP-COVERAGE-SELFTEST-OK  正常 8 省全绿 / 截断 tile 被抓 / 双向注入被抓")
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    fails = check()
    if fails:
        for x in fails:
            print("MISMATCH " + x)
        print("DISP-COVERAGE-FAIL %d 个省不一致" % len(fails))
        return 1
    print("DISP-COVERAGE-OK  %d 省双向等价 + 全覆盖" % len(provinces()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
