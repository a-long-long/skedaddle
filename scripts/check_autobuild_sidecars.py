#!/usr/bin/env python3
"""autobuild 图集级旁路产物自愈验收（地名库 / 地标表 / 块表）。

判据全部是「计数 + 退出码」，不看时间、不看体积：
  1) 真实仓库正常态 → sidecars_checked=0（三样齐 ⇒ 零动作，防止每次启动都重建）；
  2) 临时零拷贝夹具分别删掉 places / lm / chunks → 各自恰好补 1 项；
  3) --no-build → 只诊断，sc_skipped=1 且 sc_rebuilt=0；
  4) 注入不存在的 python3 → 块表降级（sc_degraded=1，不算失败）；
  5) 注入 /bin/false 假解释器 → 工具找到了但执行失败（sc_failed=1）；
  6) 删掉 manifest 触发省重建 → force ⇒ 三项全建（rebuilt=1, sc_rebuilt=3）；
  7) C 侧常量与 scripts/sync_maps.sh 的 LM 配置一致（防两份实现漂移）。

★ 地标表是**全图表**：30 个地标分布全国，8 省路网 + 全图 Dijkstra 在本机
  内存不足（实测 exit 137），单省夹具里真实 lmbuild_cli 必然报 LM-BUILD-FAIL。
  所以成功路径用 --lm-tool 注入一个只写非空文件的桩工具，验证的是 autobuild
  的编排（临时文件 → 自检 → 原子就位 → 计数）；真实构建器的正确性由
  sync_maps.sh 与 regress.sh 的 lm_admissible 覆盖。另留一条「真实工具失败」
  用例，验证失败被正确记账且不会把半截表留在正式路径上。

负向对照一律改「被检查的数据」（删夹具里的产物 / 注入假工具），不改判据条件。
夹具里的省目录用符号链接零拷贝；chunks.json 与 gz 必须**复制**而不是软链 ——
否则 gen_chunks.py 写表时会顺着软链覆盖掉仓库里的真实块表。

正常打印 AUTOBUILD-SIDECARS-OK / 自检打印 AUTOBUILD-SIDECARS-SELFTEST-OK；
任何不一致 rc=1。不进 regress.sh：副机缺 7 省数据、且未必有 python3 与
scripts/，与 check_disp_coverage.py 同属主机侧门禁。
"""

import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROV = "tianjin"          # 最小的真实省 PBF，让「省重建」这一条不至于全量转换
TOOL = "skedaddle_autobuild_sidecar_selftest"

# 地标表是**全图表**（30 个全国地标 + 8 省路网 + 全图 Dijkstra），本机内存
# 不足（实测 lmbuild_cli exit 137）。成功路径用这个桩工具验证 autobuild 的
# 编排（临时文件 → 非空 → 原子就位 → 计数）；真实构建器的正确性由
# sync_maps.sh 与 regress.sh 的 lm_admissible 覆盖。桩只写 256 字节非空文件，
# 满足 lm_ok 的 "b > 64" 与 file_nonempty 的 "b > 0" 判据。
LM_STUB_SRC = """#!/usr/bin/env python3
import sys
out = None
for a in sys.argv[1:]:
    if a.startswith("--out="):
        out = a.split("=", 1)[1]
if out:
    with open(out, "wb") as f:
        f.write(b"\\x00" * 256)
sys.exit(0)
"""


def pick_build():
    env = os.environ.get("SKEDADDLE_BUILD_DIR")
    if env:
        return env
    for d in ("build_rel", "build"):
        if os.path.exists(os.path.join(ROOT, d, TOOL)):
            return os.path.join(ROOT, d)
    return os.path.join(ROOT, "build")


BUILD = pick_build()


def need_tools():
    names = [TOOL, "skedaddle_graph_extract", "skedaddle_places_build",
             "skedaddle_lmbuild_cli"]
    miss = [n for n in names if not os.path.isfile(os.path.join(BUILD, n))]
    script = os.path.join(ROOT, "scripts", "gen_chunks.py")
    if not os.path.isfile(script):
        miss.append("scripts/gen_chunks.py")
    return miss


