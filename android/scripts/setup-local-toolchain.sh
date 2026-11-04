#!/usr/bin/env bash
# setup-local-toolchain.sh — aarch64 Linux 宿主专用：从 Android NDK r27b zip 提取
# 本机构建所需的“架构无关资产”，使其能在 ARM64 机上交叉编译 libskedaddle.so。
#
# 背景：官方 NDK 只发布 x86_64 宿主二进制，其 clang/ninja 无法在 ARM64 机运行。
#       方案是只用 NDK 里架构无关的部分（sysroot + 目标代码的 builtins），编译
#       链接全用本机原生 clang 24。详见 docs/android.md §5.1。
#
# 产出：
#   ~/tools/android/ndk-sysroot   ← NDK 的 sysroot（Bionic 头/桩库）
#   ~/tools/android/clang-res     ← clang 24 运行时（include 符号链接 + builtins）
#
# 用法：bash android/scripts/setup-local-toolchain.sh <ndk-zip> [api]
#   <ndk-zip>   android-ndk-r27b-linux.zip 的本地路径（或设 SK_NDK_ZIP）
#   [api]       minSdk，默认 26（决定 clang-res 子目录名，须与 toolchain.cmake 一致）
set -euo pipefail

NDK_ZIP="${1:-${SK_NDK_ZIP:-}}"
API="${2:-26}"
DEST="$HOME/tools/android"
SYSROOT="$DEST/ndk-sysroot"
CLANG_RES="$DEST/clang-res"

[ -n "$NDK_ZIP" ] || { echo "用法: $0 <android-ndk-r27b-linux.zip> [api]" >&2; exit 1; }
[ -f "$NDK_ZIP" ] || { echo "错误：NDK zip 不存在：$NDK_ZIP" >&2; exit 1; }

CLANG_VER=$(/usr/local/bin/clang -dumpversion 2>/dev/null || echo 24)
[ -d /usr/local/lib/clang/24/include ] || {
  echo "错误：未找到本机 clang 24 资源头 /usr/local/lib/clang/24/include" >&2; exit 1; }

TMP=$(mktemp -d)
echo "==> 解包 NDK: $NDK_ZIP"
unzip -q -o "$NDK_ZIP" -d "$TMP"
NDK_DIR=$(find "$TMP" -maxdepth 1 -type d -name 'android-ndk-*' | head -1)
[ -n "$NDK_DIR" ] || { echo "错误：NDK zip 内未找到 android-ndk-* 目录" >&2; exit 1; }

# 1) sysroot（Bionic 头/桩库）—— toolchain.cmake 期望路径：
#    $SYSROOT/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/jni.h
echo "==> 提取 sysroot → $SYSROOT"
rm -rf "$SYSROOT"
mkdir -p "$SYSROOT"
cp -a "$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/sysroot" \
      "$SYSROOT/toolchains/llvm/prebuilt/linux-x86_64/sysroot"

# 2) clang-res：include 符号链接到本机 clang 24 资源目录；builtins 摆成 clang 期望布局
NDK_CLANG_VER=$(basename "$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/"*/)
BUILTINS="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/$NDK_CLANG_VER/lib/linux/aarch64/libclang_rt.builtins.a"
[ -f "$BUILTINS" ] || { echo "错误：未找到 NDK builtins：$BUILTINS" >&2; exit 1; }

echo "==> 构建 clang-res → $CLANG_RES"
rm -rf "$CLANG_RES"
mkdir -p "$CLANG_RES/include" "$CLANG_RES/lib/aarch64-unknown-linux-android$API"
ln -s /usr/local/lib/clang/24/include "$CLANG_RES/include"
cp "$BUILTINS" "$CLANG_RES/lib/aarch64-unknown-linux-android$API/libclang_rt.builtins.a"

rm -rf "$TMP"
echo "完成："
echo "  $SYSROOT  (NDK sysroot)"
echo "  $CLANG_RES  (clang 运行时，NDK clang 版本 $NDK_CLANG_VER → api $API)"
echo "接下来：bash android/scripts/build-so-local.sh Release"
