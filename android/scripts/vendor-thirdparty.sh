#!/usr/bin/env bash
# vendor-thirdparty.sh — 把 NDK 不自带的 sqlite3 + protobuf-c 运行时源码拉进
# android/app/src/main/cpp/thirdparty/。
#
# 这些源码被 android/.gitignore 排除，不进 git；本脚本让其他构建机或新克隆
# 后能一键补齐（否则 cmake 只报 WARNING，链接阶段才因 sqlite3_*/protobuf_c_*
# 未定义而失败）。
#
# 用法：bash android/scripts/vendor-thirdparty.sh
# 可用环境变量覆盖：
#   SK_SQLITE_VER   amalgamation 版本号（默认 3430100 = 3.43.1）
#   SK_SQLITE_ZIP   本地已下载的 amalgamation zip 路径（跳过下载）
#   SK_SQLITE_URL   下载地址（默认 sqlite.org）
#   SK_PROTOBUFC_REPO  protobuf-c 仓库（默认 github，直连不通自动走 ghfast 镜像）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TP="$ROOT/android/app/src/main/cpp/thirdparty"
mkdir -p "$TP/sqlite3" "$TP/protobuf-c"

# --- sqlite3 amalgamation ---
SQLITE_VER="${SK_SQLITE_VER:-3430100}"
if [ -f "$TP/sqlite3/sqlite3.c" ]; then
  echo "sqlite3 已存在，跳过"
else
  ZIP="${SK_SQLITE_ZIP:-/tmp/sqlite-amalgamation-$SQLITE_VER.zip}"
  URL="${SK_SQLITE_URL:-https://www.sqlite.org/2023/sqlite-amalgamation-$SQLITE_VER.zip}"
  if [ ! -f "$ZIP" ]; then
    echo "==> 下载 sqlite amalgamation: $URL"
    curl -fL "$URL" -o "$ZIP"
  fi
  TMP=$(mktemp -d)
  unzip -o "$ZIP" -d "$TMP" >/dev/null
  cp "$TMP"/*/sqlite3.c "$TMP"/*/sqlite3.h "$TP/sqlite3/"
  rm -rf "$TMP"
  echo "sqlite3 → $TP/sqlite3/"
fi

# --- protobuf-c 运行时（仅 protobuf-c.c + protobuf-c.h）---
if [ -f "$TP/protobuf-c/protobuf-c.c" ]; then
  echo "protobuf-c 已存在，跳过"
else
  REPO="${SK_PROTOBUFC_REPO:-https://github.com/protobuf-c/protobuf-c.git}"
  TMP=$(mktemp -d)
  echo "==> 克隆 protobuf-c（直连不通自动走 ghfast 镜像）"
  git clone --depth 1 "$REPO" "$TMP/pbc" 2>/dev/null \
    || git clone --depth 1 "https://ghfast.top/$REPO" "$TMP/pbc"
  cp "$TMP/pbc/protobuf-c/protobuf-c.c" "$TMP/pbc/protobuf-c/protobuf-c.h" "$TP/protobuf-c/"
  rm -rf "$TMP"
  echo "protobuf-c → $TP/protobuf-c/"
fi

echo "完成：thirdparty 已就绪（被 .gitignore 排除，不进 git）。"