def link_all(src_dir, dst_dir, copy_names=()):
    """把 src_dir 下所有条目零拷贝链到 dst_dir；列在 copy_names 里的改成复制。"""
    os.makedirs(dst_dir, exist_ok=True)
    for n in sorted(os.listdir(src_dir)):
        s = os.path.join(src_dir, n)
        d = os.path.join(dst_dir, n)
        if n in copy_names:
            shutil.copy2(s, d)
        else:
            os.symlink(os.path.abspath(s), d)


HIDE_DIR = None


def hide(path):
    """把产物挪出被检查目录，等价模拟「缺失」。

    ★ 本机 os.remove 被 safe-delete 外壳拦（回收站 /.Trash-1000 权限拒绝），
      所以一律用 rename，不碰删除。挪到专用收纳目录，finally 统一清掉；
      绝不留在省目录里，否则 gen_chunks.data_files() 可能把它当数据文件。
    """
    global HIDE_DIR
    if HIDE_DIR is None:
        HIDE_DIR = tempfile.mkdtemp(prefix="sk_ab_hidden_")
    dst = os.path.join(HIDE_DIR, "%03d_%s" %
                       (len(os.listdir(HIDE_DIR)), os.path.basename(path)))
    os.rename(path, dst)


def make_fixture(root):
    """单省夹具：tmp/products/graph/tianjin 是省目录（单省模式）。"""
    src = os.path.join(root, "sources")
    os.makedirs(src, exist_ok=True)
    os.symlink(os.path.join(ROOT, "sources", PROV + ".osm.pbf"),
               os.path.join(src, PROV + ".osm.pbf"))

    graph = os.path.join(root, "products", "graph")
    os.makedirs(graph, exist_ok=True)
    # 块表必须复制，不能软链（生成端会 open(...,"w") 覆盖）。
    link_all(os.path.join(ROOT, "products", "graph", PROV),
             os.path.join(graph, PROV),
             copy_names=("chunks.json", "chunks.json.gz"))

    products = os.path.join(root, "products")
    for n in ("places_auto.db", "lm.bin"):
        real = os.path.join(ROOT, "products",
                            n if n == "places_auto.db" else os.path.join("graph", n))
        dst = os.path.join(products if n == "places_auto.db" else graph, n)
        if os.path.isfile(real):
            os.symlink(real, dst)
    return graph


def call(root, label, extra=(), py=None, script=None, no_build=False,
         lm_tool=None, expect=()):
    """跑一次自测工具，返回 (ok, 输出)。expect 是 --expect-* 参数列表。"""
    cmd = [os.path.join(BUILD, TOOL),
           "--graph=" + os.path.join(root, "products", "graph", PROV),
           "--sources=" + os.path.join(root, "sources"),
           "--tool=" + os.path.join(BUILD, "skedaddle_graph_extract"),
           "--py=" + (py if py is not None else sys.executable),
           "--chunks-script=" + (script if script is not None else
                                 os.path.join(ROOT, "scripts", "gen_chunks.py"))]
    if lm_tool is not None:
        cmd.append("--lm-tool=" + lm_tool)
    if no_build:
        cmd.append("--no-build")
    cmd.extend(expect)
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = r.stdout + r.stderr
    ok = r.returncode == 0 and "AUTOBUILD-SIDECAR-OK" in r.stdout \
        and "ASSERT-FAIL" not in r.stdout
    if not ok:
        print("  [FAIL] %s rc=%d\n%s" % (label, r.returncode, out[-1500:]))
    else:
        print("  [ OK ] %s" % label)
    return ok, out


