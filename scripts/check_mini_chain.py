#!/usr/bin/env python3
"""tutorial/mini/ 演化链门禁：证明「从最小框架逐课长大到最终形态」是真的。

与 check_tutorial_tracks.py 互补：那个守「讲义编号 / 核心-跨平台分类」，
本脚本守「代码链本身」。六组判据：

  1. 生成物新鲜度   regen_mini_chain.py --check（源 → 快照/diff 逐字一致）
  2. 累加不变量     第 N 步每个源码文件都是第 N+1 步的【行子序列】；
                    除 main.c 外必须逐字相同；每步恰好新增 1 个 stepNN.h
  3. diff 一致性     diffs/NN_to_MM.diff 与相邻快照的真实差异逐字一致
  4. 18 步编译运行   -Wall -Wextra -Werror -lm 零警告；rc=0；
                     0..N 的 MINI-STEP-KK-OK 齐全；steps=N+1、gates_fail=0；
                     gates_ok 严格递增、ink 非递减、BMP 两次运行逐字节相同
  5. 组织纪律        NOTE.md 分类与 chain.json / 段边界一致；全链只有
                     step15.h 允许出现平台宏 #if defined(_WIN32)
  6. 合流点          MAP.md 里引用的 src/... 路径必须真实存在

★ 负向对照：--self-test 在临时副本里分别注入 5 类破坏（删旧行 / 改 diff /
  让内置判据 FAIL / 业务代码里加平台宏 / 制造编译错误），断言本门禁每次都
  FAIL 且带对应标签。只跑通正常路径不算验收 —— 这是项目的既有纪律。

★ 本脚本【未接进 regress.sh】：与 check_tutorial_tracks.py 同理，避免改变
  87/0 基线与 Windows 副机行为；改完 mini/ 手动跑一次即可。接门禁属于基线变更。

用法：
    python3 scripts/check_mini_chain.py
    python3 scripts/check_mini_chain.py --self-test
"""
from pathlib import Path
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
MINI = ROOT / "tutorial" / "mini"
PARTS = MINI / "_parts"
STEPS_DIR = MINI / "steps"
DIFFS_DIR = MINI / "diffs"
REGEN = ROOT / "scripts" / "regen_mini_chain.py"

CROSS_FROM = 15
LAST = 17
SOURCE_SUFFIX = (".h", ".c")
NOTE = "NOTE.md"


def read_text(p: Path) -> str:
    with open(p, "r", encoding="utf-8", newline="") as f:
        return f.read()


def snapshot_files(d: Path):
    """该步快照的源码文件（排除 NOTE.md 说明页）。"""
    return {p.name: read_text(p) for p in sorted(d.iterdir())
            if p.is_file() and p.name != NOTE and p.suffix in SOURCE_SUFFIX}


def is_subsequence(small, big) -> bool:
    """small 的每一行按序出现在 big 里（允许中间插入新行，不许删/改旧行）。"""
    j = 0
    for line in small:
        while j < len(big) and big[j] != line:
            j += 1
        if j == len(big):
            return False
        j += 1
    return True


def want_track(n: int) -> str:
    return "跨平台代码" if n >= CROSS_FROM else "核心代码"


