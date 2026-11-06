#!/usr/bin/env python3
"""把讲义里「逐字内嵌」的源码块从当前 src/ 重新生成。

为什么需要它
------------
`docs/teaching/16_qt.md` 与 `11_gates.md` 承诺「完整体、逐字、可编译」，
但源码一改，内嵌块就悄悄过期：行号锚点漂移、新函数缺席、删掉的函数
还留在讲义里。手工同步既慢又容易漏（本项目踩过：删了窗口按钮，
讲义里的自绘代码还教着怎么画）。本脚本让讲义块成为源码的**确定函数**，
改完代码跑一次即可。

覆盖范围
--------
* 16_qt.md §3.4  `main_window.h` 全文
* 16_qt.md §3.5  `main_window.cpp` 的 4 个代码块，按**方法边界**切分：
    块 1 = runUiSelfTest
    块 3 = eventFilter / clearMenus / rebuildGridCtx / buildMenus
    块 4 = setGridType … buildStepsDock（源码连续段）
    块 2 = 其余全部方法（构造 / 搜索浮层 / 右栏 / 窗口拖动带 / 下载 / 按键…）
  四块合起来正好覆盖文件中**每一个** MainWindow 方法，不重不漏。
  每块内容 = 方法定义行 → 下一个方法定义行前，逐字复制（含方法之间的
  静态辅助函数与空行），因此新加的方法会自动落到对应块里。
* 11_gates.md §3.6 内嵌的 `scripts/regress.sh` 全文。

纪律
----
* 不做「猜归属」的行号审计：一篇讲义的表格里一行可能有多个符号、多个
  行号，自动匹配必然误判（踩过：把 bbox_overlap 的行号改成 src_unload）。
  这里只按**方法定义的文本边界**切，判定是确定性的。
* 幂等：重复执行结果逐字相同；写回统一用 newline=''（本仓库
  core.autocrlf=true，Python 默认文本模式会把 LF 写成 CRLF 造成假 M）。
* 自检：四块方法集合必须与源码方法集合相等且无重复，否则直接报错退出，
  绝不写出半份讲义。

用法：python3 scripts/regen_tutorial_blocks.py [--check]
      --check 只报告「会不会变」，不写文件（可接进回归做门禁）。
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QT_DOC = ROOT / "docs/teaching/16_qt.md"
GATE_DOC = ROOT / "docs/teaching/11_gates.md"
MW_CPP = ROOT / "src/ui_qt/main_window.cpp"
MW_H = ROOT / "src/ui_qt/main_window.h"
REGRESS = ROOT / "scripts/regress.sh"

# 行首、无缩进的 MainWindow 方法定义（含构造/析构；不含缩进的 lambda 或调用）
METHOD = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_:<> *&]*\s+)?MainWindow::([A-Za-z_~][A-Za-z0-9_]*)\s*\("
)


def read(p):
    return p.read_text(encoding="utf-8", newline="")


def lines(p):
    return read(p).split("\n")


def method_spans(src):
    """返回 [(名字, 定义行下标, [源码行...])]，span 到下一个方法定义前。"""
    defs = [(i, m.group(1)) for i, l in enumerate(src) if (m := METHOD.match(l))]
    out = []
    for k, (i, name) in enumerate(defs):
        end = defs[k + 1][0] if k + 1 < len(defs) else len(src)
        out.append((name, i, src[i:end]))
    return out


def plan_blocks(src):
    """把方法分配到 4 个块；返回 [(块说明, [方法 span...])]。"""
    spans = method_spans(src)
    names = [s[0] for s in spans]
    if len(names) != len(set(names)):
        raise SystemExit("FATAL: 源码里有同名 MainWindow 方法（重载）？本脚本按名字切块，需人工处理")

    def take(*wanted):
        pick = set(wanted)
        return [s for s in spans if s[0] in pick]

    blk1 = take("runUiSelfTest")
    blk3 = take("eventFilter", "clearMenus", "rebuildGridCtx", "buildMenus")

    # 块 4：setGridType 起、buildStepsDock 止（源码连续一段）
    i0 = next(i for i, s in enumerate(spans) if s[0] == "setGridType")
    i1 = next(i for i, s in enumerate(spans) if s[0] == "buildStepsDock")
    blk4 = spans[i0:i1 + 1]

    used = {s[0] for s in blk1 + blk3 + blk4}
    blk2 = [s for s in spans if s[0] not in used]

    all_names = {s[0] for s in blk1 + blk2 + blk3 + blk4}
    if all_names != set(names) or sum(len(b) for b in (blk1, blk2, blk3, blk4)) != len(spans):
        raise SystemExit("FATAL: 四块未完整覆盖源码方法（有重复或遗漏）")
    for must in ("runUiSelfTest", "eventFilter", "setGridType", "buildStepsDock"):
        if must not in all_names:
            raise SystemExit(f"FATAL: 源码里找不到关键方法 {must}，块划分规则需更新")
    return [
        ("无头 UI 自检 runUiSelfTest", blk1),
        ("构造 / 搜索浮层 / 右栏 / 其余槽", blk2),
        ("事件过滤器 + 菜单栏", blk3),
        ("其余 MainWindow 方法", blk4),
    ]


def block_body(spans):
    """逐字拼接若干方法 span，去掉尾部多余空行。"""
    out = []
    for _, _, body in spans:
        out.extend(body)
    while out and out[-1].strip() == "":
        out.pop()
    return out


def find_fences(ls):
    """返回 [(开始下标, 结束下标)]，开始/结束都是 ``` 行下标。"""
    out, start = [], None
    for i, l in enumerate(ls):
        if l.startswith("```"):
            if start is None:
                start = i
            else:
                out.append((start, i))
                start = None
    if start is not None:
        raise SystemExit("FATAL: 讲义里有未闭合的代码围栏")
    return out


