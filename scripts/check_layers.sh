#!/usr/bin/env bash
#
# 分层方向门禁：机器检查模块依赖方向，防止倒置回流。
#
#   用法: ./scripts/check_layers.sh              检查 src/ 全量（打印依赖图）
#         ./scripts/check_layers.sh --quiet      只在失败时输出
#         ./scripts/check_layers.sh --self-test  负向对照（临时夹具，不碰仓库 src/）
#
# 为什么要有它：
#   PLAN.md §53 的 R1/R2 把两处模块级倒置（map→render、render→ui）消掉了，
#   但**倒置是会被手痒写回来的**。原先没有任何机器检查，全靠人自觉 ——
#   而 ARCH_ASSESSMENT.md §4 早就画出了这两条边，只标注"语义上合理、无需破坏"，
#   结果它们一待就是几个月。
#   更糟的是：符号级环曾迫使 CMake 用 -Wl,--start-group 包住全部模块库，
#   链接器多轮扫描会**悄悄补上**新引入的反向依赖 ⇒ 边界形同虚设。
#   （R2 已移除 start-group，现在新引入的反向依赖会立刻 undefined-reference
#     —— 链接器这道防线回来了，但只有"方向真的一直对"才有效。）
#
# 判据：解析每个源文件的**真实** #include（剥掉块注释与行注释），
#       算出模块级依赖图，与允许的方向表比对。有违规则退出码非 0。
#
# ★ 负向对照（必须有分辨力，否则就是假门禁）：
#   本脚本曾被人为插入一条 map→render 的 include 验证过：
#   它确实 FAIL 并指出具体文件行号；撤掉后恢复 PASS。
#   见 PLAN.md §53.5。

set -u
cd "$(dirname "$0")/.."
# ★ --self-test 用临时夹具当 ROOT，正常验收仍用仓库根。
ROOT=${SK_LAYERS_ROOT:-$PWD}

QUIET=0
SELFTEST=0
for a in "$@"; do
    case "$a" in
        --quiet) QUIET=1 ;;
        --self-test) SELFTEST=1 ;;
        *) echo "未知参数: $a" >&2; exit 2 ;;
    esac
done

say() { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }

# ---------------------------------------------------------------------------
# --self-test：可复现的负向对照（不是「当年手工插过一次」的口头记录）。
#   在临时夹具里造两个模块：正常夹具 0 违规；插一条 map → render 的 include
#   后必须 FAIL 且点名这条边。两边任一不符都判自检失败。
#   ★ 改的是被检查的数据（夹具源码），不是判据条件本身。
# ---------------------------------------------------------------------------
if [ "$SELFTEST" = 1 ]; then
    FIX=$(mktemp -d -t sk_layers_XXXXXX)
    trap '/bin/rm -rf "$FIX"' EXIT
    mkdir -p "$FIX/src/map" "$FIX/src/render"
    printf '/* 夹具：map 模块 */\n#include "utils/plat.h"\n' > "$FIX/src/map/probe.c"
    printf '/* 夹具：render 模块 */\n#include "utils/plat.h"\n' > "$FIX/src/render/probe.c"

    SK_LAYERS_ROOT="$FIX" bash "$0" --quiet > "$FIX/clean.log" 2>&1
    rc_clean=$?
    printf '/* 夹具：map 模块 */\n#include "render/paint.h"\n' > "$FIX/src/map/probe.c"
    SK_LAYERS_ROOT="$FIX" bash "$0" --quiet > "$FIX/bad.log" 2>&1
    rc_bad=$?
    fails=0
    if [ "$rc_clean" != 0 ] || ! grep -q "分层方向门禁 PASS" "$FIX/clean.log"; then
        echo "  FAIL 干净夹具应当 PASS（rc=$rc_clean）"; fails=$((fails+1))
    else
        echo "  OK   干净夹具 0 违规"
    fi
    if [ "$rc_bad" = 0 ] || ! grep -q "map → render" "$FIX/bad.log"; then
        echo "  FAIL 注入 map → render 后应当 FAIL 并点名该边（rc=$rc_bad）"; fails=$((fails+1))
    else
        echo "  OK   注入 map → render 被抓到"
    fi
    if [ "$fails" != 0 ]; then
        echo "LAYERS-SELFTEST-FAIL ($fails 项未达预期)"; exit 1
    fi
    echo "LAYERS-SELFTEST-OK 正常夹具通过 / map → render 逆向依赖被抓到"
    exit 0
fi

# ★ 结论行**始终**打印（不受 --quiet 影响）：回归门禁的 run() 靠
#   "必须出现的标记 + 退出码"双判据判定，标记缺失会被判成假 FAIL。
say_result() { printf '%s\n' "$*"; }