def main() -> int:
    self_test = "--self-test" in sys.argv[1:]
    if self_test:
        return run_self_test()

    fails = []

    def bad(tag, msg):
        fails.append(tag)
        print(f"ASSERT-FAIL [{tag}] {msg}")

    def ok(msg):
        print("OK: " + msg)

    # ---- 1. 生成物新鲜度 ----
    r = subprocess.run([sys.executable, str(REGEN), "--check"],
                       capture_output=True, text=True)
    if r.returncode == 0:
        ok("regen --check：18 个快照 + 17 个 diff 与 _parts/ 逐字一致")
    else:
        bad("REGEN", "生成物过期或与源不一致：" + str(r.stdout.strip().splitlines()[-1:]))

    meta = json.loads(read_text(PARTS / "chain.json"))
    steps_meta = meta["steps"]
    dirs = [STEPS_DIR / f"step{s['n']:02d}_{s['slug']}" for s in steps_meta]
    snaps = []
    for i, d in enumerate(dirs):
        if not d.is_dir():
            bad("LAYOUT", f"缺快照目录 {d.name}")
            snaps.append({})
        else:
            snaps.append(snapshot_files(d))

    # ---- 2. 累加不变量 ----
    accum_ok = True
    for n in range(1, LAST + 1):
        old, new = snaps[n - 1], snaps[n]
        missing = set(old) - set(new)
        if missing:
            bad("ACCUM", f"第 {n} 步丢了上一步的文件：{sorted(missing)}")
            accum_ok = False
        added = set(new) - set(old)
        if added != {f"step{n:02d}.h"}:
            bad("ACCUM", f"第 {n} 步新增文件应为 step{n:02d}.h，实际 {sorted(added)}")
            accum_ok = False
        for name, text in old.items():
            if name not in new:
                continue
            o_lines, n_lines = text.splitlines(), new[name].splitlines()
            if name == "main.c":
                if not is_subsequence(o_lines, n_lines):
                    bad("ACCUM", f"第 {n} 步 main.c 删改了旧行（不再是上一步的行子序列）")
                    accum_ok = False
            elif text != new[name]:
                bad("ACCUM", f"第 {n} 步 {name} 被改写（应逐字不变）")
                accum_ok = False
    if accum_ok and len(fails) == 0:
        ok("累加不变量：17 组相邻步，旧文件零删改 + 每步恰好新增 1 个模块")
    elif accum_ok:
        ok("累加不变量：17 组相邻步全部通过")

    # ---- 3. diff 一致性（独立重算，不复用生成器结果） ----
    diff_ok = True
    for n in range(1, LAST + 1):
        a, b = snaps[n - 1], snaps[n]
        lines = []
        for name in sorted(set(a) | set(b)):
            old = a.get(name, "").splitlines(keepends=True)
            new = b.get(name, "").splitlines(keepends=True)
            if old == new:
                continue
            lines.extend(difflib.unified_diff(
                old, new,
                fromfile="/dev/null" if name not in a else f"a/step{n - 1:02d}/{name}",
                tofile=f"b/step{n:02d}/{name}", n=3))
        expect = "".join(lines)
        p = DIFFS_DIR / f"{n - 1:02d}_to_{n:02d}.diff"
        if not p.is_file() or read_text(p) != expect:
            bad("DIFF", f"{p.name} 与相邻快照的真实差异不一致")
            diff_ok = False
    if diff_ok:
        ok("diff 一致性：17 个相邻差异文件独立重算逐字一致")

    # ---- 4. 18 步编译 + 运行 ----
    gate_seq, ink_seq = [], []
    run_ok = True
    with tempfile.TemporaryDirectory(prefix="mini_chain_") as td:
        tmp = Path(td)
        for n, d in enumerate(dirs):
            exe = tmp / f"mini_{n:02d}"
            cc = subprocess.run(
                ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-lm", "-o", str(exe), str(d / "main.c")],
                capture_output=True, text=True)
            if cc.returncode != 0:
                bad("BUILD", f"第 {n} 步编译失败：{cc.stderr.strip()[:160]}")
                run_ok = False
                continue
            outputs, digests = [], []
            for rep in range(2):
                work = tmp / f"w{n:02d}_{rep}"
                work.mkdir()
                rr = subprocess.run([str(exe)], cwd=str(work),
                                    capture_output=True, text=True)
                outputs.append(rr)
                bmp = work / "mini_out.bmp"
                digests.append(hashlib.md5(bmp.read_bytes()).hexdigest() if bmp.is_file() else "")
            rr = outputs[0]
            if rr.returncode != 0:
                bad("RUN", f"第 {n} 步退出码 {rr.returncode}（gates 有 FAIL）")
                run_ok = False
            out = rr.stdout
            for k in range(n + 1):
                if f"MINI-STEP-{k:02d}-OK" not in out:
                    bad("RUN", f"第 {n} 步缺第 {k} 课的 MINI-STEP-{k:02d}-OK")
                    run_ok = False
            m = re.search(r"MINI-CHAIN-OK steps=(\d+) ink=(\d+) gates=(\d+)/(\d+)", out)
            if not m:
                bad("RUN", f"第 {n} 步缺 MINI-CHAIN-OK 汇总行")
                run_ok = False
                continue
            steps_n, ink, gok, gfail = (int(x) for x in m.groups())
            if steps_n != n + 1:
                bad("RUN", f"第 {n} 步 steps={steps_n}，应为 {n + 1}")
                run_ok = False
            if gfail != 0:
                bad("RUN", f"第 {n} 步 gates_fail={gfail}")
                run_ok = False
            if gok < 1:
                bad("RUN", f"第 {n} 步 gates_ok={gok}，判据没长出来")
                run_ok = False
            gate_seq.append(gok)
            ink_seq.append(ink)
            if digests[0] == "" or digests[0] != digests[1]:
                bad("RUN", f"第 {n} 步两次运行的 BMP 不一致（非确定性）")
                run_ok = False
            if (n == LAST) != ("MINI-FINAL-OK" in out):
                bad("RUN", f"第 {n} 步 MINI-FINAL-OK 标记位置不对")
                run_ok = False
        if len(gate_seq) == LAST + 1:
            if any(gate_seq[i + 1] <= gate_seq[i] for i in range(LAST)):
                bad("RUN", f"gates_ok 未严格递增：{gate_seq}")
                run_ok = False
            if any(ink_seq[i + 1] < ink_seq[i] for i in range(LAST)):
                bad("RUN", f"ink 出现下降：{ink_seq}")
                run_ok = False
            if ink_seq[-1] <= ink_seq[0]:
                bad("RUN", "末步画面没有比最小框架更丰富")
                run_ok = False
    if run_ok:
        ok(f"18 步编译运行：-Werror 零警告 / rc=0 / gates_ok {gate_seq[0]}→{gate_seq[-1]} "
           f"严格递增 / ink {ink_seq[0]}→{ink_seq[-1]} 非递减 / BMP 两次逐字节相同")

    # ---- 5. 组织纪律 ----
    org_ok = True
    for n, s in enumerate(steps_meta):
        note = dirs[n] / NOTE
        if not note.is_file():
            bad("ORG", f"第 {n} 步缺 {NOTE}")
            org_ok = False
            continue
        tags = [l for l in read_text(note).splitlines() if l.startswith("> 分类：")]
        want = want_track(n)
        if len(tags) != 1 or tags[0].split("：", 1)[1].strip() != want:
            bad("ORG", f"第 {n} 步 NOTE 分类应为「{want}」")
            org_ok = False
        if s["track"] != want:
            bad("ORG", f"chain.json 第 {n} 课 track 应为「{want}」")
            org_ok = False
    plat_hits = []
    for n, d in enumerate(dirs):
        p = d / f"step{n:02d}.h"
        if p.is_file() and re.search(r"#\s*if\s+defined\(\s*_WIN32\s*\)", read_text(p)):
            plat_hits.append(n)
    if plat_hits != [15]:
        bad("ORG", f"平台宏应只出现在 step15.h，实际出现在 {plat_hits}")
        org_ok = False
    if org_ok:
        ok("组织纪律：18 篇 NOTE 分类与段边界一致 / 平台差异只在 step15.h 一处")

    # ---- 6. 合流点 MAP.md 的路径 ----
    map_path = MINI / "MAP.md"
    if not map_path.is_file():
        bad("MAP", "tutorial/mini/MAP.md 不存在（演化链缺合流点）")
    elif not (ROOT / "src").is_dir():
        print("SKIP: 非完整仓库副本（--self-test 场景），跳过 MAP 路径存在性检查")
    else:
        text = read_text(map_path)
        refs = re.findall(r"`((?:src|scripts|android|lang|docs|tutorial)/[^`\s]+)`", text)
        refs = [r for r in refs if not any(ch in r for ch in "*()")]
        missing = [r for r in refs if not (ROOT / r).exists()]
        if not refs:
            bad("MAP", "MAP.md 没有引用任何真实源码路径")
        elif missing:
            bad("MAP", f"MAP.md 引用了不存在的路径：{missing}")
        else:
            ok(f"合流点：MAP.md 引用 {len(set(refs))} 个真实路径，全部存在")

    if fails:
        print(f"MINI-CHAIN-FAIL  ({len(fails)} 处不一致，标签 {sorted(set(fails))})")
        return 1
    print("MINI-CHAIN-OK  (18 步累加链 + 17 个 diff + 54 条内置判据全绿)")
    return 0


