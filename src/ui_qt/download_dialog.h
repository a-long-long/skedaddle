#pragma once
#include <QDialog>
#include <QVector>
#include <QProcess>   /* onConvertFinished 的 ExitStatus 参数需要完整类型 */
#include <functional> /* 用回调代替信号槽：本项目 Qt 前端刻意不用 Q_OBJECT，免去 moc */

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QCheckBox;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QFile;

/* 一个可下载区域（Geofabrik）。叶子（kids 空）才有真实下载路径；
 * 枝只用于树形归类。path 是 geofabrik 相对路径（不含 -latest.osm.pbf 后缀）。 */
struct GeoRegion {
    QString name;                 /* 显示名 */
    QString path;                 /* geofabrik 相对路径；枝为空串 */
    QVector<GeoRegion> kids;
};

class DownloadDialog : public QDialog {
public:
    /* sourcesDir：项目的 sources/ 目录（PBF 落盘处）；
     * repoRoot：项目根（含 scripts/sync_maps.sh），用于触发转换。
     * 二者用于"下载 → 转换 → 重新加载"这条链，无需重启程序。 */
    DownloadDialog(const QString &sourcesDir, const QString &repoRoot,
                   QWidget *parent = nullptr);

    /* 语言切换后重设所有常驻文字（标题/表头/按钮/勾选框），并刷新整棵树
     * （叶子/枝名 + ✓/· 后缀跟随当前语言）。MainWindow::retranslateUi 在切语言时调用。 */
    void retranslateUi();

    /* 预选一个地区（按 Geofabrik 叶子名，如 "hebei"）：展开祖先、选中叶子并滚动到
     * 可见处。供"浏览到无图区 → 反查省 → 一键下载该地区"使用。
     * 返回是否找到；找不到时对话框保持原样（调用方据此决定提示语）。 */
    bool selectByLeaf(const QString &leaf);

    /* 当前选中项的叶子名（如 "chongqing"）；无选中返回空串。
     * 自检用：只验"selectByLeaf 返回 true"不够——同名叶子可能出现在别的国家，
     * 真正要钉的是"选中的那一行就是期望的那个"。 */
    QString currentLeafForTest() const;

    /* 自检用：按叶子名查该区域是否已下载。复用与树标注（✓/·）同源的
     * regionDownloaded 判定，避免在测试里解析树控件文本。 */
    bool isLeafDownloaded(const QString &leaf) const;

    /* 下载 + 转换都成功后回调；name 为省/地区名（不含扩展名），便于提示。
     * 用回调代替 Qt 信号槽：本项目 Qt 前端刻意不用 Q_OBJECT，免去 moc 步骤。 */
    void setOnFinished(std::function<void(const QString &name)> cb)
    { m_onFinished = std::move(cb); }

    /* ---- 无头下载自检（--dl-test=<叶子> 用，2026-11-13）----
     * 真实网络链路无法用离屏像素断言，只能从外部读状态：触发下载、
     * 读日志文本（判定"多点/单流"）、读转换进程状态与退出码。
     * 网络依赖 ⇒ 不进默认 regress，单独手动跑。 */
    void startDownloadForTest() { startDownload(); }
    QString logTextForTest() const;      /* 实现在 cpp（QTextEdit 仅前向声明） */
    bool   isConvertingForTest() const   /* sync_maps.sh 还在跑 */
    { return m_proc && m_proc->state() != QProcess::NotRunning; }
    int    convertExitForTest() const { return m_proc ? m_proc->exitCode() : -1; }

    /* ---- aria2 下载引擎（2026-11-15）----
     * aria2c.exe 存在时优先用它：4 连接分块 + 控制文件（<target>.aria2）
     * 断点续传 + 内置自动重试，比内置 Qt 段实现更抗弱网。
     * 缺失或 SKEDADDLE_NO_ARIA2=1 时回退 Qt 段实现（两条路径互不干扰：
     * aria2 写正式文件名 + .aria2 控制文件；Qt 路径写 .part + .part.meta）。 */
    QString findAria2() const;
    bool   useAria2() const;
    void   downloadNow();                /* 按可用性选 aria2 或 Qt 段路径 */
    void   startAria2();
    void   onAria2Output();
    void   onAria2Finished(int code, QProcess::ExitStatus st);
    QString aria2ProxyArg() const;
    int    m_lastAriaPct = -1;           /* aria2 进度去抖（只刷变化的百分比） */

private:
    /* 判定某 geofabrik 路径（叶子）是否已下载：检查 products/graph/<名> 目录
     * 或 sources/<名>-latest.osm.pbf 是否存在。枝（path 空）恒返回 false。 */
    bool regionDownloaded(const QString &path) const;
    /* 刷新整棵树的状态标注（已下载/未下载 + 枝节点子区域计数）。 */
    void refreshStatus();
    /* 递归刷新单个节点；返回 (叶子总数, 已下载叶子数)，供父节点汇总。 */
    QPair<int,int> refreshItem(QTreeWidgetItem *it);

