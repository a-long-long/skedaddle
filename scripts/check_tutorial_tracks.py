#!/usr/bin/env python3
"""教程「两段式实现顺序」分类门禁。

2026-11-06 起教程按**先核心代码、最后跨平台代码**重排：
    核心代码   = 第 0–14 课（平台无关的 C 核心 + 工程化配套）
    跨平台代码 = 第 15–17 课（plat / ui_qt / android，换壳不换核心）

本脚本把这条组织纪律固化成机器判据（与 check_doc_numbers.sh 互补：
那个守「文件名 NN ↔ H1 第 N 课」，本脚本守「课次 ↔ 分类 ↔ 段边界」）：

  1. 每篇讲义 H1 下方必须有且仅有一行 `> 分类：核心代码` / `> 分类：跨平台代码`；
  2. 分类必须与课次段一致（0–14 核心，15–17 跨平台）；
  3. `docs/teaching/index.md` 与 `docs/teaching-plan.md` 的课程表必须按课次 0..17
     **严格递增**，且每行的分类列与讲义标签一致 —— 这就守住了「先核心、最后跨平台」；
  4. `tutorial/NN_*/README.md`（00–16）的分类标签与同号讲义一致；第 17 课无小程序。

判据（与 regress.sh 的 run() 双判据对齐）：
  - 全通过 → 打印 TUTORIAL-TRACKS-OK 且退出码 0
  - 任一不一致 → 打印 ASSERT-FAIL:<说明> 并 TUTORIAL-TRACKS-FAIL，退出码 1

★ 负向对照：改任意一篇讲义的分类标签（或调换 index 表格两行）本脚本必须 FAIL；
  否则就是假门禁，测不出「核心段里混进跨平台课」这类漂移。

★ 本脚本目前**未接进 regress.sh**（避免改变 87/0 基线与 Windows 副机行为），
  改完讲义编号 / 分类后手动跑一次；接门禁属于基线变更，需单独决定。
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
TEACH = ROOT / "docs" / "teaching"
INDEX = TEACH / "index.md"
PLAN = ROOT / "docs" / "teaching-plan.md"

CORE = "核心代码"
CROSS = "跨平台代码"
CROSS_FROM = 15
LAST = 17


def want(n):
    return CROSS if n >= CROSS_FROM else CORE


def read(p):
    return p.read_text(encoding="utf-8")


def main():
    fails = []

    def bad(msg):
        fails.append(msg)
        print("ASSERT-FAIL: " + msg)

    def ok(msg):
        print("OK: " + msg)

    # ---- 1/2. 讲义分类标签 ----
    doc_tag = {}
    for p in sorted(TEACH.glob("[0-9][0-9]_*.md")):
        n = int(p.name[:2])
        lines = read(p).splitlines()
        h1 = next((i for i, l in enumerate(lines) if l.startswith("# ")), None)
        if h1 is None:
            bad(f"{p.name} 找不到 H1")
            continue
        if f"第 {n} 课" not in lines[h1]:
            bad(f"{p.name} H1 应为「第 {n} 课」，实际 {lines[h1][:60]}")
        tags = [l for l in lines if l.startswith("> 分类：")]
        if len(tags) != 1:
            bad(f"{p.name} 分类标签应恰好 1 行，实际 {len(tags)} 行")
            continue
        got = tags[0].split("：", 1)[1].strip()
        exp = want(n)
        if got != exp:
            bad(f"{p.name} 分类应为「{exp}」，实际「{got}」")
        else:
            ok(f"{p.name:20s} 第 {n:2d} 课 → {got}")
        doc_tag[n] = got

    # ---- 3. 两个课程表的顺序与分类 ----
    def check_table(path, header, row_re, n_group, cls_group):
        """只取 header 之后那一张连续表格，避免把附录里的数字表格也算进来。"""
        if not path.is_file():
            bad(f"{path.relative_to(ROOT)} 不存在")
            return
        lines = read(path).splitlines()
        st = next((i for i, l in enumerate(lines) if l.startswith(header)), None)
        if st is None:
            bad(f"{path.name} 找不到课程表表头「{header}」")
            return
        rows = []
        for l in lines[st + 2:]:                  # 跳过表头与分隔行
            if not l.startswith("|"):
                break
            m = row_re.match(l)
            if m:
                rows.append((int(m.group(n_group)), m.group(cls_group).strip(), l))
        before = len(fails)
        nums = [r[0] for r in rows]
        if nums != list(range(LAST + 1)):
            bad(f"{path.name} 课程表课次不是 0..{LAST} 严格递增：{nums}")
        for n, cl, line in rows:
            if cl != want(n):
                bad(f"{path.name} 第 {n} 课分类列应为「{want(n)}」，实际「{cl}」 | {line[:70]}")
            if n in doc_tag and cl != doc_tag[n]:
                bad(f"{path.name} 第 {n} 课分类「{cl}」与讲义标签「{doc_tag[n]}」不一致")
        if len(fails) == before:
            ok(f"{path.name} 课程表 {len(rows)} 行，顺序与分类一致")

    # index: | [00_overview.md](00_overview.md) | 0 | 核心代码 | ...
    check_table(INDEX, "| 文件 | 课次 | 分类 |",
                re.compile(r"^\| \[\d{2}_[^\]]+\.md\]\([^)]*\) \| (\d+) \| ([^|]+) \|"), 1, 2)
    # plan:  | 0 | 核心代码 | 主题 | ...
    check_table(PLAN, "| 课次 | 分类 |",
                re.compile(r"^\| (\d+) \| (核心代码|跨平台代码) \|"), 1, 2)

    # ---- 4. tutorial 小程序 README ----
    for n in range(LAST):
        dirs = sorted((ROOT / "tutorial").glob(f"{n:02d}_*"))
        if not dirs:
            bad(f"tutorial/{n:02d}_* 不存在（第 {n} 课小程序缺失）")
            continue
        rp = dirs[0] / "README.md"
        if not rp.is_file():
            bad(f"{rp.relative_to(ROOT)} 不存在")
            continue
        tags = [l for l in read(rp).splitlines() if l.startswith("> 分类：")]
        if len(tags) != 1 or tags[0].split("：", 1)[1].strip() != want(n):
            bad(f"{rp.relative_to(ROOT)} 分类应为「{want(n)}」")
    if (ROOT / "tutorial" / "17_android").exists():
        bad("第 17 课不应有 tutorial/17_android 小程序（配套是 android/ 真实工程）")

    if fails:
        print(f"TUTORIAL-TRACKS-FAIL  ({len(fails)} 处不一致)")
        return 1
    print(f"TUTORIAL-TRACKS-OK  (已核对 {len(doc_tag)} 篇讲义 + 2 张课程表 + 17 个小程序)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
