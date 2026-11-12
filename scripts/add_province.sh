#!/usr/bin/env bash
# add_province.sh —— 新增一个省的离线地图数据（可复现流程，不联网也能用本地 PBF）
#
#   用法：
#     ./scripts/add_province.sh <省名> <本地 .osm.pbf 路径>
#     ./scripts/add_province.sh <省名> <URL>          # 用 curl 下载
#     ./scripts/add_province.sh --check               # 只检查现有覆盖是否自洽
#
# 为什么需要这个壳：sync_maps.sh 已经支持「sources/ 下放一份新 PBF 就自动
# 转图 + 重建全国共用 places_auto.db」，但这一步之前还有三件容易漏的事：
#   1) 文件名必须是 <省名>.osm.pbf，且省名会成为 products/graph/<省名>/；
#   2) 下载来的文件可能是 HTML 错误页 / 截断包，直接跑转换会浪费几小时；
#   3) 转完要确认 disp.idx / lanes.bin / t_* 三件套齐、搜索库确实变大。
# 本脚本把这三件收成一条命令；真正的转换仍交给 sync_maps.sh（单一可信源）。
#
# ★ 2026-11-12：本机只有 8 省 PBF（见 `ls sources/`），没有下载新省数据。
#   因此本次交付的是**流程与不变量门禁**，不是新增数据本身。

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ "${1:-}" = "--check" ]; then
    exec python3 "$ROOT/scripts/check_province_coverage.py"
fi
if [ $# -lt 2 ]; then
    sed -n '2,18p' "${BASH_SOURCE[0]}"
    exit 2
fi
NAME=$1
SRC=$2
DST="$ROOT/sources/$NAME.osm.pbf"

case "$NAME" in
    *[!a-z0-9_-]*) echo "省名只能用小写字母/数字/下划线/连字符: $NAME" >&2; exit 2 ;;
esac
if [ -e "$DST" ]; then
    echo "已存在 $DST —— 想强制重转请删掉旧 PBF 后重试（sync_maps.sh 认指纹）" >&2
    exit 2
fi

if [ -f "$SRC" ]; then
    cp -p "$SRC" "$DST" || exit 1
else
    echo "下载 $SRC → $DST"
    curl -fL --retry 3 --connect-timeout 20 -o "$DST.part" "$SRC" || {
        echo "下载失败（国内直连 Geofabrik 常不通，可先手工下载再用本地路径调用）" >&2
        /bin/rm -f "$DST.part" 2>/dev/null
        exit 1
    }
    mv "$DST.part" "$DST"
fi

# 完整性预检：PBF 的第一个 blob 头必须以 0x0A 开头、长度字段后是 BlobHeader。
# 这里只做「非空 + 非 HTML」的最小判据，真正的解码预检由 sync_maps.sh 的
# skedaddle_pbf_info 负责（--no-probe 才跳过）。
size=$(stat -c %s "$DST" 2>/dev/null || stat -f %z "$DST")
if [ "${size:-0}" -lt 1024 ]; then
    echo "文件只有 ${size:-0} 字节，不像 PBF" >&2; exit 1
fi
if head -c 16 "$DST" | grep -qi '<html\|<!doctype'; then
    echo "下载得到的是 HTML 页面，不是 PBF" >&2; exit 1
fi

echo "PBF 就位     $DST ($size 字节)"
echo "开始转换     sync_maps.sh --only=$NAME（会同时重建全国共用 places_auto.db）"
"$ROOT/scripts/sync_maps.sh" "--only=$NAME" || exit 1
python3 "$ROOT/scripts/check_province_coverage.py" || exit 1
echo "扩省完成     $NAME（--graph=products/graph 会自动带上它；搜索库已重建）"
