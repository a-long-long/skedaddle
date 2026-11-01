#ifndef SKEDADDLE_QT_SATELLITE_MODEL_H
#define SKEDADDLE_QT_SATELLITE_MODEL_H

#include <QObject>
#include <QVector>
#include "skyplot_draw.h"

/* 卫星数据模型：持有当前可见/在用卫星列表。
 *
 * 两种数据来源：
 *   · 模拟星座（默认）：用一组确定性"轨道参数"生成 GPS/北斗/格洛纳斯/伽利略
 *     风格的卫星，随时间缓慢漂移（方位角平移 + 仰角正弦起伏），信号强度随仰角
 *     变化、部分星被标记为"在用"；始终有数据，可演示、可验证。
 *   · 真实源（HAVE_QT_POSITIONING）：由 SkSatelliteSource 把系统定位后端的
 *     卫星信息 setRealSatellites() 灌进来；此时模拟暂停。
 * 绘制层（skyplot_draw）只认 SkSatellite 数组，与本模型解耦。 */
/* ★ 不继承 Q_OBJECT：本项目 Qt 前端刻意不用信号槽/moc（见 download_dialog.h）。
 *   模拟推进由 MapCanvas::tick 主动调用 advance()，真实数据由 SkSatelliteSource
 *   直接 setRealSatellites()，无需 updated() 信号。保留 QObject 基类只为
 *   parent 内存托管（new SkSatelliteModel(this)）。 */
class SkSatelliteModel : public QObject {
public:
    explicit SkSatelliteModel(QObject *parent = nullptr);

    int count() const { return m_sats.size(); }
    const SkSatellite *satellites() const { return m_sats.constData(); }
    bool hasFix() const { return m_hasFix; }
    int  visibleCount() const;
    int  inUseCount() const;
    bool isReal() const { return m_real; }

    /* 推进模拟（dt 秒）。真实源接管后不应再调用。 */
    void advance(double dt);

    /* 真实数据接入（仅 HAVE_QT_POSITIONING 调用）：传入后切到真实模式。 */
    void setRealSatellites(const QVector<SkSatellite> &sats);

    /* 复位为模拟模式（真实源丢失时调用）。 */
    void setSimulation(bool on);

private:
    struct Orbit {
        int    system;
        int    prn;
        double az0;        /* 初始方位角 */
        double el0;        /* 基准仰角 */
        double azRate;     /* 方位角漂移速率 deg/s（含正负） */
        double elAmp;      /* 仰角正弦振幅 */
        double elPhase;    /* 仰角正弦相位 */
        double elFreq;     /* 仰角正弦频率 1/s */
    };

    void regenerate();     /* 生成确定性模拟轨道参数 */
    void simulate(double t);

    QVector<SkSatellite> m_sats;
    QVector<Orbit>       m_orbits;
    bool                 m_real = false;
    bool                 m_hasFix = false;
    double               m_t = 0.0;
};

#endif
