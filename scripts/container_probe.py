#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""O6（P2）前置量测探针 —— 只读，不改任何产物/源码/配置。

用途：给「4 KiB 块对齐压缩容器」的决策门提供真实数字，而不是估算。
回答三个问题：
  1. 8 省产物按 4 KiB 独立 deflate 后，压缩比到底多少？有多少块压不动？
  2. 差分路径下，改 1 块从 4096 B 能降到多少？块索引要额外付多少？
  3. 用户态解压吞吐是多少？（只报本机数字，跨平台只迁机制不迁倍数）

★ 口径纪律：
  - M2 差分走的是**裸 .bin 的 HTTP Range**（ChunkDelta.HttpRangeSource），
    不是 zip；整包路径本来就有 deflate（Downloader.extractZip）。
    所以这里量的是「差分路径的裸块」压缩空间，不是「再压一遍 zip」。
  - 每块独立压缩（raw deflate，无 zlib 头），块与块之间无依赖 ⇒ 仍可随机取单块。
  - 帧时间/渲染性能**不在此报告范围内**：运行时仍读逐字节相同的 NMBIN。

用法：
  python3 scripts/container_probe.py                 # 全部省，全量块
  python3 scripts/container_probe.py --sample=256   # 每文件最多采 256 块（快速摸底）
  python3 scripts/container_probe.py --level=9      # 换压缩级别
  python3 scripts/container_probe.py beijing        # 只测指定省目录