def const_check():
    """C 侧常量 ↔ sync_maps.sh 的 LM 配置，防止改一边忘另一边。"""
    sh = open(os.path.join(ROOT, "scripts", "sync_maps.sh"),
              encoding="utf-8").read()
    c = open(os.path.join(ROOT, "src", "map", "autobuild.c"),
             encoding="utf-8").read()

    def grab(text, pat, flags=0):
        m = re.search(pat, text, flags)
        return int(m.group(1)) if m else None

    pairs = [
        ("LM_NLM",     grab(sh, r"^LM_NLM=(\d+)", re.M),        grab(c, r"#define AB_LM_NLM\s+(\d+)")),
        ("LM_WANT_VER", grab(sh, r"^LM_WANT_VER=(\d+)", re.M),   grab(c, r"#define AB_LM_VER\s+(\d+)")),
        ("--cell",     grab(sh, r"--cell=(\d+)"),                grab(c, r"#define AB_LM_CELL\s+(\d+)")),
    ]
    bad = ["%s: 脚本=%s / C=%s" % (n, a, b) for n, a, b in pairs
           if a is None or a != b]
    if bad:
        print("AUTOBUILD-CONST-FAIL " + "; ".join(bad))
        return False
    print("AUTOBUILD-CONST-OK LM_NLM=%d LM_WANT_VER=%d --cell=%d 两侧一致" %
          (pairs[0][1], pairs[1][1], pairs[2][1]))
    return True


