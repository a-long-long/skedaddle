#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成一张极小的 OSM PBF，当作「新增一张地图」的验收夹具。
 *
 * 用途：证明 sources/ 里多放一份 PBF，它的 POI 会被 places_build 一起写进
 * place/gram，因而**无需改任何代码**就能被 search_query 直接检索到。
 *
 * 只用手写 protobuf（varint / length-delimited），不依赖 protobuf 库；
 * 字段编号与 proto/fileformat.proto、proto/osmformat.proto 一致。
 * 用非 dense 的 Node 消息（tools/places_build.c:979 那条分支支持）。
"""
import struct
import sys
import zlib


def vi(n):
    assert n >= 0, n
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def zz(n):
    return 2 * n if n >= 0 else -2 * n - 1


def f_vi(field, n):
    return vi((field << 3) | 0) + vi(n)


def f_si(field, n):
    return vi((field << 3) | 0) + vi(zz(n))


def f_by(field, data):
    return vi((field << 3) | 2) + vi(len(data)) + data


def blob_msg(payload):
    # Blob: raw_size=2 (int32), raw=1 (bytes), zlib_data=3 (bytes)
    z = zlib.compress(payload, 9)
    return f_vi(2, len(payload)) + f_by(3, z)


def header_msg(blob_type, size):
    # BlobHeader: type=1 (string, required), datasize=3 (int32, required)
    return f_by(1, blob_type.encode('ascii')) + f_vi(3, size)


def emit_blob(fh, blob_type, payload):
    b = blob_msg(payload)
    h = header_msg(blob_type, len(b))
    fh.write(struct.pack('>I', len(h)))
    fh.write(h)
    fh.write(b)


def stringtable(strings):
    # StringTable: s=1 repeated bytes；索引 0 必须是空串（OSM 约定）
    return b''.join(f_by(1, s.encode('utf-8')) for s in strings)


def node(nid, lat_e7, lon_e7, keys, vals):
    # Node: id=1 sint64, keys=2 packed uint32, vals=3 packed uint32,
    #       lat=8 sint64, lon=9 sint64（granularity=100 ⇒ 数值就是 1e-7 度）
    out = f_si(1, nid)
    out += f_by(2, b''.join(vi(k) for k in keys))
    out += f_by(3, b''.join(vi(v) for v in vals))
    out += f_si(8, lat_e7)
    out += f_si(9, lon_e7)
    return out


def way(wid, keys, vals, refs):
    # Way: id=1 int64, keys=2, vals=3, refs=8 packed sint64（delta 编码）
    # 只放一条无名 highway，class_way_tags 的 skip_keys 会跳过它 ⇒ 不进 place；
    # 但 skedaddle_pbf_info 的完整性预检要求「至少一条 way」，没有它 rc=1。
    out = f_vi(1, wid)
    out += f_by(2, b''.join(vi(k) for k in keys))
    out += f_by(3, b''.join(vi(v) for v in vals))
    out += f_by(8, b''.join(vi(zz(d)) for d in refs))
    return out


def main(path):
    strings = [
        '',                 # 0 必须是空串
        'name',             # 1
        '腾云新图观测站',    # 2
        'amenity',          # 3
        'restaurant',       # 4
        '腾云新图书屋',      # 5
        'library',          # 6
        '腾云新图食堂',      # 7
        'tourism',          # 8
        'viewpoint',        # 9
        'highway',          # 10
        'residential',      # 11
    ]
    nodes = [
        node(900001, 399087000, 1163971000, [1, 3], [2, 4]),
        node(900002, 399123000, 1164022000, [1, 3], [5, 6]),
        node(900003, 399055000, 1163910000, [1, 8], [7, 9]),
    ]
    ways = [
        way(900010, [10], [11], [900001, 1, 1]),
    ]

    hdr = f_by(4, b'OsmSchema-V0.6') + f_by(4, b'DenseNodes') \
        + f_by(16, b'skedaddle-mini-pbf')

    pg = b''.join(f_by(1, nd) for nd in nodes)          # PrimitiveGroup.nodes=1
    pg += b''.join(f_by(3, w) for w in ways)            # PrimitiveGroup.ways=3
    pb = f_by(1, stringtable(strings))                   # PrimitiveBlock.stringtable=1
    pb += f_by(2, pg)                                    # primitivegroup=2
    pb += f_vi(17, 100)                                  # granularity
    pb += f_vi(18, 1000)                                 # date_granularity

    with open(path, 'wb') as fh:
        emit_blob(fh, 'OSMHeader', hdr)
        emit_blob(fh, 'OSMData', pb)
    print('%s 写出 %d 个带名字的 POI 节点' % (path, len(nodes)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'tests/xintu.osm.pbf'))
