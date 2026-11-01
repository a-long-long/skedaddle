#ifndef SKEDADDLE_QT_NMEA_SOURCE_H
#define SKEDADDLE_QT_NMEA_SOURCE_H

#include <QObject>
#include <QVector>
#include <QByteArray>
#include "nmea_parser.h"
#include "skyplot_draw.h"   /* SkSatellite：卫星天顶图绘制层（POD 结构） */

#ifdef HAVE_QT_SERIALPORT
#include <QSerialPort>
#endif

/* 外部定位信号源：把 NMEA-0183 流（来自外部 GNSS / 蓝牙 GPS 接收机）接成
 * skedaddle 的定位源 + 天顶图卫星数据。设计参照 OsmAnd 的 AIS 插件
 * 「Use NMEA as location source」：经 TCP/UDP/串口收 NMEA，RMC/GGA 给出位置、
 * GSV/GSA 给出天空图。
 *
 * ★ 不继承 Q_OBJECT（无 Q_OBJECT 宏）：本项目 Qt 前端刻意不用信号槽/moc
 *   （见 satellite_source.h）。用新语法 lambda 绑定 QTcpSocket/QUdpSocket 的
 *   信号即可，无需 moc。
 *
 * 用法：MainWindow 持有本对象；每帧调用 consume() 把最新定位/卫星接入 App
 * 与 SkSatelliteModel；stale() 判定信号丢失→回退模拟。 */
class SkNmeaSource : public QObject {
public:
    enum Proto { PROTO_TCP, PROTO_UDP, PROTO_SERIAL };

    explicit SkNmeaSource(QObject *parent = nullptr);
    ~SkNmeaSource();

    /* 建立连接：TCP/UDP 用 host+port；SERIAL 用 device+baud（需 HAVE_QT_SERIALPORT）。
     * 返回 true 表示已开始连接/监听（异步，成功与否看 active()/errorString()）。 */
    bool connectTo(Proto p, const QString &host, int port,
                   const QString &device = QString(), int baud = 4800);

    void disconnect();
    bool active() const { return m_active; }

    /* 最近一次定位（valid 可能为 false）。 */
    const SkNmeaFix &fix() const { return m_fix; }
    /* 当前卫星集（视野内全部，含 in_use 标注）。 */
    QVector<SkSatellite> sats() const;

    /* 定位刷新距 nowMs 的毫秒数；无定位返回极大值。 */
    qint64 fixAgeMs(qint64 nowMs) const;
    /* 信号是否应判为丢失（未连接，或距上次定位刷新超过 timeoutMs）。 */
    bool stale(qint64 nowMs, qint64 timeoutMs) const;

    QString errorString() const { return m_err; }

    /* 每帧由 MainWindow 调用：把最新定位/卫星接入 App 与模型。
     * 返回 true 表示本次有数据刷新（用于触发重绘）。mode：是否把定位写成 App 外部源。 */
    bool consume();

private:
    void ingest(const QByteArray &chunk);          /* 拆行喂解析器 + 更新时间戳 */
    void onParserRefresh();
    void closeSockets();

    SkNmeaParser      m_parser;
    SkNmeaFix         m_fix;
    std::vector<SkNmeaSat> m_sats;
    qint64            m_lastFixMs = 0;
    qint64            m_lastSatMs = 0;
    bool              m_active = false;
    bool              m_dirty = false;   /* 自上次 consume() 以来有新数据 */
    QString           m_err;

#ifdef HAVE_QT_SERIALPORT
    QSerialPort      *m_serial = nullptr;
#endif
    class QTcpSocket *m_tcp = nullptr;
    class QUdpSocket *m_udp = nullptr;

    QByteArray         m_pend;     /* TCP 分包时的行缓冲 */
};

#endif
