package com.skedaddle.android;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.channels.FileChannel;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;

/**
 * 落盘提交的唯一入口：临时文件 → 正式文件，**一次 rename(2)** 完成替换。
 *
 * <p>★ 2026-11-16 修「增量更新提交不是原子的」：旧代码四处都是同一个写法
 *
 * <pre>
 *   if (dest.exists() &amp;&amp; !dest.delete()) throw new IOException("无法覆盖 " + dest);
 *   if (!tmp.renameTo(dest))                throw new IOException("rename failed: " + dest);
 * </pre>
 *
 * <p>两步之间进程被杀（Android 上一点都不罕见：低内存 LMK、用户「强行停止」、
 * OTA 重启、system_server 回收后台进程），目录里就只剩 <code>xxx.new</code>
 * 而没有正式的 <code>xxx</code> —— 地图省整个消失，而 <code>chunks.json</code>
 * 还是旧的，下次更新按旧块表去 patch 一个不存在的文件，只能回落整包。
 * 更糟的是这个窗口**不在我们自己代码的可控范围内**：它由内核的两次系统调用
 * 之间的任意时刻决定。
 *
 * <p>而 POSIX 的 <code>rename(2)</code> 本来就是「原子替换已存在的目标」：
 * 目录项要么指向旧 inode、要么指向新 inode，不存在「没有」的中间态。所以
 * 先删再改纯属自找，正确做法只有一步。
 *
 * <p>两个容易漏的细节：
 * <ul>
 *   <li><b>先 fsync 再 rename</b>：rename 只改目录项，数据可能还在页缓存里。
 *       掉电后可能出现「新文件名 + 空内容」，比丢失更难查 —— 长度对不上、
 *       哈希对不上，但文件明明「装好了」。{@link #syncFile} 补这一刀。</li>
 *   <li><b>rename 之后 fsync 目录</b>：否则目录项本身可能回退。{@link #syncDir}
 *       尽力而为（某些 FUSE/虚拟盘不让 open 目录，跳过即可，不影响原子性）。</li>
 * </ul>
 *
 * <p>纯 JDK（{@code java.nio.file} 自 API 26 起可用，与本模块 minSdk 一致），
 * 主机夹具 {@code scripts/java_gate.sh} 能直接编译实跑，不需要 Android 运行时。
 */
public final class AtomicCommit {

    private AtomicCommit() {}

    /**
     * 用 tmp 的内容原子替换 dest。任何失败都抛 {@link IOException}，且 **dest
     * 保持原样**——这条比「成功」更重要：调用方的兜底是「保留旧图回落整包」，
     * 前提就是失败时旧文件还在。
     */
    public static void commit(File tmp, File dest) throws IOException {
        if (tmp == null || dest == null) throw new IOException("commit 参数为空");
        /* ★ 顺序：先确认临时文件存在，再去碰 dest。
         *   旧代码反过来（先 delete(dest) 再 rename），临时文件缺失或 rename
         *   失败时旧文件已经没了 —— 这是「提交不是原子的」最直接的后果。 */
        if (!tmp.isFile()) {
            throw new IOException("待提交的临时文件不存在: " + tmp);
        }
        File parent = dest.getAbsoluteFile().getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("无法创建目录 " + parent);
        }
        syncFile(tmp);
        Path src = tmp.toPath();
        Path dst = dest.toPath();
        try {
            /* Unix 上 ATOMIC_MOVE 就是 rename(2)：目标存在则整块替换，中间没有
             * 「dest 不存在」的瞬间。REPLACE_EXISTING 一并给上，是因为 javadoc
             * 对「只给 ATOMIC_MOVE 时目标已存在」留了实现自定的余地，显式声明
             * 才能保证各平台语义一致。 */
            Files.move(src, dst, StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException e) {
            /* 同目录 rename 不该走到这里（只有跨设备 EXDEV 才会）。宁可失败，
             * 也不退回「先删后改」——那正是本次要消灭的窗口。 */
            throw new IOException("目标文件系统不支持原子替换: " + dst, e);
        } catch (IOException e) {
            throw new IOException("原子替换失败: " + dst + "（" + e.getMessage() + "）", e);
        }
        syncDir(parent);
    }

    /** 把已写入的内容刷到存储介质（rename 只改目录项，不保证数据落盘）。 */
    public static void syncFile(File f) throws IOException {
        try (RandomAccessFile raf = new RandomAccessFile(f, "rw")) {
            raf.getFD().sync();
        }
    }

    /**
     * 目录项落盘（尽力而为）。拿不到目录句柄就跳过：原子性由 rename 提供，
     * 这里只补「掉电后 rename 是否还在」这一层，不该让它变成失败原因。
     */
    public static void syncDir(File dir) {
        if (dir == null) return;
        try (FileChannel ch = FileChannel.open(dir.toPath(), StandardOpenOption.READ)) {
            ch.force(true);
        } catch (IOException | RuntimeException ignore) {
            /* 部分文件系统（FUSE / 虚拟盘）不允许 open 目录 */
        }
    }

    /** 丢弃临时文件（失败清理用）；删不掉就登记退出时删，不抛。 */
    public static void discard(File f) {
        if (f == null || !f.exists()) return;
        if (!f.delete()) f.deleteOnExit();
    }
}
