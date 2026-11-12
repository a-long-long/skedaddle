#!/usr/bin/env python3
"""NMBIN M2 / O3：4 KiB 块哈希 sidecar（chunks.json）生成与离线校验。

设计边界（与 NMBIN-OPTIMIZATION-PLAN-2026-11-12.md §2 O3 对齐）：
  · 块表是**校验清单 + 差分索引**，不是把 NMBIN 变成分块格式；运行时读取路径
    一个字节都不改，加载期也不读它（否则会破坏按需 mmap）。
  · 表内存的是「每个数据文件」的全文件 sha256、逐 4 KiB 块 sha256、大小；
    对 NMBIN 文件额外记 record_sha256 与头内 O2 摘要，形成 O2 ↔ O3 闭环。
  · ★ 口径纠偏：O2 头里的摘要是 sha256(**记录区**，从 @512 起 payload_bytes)，
    不是全文件 sha256。所以 manifest 的 zip 整包哈希、块表的文件全哈希、
    O2 的记录区摘要是三个不同口径，不能互相代替；这里逐项分开记并在校验时
    交叉核对「头内 digest == 记录区重算值」。

用法：
  python3 scripts/gen_chunks.py <省目录> [<省目录>...]   # 生成/覆盖 chunks.json
  python3 scripts/gen_chunks.py --verify <省目录>...     # 离线全量重算校验
  python3 scripts/gen_chunks.py --all                   # products/graph/*/ 全部生成
  python3 scripts/gen_chunks.py --self-test             # 合成夹具（正向 + 负向）
  python3 scripts/gen_chunks.py --inject=all            # 只报负向注入标记（regress 用）

判据标记：CHUNKS-OK / CHUNKS-VERIFY-OK / CHUNKS-INJECT-CAUGHT /
          CHUNKS-SELFTEST-OK；任何不一致 rc=1（判成功看退出码）。
"""

import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

VERSION = 1
CHUNK_SIZE = 4096
CHUNKS_NAME = "chunks.json"
ALGO = "sha256"

# NMBIN 头是手工序列化（binfmt.h），这些常量必须与 C 侧逐字一致：
#   magic(8)@0  flags(4)@32  payload_bytes(8)@36  digest(32)@76
#   记录区起点 = 128B 头 + 24B × 16 个字段槽 = 512
NMBIN_MAGIC = b"NAVMBIN\x01"
HDR_BYTES = 128
FIELD_BYTES = 24
MAX_FIELDS = 16
DATA_OFF = HDR_BYTES + FIELD_BYTES * MAX_FIELDS
FLAG_DIGEST = 0x1
OFF_FLAGS = 32
OFF_PAYLOAD = 36
OFF_DIGEST = 76

# 元数据 / 日志不属于地图数据，不进块表（生成端与「块表齐全」门禁共用这份名单）。
SKIP_NAMES = {CHUNKS_NAME, "manifest.txt", "extract.log", ".skversion"}


def data_files(d):
    """省目录里需要进块表的常规文件（排序后返回相对文件名）。"""
    out = []
    for n in sorted(os.listdir(d)):
        p = os.path.join(d, n)
        if not os.path.isfile(p):
            continue
        if n in SKIP_NAMES or n.endswith(".log") or n.startswith("failed-"):
            continue
        out.append(n)
    return out


def expected_chunk_count(size):
    return (size + CHUNK_SIZE - 1) // CHUNK_SIZE


def _sha(b):
    return hashlib.sha256(b).hexdigest()


def nmbin_info(path):
    """(头内摘要 hex 或 '', 记录区起点, 记录区长度)；非 NMBIN 返回 ('', None, None)。"""
    with open(path, "rb") as f:
        h = f.read(HDR_BYTES)
    if len(h) < HDR_BYTES or h[:8] != NMBIN_MAGIC:
        return "", None, None
    flags = struct.unpack_from("<I", h, OFF_FLAGS)[0]
    payload = struct.unpack_from("<Q", h, OFF_PAYLOAD)[0]
    dig = h[OFF_DIGEST:OFF_DIGEST + 32].hex() if (flags & FLAG_DIGEST) else ""
    return dig, DATA_OFF, payload