"""
import os
import sys
import time
import zlib
import platform

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRAPH = os.path.join(ROOT, "products", "graph")
CHUNK = 4096
# 与 gen_chunks.SKIP_NAMES 同口径：块表及其 P2-A gzip 伴生都是元数据，
# 不进「数据块压缩空间」样本，否则伴生会被当成地图数据再压一遍。
SKIP = {"chunks.json", "chunks.json.gz", "manifest.txt", "extract.log", ".skversion"}


def data_files(d):
    """递归取目录里的常规数据文件。

    ★ 与 gen_chunks.py 的「只扫顶层」不同：本机（Windows 副机）的产物是
      ``products/graph/<省>/beijing/<文件>`` 嵌套结构，顶层为空；只扫顶层
      会把另外 7 省全算成 0 文件。压缩比是内容属性，与目录层级无关，
      所以这里递归，并在报告里注明样本结构。
    """
    out = []
    for cur, dirs, names in os.walk(d):
        dirs[:] = [x for x in dirs if not x.startswith(".")]
        for n in sorted(names):
            if n in SKIP or n.startswith(".") or n.endswith(".log") or n.startswith("failed-"):
                continue
            p = os.path.join(cur, n)
            if os.path.isfile(p):
                out.append(p)
    return out


def compress_block(b, level):
    """raw deflate：每块独立帧，无跨块依赖。"""
    c = zlib.compressobj(level, zlib.DEFLATED, -15)
    return c.compress(b) + c.flush()


def probe_dir(d, sample, level):
    raw_total = cmp_total = zero_total = 0
    blocks = 0
    incompressible = 0
    sizes = []
    comp_sizes = []
    files = 0
    t0 = time.perf_counter()
    decomp_bytes = 0
    t_dec = 0.0

    for p in data_files(d):
        files += 1
        sz = os.path.getsize(p)
        n_all = (sz + CHUNK - 1) // CHUNK
        # 均匀抽样：从头到尾等距取块，避免只看文件头的偏置。
        if sample > 0 and n_all > sample:
            idxs = sorted(set(int(i * (n_all - 1) / (sample - 1)) for i in range(sample)))
        else:
            idxs = range(n_all)
        with open(p, "rb") as f:
            for i in idxs:
                f.seek(i * CHUNK)
                b = f.read(CHUNK)
                if not b:
                    continue
                cb = compress_block(b, level)
                raw_total += len(b)
                cmp_total += len(cb)
                zero_total += b.count(0)
                sizes.append(len(b))
                comp_sizes.append(len(cb))
                blocks += 1
                if len(cb) >= len(b):
                    incompressible += 1
                # 解压计时：只对前 2000 块取样，避免 I/O 主导
                if blocks <= 2000:
                    ts = time.perf_counter()
                    dc = zlib.decompressobj(-15)
                    got = dc.decompress(cb) + dc.flush()
                    t_dec += time.perf_counter() - ts
                    decomp_bytes += len(got)
                    assert got == b, "解压结果与原块不一致"

    return {
        "province": os.path.basename(d.rstrip(os.sep)),
        "files": files,
        "blocks": blocks,
        "raw": raw_total,
        "compressed": cmp_total,
        "zero_ratio": (zero_total / raw_total) if raw_total else 0.0,
        "ratio": (cmp_total / raw_total) if raw_total else 0.0,
        "incompressible": incompressible,
        "comp_sizes": comp_sizes,
        "decomp_mbs": (decomp_bytes / 1048576.0 / t_dec) if t_dec > 0 else 0.0,
        "elapsed_s": time.perf_counter() - t0,
    }


def main(argv):
    args = argv[1:]
    level = 6
    sample = 0
    for a in list(args):
        if a.startswith("--level="):
            level = int(a.split("=", 1)[1]); args.remove(a)
        elif a.startswith("--sample="):
            sample = int(a.split("=", 1)[1]); args.remove(a)
        elif a == "--all":
            args.remove(a)

    if args:
        dirs = [os.path.join(GRAPH, a) if not os.path.isdir(a) else a for a in args]
    else:
        dirs = [os.path.join(GRAPH, n) for n in sorted(os.listdir(GRAPH))
                if os.path.isdir(os.path.join(GRAPH, n))]
    dirs = [d for d in dirs if os.path.isdir(d)]
    if not dirs:
        print("CONTAINER-PROBE-FAIL 找不到产物目录")
        return 1

    print("平台: %s %s / Python %s / zlib %s" %
          (platform.system(), platform.machine(), platform.python_version(), zlib.ZLIB_VERSION))
    print("压缩: raw deflate level=%d / 块=%d B / 抽样=%s" %
          (level, CHUNK, "全量" if sample == 0 else "每文件最多 %d 块" % sample))
    print("扫描: 递归（副机产物为 <省>/beijing/ 嵌套结构；gen_chunks.py 只扫顶层）")
    print("")
    print("%-12s %6s %10s %14s %14s %8s %8s" %
          ("省", "文件", "块数", "原始(B)", "压缩后(B)", "压缩比", "零字节"))
    results = []
    for d in dirs:
        r = probe_dir(d, sample, level)
        results.append(r)
        print("%-12s %6d %10d %14d %14d %7.1f%% %7.1f%%" %
              (r["province"], r["files"], r["blocks"], r["raw"], r["compressed"],
               r["ratio"] * 100, r["zero_ratio"] * 100))

    raw = sum(r["raw"] for r in results)
    cmp_ = sum(r["compressed"] for r in results)
    nb = sum(r["blocks"] for r in results)
    nz = sum(r["incompressible"] for r in results)
    all_sizes = [s for r in results for s in r["comp_sizes"]]
    all_sizes.sort()
    p50 = all_sizes[len(all_sizes) // 2] if all_sizes else 0
    p10 = all_sizes[len(all_sizes) // 10] if all_sizes else 0
    p90 = all_sizes[len(all_sizes) * 9 // 10] if all_sizes else 0
    dec = [r["decomp_mbs"] for r in results if r["decomp_mbs"] > 0]

    print("")
    print("=== 合计 ===")
    print("  原始 %d B / 压缩后 %d B / 总体压缩比 %.2f%% / 省下 %.1f MiB" %
          (raw, cmp_, cmp_ * 100.0 / raw if raw else 0, (raw - cmp_) / 1048576.0))
    print("  块数 %d / 压不动的块 %d (%.2f%%)" % (nb, nz, nz * 100.0 / nb if nb else 0))
    print("  压缩后单块长度  p10=%d B  p50=%d B  p90=%d B  (未压缩恒为 %d B)" %
          (p10, p50, p90, CHUNK))
    if dec:
        print("  用户态解压吞吐（本机，仅机制不迁倍数）: %.0f MiB/s" % (sum(dec) / len(dec)))

    # ---- 决策门数据：差分路径的净收益 ----
    # M2 现有块表每块存 64 hex + 分隔符约 67 B；O6 若追加「压缩帧偏移+长度」
    # 的 JSON 字段，按每块额外 30 B 估（二进制索引可压到 12 B）。
    idx_json_b = nb * 30
    idx_bin_b = nb * 12
    print("")
    print("=== 差分路径净收益估算（改 1 块）===")
    print("  M2 现状: 传 1 个 Range = %d B（裸块，零压缩）" % CHUNK)
    print("  O6 按块压缩: 平均传 %d B ⇒ 单块省 %d B" % (p50, CHUNK - p50))
    print("  但块索引要全量拉: 追加 offset/len ⇒ 8 省 +%d B (JSON) / +%d B (二进制)" %
          (idx_json_b, idx_bin_b))
    print("  ⇒ 只有当「本次更新的变化块数 × %d B」> 索引增量时，O6 才净赚。" %
          (CHUNK - p50))
    print("  ★ 整包路径本来就是 zip+deflate，O6 对它没有新增收益，别重复劳动。")

    print("")
    print("CONTAINER-PROBE-OK %d 省 / %d 文件 / %d 块" % (len(results),
          sum(r["files"] for r in results), nb))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
