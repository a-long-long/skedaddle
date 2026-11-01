#ifndef SKEDADDLE_QT_SATELLITE_SOURCE_H
#define SKEDADDLE_QT_SATELLITE_SOURCE_H

#include <QObject>
#include "satellite_model.h"

#ifdef HAVE_QT_POSITIONING
#include <QGeoSatelliteInfoSource>
#include <QGeoSatelliteInfo>
#endif

/* 真实卫星源封装。
 *
 * 若平台定位后端可用（HAVE_QT_POSITIONING 且创建到默认源），把
 * QGeoSatelliteInfoSource 的 satellitesInView / satellitesInUse 转成
 * SkSatellite 灌进模型、暂停模拟；不可用或丢失时让模型回到模拟。
 *
 * 无 Positioning 模块（本机当前情况）时本类退化为空壳：available() 恒 false，
 * start() 空操作——功能仍由模拟星座兜底，保证可演示。 */
/* ★ 不继承 Q_OBJECT：本项目 Qt 前端刻意不用信号槽/moc。connect 用新语法
 *   （函数指针 + lambda）绑定 QGeoSatelliteInfoSource 的信号，不需要 moc。 */
class SkSatelliteSource : public QObject {
public:
    explicit SkSatelliteSource(SkSatelliteModel *model, QObject *parent = nullptr);
    bool available() const { return m_available; }
    void start();

private:
#ifdef HAVE_QT_POSITIONING
    void applyInView(const QList<QGeoSatelliteInfo> &sats);
    QGeoSatelliteInfoSource *m_src = nullptr;
    QVector<SkSatellite>     m_lastView;   /* 缓存最近一次 inView，供 inUse 合并 */
#endif
    SkSatelliteModel *m_model;
    bool              m_available = false;
};

#endif