def record_sha256(path, off, length):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        f.seek(off)
        left = length
        while left:
            b = f.read(min(1 << 20, left))
            if not b:
                break
            h.update(b)
            left -= len(b)
    return h.hexdigest()


def build_entry(path):
    size = os.path.getsize(path)
    whole = hashlib.sha256()
    chunks = []
    with open(path, "rb") as f:
        while True:
            b = f.read(CHUNK_SIZE)
            if not b:
                break
            whole.update(b)
            chunks.append(_sha(b))
    e = {"size": size, "sha256": whole.hexdigest(), "chunks": chunks}
    dig, off, ln = nmbin_info(path)
    if off is not None:
        rec = record_sha256(path, off, ln)
        e["record_sha256"] = rec
        e["nmbin_digest"] = dig
        # 生成端就拦下「头里写了摘要、记录区却对不上」的产物（O2 ↔ O3 闭环）。
        if dig and dig != rec:
            raise ValueError("%s 头内 O2 摘要与记录区不符" % os.path.basename(path))
    return e


def build_table(d):
    return {
        "version": VERSION,
        "chunk_size": CHUNK_SIZE,
        "algo": ALGO,
        "files": {n: build_entry(os.path.join(d, n)) for n in data_files(d)},
    }


def write_chunks(d, table=None):
    t = table if table is not None else build_table(d)
    # core.autocrlf=true：按字节写，避免 Windows 上把 JSON 变成 CRLF。
    raw = json.dumps(t, separators=(",", ":"), sort_keys=True, ensure_ascii=False)
    with open(os.path.join(d, CHUNKS_NAME), "wb") as f:
        f.write(raw.encode("utf-8"))
    return t


def load_table(d):
    p = os.path.join(d, CHUNKS_NAME)
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)


def verify(d, table=None):
    """离线全量重算；返回失败原因列表（空 = 通过）。负向注入改的是被检查的数据。"""
    if table is None:
        if not os.path.isfile(os.path.join(d, CHUNKS_NAME)):
            return ["缺 %s" % CHUNKS_NAME]
        try:
            table = load_table(d)
        except Exception as e:
            return ["块表打不开: %s" % e]
    fails = []
    if not isinstance(table, dict):
        return ["块表不是对象"]
    if table.get("version") != VERSION:
        fails.append("version 应为 %d，实际 %r" % (VERSION, table.get("version")))
    if table.get("chunk_size") != CHUNK_SIZE:
        fails.append("chunk_size 应为 %d，实际 %r" % (CHUNK_SIZE, table.get("chunk_size")))
    if table.get("algo") != ALGO:
        fails.append("algo 应为 sha256，实际 %r" % table.get("algo"))
    files = table.get("files")
    if not isinstance(files, dict):
        return fails + ["files 不是对象"]
    actual = data_files(d)
    listed = set(files)
    for n in sorted(set(actual) - listed):
        fails.append("块表缺文件 %s" % n)
    for n in sorted(listed - set(actual)):
        fails.append("块表多出文件 %s" % n)
    if fails:            # 文件集合不一致就早退，避免后面刷屏
        return fails

    for n in actual:
        e = files[n]
        p = os.path.join(d, n)
        if not isinstance(e, dict):
            fails.append("%s 条目不是对象" % n)
            continue
        size = os.path.getsize(p)
        if e.get("size") != size:
            fails.append("%s size 不符（表 %r / 实际 %d）" % (n, e.get("size"), size))
        ch = e.get("chunks")
        want = expected_chunk_count(size)
        if not isinstance(ch, list) or len(ch) != want:
            fails.append("%s 块数不符（表 %s / 应为 %d）" %
                         (n, len(ch) if isinstance(ch, list) else "<非数组>", want))
            continue
        whole = hashlib.sha256()
        bad_at = -1
        with open(p, "rb") as f:
            i = 0
            while True:
                b = f.read(CHUNK_SIZE)
                if not b:
                    break
                whole.update(b)
                if bad_at < 0 and _sha(b) != ch[i]:
                    bad_at = i
                i += 1
        if bad_at >= 0:
            fails.append("%s 第 %d 块哈希不符" % (n, bad_at))
        if whole.hexdigest() != e.get("sha256"):
            fails.append("%s 全文件 sha256 不符" % n)
        dig, off, ln = nmbin_info(p)
        if off is not None:
            rec = record_sha256(p, off, ln)
            if rec != e.get("record_sha256"):
                fails.append("%s record_sha256 不符" % n)
            if dig != e.get("nmbin_digest", ""):
                fails.append("%s 头内 nmbin_digest 与文件不符" % n)
            if dig and dig != rec:
                fails.append("%s 头内 O2 摘要与记录区不符" % n)
    return fails


