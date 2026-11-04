#!/usr/bin/env bash
# build-so-local.sh — 在 aarch64 Linux 宿主机上构建 libskedaddle.so
#
# 背景：本机是 ARM64 Linux，官方 NDK 只发 x86_64 宿主二进制，其 clang/ninja 无法运行。
# 方案：只使用 NDK 包中架构无关的 sysroot（Bionic 头文件/桩库/libc++ 头），
#       编译与链接全部使用本机原生 LLVM/clang 24（/usr/local/bin）。
#       protoc-c 代码生成由 cpp/CMakeLists.txt 内部调用宿主原生 protoc-c 完成。
# 产出：android/app/src/main/jniLibs/arm64-v8a/libskedaddle.so（供 Gradle 打包）
#
# 用法：bash android/scripts/build-so-local.sh [Release|Debug]
# 依赖：~/tools/android/ndk-sysroot（由 setup-local-toolchain.sh 从 NDK zip 提取）

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"     # skedaddle 仓库根

# 自动带上 JDK/protoc-c/ninja 等宿主环境（幂等，重复 source 无副作用）
# shellcheck source=env-android.sh
source "$ROOT/android/scripts/env-android.sh"

BUILD_TYPE="${1:-Release}"
BUILD_DIR="$ROOT/build/android-$BUILD_TYPE"
OUT_DIR="$ROOT/android/app/src/main/jniLibs/arm64-v8a"

export SK_NDK_SYSROOT="${SK_NDK_SYSROOT:-$HOME/tools/android/ndk-sysroot/toolchains/llvm/prebuilt/linux-x86_64/sysroot}"

[ -f "$SK_NDK_SYSROOT/usr/include/jni.h" ] || {
  echo "错误：未找到 NDK sysroot（$SK_NDK_SYSROOT）。请先运行 setup-local-toolchain.sh" >&2
  exit 1
}
command -v protoc-c >/dev/null || [ -x "$HOME/.local/usr/bin/protoc-c" ] || {
  echo "错误：宿主 protoc-c 未找到（source env.sh 或安装 protobuf-c）" >&2
  exit 1
}
[ -x "$HOME/.local/usr/bin/protoc-c" ] && export PATH="$HOME/.local/usr/bin:$HOME/.local/bin:$PATH"
# protoc-c 的运行库 libprotoc.so.17 在 ~/.local/usr/lib，需显式告知动态链接器
export LD_LIBRARY_PATH="$HOME/.local/usr/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

mkdir -p "$BUILD_DIR" "$OUT_DIR"

echo "==> 交叉编译 libskedaddle.so（原生 clang + NDK sysroot, $BUILD_TYPE）"
cd "$BUILD_DIR"
cmake "$ROOT/android/app/src/main/cpp" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/android/scripts/local-clang-android.toolchain.cmake" \
  -DSKEDADDLE_SRC_ROOT="$ROOT" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -G Ninja
ninja -j"$(nproc)"

echo "==> 拷贝产物到 jniLibs"
cp -f "$BUILD_DIR/libskedaddle.so" "$OUT_DIR/libskedaddle.so"

llvm-readelf -d "$OUT_DIR/libskedaddle.so" 2>/dev/null | grep -E 'SONAME|NEEDED' || true
echo "完成：$OUT_DIR/libskedaddle.so"
