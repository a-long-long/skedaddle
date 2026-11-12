#ifndef SKEDADDLE_QT_TRC_H
#define SKEDADDLE_QT_TRC_H

/* main_window.cpp / main_window_uitest.cpp 两个翻译单元共享的小助手。
 * static inline —— 各 TU 独立副本、零 ODR 风险，同时保留原函数名，
 * 免去调用点的机械改名。（2026-11-12 从 main_window.cpp 提出。）
 */
#include <QString>
#include <QDir>
extern "C" {
#include "i18n/i18n.h"
}

/* 多语言便捷封装：以中文为 key，按当前语言返回串（默认中文原样返回）。 */
static inline QString trc(const char *zh) { return QString::fromUtf8(i18n_tr(zh)); }

/* 从 graph_dir（可能是 products/graph 或单省 products/graph/beijing）向上找
 * 项目根：含 sources/ 与 scripts/ 的那个目录。向下兼容旧的两级 cdUp 写法。 */
static inline QString repoRootFromGraphDir(const QString &graphDir)
{
    QDir d(graphDir);
    for (int i = 0; i < 6; i++) {
        if (QDir(d.filePath(QStringLiteral("sources"))).exists() &&
            QDir(d.filePath(QStringLiteral("scripts"))).exists())
            return d.absolutePath();
        if (!d.cdUp()) break;
    }
    QDir f(graphDir); f.cdUp(); f.cdUp();
    return f.absolutePath();
}

#endif