# --------------------------------------------------------------------------
# 负向对照：门禁必须能失败，否则就是假门禁
# --------------------------------------------------------------------------
def run_self_test() -> int:
    cases = [
        ("删掉第 10 步快照的一行旧代码", "ACCUM",
         lambda r: _inject_drop_line(r / "tutorial/mini/steps/step10_navvoice/step10.h")),
        ("篡改一个相邻 diff", "DIFF",
         lambda r: _inject_text(r / "tutorial/mini/diffs/09_to_10.diff", "TAMPERED\n")),
        # ★ 不能把判据条件直接改成常量 0：那会让变量变成未使用，
        #   -Werror 先在 BUILD 阶段拦下，测不到 RUN 判据。改的是被检查的【数据】。
        ("篡改数据让一条内置判据 FAIL", "RUN",
         lambda r: _inject_replace(r / "tutorial/mini/_parts/step03.h",
                                   "b[0] = 'S';", "b[0] = 'X';")),
        ("在业务代码里塞平台宏", "ORG",
         lambda r: _inject_text(r / "tutorial/mini/_parts/step09.h",
                                "#if defined(_WIN32)\n#endif\n")),
        ("制造一处编译错误", "BUILD",
         lambda r: _inject_text(r / "tutorial/mini/_parts/step04.h",
                                "this_is_not_valid_c;\n")),
    ]
    all_ok = True
    for name, expect_tag, inject in cases:
        with tempfile.TemporaryDirectory(prefix="mini_selftest_") as td:
            tmp = Path(td)
            (tmp / "scripts").mkdir()
            shutil.copytree(MINI, tmp / "tutorial/mini")
            shutil.copy2(REGEN, tmp / "scripts" / "regen_mini_chain.py")
            shutil.copy2(Path(__file__).resolve(), tmp / "scripts" / "check_mini_chain.py")
            inject(tmp)
            # 场景 3/4 改的是 _parts，需要重新生成才能让快照带上破坏
            if expect_tag in ("RUN", "ORG", "BUILD"):
                subprocess.run([sys.executable, str(tmp / "scripts/regen_mini_chain.py")],
                               capture_output=True, text=True)
            r = subprocess.run([sys.executable, str(tmp / "scripts/check_mini_chain.py")],
                               capture_output=True, text=True,
                               env={**os.environ, "PYTHONPATH": ""})
            hit = f"[{expect_tag}]" in r.stdout
            good = r.returncode != 0 and hit
            print(f"{'OK' if good else 'FAIL'}: 负向对照「{name}」→ rc={r.returncode}, "
                  f"含 [{expect_tag}]={hit}")
            if not good:
                all_ok = False
    if all_ok:
        print("MINI-CHAIN-SELFTEST-OK  (5 类破坏全部让门禁失败)")
        return 0
    print("MINI-CHAIN-SELFTEST-FAIL  (有破坏没被测出 ⇒ 假门禁)")
    return 1


def _inject_drop_line(p: Path):
    lines = read_text(p).splitlines(keepends=True)
    idx = next(i for i, l in enumerate(lines) if "MINI_ASSERT" in l)
    del lines[idx]
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write("".join(lines))


def _inject_text(p: Path, extra: str):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(read_text(p) + extra)


def _inject_replace(p: Path, old: str, new: str):
    text = read_text(p)
    assert old in text, f"负向对照注入点失效：{old}"
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(text.replace(old, new, 1))


if __name__ == "__main__":
    sys.exit(main())
