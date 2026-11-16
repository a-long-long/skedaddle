#!/usr/bin/env bash
#
# NMBIN M2 的 Java 侧门禁：编译 + 夹具实跑（不需要 Android SDK / 真机）。
#
#   ★ 只编译「纯 JDK + 两个测试替身」能覆盖的类：Manifests / Downloader /
#     AtomicCommit / ChunkDelta / ChunkUpdater / M2JavaTest。DownloadActivity 依赖
#     androidx，主机没有 Android SDK ⇒ 不编译，只做接线静态检查（见 ACTIVITY-WIRE-OK）。
#     org.json / android.util.Log 用 android/tests/stub/ 的签名级替身；真机跑系统实现。
#   ★ 判据必须能失败：夹具内部每类正向都配负向注入，错哈希/脏块必须抛异常。
#   ★ 落盘提交只许走 AtomicCommit.commit（rename(2) 原子替换）：源码里重新出现
#     File.renameTo 就说明"先删后改"回来了，COMMIT-PATH-OK 会失败。
#
# 用法：./scripts/java_gate.sh
# 标记：JAVA-COMPILE-OK / ACTIVITY-WIRE-OK / COMMIT-PATH-OK / MANIFEST-OK /
#       MANIFEST-INJECT-CAUGHT / SIZE-INJECT-CAUGHT / COMMIT-INJECT-CAUGHT /
#       COMMIT-ATOMIC-OK / COMMIT-RECOVER-OK / DELTA-OK / DELTA-ZERO-OK /
#       DELTA-INJECT-CAUGHT / M2-JAVA-OK

set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
JAVAC=${JAVAC:-javac}
JAVA=${JAVA:-java}

SRC="$ROOT/android/app/src/main/java/com/skedaddle/android"
STUB="$ROOT/android/tests/stub"
TEST="$ROOT/android/tests/M2JavaTest.java"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/skedaddle_m2_java.XXXXXX") || exit 1

if ! "$JAVAC" -encoding UTF-8 -d "$OUT" \
        "$STUB/org/json/JSONException.java" \
        "$STUB/org/json/JSONArray.java" \
        "$STUB/org/json/JSONObject.java" \
        "$STUB/android/util/Log.java" \
        "$SRC/Manifests.java" \
        "$SRC/Downloader.java" \
        "$SRC/AtomicCommit.java" \
        "$SRC/ChunkDelta.java" \
        "$SRC/ChunkUpdater.java" \
        "$TEST"; then
    echo "ASSERT-FAIL javac 编译失败（M2 Java 源码或测试替身有错）"
    exit 1
fi
echo "JAVA-COMPILE-OK 9 个源文件零错误编译（javac $("$JAVAC" -version 2>&1)）"

# 接线静态检查：DownloadActivity 不进编译，但三条关键调用不能丢。
ACT="$SRC/DownloadActivity.java"
for needle in "downloadVerified" "ChunkUpdater" "r.sha256" "r.size" "r.chunksUrl"; do
    if ! grep -q -- "$needle" "$ACT"; then
        echo "ASSERT-FAIL DownloadActivity 缺接线 $needle"
        exit 1
    fi
done
echo "ACTIVITY-WIRE-OK DownloadActivity 的哈希 / 字节数 / 差分入口 / Row 字段接线齐全"

# 结构判据：落盘提交只许走 AtomicCommit.commit。
# File.renameTo 不能替换已存在的目标（要能覆盖就得先 delete），"先删后改"这个
# 两步窗口正是 2026-11-16 修的 bug；它一旦回到源码里，这条判据立刻失败。
_HITS=$(grep -rn -- '\.renameTo(' "$SRC" 2>/dev/null | grep -v -E ':[0-9]+:[[:space:]]*(\*|//)')
if [ -n "$_HITS" ]; then
    echo "ASSERT-FAIL 落盘路径出现 File.renameTo（应先删后改→AtomicCommit.commit 的 rename(2)）"
    echo "$_HITS"
    exit 1
fi
for f in ChunkDelta.java ChunkUpdater.java; do
    if ! grep -q 'AtomicCommit.commit' "$SRC/$f"; then
        echo "ASSERT-FAIL $f 的提交没走 AtomicCommit.commit"
        exit 1
    fi
done
if [ "$(grep -c 'AtomicCommit.commit(' "$SRC/ChunkUpdater.java")" -lt 2 ]; then
    echo "ASSERT-FAIL ChunkUpdater 应有两处原子提交（数据文件 + 块表）"
    exit 1
fi
echo "COMMIT-PATH-OK 源码零 renameTo：数据/块表/整包/半包四处提交统一走 rename(2) 原子替换"

"$JAVA" -cp "$OUT" com.skedaddle.android.M2JavaTest
rc=$?
if [ "$rc" != 0 ]; then
    echo "ASSERT-FAIL 夹具退出码 $rc"
    exit "$rc"
fi
exit 0