# ---------------------------------------------------------------------------
# 允许的依赖方向（from 模块 → 允许依赖的模块集合，空格分隔）
#
# 层级（自下而上）：
#   i18n     叶子（多语言查表），零内部依赖
#   utils    叶子（平台抽象 / 几何 / 内存 / 字符串池），唯一下探：datum → i18n
#            （大地基准名的本地化文案；geo 库因此 PUBLIC 链 skedaddle_i18n）
#   pbf      叶子（PBF 解析），零内部依赖
#   map      → pbf, utils
#   route    → map, utils
#   search   → utils
#   settings → utils
#   render   → map, route, search, settings, utils   （纯光栅层，零 ui 依赖）
#   ui       → map, render, route, search, settings, utils, i18n
#   ui_qt    → map, render, settings, ui, utils, i18n
#
# ★ 同层依赖允许（render→render、ui→ui）：那是模块内部拆分，不是层级倒置。
# ★ 刻意**不**允许的（就是本门禁要守的）：
#     map  → render / route / ui / ui_qt
#     render → ui / ui_qt
#     route → render / ui / ui_qt
#     search→ map / render / route / ui / ui_qt
# ---------------------------------------------------------------------------
export SK_ALLOWED_i18n="i18n"
export SK_ALLOWED_map="map pbf utils"
export SK_ALLOWED_pbf="pbf"
export SK_ALLOWED_route="route map utils"
export SK_ALLOWED_search="search utils"
export SK_ALLOWED_settings="settings utils"
export SK_ALLOWED_render="render map route search settings utils"
export SK_ALLOWED_ui="ui map render route search settings utils i18n"
export SK_ALLOWED_ui_qt="ui_qt map render settings ui utils i18n"
export SK_ALLOWED_utils="utils i18n"

PY=$(command -v python3 || command -v python)
if [ -z "$PY" ]; then
    echo "需要 python3 才能解析 include" >&2
    exit 2
fi

OUT=$("$PY" - "$ROOT" <<'PYEOF'
import os, re, sys, glob

root = sys.argv[1]
src = os.path.join(root, "src")
if not os.path.isdir(src):
    print("ERR: no src dir", file=sys.stderr)
    sys.exit(2)

MODS = ["i18n", "utils", "pbf", "map", "route", "search", "settings", "render", "ui", "ui_qt"]

def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    return s

inc_re = re.compile(r'#\s*include\s*"([^"]+)"')

# 收集真实 include 边：逐文件、逐行剥注释后匹配
edges = []   # (from_mod, to_mod, relfile, lineno, target)
for mod in MODS:
    files = []
    for ext in ("c", "h", "cpp", "hpp", "cc"):
        files += glob.glob(os.path.join(src, mod, "**", "*." + ext), recursive=True)
    for f in sorted(set(files)):
        try:
            raw = open(f, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        rel = os.path.relpath(f, root)
        for ln, line in enumerate(raw.splitlines(), 1):
            l = strip_comments(line)
            for m in inc_re.finditer(l):
                tgt = m.group(1)
                if "/" not in tgt:
                    continue
                tmod = tgt.split("/", 1)[0]
                if tmod not in MODS:
                    continue
                edges.append((mod, tmod, rel, ln, tgt))

# 允许方向表
allowed = {}
for mod in MODS:
    env_key = "SK_ALLOWED_" + mod
    allowed[mod] = set(os.environ.get(env_key, "").split())

# 汇总模块级依赖（去重）
dep = {}
for fm, tm, rel, ln, tgt in edges:
    dep.setdefault(fm, set()).add(tm)

# 违规边（逐条 include 报出来，便于定位到行号）
viol = []
for fm, tm, rel, ln, tgt in edges:
    if tm not in allowed.get(fm, set()):
        viol.append((fm, tm, rel, ln, tgt))

# 输出格式：
#   DEP|<mod>|<dep1> <dep2> ...
#   VIOL|<from>|<to>|<file>|<line>|<target>
for mod in MODS:
    d = sorted(dep.get(mod, set()))
    print("DEP|%s|%s" % (mod, " ".join(d)))
for fm, tm, rel, ln, tgt in sorted(set(viol)):
    print("VIOL|%s|%s|%s|%d|%s" % (fm, tm, rel, ln, tgt))

print("COUNT|%d" % len(set(viol)))
PYEOF
)
rc=$?
if [ "$rc" != 0 ]; then
    echo "分层检查脚本自身执行失败（python 退出码 $rc）" >&2
    exit 2
fi

viol_count=$(printf '%s\n' "$OUT" | grep '^COUNT|' | cut -d'|' -f2)
viol_count=${viol_count:-0}

say "=== 模块依赖图（真实 include，剥注释后）==="
printf '%s\n' "$OUT" | grep '^DEP|' | while IFS='|' read -r _ mod deps; do
    say "$(printf '  %-9s -> %s' "$mod" "${deps:-（无）}")"
done

if [ "$viol_count" != 0 ]; then
    say ""
    say "=== 分层违规 ${viol_count} 处 ==="
    printf '%s\n' "$OUT" | grep '^VIOL|' | while IFS='|' read -r _ fm tm rel ln tgt; do
        printf '  ❌ %s → %s  (%s:%s  #include "%s")\n' "$fm" "$tm" "$rel" "$ln" "$tgt"
    done
    say ""
    say_result "分层方向门禁 FAIL：模块依赖方向必须保持 DAG，不得反向依赖。"
    exit 1
fi

say ""
say_result "分层方向门禁 PASS：0 违规"
exit 0