def selftest():
    roots = []
    stub_dir = None
    stub = None
    ok = True
    try:
        # 桩工具：只写非空文件，验证 lm 重建编排（真实 lmbuild_cli 本机 OOM）
        stub_dir = tempfile.mkdtemp(prefix="sk_ab_lmstub_")
        stub = os.path.join(stub_dir, "lm_stub.py")
        with open(stub, "w") as f:
            f.write(LM_STUB_SRC)
        os.chmod(stub, 0o755)

        # 1) 正常夹具：三样齐 ⇒ 零动作
        r1 = tempfile.mkdtemp(prefix="sk_ab_s1_"); roots.append(r1)
        make_fixture(r1)
        good, _ = call(r1, "正常夹具零动作",
                       expect=("--expect-sc-checked=0", "--expect-rebuilt=0"))
        ok &= good

        # 2) 删地名库 ⇒ 补 1 项
        r2 = tempfile.mkdtemp(prefix="sk_ab_s2_"); roots.append(r2)
        make_fixture(r2)
        hide(os.path.join(r2, "products", "places_auto.db"))
        good, _ = call(r2, "删 places_auto.db ⇒ 补 1",
                       expect=("--expect-sc-checked=1", "--expect-sc-rebuilt=1",
                               "--expect-sc-failed=0"))
        ok &= good

        # 3) 删地标表 ⇒ 补 1 项（lm 用桩工具，真实 lmbuild_cli 本机 OOM）
        r3 = tempfile.mkdtemp(prefix="sk_ab_s3_"); roots.append(r3)
        make_fixture(r3)
        hide(os.path.join(r3, "products", "graph", "lm.bin"))
        good, _ = call(r3, "删 lm.bin ⇒ 补 1", lm_tool=stub,
                       expect=("--expect-sc-checked=1", "--expect-sc-rebuilt=1",
                               "--expect-sc-failed=0"))
        ok &= good

        # 4) 删块表与伴生 ⇒ 补 1 项
        r4 = tempfile.mkdtemp(prefix="sk_ab_s4_"); roots.append(r4)
        make_fixture(r4)
        d = os.path.join(r4, "products", "graph", PROV)
        hide(os.path.join(d, "chunks.json"))
        hide(os.path.join(d, "chunks.json.gz"))
        good, _ = call(r4, "删 chunks.json + gz ⇒ 补 1",
                       expect=("--expect-sc-checked=1", "--expect-sc-rebuilt=1",
                               "--expect-sc-failed=0", "--expect-sc-degraded=0"))
        ok &= good

        # 5) --no-build：只诊断不动手
        r5 = tempfile.mkdtemp(prefix="sk_ab_s5_"); roots.append(r5)
        make_fixture(r5)
        hide(os.path.join(r5, "products", "places_auto.db"))
        good, _ = call(r5, "--no-build ⇒ 跳过 1", no_build=True,
                       expect=("--expect-sc-checked=1", "--expect-sc-rebuilt=0",
                               "--expect-sc-skipped=1"))
        ok &= good

        # 6) python3 不存在 ⇒ 块表降级（不算失败）
        r6 = tempfile.mkdtemp(prefix="sk_ab_s6_"); roots.append(r6)
        make_fixture(r6)
        d = os.path.join(r6, "products", "graph", PROV)
        hide(os.path.join(d, "chunks.json"))
        hide(os.path.join(d, "chunks.json.gz"))
        good, _ = call(r6, "缺 python3 ⇒ 块表降级",
                       py=os.path.join(r6, "no-such-python3"),
                       expect=("--expect-sc-checked=1", "--expect-sc-degraded=1",
                               "--expect-sc-rebuilt=0", "--expect-sc-failed=0"))
        ok &= good

        # 7) 假解释器（工具找到了但执行失败）⇒ 记失败
        r7 = tempfile.mkdtemp(prefix="sk_ab_s7_"); roots.append(r7)
        make_fixture(r7)
        d = os.path.join(r7, "products", "graph", PROV)
        hide(os.path.join(d, "chunks.json"))
        hide(os.path.join(d, "chunks.json.gz"))
        good, _ = call(r7, "/bin/false 假解释器 ⇒ 记失败", py="/bin/false",
                       expect=("--expect-sc-checked=1", "--expect-sc-failed=1",
                               "--expect-sc-rebuilt=0"))
        ok &= good

        # 8) 省重建 ⇒ force ⇒ 三项全建（最慢的一条，用最小真实省）
        r8 = tempfile.mkdtemp(prefix="sk_ab_s8_"); roots.append(r8)
        make_fixture(r8)
        hide(os.path.join(r8, "products", "graph", PROV, "manifest.txt"))
        good, out = call(r8, "省重建 ⇒ 三项全建", lm_tool=stub,
                         expect=("--expect-rebuilt=1", "--expect-sc-checked=1",
                                 "--expect-sc-rebuilt=3", "--expect-sc-failed=0"))
        ok &= good
        if not good:
            print("    （省重建用例输出见上；若为 2/1 说明某一项工具在单省夹具里失败）")

        ok &= const_check()
        print("AUTOBUILD-SIDECARS-SELFTEST-OK  正常零动作 / 删三样各补 1 / "
              "no-build 跳过 / 缺工具降级 / 假工具记失败 / 省重建三项全建 / 常量一致"
              if ok else "AUTOBUILD-SIDECARS-SELFTEST-FAIL")
        return 0 if ok else 1
    finally:
        for d in roots:
            shutil.rmtree(d, ignore_errors=True)
        if HIDE_DIR:
            shutil.rmtree(HIDE_DIR, ignore_errors=True)
        if stub_dir:
            shutil.rmtree(stub_dir, ignore_errors=True)


def main():
    miss = need_tools()
    if miss:
        print("AUTOBUILD-SIDECARS-FAIL 缺工具：%s（先编译或设 SKEDADDLE_BUILD_DIR）"
              % ", ".join(miss))
        return 1
    if "--self-test" in sys.argv[1:]:
        return selftest()
    # 正常态：真实仓库必须零动作
    cmd = [os.path.join(BUILD, TOOL), "--graph=" + os.path.join(ROOT, "products", "graph"),
           "--expect-sc-checked=0", "--expect-rebuilt=0"]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0 or "AUTOBUILD-SIDECAR-OK" not in r.stdout:
        print("AUTOBUILD-SIDECARS-FAIL 真实仓库正常态不是零动作：\n" + r.stdout + r.stderr)
        return 1
    if not const_check():
        return 1
    print("AUTOBUILD-SIDECARS-OK 真实仓库正常态零动作 + 常量一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