# ---------------------------------------------------------------------------
# 合成夹具：不依赖 8 省真实产物（副机缺 7 省也能跑），负向注入改「被检查的数据」
# ---------------------------------------------------------------------------
def make_fixture(d):
    # ① 合法 sealed NMBIN：128B 头 + 384B 字段表 + 2.5 个块的记录区
    payload = bytes((i * 7 + 13) % 256 for i in range(CHUNK_SIZE * 2 + 137))
    h = bytearray(HDR_BYTES)
    h[:8] = NMBIN_MAGIC
    struct.pack_into("<I", h, 8, 3)          # version
    struct.pack_into("<I", h, 12, 1)         # kind
    struct.pack_into("<I", h, 16, 4)         # rec_size
    struct.pack_into("<Q", h, 20, len(payload) // 4)
    struct.pack_into("<I", h, 28, 0)         # n_fields
    struct.pack_into("<I", h, OFF_FLAGS, FLAG_DIGEST)
    struct.pack_into("<Q", h, OFF_PAYLOAD, len(payload))
    h[OFF_DIGEST:OFF_DIGEST + 32] = hashlib.sha256(payload).digest()
    with open(os.path.join(d, "nodes.bin"), "wb") as f:
        f.write(bytes(h))
        f.write(b"\0" * (FIELD_BYTES * MAX_FIELDS))
        f.write(payload)
    # ② 普通数据文件（2 块，尾块不足 4 KiB）
    with open(os.path.join(d, "disp.idx"), "wb") as f:
        f.write(bytes((i * 3) % 256 for i in range(CHUNK_SIZE + 91)))
    # ③ 瓦片文件（1 块）
    with open(os.path.join(d, "t_0_9.areas"), "wb") as f:
        f.write(b"tile-data" * 12)
    # ④ 元数据，必须被跳过
    with open(os.path.join(d, "manifest.txt"), "wb") as f:
        f.write(b"nodes 1\nedges 2\n")
    with open(os.path.join(d, "extract.log"), "wb") as f:
        f.write(b"log\n")


def _injections(d, clean):
    """返回 (名字, 被破坏的表/文件, 期望被抓到的原因关键字)。"""
    cases = []

    def copy():
        return json.loads(json.dumps(clean))

    t = copy()
    t["files"]["nodes.bin"]["chunks"][1] = "0" * 64
    cases.append(("块哈希被改", t, "块哈希不符"))

    t = copy()
    t["files"]["disp.idx"]["chunks"].pop()
    cases.append(("删一个块条目", t, "块数不符"))

    t = copy()
    t["files"]["nodes.bin"]["sha256"] = "1" * 64
    cases.append(("整包哈希被换", t, "全文件 sha256 不符"))

    t = copy()
    t["files"]["nodes.bin"]["nmbin_digest"] = "2" * 64
    cases.append(("头内 O2 摘要被换", t, "nmbin_digest"))

    # 直接改被检查的文件字节（不是改表）：干净表 + 脏文件
    cases.append(("文件字节被翻转", None, "块哈希不符"))
    return cases


def selftest(report_positive=True):
    tmp = tempfile.mkdtemp(prefix="sk_chunks_")
    try:
        make_fixture(tmp)
        clean = build_table(tmp)
        if verify(tmp, clean):
            print("CHUNKS-SELFTEST-FAIL 干净夹具就不该 FAIL: %s" % verify(tmp, clean))
            return 1
        write_chunks(tmp, clean)
        if verify(tmp):
            print("CHUNKS-SELFTEST-FAIL 写盘后重载校验失败")
            return 1
        n_files = len(clean["files"])
        n_chunks = sum(len(e["chunks"]) for e in clean["files"].values())
        if report_positive:
            print("CHUNKS-OK 合成夹具 %d 文件 / %d 块（含 sealed NMBIN 的 O2 摘要闭环）" %
                  (n_files, n_chunks))
            print("CHUNKS-SELFTEST-OK 生成 → 写盘 → 全量重算逐项一致")

        # 负向注入：每条都必须让 verify() FAIL，且原因对得上
        caught = 0
        for name, dirty, why in _injections(tmp, clean):
            work = tempfile.mkdtemp(prefix="sk_chunks_neg_")
            try:
                for n in os.listdir(tmp):
                    src = os.path.join(tmp, n)
                    if os.path.isfile(src):
                        shutil.copyfile(src, os.path.join(work, n))
                if dirty is None:
                    # 改「被检查的数据」：翻转 nodes.bin 记录区中间一个字节
                    p = os.path.join(work, "nodes.bin")
                    with open(p, "r+b") as f:
                        f.seek(DATA_OFF + CHUNK_SIZE + 5)
                        b = f.read(1)
                        f.seek(DATA_OFF + CHUNK_SIZE + 5)
                        f.write(bytes([b[0] ^ 0xAA]))
                    bad = verify(work)                 # 用干净表 + 脏文件
                else:
                    write_chunks(work, dirty)
                    bad = verify(work)
                if not bad:
                    print("CHUNKS-SELFTEST-FAIL 注入「%s」没被抓到" % name)
                    return 1
                if not any(why in x for x in bad):
                    print("CHUNKS-SELFTEST-FAIL 注入「%s」抓到了但原因不对: %s" % (name, bad))
                    return 1
                caught += 1
            finally:
                shutil.rmtree(work, ignore_errors=True)
        print("CHUNKS-INJECT-CAUGHT %d 种破坏全部被拒（块哈希/块条目/整包哈希/O2 摘要/文件字节）" %
              caught)
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv):
    args = argv[1:]
    if "--self-test" in args:
        return selftest(report_positive=True)
    if "--inject=all" in args:
        return selftest(report_positive=False)

    if "--all" in args:
        g = os.path.join(ROOT, "products", "graph")
        dirs = [os.path.join(g, n) for n in sorted(os.listdir(g))
                if os.path.isdir(os.path.join(g, n))]
    else:
        do_verify = "--verify" in args
        dirs = [a for a in args if not a.startswith("--")]
        if not dirs:
            print(__doc__)
            return 2
        for d in dirs:
            if not os.path.isdir(d):
                print("CHUNKS-FAIL 不是目录: %s" % d)
                return 1
        if do_verify:
            bad = 0
            for d in dirs:
                f = verify(d)
                if f:
                    bad = 1
                    for x in f:
                        print("MISMATCH %s: %s" % (d, x))
                else:
                    t = load_table(d)
                    n_chunks = sum(len(e["chunks"]) for e in t["files"].values())
                    print("%s %d 文件 / %d 块" % (os.path.basename(d),
                                                 len(t["files"]), n_chunks))
            if bad:
                print("CHUNKS-FAIL 至少一份块表与产物不一致")
                return 1
            print("CHUNKS-VERIFY-OK %d 个省目录逐块重算一致" % len(dirs))
            return 0

    total_files = 0
    total_chunks = 0
    for d in dirs:
        try:
            t = write_chunks(d)
        except Exception as e:
            print("CHUNKS-FAIL %s: %s" % (d, e))
            return 1
        nf = len(t["files"])
        nc = sum(len(e["chunks"]) for e in t["files"].values())
        total_files += nf
        total_chunks += nc
        print("%s %d 文件 / %d 块" % (os.path.basename(d), nf, nc))
    print("CHUNKS-OK %d 个省目录，%d 文件 / %d 块（4 KiB / sha256）" %
          (len(dirs), total_files, total_chunks))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
