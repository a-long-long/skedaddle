#!/usr/bin/env python3
"""生成 tutorial/mini/ 的「同一份工程逐课长大」演化链。

与 regen_tutorial_blocks.py 同范式：源在 tutorial/mini/_parts/，生成物在
tutorial/mini/{steps,diffs}/，--check 逐字比对（讲义/快照过期就 rc=1）。

★ 演化链的唯一硬规则（由本脚本的拼装方式保证、由 check_mini_chain.py 独立验收）：
    第 N 步的快照 = 第 N-1 步的全部文件（逐字不变）
                  + 新增 stepNN.h
                  + main.c 在 @@INCLUDE@@ / @@CALL@@ 两个锚点前各插入一行
  任何旧文件都不许删除或改写。这样「从最小框架逐步完善」是可机械验证的事实，
  而不是文档里的一句宣称。

用法：
    python3 scripts/regen_mini_chain.py            # 重新生成
    python3 scripts/regen_mini_chain.py --check    # 只校验，不写文件；过期 rc=1

★ 本机 core.autocrlf=true：所有读写都用 newline=''，否则 Python 文本模式会
  把 LF 写成 CRLF，造成 git diff 为空却显示 M 的假修改。
"""
from pathlib import Path
import difflib
import json
import sys

ROOT = Path(__file__).resolve().parent.parent
MINI = ROOT / "tutorial" / "mini"
PARTS = MINI / "_parts"
STEPS_DIR = MINI / "steps"
DIFFS_DIR = MINI / "diffs"

INC_ANCHOR = "/* @@INCLUDE@@ */"
CALL_ANCHOR = "    /* @@CALL@@ */"
GENERATED_NOTE = "NOTE.md"       # diff 里排除说明页，只呈现源码演化


def read_text(p: Path) -> str:
    with open(p, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write_text(p: Path, text: str) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def render_main(tpl: str, incs, calls) -> str:
    inc_block = "\n".join(incs)
    call_block = "\n".join(calls)
    out = tpl.replace(INC_ANCHOR, inc_block + "\n" + INC_ANCHOR)
    out = out.replace(CALL_ANCHOR, call_block + "\n" + CALL_ANCHOR)
    return out


def note_for(step, prev_n, n) -> str:
    n = step["n"]
    src = "、".join(f"`{s}`" for s in step["src"])
    if prev_n is None:
        diff_line = "本步是演化链起点，没有上一步。"
    else:
        name = f"{prev_n:02d}_to_{n:02d}.diff"
        diff_line = (f"与上一步的源码差异：[`{name}`](../../diffs/{name})"
                     f"（只含源码文件，`{GENERATED_NOTE}` 说明页不计入）。")
    return f"""# 第 {n} 课 · {step['title']}
> 分类：{step['track']}

> 演化链第 {n} 步的完整可编译快照：`tutorial/mini/steps/step{n:02d}_{step['slug']}/`
> 源码讲义：[`docs/teaching/{step['lesson']}`](../../../../docs/teaching/{step['lesson']})
> 同号独立小程序：`{step['mini']}`
> 真实工程落点：{src}

**这一课新增**：{step['adds']}。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-{n:02d}-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps={n + 1} ink=… gates=…/0`。

{diff_line}

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step{n:02d}.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
"""


def build():
    """返回 {相对路径: 文本}，即所有生成物的期望内容。"""
    tpl = read_text(PARTS / "main.c.tpl")
    meta = json.loads(read_text(PARTS / "chain.json"))
    steps = meta["steps"]
    assert len(steps) == 18, f"chain.json 应有 18 课，实际 {len(steps)}"
    assert [s["n"] for s in steps] == list(range(18)), "课次必须 0..17 严格递增"

    expected = {}
    incs, calls = [], []
    files = {}                       # 该步快照的源码文件（不含 NOTE.md）
    snapshots = []

    for step in steps:
        n = step["n"]
        code = read_text(PARTS / f"step{n:02d}.h")
        assert INC_ANCHOR not in code and CALL_ANCHOR not in code, f"step{n:02d}.h 不应含锚点"
        incs.append(f'#include "step{n:02d}.h"')
        calls.append(f"    mini_step_{n:02d}(&c);")
        files[f"step{n:02d}.h"] = code
        files["main.c"] = render_main(tpl, incs, calls)
        snap = dict(files)
        snapshots.append(snap)

        d = STEPS_DIR / f"step{n:02d}_{step['slug']}"
        for name, text in snap.items():
            expected[str((d / name).relative_to(ROOT))] = text
        expected[str((d / GENERATED_NOTE).relative_to(ROOT))] = note_for(
            step, n - 1 if n > 0 else None, n)

    # 相邻步的源码 diff（排除 NOTE.md 说明页，只呈现代码演化）
    for n in range(1, 18):
        a, b = snapshots[n - 1], snapshots[n]
        lines = []
        for name in sorted(set(a) | set(b)):
            old = a.get(name, "").splitlines(keepends=True)
            new = b.get(name, "").splitlines(keepends=True)
            if old == new:
                continue
            fromfile = "/dev/null" if name not in a else f"a/step{n - 1:02d}/{name}"
            tofile = f"b/step{n:02d}/{name}"
            lines.extend(difflib.unified_diff(old, new, fromfile=fromfile,
                                              tofile=tofile, n=3))
        expected[str((DIFFS_DIR / f"{n - 1:02d}_to_{n:02d}.diff").relative_to(ROOT))] = \
            "".join(lines)

    return expected, steps


def main() -> int:
    check = "--check" in sys.argv[1:]
    expected, steps = build()
    problems = []

    if check:
        for rel, text in sorted(expected.items()):
            p = ROOT / rel
            if not p.is_file():
                problems.append(f"MISSING  {rel}")
            elif read_text(p) != text:
                problems.append(f"STALE    {rel}")
        # 生成物目录里不应有多余文件（旧 slug 残留、手改副本等）
        actual = set()
        for base in (STEPS_DIR, DIFFS_DIR):
            if base.is_dir():
                for p in base.rglob("*"):
                    if p.is_file() and p.name != ".gitkeep":
                        actual.add(str(p.relative_to(ROOT)))
        for rel in sorted(actual - set(expected)):
            problems.append(f"EXTRA    {rel}")
        if problems:
            for x in problems:
                print(x)
            print(f"MINI-CHAIN-STALE  ({len(problems)} 处不一致；跑 python3 scripts/regen_mini_chain.py 重建)")
            return 1
        print(f"MINI-CHAIN-CONTENT-OK  ({len(expected)} 个生成物逐字一致，18 个快照 + 17 个 diff)")
        return 0

    for rel, text in sorted(expected.items()):
        write_text(ROOT / rel, text)
    print(f"MINI-CHAIN-GENERATED  {len(steps)} 个快照 + 17 个相邻 diff，共 {len(expected)} 个文件")
    return 0


if __name__ == "__main__":
    sys.exit(main())