    void onSelChanged();
    void startDownload();
    /* ---- 断点续传 + 多点传输（2026-11-13）----
     * 所有段写同一个 <target>.part；段布局与进度存 <target>.part.meta（JSON）。
     * 约定：Seg.start 是段在文件里的**绝对偏移**，got 是段内已写字节数，
     * 请求区间恒为 bytes=(start+got)-(end)（end<0 表示"到文件尾"），
     * 进度 = Σ got，完成判定 = got == end-start+1（end<0 时由 reply 结束定）。
     * 多点：HEAD 拿到 Content-Length 且 ≥32MB 时拆 4 段并行；任何一段收到
     * 200（服务器不认 Range / 文件已变）⇒ 整体丢弃 .part 重来（自动降级单流）。
     * 续传：Range + If-Range（Last-Modified）——远端文件换过就不续，重头下。 */
    struct Seg {
        qint64 start = 0, end = 0, got = 0;
        QNetworkReply *reply = nullptr;
        bool checked = false;   /* 响应头已校验（206/200 分流只做一次） */
        bool failed = false;
    };
    void onHeadFinished(QNetworkReply *r);      /* 本地已有 pbf：远端新鲜度比对 */
    void headForDownload();                     /* 为"开始/续传"取远端元信息 */
    void onHeadForDl(QNetworkReply *r);
    void tryResume(qint64 total, const QString &lm, const QString &etag);
    void startFresh(qint64 total, const QString &lm, const QString &etag,
                    bool allowSplit = true);
    void startSegs(const QVector<Seg> &segs, qint64 total,
                   const QString &lm, const QString &etag, bool resume);
    void onSegData(int idx);                    /* readyRead：seek+write 本段 */
    void onSegFinished(int idx);
    void onAllSegsDone();                       /* 全段完成：校验+定稿 → 转换 */
    void abortSegs();                           /* 中止全部段请求（保留 .part） */
    void scheduleRetry(const QString &why);     /* 自动续传重试（最多 3 次） */
    void restartAsFresh(qint64 total, const QString &lm, const QString &etag);
    void updateProgress();
    qint64 sumGot() const;
    void saveMeta();
    bool loadMeta(QVector<Seg> *segs, qint64 *total,
                  QString *lm, QString *etag) const;
    void clearPart();                           /* 删 .part 与 .part.meta */
    void startConvert();                        /* 下载定稿后触发 sync_maps.sh */
    void setBusy(bool b);
    void logLine(const QString &s);
    void copyUrl();
    void onConvertFinished(int code, QProcess::ExitStatus st);
    void onConvertOutput();

private:
    void           fillTree(QTreeWidgetItem *parent, const QVector<GeoRegion> &rs);
    QVector<GeoRegion> buildRegions() const;
    QString        currentPath() const;   /* 选中叶子的 geofabrik 路径（枝为空） */
    QString        currentUrl()  const;   /* 完整下载 URL */
    QString        currentName() const;   /* 文件名（不含扩展名），如 gansu */
    void           runContoursGeneration(); /* 生成等高线（若 m_contourCb 勾选） */

    QTreeWidget   *m_tree  = nullptr;
    QLabel        *m_info  = nullptr;
    QLineEdit     *m_url   = nullptr;
    QProgressBar  *m_prog  = nullptr;
    QTextEdit     *m_log   = nullptr;
    QPushButton   *m_dl    = nullptr;
    QPushButton   *m_copy  = nullptr;
    QCheckBox          *m_contourCb = nullptr;  /* "同时生成 SRTM 等高线" 勾选框 */
    QPushButton   *m_close = nullptr;

    QNetworkAccessManager *m_nam   = nullptr;
    QVector<Seg>  m_segs;                  /* 并行下载段（多点传输） */
    QFile        *m_part  = nullptr;       /* <target>.part，全段共写 */
    QProcess     *m_proc  = nullptr;

    QString m_sources;     /* sources/ 目录 */
    QString m_repo;        /* 项目根 */
    QString m_target;      /* 下载目标文件（sources/<name>-latest.osm.pbf） */
    QString m_dlName;      /* 当前下载的省/地区名（回调用） */
    QString m_dlUrl;       /* 当前下载 URL（跨重试/多段共用同一份；m_url 是输入框） */
    qint64  m_total = 0;           /* 远端全文件大小；0 = 未知 */
    bool    m_totalKnown = false;
    QString m_lm, m_etag;          /* 远端新鲜度（If-Range / meta 校验） */
    int     m_retries = 0;         /* 自动续传重试计数 */
    qint64  m_lastMetaMs = 0;      /* 上次落盘 meta 的时刻（节流） */
    bool    m_noRange = false;     /* 本会话已确认服务器不认 Range ⇒ 后续全单流 */
    std::function<void(const QString &name)> m_onFinished;  /* 下载+转换完成回调 */
};