def rebuild_qt(src_cpp, src_h, doc):
    ls = doc.split("\n")
    fences = find_fences(ls)

    # ---- §3.4：主窗口头全文（定位「### 3.4」之后的第一个代码块）----
    h_sec = next(i for i, l in enumerate(ls) if l.startswith("### 3.4 主窗口头"))
    h_fence = next(f for f in fences if f[0] > h_sec)
    # ---- §3.5：四个 main_window.cpp 代码块 ----
    c_sec = next(i for i, l in enumerate(ls) if l.startswith("### 3.5 主窗口实现"))
    c_fences = [f for f in fences if f[0] > c_sec][:4]
    if len(c_fences) != 4:
        raise SystemExit("FATAL: §3.5 之后找不到 4 个代码块，讲义结构变了")

    blocks = plan_blocks(src_cpp)
    edits = []

    h_body = src_h[:]
    while h_body and h_body[-1].strip() == "":
        h_body.pop()
    edits.append((h_fence, h_body))

    for fence, (title, spans) in zip(c_fences, blocks):
        body = block_body(spans)
        # 块可能由源码里好几段不相邻的连续区间拼成（例如跳过 runUiSelfTest
        # 的那一大段），注释里必须把每段都列出来，不能写成 100-3144
        # 这种"看起来是一整块"的假区间。
        segs = []
        for _, i, chunk in spans:
            a, b = i + 1, i + len(chunk)
            while b > a and src_cpp[b - 1].strip() == "":
                b -= 1
            # 相邻方法之间通常空 1~3 行（含分隔注释），仍视作同一段；
            # 若中间隔着整块别的讲义内容（如 runUiSelfTest 720 行），
            # 间隔远大于 4，自然断成下一段。
            if segs and a <= segs[-1][1] + 4:
                segs[-1] = (segs[-1][0], max(segs[-1][1], b))
            else:
                segs.append((a, b))
        rng = ", ".join(f"{a}-{b}" if a != b else str(a) for a, b in segs)
        note = (f"/* src/ui_qt/main_window.cpp:{rng} —— {title}"
                f"（完整体，逐字；由 scripts/regen_tutorial_blocks.py 从当前源码生成） */")
        edits.append((fence, [note, ""] + body))

    # 从后往前替换，下标不失效
    for (a, b), body in sorted(edits, key=lambda e: -e[0][0]):
        ls[a + 1:b] = body
    return "\n".join(ls), blocks


def rebuild_gates(src_regress, doc):
    ls = doc.split("\n")
    fences = find_fences(ls)
    anchor = next(i for i, l in enumerate(ls) if "完整源码 · `scripts/regress.sh`" in l)
    fence = next(f for f in fences if f[0] > anchor)
    body = src_regress[:]
    while body and body[-1].strip() == "":
        body.pop()
    # 保留块首那行「逐字内嵌」说明，但把行数改成当前真实值
    note = f"> 完整源码 · `scripts/regress.sh`（逐字内嵌，全文 {len(body)} 行）"
    ls[fence[0] + 1:fence[1]] = [note] + body
    return "\n".join(ls)


def main():
    check = "--check" in sys.argv[1:]
    src_cpp = lines(MW_CPP)
    src_h = lines(MW_H)
    src_regress = lines(REGRESS)

    qt_old, gate_old = read(QT_DOC), read(GATE_DOC)
    qt_new, blocks = rebuild_qt(src_cpp, src_h, qt_old)
    gate_new = rebuild_gates(src_regress, gate_old)
    # 自检：再生成一次必须逐字相同（幂等 ⇒ 块结构与源码边界都自洽）
    if rebuild_qt(src_cpp, src_h, qt_new)[0] != qt_new or \
       rebuild_gates(src_regress, gate_new) != gate_new:
        raise SystemExit("FATAL: 生成结果不幂等，块定位规则与源码边界不自洽")

    changed = []
    for path, old, new in ((QT_DOC, qt_old, qt_new), (GATE_DOC, gate_old, gate_new)):
        if old != new:
            changed.append(path)
            if not check:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(new)
    print(f"源码方法总数: {len(method_spans(src_cpp))}")
    for title, spans in blocks:
        print(f"  {title:32s} {len(spans):2d} 个方法 / {sum(len(s[2]) for s in spans):4d} 行")
    def nlines(p):
        s = lines(p)
        while s and s[-1].strip() == "":
            s.pop()
        return len(s)
    print(f"regress.sh: {nlines(REGRESS)} 行；main_window.h: {nlines(MW_H)} 行")
    if changed:
        verb = "需要更新（--check 不写文件）" if check else "已更新"
        print(f"{verb}: " + ", ".join(str(p.relative_to(ROOT)) for p in changed))
    else:
        print("讲义逐字块已是最新（幂等）")
    # ★ 判「成功」看退出码而不是输出行数：--check 发现讲义过期必须 rc=1，
    #   否则接进门禁就是永远绿的假判据。
    return 1 if (check and changed) else 0


if __name__ == "__main__":
    sys.exit(main())
