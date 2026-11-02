#include "nmea_source.h"

#include <QDateTime>
#include <QAbstractSocket>
#include <QTcpSocket>
#include <QUdpSocket>
#include <climits>

/* 外部定位信号源实现。传输层（TCP/UDP/串口）只负责把收到的字节流拆成 NMEA 行，
 * 解析全部交给纯 C++ 的 SkNmeaParser；本类再把它持有的最新定位/卫星暴露给 MainWindow。 */

SkNmeaSource::SkNmeaSource(QObject *parent) : QObject(parent) {}

SkNmeaSource::~SkNmeaSource()
{
    closeSockets();
}

void SkNmeaSource::closeSockets()
{
    if (m_tcp) { m_tcp->disconnect(); m_tcp->deleteLater(); m_tcp = nullptr; }
    if (m_udp) { m_udp->disconnect(); m_udp->deleteLater(); m_udp = nullptr; }
#ifdef HAVE_QT_SERIALPORT
    if (m_serial) { m_serial->disconnect(); m_serial->deleteLater(); m_serial = nullptr; }
#endif
    m_active = false;
}

void SkNmeaSource::disconnect()
{
    closeSockets();
    m_err.clear();
}

bool SkNmeaSource::connectTo(Proto p, const QString &host, int port,
                             const QString &device, int baud)
{
    closeSockets();
    m_err.clear();
    m_pend.clear();
#ifndef HAVE_QT_SERIALPORT
    (void)device; (void)baud;   /* 仅串口模式才用到，未启用时显式消除未用告警 */
#endif

    if (p == PROTO_TCP) {
        m_tcp = new QTcpSocket(this);
        connect(m_tcp, &QTcpSocket::readyRead, this, [this]() {
            ingest(m_tcp->readAll());
        });
        connect(m_tcp, &QTcpSocket::connected, this, [this]() {
            m_active = true;
        });
        connect(m_tcp, &QTcpSocket::disconnected, this, [this]() {
            m_active = false;
        });
        /* errorOccurred 是 Qt 5.15+ 的信号；Qt 6 也只保留它。
         * 本机构建环境是 Qt 5.11.3（Windows 副机是 Qt 6），故按版本守卫：
         * 5.15+ 用 errorOccurred，更早版本用旧的 error 信号（用 QOverload 消歧义，
         * 因为 error 同时是方法名）。这样同一份代码在两端都能编译。 */
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        connect(m_tcp, &QAbstractSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) {
                    if (m_tcp) m_err = m_tcp->errorString();
                });
#else
        connect(m_tcp, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
                this, [this](QAbstractSocket::SocketError) {
                    if (m_tcp) m_err = m_tcp->errorString();
                });
#endif
        m_tcp->connectToHost(host, (quint16)port);   /* 异步；connected 后再 active */
    } else if (p == PROTO_UDP) {
        m_udp = new QUdpSocket(this);
        connect(m_udp, &QUdpSocket::readyRead, this, [this]() {
            while (m_udp->hasPendingDatagrams()) {
                QByteArray dgram;
                dgram.resize((int)m_udp->pendingDatagramSize());
                qint64 got = m_udp->readDatagram(dgram.data(), dgram.size());
                if (got > 0) ingest(QByteArray(dgram.constData(), (int)got));
            }
        });
        if (!m_udp->bind((quint16)port, QUdpSocket::ShareAddress |
                                        QUdpSocket::ReuseAddressHint)) {
            m_err = m_udp->errorString();
            m_active = false;
            return false;
        }
        m_active = true;      /* UDP 监听立即就绪 */
    } else {  /* SERIAL */
#ifdef HAVE_QT_SERIALPORT
        m_serial = new QSerialPort(this);
        m_serial->setPortName(device);
        m_serial->setBaudRate((qint32)baud);
        if (!m_serial->open(QIODevice::ReadOnly)) {
            m_err = m_serial->errorString();
            m_active = false;
            return false;
        }
        connect(m_serial, &QSerialPort::readyRead, this, [this]() {
            ingest(m_serial->readAll());
        });
        m_active = true;
#else
        m_err = QStringLiteral("本构建未启用 Qt SerialPort，不支持串口");
        m_active = false;
        return false;
#endif
    }
    return true;
}

void SkNmeaSource::ingest(const QByteArray &chunk)
{
    m_pend.append(chunk);
    /* 按 '\n' 拆出完整行（TCP 可能分包，残行留到下次）。 */
    int from = 0;
    for (int i = 0; i < m_pend.size(); i++) {
        if (m_pend[i] == '\n') {
            int len = i - from;
            if (len > 0) {
                QByteArray line = m_pend.mid(from, len);
                if (line.endsWith('\r')) line.chop(1);
                if (m_parser.feed(line.constData(), (size_t)line.size()))
                    onParserRefresh();
            }
            from = i + 1;
        }
    }
    if (from > 0) m_pend = m_pend.mid(from);
}

void SkNmeaSource::onParserRefresh()
{
    m_fix = m_parser.fix();
    m_sats = m_parser.sats();
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastFixMs = now;
    m_lastSatMs = now;
    m_dirty = true;
}

QVector<SkSatellite> SkNmeaSource::sats() const
{
    QVector<SkSatellite> out;
    out.reserve((int)m_sats.size());
    for (const SkNmeaSat &s : m_sats) {
        SkSatellite k;
        k.prn = s.prn; k.system = s.system; k.azimuth = s.azimuth;
        k.elevation = s.elevation; k.snr = s.snr; k.in_use = s.in_use;
        out.push_back(k);
    }
    return out;
}

qint64 SkNmeaSource::fixAgeMs(qint64 nowMs) const
{
    if (m_lastFixMs == 0) return LLONG_MAX;
    return nowMs - m_lastFixMs;
}

bool SkNmeaSource::stale(qint64 nowMs, qint64 timeoutMs) const
{
    if (!m_active) return true;
    if (m_lastFixMs == 0) return true;
    return (nowMs - m_lastFixMs) > timeoutMs;
}

bool SkNmeaSource::consume()
{
    bool d = m_dirty;
    m_dirty = false;
    return d;
}
