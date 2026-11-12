#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""守「手工维护的讲义逐字块 / 行号」是否仍与当前 src/ 对得上。

背景：2026-11-10 分带并行、11-11 导航五项把 labels.c 从 560 行长到 996 行，
第 7 课讲的函数行号整体下移两三白行，且 font_draw_halo 先变死代码、后于 11-12 删除，
但所有生成式门禁（regen_tutorial_blocks.py 只覆盖 16_qt / 11_gates）都没报错。
本脚本补上手工讲义这一块的硬判据：

  1. 两个「完整体，逐字」代码块 == 源码对应行区间（区间由讲义自己声明）；
  2. 「二、功能构成」表格里声明的每个 file.c:A-B 区间，确实能找到该行提到的标识符；
  3. 「源码演进补记」章节与死代码口径、以及 index.md 的演进索引不能丢。

用法：
    python3 scripts/check_teaching_sync.py               # 正常验收，rc=0 打印 TEACHING-SYNC-OK
    python3 scripts/check_teaching_sync.py --self-test   # 3 类注入破坏，证明门禁能失败

★ 与 check_tutorial_tracks.py / check_mini_chain.py 同纪律：**不接 regress.sh**，
  保当前 109/0 口径与副机行为。源码一改导致区间平移时本脚本会红，
  这正是提醒「回源重贴讲义」的信号，不要直接改数字了事。
