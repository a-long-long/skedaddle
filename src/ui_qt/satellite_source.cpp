#include "satellite_source.h"

SkSatelliteSource::SkSatelliteSource(SkSatelliteModel *model, QObject *parent)
    : QObject(parent), m_model(model)
{
#ifdef HAVE_QT_POSITIONING
    m_src = QGeoSatelliteInfoSource::createDefaultSource(this);
    if (m_src) {
        m_available = true;
        connect(m_src, &QGeoSatelliteInfoSource::satellitesInViewUpdated,
                this, [this](const QList<QGeoSatelliteInfo> &s) {
                    applyInView(s);
                });
        connect(m_src, &QGeoSatelliteInfoSource::satellitesInUseUpdated,
                this, [this](const QList<QGeoSatelliteInfo> &s) {
                    Q_UNUSED(s);
                    /* inUse 已在前一次 inView 缓存上标记，这里仅触发重绘。 */
                    if (!m_lastView.isEmpty())
                        m_model->setRealSatellites(m_lastView);
                });
    }
#else
    Q_UNUSED(m_model);
#endif
}

void SkSatelliteSource::start()
{
#ifdef HAVE_QT_POSITIONING
    if (m_src) m_src->startUpdates();
#endif
}

#ifdef HAVE_QT_POSITIONING
void SkSatelliteSource::applyInView(const QList<QGeoSatelliteInfo> &sats)
{
    QVector<SkSatellite> out;
    out.reserve(sats.size());
    for (const QGeoSatelliteInfo &gi : sats) {
        int sys = SK_SAT_BDS;     /* 默认按北斗着色（国产设备常见） */
        switch (gi.system()) {
        case QGeoSatelliteInfo::GPS:      sys = SK_SAT_GPS; break;
        case QGeoSatelliteInfo::GLONASS:  sys = SK_SAT_GLO; break;
        case QGeoSatelliteInfo::Galileo:  sys = SK_SAT_GAL; break;
        default:                          sys = SK_SAT_BDS; break;
        }
        double az = 0.0, el = 0.0, snr = gi.signalStrength();
        /* 方位/仰角：多数桌面后端（含 NMEA）不提供，缺省 0——
         * 真实几何依赖平台 attribute，缺失时卫星落在地平圈（可接受）。 */
        if (gi.hasAttribute(QGeoSatelliteInfo::Azimuth))
            az = gi.attribute(QGeoSatelliteInfo::Azimuth);
        if (gi.hasAttribute(QGeoSatelliteInfo::Elevation))
            el = gi.attribute(QGeoSatelliteInfo::Elevation);
        SkSatellite s;
        s.system = sys;
        s.prn = gi.satelliteIdentifier();
        s.azimuth = az; s.elevation = el; s.snr = snr; s.in_use = false;
        out.push_back(s);
    }
    m_lastView = out;
    m_model->setRealSatellites(out);
}
#endif
