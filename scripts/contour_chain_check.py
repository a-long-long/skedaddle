#!/usr/bin/env python3
"""等高线全链路判据：合成 DEM → 生成器三件套 → nmbin_open → paint_contours 像素。

为什么用合成 DEM：本机 SRTM S3 直连超时（2026-11-12 实测 90 s / 0 张瓦片），
而「代码链路通不通」与「有没有真实地形数据」是两件事。这里造一座确定性的
圆丘（0..210 m）与一张平地（恒 50 m），把中间产物与最终像素都钉成可复现的量，
不伪造任何真实地形、不写进 products/。

  python3 scripts/contour_chain_check.py                 # 正常：CONTOUR-OK
  python3 scripts/contour_chain_check.py --inject       # 负向：CONTOUR-INJECT-CAUGHT

★ 负向对照：注入模式把「圆丘」那份 DEM 换成平地，判据必须抓到
  「本该有等高线却一条都没有」。抓不到 ⇒ rc=1，说明判据是假绿。
"""

import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("SKEDADDLE_BUILD_DIR", os.path.join(ROOT, "build_rel"))
GEN = os.path.join(BUILD, "skedaddle_gen_contours")
RENDER = os.path.join(BUILD, "skedaddle_contour_render_check")
BBOX = "39.2,115.2,39.6,115.6"
TILE = "N39E115.hgt"
N = 121                      # 每边点数；生成器按 sqrt(字节/2) 自动识别，任意 N 都行


def write_hgt(path, hill):
    """写一张大端 int16 的 .hgt。row0 = 北（生成器按 1-fy 取行）。"""
    with open(path, "wb") as f:
        for row in range(N):
            for col in range(N):
                if hill:
                    dx = col / (N - 1) - 0.5
                    dy = row / (N - 1) - 0.5
                    d = math.sqrt(dx * dx + dy * dy)
                    elev = int(210.0 * max(0.0, 1.0 - 2.0 * d))
                else:
                    elev = 50
                f.write(struct.pack(">h", elev))


def run(args):
    p = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def rec_count(path):
    """NMBinHeader 是**手工序列化**的（binfmt.c:hdr_to_bytes），不是 C struct
    的内存布局：magic(8) ver(4)@8 kind(4)@12 rec_size(4)@16 rec_count(8)@20。
    ★ 别按 struct 的 8 字节对齐去猜 @24 —— 那会读出 0x600000000 这种荒谬值。"""
    if not os.path.isfile(path):
        return -1
    with open(path, "rb") as f:
        head = f.read(28)
    if len(head) < 28 or head[:8] != b"NAVMBIN\x01":
        return -1
    return struct.unpack_from("<Q", head, 20)[0]


def generate(tmp, tag, hill):
    hd = os.path.join(tmp, "hgt_" + tag)
    od = os.path.join(tmp, "out_" + tag)
    os.makedirs(hd, exist_ok=True)
    os.makedirs(od, exist_ok=True)
    write_hgt(os.path.join(hd, TILE), hill)
    rc, out = run([GEN, "--out-dir=" + od, "--hgt-dir=" + hd,
                   "--bbox=" + BBOX, "--interval=20", "--index=100",
                   "--prefix=contour"])
    if rc != 0:
        print("MISMATCH 生成器 rc=%d (%s)\n%s" % (rc, tag, out[-800:]))
    return od, rc, out


def pixels(od, tag):
    if not os.path.isfile(RENDER):
        print("MISMATCH 找不到渲染判据程序 %s" % RENDER)
        return -1
    rc, out = run([RENDER, od])
    if rc != 0:
        print("MISMATCH 渲染判据 rc=%d (%s)\n%s" % (rc, tag, out[-500:]))
        return -1
    for line in out.splitlines():
        if line.startswith("CONTOUR-PIXELS"):
            return int(line.split()[1])
    print("MISMATCH 渲染判据没输出像素数 (%s)" % tag)
    return -1


def main():
    inject = "--inject" in sys.argv[1:]
    if not os.path.isfile(GEN):
        print("CONTOUR-FAIL 找不到生成器 %s" % GEN)
        return 1
    tmp = tempfile.mkdtemp(prefix="sk_contour_chain_")
    fails = []
    try:
        # ★ 注入：把「圆丘」换成平地，其余判据一字不改。
        hill_od, rc_h, log_h = generate(tmp, "hill", hill=not inject)
        flat_od, rc_f, log_f = generate(tmp, "flat", hill=False)
        if rc_h != 0 or rc_f != 0:
            fails.append("生成器退出码")

        hill_counts = [rec_count(os.path.join(hill_od, "contour_contours.bin")),
                       rec_count(os.path.join(hill_od, "contour_contour_polyrefs.bin")),
                       rec_count(os.path.join(hill_od, "contour_contour_pts.bin"))]
        flat_count = rec_count(os.path.join(flat_od, "contour_contours.bin"))
        px_hill = pixels(hill_od, "hill")
        px_flat = pixels(flat_od, "flat")

        print("圆丘 DEM    contours/polyrefs/pts = %s" % hill_counts)
        print("平地 DEM    contours = %d" % flat_count)
        print("绘制像素    圆丘 %d / 平地 %d" % (px_hill, px_flat))

        for name, c in zip(("contours", "polyrefs", "pts"), hill_counts):
            if c <= 0:
                fails.append("圆丘缺 %s 记录" % name)
        if flat_count != 0:
            fails.append("平地不该产生等高线（得到 %d 条）" % flat_count)
        if px_hill <= 0:
            fails.append("圆丘没画出任何像素")
        if px_flat != 0:
            fails.append("平地平白画出 %d 个像素" % px_flat)

        if inject:
            if fails:
                print("CONTOUR-INJECT-CAUGHT 平地冒充圆丘被全链判据抓到（%d 处）" % len(fails))
                return 0
            print("CONTOUR-INJECT-MISSED 判据没有分辨力")
            return 1
        if fails:
            for f in fails:
                print("MISMATCH " + f)
            print("CONTOUR-FAIL %d 处不一致" % len(fails))
            return 1
        print("CONTOUR-OK 合成 DEM → 生成器 → 三件套 → 加载 → 绘制像素（含平地负向）")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