"""
from __future__ import annotations

import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = Path("docs/teaching/07_pipeline.md")
INDEX = Path("docs/teaching/index.md")

# 讲义里用短文件名；显式映射避免 src/ 下重名文件歧义。
FILE_MAP = {
    "window.c": "src/ui/window.c",
    "paint.h": "src/render/paint.h",
    "navdraw.c": "src/render/navdraw.c",
    "audit.c": "src/ui/audit.c",
    "audit.h": "src/ui/audit.h",
    "chrome.c": "src/ui/chrome.c",
    "chrome.h": "src/ui/chrome.h",
    "labels.c": "src/render/labels.c",
    "labels.h": "src/render/labels.h",
    "font.c": "src/render/font.c",
    "paint.c": "src/render/paint.c",
    "band.h": "src/render/band.h",
}

REF_RE = re.compile(r"`([A-Za-z_][A-Za-z0-9_]*\.[ch]):(\d+)(?:-(\d+))?`")
ID_RE = re.compile(r"`([A-Za-z_][A-Za-z0-9_]*)`")

# (代码块序号, 声明区间的正则, 块内必须出现的关键字)
BLOCK_DECLS = [
    (0, re.compile(r"### 1\.[^\n]*?`([A-Za-z_][A-Za-z0-9_]*\.[ch]):(\d+)-(\d+)`"), "paint_frame"),
    (11, re.compile(r"以下为 `([A-Za-z_][A-Za-z0-9_]*\.[ch]):(\d+)-(\d+)` 完整体（当前真实实现）"), "label_task"),
]


def code_blocks(lines: list[str]) -> list[list[str]]:
    out, i = [], 0
    while i < len(lines):
        if lines[i].strip() == "```c":
            s = i + 1
            e = next((j for j in range(s, len(lines)) if lines[j].strip() == "```"), len(lines))
            out.append(lines[s:e])
            i = e + 1
        else:
            i += 1
    return out


def read_lines(root: Path, rel: str) -> list[str]:
    return (root / rel).read_text(encoding="utf-8", errors="replace").splitlines()


def check(root: Path) -> list[str]:
    errs: list[str] = []
    doc = root / DOC
    if not doc.is_file():
        return ["DOC: 讲义不存在"]
    text = doc.read_text(encoding="utf-8")
    lines = text.splitlines()
    blocks = code_blocks(lines)

    # ---- 1. 逐字块 ----
    if len(blocks) != 12:
        errs.append(f"DOC: 代码块数 {len(blocks)} != 12（讲义结构变了，须重核本脚本）")
    else:
        for bi, pat, keyword in BLOCK_DECLS:
            m = pat.search(text)
            if not m:
                errs.append(f"BLOCK: 第 {bi + 1} 个逐字块找不到区间声明")
                continue
            short, a, b = m.group(1), int(m.group(2)), int(m.group(3))
            rel = FILE_MAP.get(short)
            if not rel or not (root / rel).is_file():
                errs.append(f"BLOCK: 第 {bi + 1} 块声明的 {short} 未映射/不存在")
                continue
            try:
                src = read_lines(root, rel)[a - 1:b]
            except OSError as ex:
                errs.append(f"BLOCK: 读 {rel} 失败: {ex}")
                continue
            if blocks[bi] != src:
                errs.append(f"BLOCK: 第 {bi + 1} 个逐字块与 {short}:{a}-{b} 不逐字一致")
            if keyword not in "\n".join(blocks[bi]):
                errs.append(f"BLOCK: 第 {bi + 1} 个逐字块缺关键字 {keyword}")

    # ---- 2. 「二、功能构成」表格的区间 ----
    try:
        i0 = next(i for i, l in enumerate(lines) if l.startswith("## 二、功能构成"))
    except StopIteration:
        errs.append("DOC: 找不到「二、功能构成」")
        i0 = None
    if i0 is not None:
        table = []
        for l in lines[i0 + 1:]:
            if l.startswith("|"):
                table.append(l)
            elif table:
                break
        for l in table:
            refs = [r for r in REF_RE.findall(l) if r[0] in FILE_MAP]
            if not refs:
                continue
            ids = ID_RE.findall(l)
            hit = False
            for short, a, b in refs:
                rel = FILE_MAP[short]
                f = root / rel
                if not f.is_file():
                    continue
                a, b = int(a), int(b or a)
                seg = "\n".join(read_lines(root, rel)[a - 1:b])
                if ids and any(re.search(r"\b" + re.escape(x) + r"\b", seg) for x in ids):
                    hit = True
            if not hit:
                errs.append(f"SPAN: 表格行 {l[:56]}... 的区间内找不到所列标识符")

    # ---- 3. 演进说明不能丢 ----
    if "## 六、源码演进补记" not in text:
        errs.append("SECTION: 第 7 课缺「源码演进补记」")
    elif "已删除" not in text:
        errs.append("SECTION: 演进补记缺 font_draw_halo 已删除口径")
    idx = root / INDEX
    if not idx.is_file() or "源码演进索引" not in idx.read_text(encoding="utf-8"):
        errs.append("SECTION: index.md 缺「源码演进索引」")
    return errs


def self_test() -> int:
    need = [DOC, INDEX] + [Path(v) for v in FILE_MAP.values()]
    cases = [
        ("改坏 paint_frame 逐字块一行", "BLOCK",
         lambda r: _inject(r, "static void paint_frame(Raster *rs, App *a, PaintStats *st)",
                           "static void paint_frame_V2(Raster *rs, App *a, PaintStats *st)")),
        ("把表格里 paint_frame 的区间改错", "SPAN",
         lambda r: _inject(r, "`window.c:1309-1573` | 一帧的 9 步编排", "`window.c:1-5` | 一帧的 9 步编排")),
        ("删掉源码演进补记", "SECTION",
         lambda r: _inject(r, None, None, cut="## 六、源码演进补记")),
    ]
    failed = 0
    for name, tag, fn in cases:
        with tempfile.TemporaryDirectory(prefix="teach_sync_") as td:
            tmp = Path(td)
            for rel in need:
                src = ROOT / rel
                if not src.is_file():
                    continue
                dst = tmp / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
            fn(tmp)
            errs = check(tmp)
            ok = any(e.startswith(tag + ":") for e in errs)
            print(f"  {'OK  ' if ok else 'FAIL'} {name:<32} → 期望 {tag}: / 实得 "
                  f"{','.join(e.split(':')[0] for e in errs) or '(无)'}")
            if not ok:
                failed += 1
    # 未破坏的副本必须全绿（排除「负向对照把正常路径也弄红」）
    with tempfile.TemporaryDirectory(prefix="teach_sync_") as td:
        tmp = Path(td)
        for rel in need:
            src = ROOT / rel
            if not src.is_file():
                continue
            dst = tmp / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        errs = check(tmp)
        print(f"  {'OK  ' if not errs else 'FAIL'} 干净副本必须全绿 → {errs or 'TEACHING-SYNC-OK'}")
        if errs:
            failed += 1
    if failed:
        print(f"TEACHING-SYNC-SELFTEST-FAIL  ({failed} 项未达预期)")
        return 1
    print("TEACHING-SYNC-SELFTEST-OK  (3 类破坏全部让门禁失败，干净副本全绿)")
    return 0


def _inject(root: Path, old: str | None, new: str | None, cut: str | None = None) -> None:
    p = root / DOC
    t = p.read_text(encoding="utf-8")
    if cut:
        assert cut in t, "cut anchor missing"
        t = t[:t.index(cut)]
    else:
        assert old in t, "inject anchor missing: " + old[:40]
        t = t.replace(old, new, 1)
    p.write_bytes(t.encode("utf-8"))


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    errs = check(ROOT)
    if errs:
        for e in errs:
            print("ASSERT-FAIL " + e)
        print(f"TEACHING-SYNC-FAIL  ({len(errs)} 项)")
        return 1
    print("TEACHING-SYNC-OK  (第 7 课 2 个逐字块 + 功能构成表区间 + 演进索引均与 src/ 一致)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
