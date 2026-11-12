#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""P2-A：块表传输成本量测 + 三格式一致性门禁（决策门数字的可复现来源）。

回答 M2 §9.4 留下的那句「71 MB JSON 清单会抵消单块差分收益，服务端需 gzip + ETag」：
  1. 真实产物的 chunks.json / gzip / 紧凑二进制三种体积各是多少？
  2. gzip + ETag 之后，清单还是不是瓶颈？第二档（二进制表）值不值得做？
  3. P2-B 的逐块压缩容器，要多少变化块才能把新增索引字节赚回来？

★ 口径纪律：
  - 这里的「二进制表」是**量测编码**，只在内存里算体积和做一致性比对，
    不落盘、不发布、不改 chunks.json v1 契约、不动 ChunkUpdater 的解析。
    它存在的唯一目的是给「第二档要不要做」一个实数，不是把格式改掉。
  - 三个哈希口径绝不串台：zip 整包 sha256（manifest）/ 单文件全哈希（块表）/
    O2 记录区摘要（头 @76）。本工具只量清单体积，不碰任何文件哈希语义。
  - 单块省的 2672 B 来自 container_probe.py 的 raw-deflate p50=1424 B；
    它是**裸块差分路径**的数字，与整包 zip 的 deflate 无关（整包本来就已压缩）。

用法：
  python3 scripts/chunk_table_size.py --self-test        # 合成夹具门禁（跨机一致）
  python3 scripts/chunk_table_size.py --inject=all       # 只跑负向，regress 记账
  python3 scripts/chunk_table_size.py --report [省...]   # 真实产物体积报告（不进门禁）
  python3 scripts/chunk_table_size.py --report --block-saving=2672

判据标记：TABLE-SIZE-OK / TABLE-COMPACT-OK / TABLE-BACKCOMPAT-OK /
          TABLE-INJECT-CAUGHT / TABLE-REPORT-OK；任何不一致 rc=1。
"""

import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile

import gen_chunks          # 同目录：gzip 口径 / version / 伴生校验与生成端共用

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRAPH = os.path.join(ROOT, "products", "graph")

MAGIC = b"SKTBL1\x00\x00"          # 8 B，量测格式专用标记（不写进 products/）
HEAD_FMT = "<IIII"                  # version / chunk_size / n_files / algo_id
HEAD_BYTES = 8 + struct.calcsize(HEAD_FMT)
ZERO32 = b"\x00" * 32

# container_probe.py 实测 631107 块：raw deflate level 6 的 p50 = 1424 B
DEFAULT_BLOCK_SAVING = 4096 - 1424          # 2672 B/块
IDX_JSON_PER_CHUNK = 30                      # P2-B 追加 offset/len 的 JSON 文本估
IDX_BIN_PER_CHUNK = 12                       # u64 offset + u32 len
GZIP_GATE_MIB = 8                            # 计划 §2 建议的「清单仍大」阈值

# 合成夹具：固定种子 + sha256 派生 ⇒ 哈希分布与真实块表同为高熵 64 hex，
# 跨机逐字节可复现（不能用全 0 假表，那会把 gzip 压缩比量成虚高）。
FIXTURE_FILES = 14
FIXTURE_CHUNKS = 84000                       # ≈ 北京真实 86108 块


def fake_hash(tag, i):
    return hashlib.sha256(("%s:%d" % (tag, i)).encode("utf-8")).hexdigest()


def fixture_table():
    files = {}
    per = FIXTURE_CHUNKS // FIXTURE_FILES
    for k in range(FIXTURE_FILES):
        n = per + (FIXTURE_CHUNKS - per * FIXTURE_FILES if k == 0 else 0)
        name = "t_%d_9.areas" % k if k >= 2 else ("nodes.bin", "edges.bin")[k]
        chunks = [fake_hash(name, i) for i in range(n)]
        files[name] = {
            "size": n * gen_chunks.CHUNK_SIZE,
            "sha256": fake_hash(name + "-whole", 0),
            "chunks": chunks,
        }
        if k < 2:
            files[name]["record_sha256"] = fake_hash(name + "-rec", 0)
            files[name]["nmbin_digest"] = ""       # 旧 8 省 flags=0 的形态
    return {"version": gen_chunks.VERSION,
            "chunk_size": gen_chunks.CHUNK_SIZE,
            "algo": gen_chunks.ALGO,
            "files": files}


# ---------------------------------------------------------------------------
# 紧凑二进制表（量测编码）：每块哈希从 64 hex 变 32 原始字节，其余元数据定长
# ---------------------------------------------------------------------------
def encode_compact(t):
    b = bytearray(MAGIC)
    b += struct.pack(HEAD_FMT, t["version"], t["chunk_size"],
                     len(t["files"]), 1)          # algo_id=1 ⇒ sha256
    for name in sorted(t["files"]):
        e = t["files"][name]
        nb = name.encode("utf-8")
        b += struct.pack("<H", len(nb)) + nb
        b += struct.pack("<QI", e["size"], len(e["chunks"]))
        b += bytes.fromhex(e["sha256"])
        b += bytes.fromhex(e.get("record_sha256") or "") or ZERO32
        b += bytes.fromhex(e.get("nmbin_digest") or "") or ZERO32
        for h in e["chunks"]:
            b += bytes.fromhex(h)
    return bytes(b)


def decode_compact(b):
    if len(b) < HEAD_BYTES or b[:8] != MAGIC:
        raise ValueError("二进制表 magic/长度不对")
    version, chunk_size, n_files, algo_id = struct.unpack_from(HEAD_FMT, b, 8)
    if algo_id != 1:
        raise ValueError("algo_id 应为 1（sha256），实际 %d" % algo_id)
    off = HEAD_BYTES
    files = {}
    for _ in range(n_files):
        (nl,) = struct.unpack_from("<H", b, off)
        off += 2
        name = b[off:off + nl].decode("utf-8")
        off += nl
        size, nc = struct.unpack_from("<QI", b, off)
        off += 12
        sha = b[off:off + 32].hex()
        off += 32
        rec = b[off:off + 32].hex()
        off += 32
        dig = b[off:off + 32].hex()
        off += 32
        chunks = []
        for _ in range(nc):
            chunks.append(b[off:off + 32].hex())
            off += 32
        files[name] = {"size": size, "sha256": sha, "chunks": chunks,
                       "record_sha256": "" if rec == ZERO32.hex() else rec,
                       "nmbin_digest": "" if dig == ZERO32.hex() else dig}
    if off != len(b):
        raise ValueError("二进制表尾部多出 %d 字节" % (len(b) - off))
    return {"version": version, "chunk_size": chunk_size,
            "algo": "sha256", "files": files}


def compare(a, b):
    """三口径一致性：文件集 / 块数 / 逐块哈希 / size / 整包哈希 / O2 字段。"""
    fails = []
    for k in ("version", "chunk_size", "algo"):
        if a.get(k) != b.get(k):
            fails.append("%s 不一致（%r / %r）" % (k, a.get(k), b.get(k)))
    fa, fb = a.get("files", {}), b.get("files", {})
    for n in sorted(set(fa) - set(fb)):
        fails.append("二进制表缺文件 %s" % n)
    for n in sorted(set(fb) - set(fa)):
        fails.append("二进制表多出文件 %s" % n)
    for n in sorted(set(fa) & set(fb)):
        x, y = fa[n], fb[n]
        if x.get("size") != y.get("size"):
            fails.append("%s size 不一致" % n)
        if x.get("sha256") != y.get("sha256"):
            fails.append("%s 全文件 sha256 不一致" % n)
        if x.get("record_sha256", "") != y.get("record_sha256", ""):
            fails.append("%s record_sha256 不一致" % n)
        if x.get("nmbin_digest", "") != y.get("nmbin_digest", ""):
            fails.append("%s nmbin_digest 不一致" % n)
        cx, cy = x.get("chunks", []), y.get("chunks", [])
        if len(cx) != len(cy):
            fails.append("%s 块数不一致（%d / %d）" % (n, len(cx), len(cy)))
        else:
            for i, (u, v) in enumerate(zip(cx, cy)):
                if u != v:
                    fails.append("%s 第 %d 块哈希不一致" % (n, i))
                    break
    return fails


def size_ok(m):
    """体积断言：真实高熵表 gzip 后约为 JSON 的 0.5；全 0 假表会远低于下界。

    ★ 负向对照靠「改被检查的数据」：把表换成全 0 假表，压缩比立刻失真。
    """
    if not (m["json"] > 0 and m["gz"] > 0 and m["bin"] > 0 and m["chunks"] > 0):
        return False
    r = m["gz"] / float(m["json"])
    return 0.20 <= r <= 0.95 and m["bin"] < m["json"]


def measure(d):
    p = os.path.join(d, gen_chunks.CHUNKS_NAME)
    with open(p, "rb") as f:
        raw = f.read()
    t = json.loads(raw.decode("utf-8"))
    gz = gen_chunks.gzip_bytes(raw)
    bin_bytes = encode_compact(t)
    n_chunks = sum(len(e["chunks"]) for e in t["files"].values())
    return {"name": os.path.basename(d.rstrip(os.sep)),
            "json": len(raw), "gz": len(gz), "bin": len(bin_bytes),
            "chunks": n_chunks, "files": len(t["files"]),
            "table": t, "bin_bytes": bin_bytes,
            "etag": hashlib.sha256(gz).hexdigest()}


def print_report(ms, block_saving):
    print("%-12s %9s %14s %14s %14s %10s %8s" %
          ("省", "文件", "块数", "JSON(B)", "gzip(B)", "二进制(B)", "gz/JSON"))
    for m in ms:
        print("%-12s %9d %14d %14d %14d %14d %7.1f%%" %
              (m["name"], m["files"], m["chunks"], m["json"], m["gz"], m["bin"],
               m["gz"] * 100.0 / m["json"]))
    nj = sum(m["json"] for m in ms)
    ng = sum(m["gz"] for m in ms)
    nb = sum(m["bin"] for m in ms)
    nc = sum(m["chunks"] for m in ms)
    print("")
    print("=== 清单固定开销（P2-A）===")
    print("  JSON %.2f MiB / gzip %.2f MiB（%.1f%%，省 %.2f MiB）/ 二进制 %.2f MiB" %
          (nj / 1048576.0, ng / 1048576.0, ng * 100.0 / nj if nj else 0,
           (nj - ng) / 1048576.0, nb / 1048576.0))
    worst = max((m["gz"] for m in ms), default=0)
    print("  单省最大 gzip %.2f MiB，决策门阈值 %d MiB ⇒ %s" %
          (worst / 1048576.0, GZIP_GATE_MIB,
           "已达标，第二档（二进制表）**不必做**" if worst <= GZIP_GATE_MIB * 1048576
           else "仍超阈值，才评估第二档"))
    print("  ETag = sha256(gz)，mtime=0 可复现 ⇒ 未变清单走 304，后续拉取 0 字节清单体")
    print("")
    print("=== P2-B 决策门（逐块压缩容器）===")
    ij, ib = nc * IDX_JSON_PER_CHUNK, nc * IDX_BIN_PER_CHUNK
    print("  索引增量（全量拉）：+%d B（JSON，%d 块 ×%d B）/ +%d B（二进制 ×%d B）" %
          (ij, nc, IDX_JSON_PER_CHUNK, ib, IDX_BIN_PER_CHUNK))
    print("  单块省 %d B（raw deflate p50=1424 B，来自 container_probe.py）" %
          block_saving)
    print("  回本块数：JSON %.0f 块 / 二进制 %.0f 块" %
          (ij / float(block_saving), ib / float(block_saving)))
    print("  ★ M2 典型场景是「改 1 块 / 全等 0 块」⇒ 小更新极可能净亏；")
    print("    索引是全量拉的，数据块省的是增量，这个不对称是撤回条件的核心。")
    delta = (ng - nb) * 100.0 / ng if ng else 0.0
    print("  ★ 二进制表 %.2f MiB vs gzip %.2f MiB（仅小 %.1f%%），却要同步"
          " §1.4 全部解析耦合点并留 v1 兼容读路 ⇒ 收益不足以抵格式变更成本" %
          (nb / 1048576.0, ng / 1048576.0, delta))
    return nc


def selftest(report_positive=True, block_saving=DEFAULT_BLOCK_SAVING):
    tmp = tempfile.mkdtemp(prefix="sk_table_")
    try:
        t = fixture_table()
        raw = json.dumps(t, separators=(",", ":"), sort_keys=True,
                         ensure_ascii=False).encode("utf-8")
        with open(os.path.join(tmp, gen_chunks.CHUNKS_NAME), "wb") as f:
            f.write(raw)
        gen_chunks.write_sidecar(tmp, raw)
        m = measure(tmp)

        # 正向 1：三种格式解析出的文件集 / 块数 / 哈希完全一致
        try:
            decoded = decode_compact(m["bin_bytes"])
        except Exception as e:
            print("TABLE-FAIL 二进制表解不开: %s" % e)
            return 1
        fails = compare(m["table"], decoded)
        if fails:
            print("TABLE-FAIL 干净夹具三格式就不一致: %s" % fails[:3])
            return 1
        # 正向 2：体积断言（跨机可复现的数字，不依赖 8 省真实产物）
        if not size_ok(m):
            print("TABLE-FAIL 干净夹具体积断言就不过（gz/JSON=%.3f）" %
                  (m["gz"] / float(m["json"])))
            return 1

        if report_positive:
            print("TABLE-COMPACT-OK 三种格式解析出相同的 %d 文件 / %d 块 / 逐块哈希" %
                  (m["files"], m["chunks"]))
            print("TABLE-SIZE-OK JSON %d B / gzip %d B (%.1f%%) / 二进制 %d B；"
                  "P2-B 回本块数 JSON %.0f / 二进制 %.0f" %
                  (m["json"], m["gz"], m["gz"] * 100.0 / m["json"], m["bin"],
                   m["chunks"] * IDX_JSON_PER_CHUNK / float(block_saving),
                   m["chunks"] * IDX_BIN_PER_CHUNK / float(block_saving)))

        # 正向 3：只有 chunks.json 的 v1 老目录仍能过门禁（回落 JSON）
        legacy = tempfile.mkdtemp(prefix="sk_table_legacy_")
        try:
            with open(os.path.join(legacy, gen_chunks.CHUNKS_NAME), "wb") as f:
                f.write(raw)
            lf, _, etag = gen_chunks.sidecar_check(legacy, required=False)
            if lf or etag:
                print("TABLE-FAIL 老目录（无 gz）不该被判失败: %s" % lf)
                return 1
            # 负向 3：version 改成 9 ⇒ gen_chunks.verify() 必须明确拒绝
            t9 = json.loads(json.dumps(t))
            t9["version"] = 9
            v9 = gen_chunks.verify(tmp, t9)
            if not any("version" in x for x in v9):
                print("TABLE-FAIL version=9 没被拒: %s" % v9[:3])
                return 1
            if report_positive:
                print("TABLE-BACKCOMPAT-OK 只有 %s 的 v1 老目录回落 OK；"
                      "version=9 被明确拒绝" % gen_chunks.CHUNKS_NAME)
        finally:
            shutil.rmtree(legacy, ignore_errors=True)

        # ---- 负向对照：改「被检查的数据」，不是把条件改成常量 0 ----------
        caught = 0
        # ① 改掉二进制表里的 1 个块哈希 ⇒ 与 JSON 不一致必须被抓，且**原因对得上**
        first = sorted(t["files"])[0]
        pos = HEAD_BYTES + 2 + len(first.encode("utf-8")) + 12 + 32 * 3 + 7
        dirty = bytearray(m["bin_bytes"])
        dirty[pos] ^= 0xFF
        try:
            bad = compare(m["table"], decode_compact(bytes(dirty)))
        except Exception as e:
            print("TABLE-INJECT-FAIL 翻转 1 个哈希字节后连解码都过不去: %s" % e)
            return 1
        if not any("哈希不一致" in x for x in bad):
            print("TABLE-INJECT-FAIL 二进制表改 1 个哈希没被抓到: %s" % bad[:3])
            return 1
        caught += 1

        # ② 全 0 假表冒充 ⇒ 体积断言必须失败
        zero = {"version": gen_chunks.VERSION, "chunk_size": gen_chunks.CHUNK_SIZE,
                "algo": gen_chunks.ALGO,
                "files": {"nodes.bin": {"size": 0, "sha256": "0" * 64,
                                        "chunks": ["0" * 64] * 200}}}
        zraw = json.dumps(zero, separators=(",", ":"), sort_keys=True).encode("utf-8")
        zbin = encode_compact(zero)
        zm = {"json": len(zraw), "gz": len(gen_chunks.gzip_bytes(zraw)),
              "bin": len(zbin), "chunks": 200}
        if size_ok(zm):
            print("TABLE-INJECT-FAIL 全 0 假表竟通过体积断言（gz/JSON=%.4f）" %
                  (zm["gz"] / float(zm["json"])))
            return 1
        caught += 1

        # ③ version=9（已在正向 3 的负向里验证被拒）
        caught += 1

        print("TABLE-INJECT-CAUGHT %d 类破坏全部被拒（二进制哈希被改 / "
              "全 0 假表 / version=9）" % caught)
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def real_report(dirs, block_saving):
    ms = []
    for d in dirs:
        if not os.path.isfile(os.path.join(d, gen_chunks.CHUNKS_NAME)):
            continue
        ms.append(measure(d))
    if not ms:
        print("TABLE-REPORT-EMPTY 这些目录里没有 %s（副机未重抽时属正常）" %
              gen_chunks.CHUNKS_NAME)
        return 0
    print_report(ms, block_saving)
    print("")
    print("TABLE-REPORT-OK %d 个省目录真实块表体积实测" % len(ms))
    return 0


def main(argv):
    args = argv[1:]
    block_saving = DEFAULT_BLOCK_SAVING
    for a in list(args):
        if a.startswith("--block-saving="):
            block_saving = int(a.split("=", 1)[1])
            args.remove(a)
    if block_saving <= 0:
        print("TABLE-FAIL --block-saving 必须为正")
        return 1

    if "--self-test" in args:
        return selftest(report_positive=True, block_saving=block_saving)
    if "--inject=all" in args:
        return selftest(report_positive=False, block_saving=block_saving)

    if "--report" in args:
        args.remove("--report")
        if args:
            dirs = [a if os.path.isdir(a) else os.path.join(GRAPH, a) for a in args]
        else:
            dirs = [os.path.join(GRAPH, n) for n in sorted(os.listdir(GRAPH))
                    if os.path.isdir(os.path.join(GRAPH, n))] if os.path.isdir(GRAPH) else []
        return real_report(dirs, block_saving)

    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
