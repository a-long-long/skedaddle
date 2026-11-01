#include "satellite_model.h"

#include <cmath>

/* 确定性伪随机：基于整数索引的 hash，保证每次启动星座布局一致。 */
static double s_hash01(int i, int salt)
{
    double v = std::sin(double(i) * 12.9898 + double(salt) * 78.233) * 43758.5453;
    return v - std::floor(v);
}

SkSatelliteModel::SkSatelliteModel(QObject *parent) : QObject(parent)
{
    regenerate();
    simulate(0.0);
}

void SkSatelliteModel::regenerate()
{
    /* 各系统的卫星数与 PRN 基址（北斗用 201+ 真实风格，格洛纳斯 65+，伽利略 301+）。 */
    struct Spec { int system; int n; int prnBase; };
    static const Spec specs[] = {
        { SK_SAT_GPS, 11, 1 },
        { SK_SAT_BDS, 9,  201 },
        { SK_SAT_GLO, 6,  65 },
        { SK_SAT_GAL, 5,  301 },
    };

    m_orbits.clear();
    m_sats.clear();
    int idx = 0;
    for (const Spec &sp : specs) {
        for (int k = 0; k < sp.n; k++, idx++) {
            Orbit o;
            o.system  = sp.system;
            o.prn     = sp.prnBase + k;
            o.az0     = s_hash01(idx, 1) * 360.0;
            o.el0     = 25.0 + s_hash01(idx, 2) * 55.0;       /* 25..80 */
            o.azRate  = (s_hash01(idx, 3) - 0.5) * 1.6;        /* -0.8..0.8 deg/s */
            o.elAmp   = 12.0 + s_hash01(idx, 4) * 22.0;        /* 12..34 */
            o.elPhase = s_hash01(idx, 5) * 6.2831853;
            o.elFreq  = 0.01 + s_hash01(idx, 6) * 0.04;        /* 0.01..0.05 1/s */
            m_orbits.push_back(o);
            SkSatellite s;
            s.system = o.system; s.prn = o.prn;
            s.azimuth = 0.0; s.elevation = 0.0; s.snr = 0.0; s.in_use = false;
            m_sats.push_back(s);
        }
    }
}

void SkSatelliteModel::simulate(double t)
{
    for (int i = 0; i < m_orbits.size(); i++) {
        const Orbit &o = m_orbits[i];
        double az = std::fmod(o.az0 + o.azRate * t, 360.0);
        if (az < 0.0) az += 360.0;
        double el = o.el0 + o.elAmp * std::sin(o.elFreq * t + o.elPhase);
        if (el < 0.0)   el = 0.0;
        if (el > 90.0)  el = 90.0;
        SkSatellite &s = m_sats[i];
        s.azimuth = az;
        s.elevation = el;
        if (el > 0.0) {
            double base = 12.0 + 34.0 * (el / 90.0);           /* 仰角越高信号越强 */
            double jitter = 4.0 * std::sin(0.7 * t + o.elPhase * 3.0);
            s.snr = base + jitter;
            if (s.snr < 0.0) s.snr = 0.0;
            if (s.snr > 50.0) s.snr = 50.0;
        } else {
            s.snr = 0.0;
        }
        s.in_use = false;
    }

    /* 选"在用"：可见星（仰角>8）按仰角降序，最多取 12 颗——稳定（每帧变化小）。 */
    struct IdxEl { int i; double el; };
    QVector<IdxEl> cand;
    for (int i = 0; i < m_sats.size(); i++)
        if (m_sats[i].elevation > 8.0 && m_sats[i].snr > 0.0)
            cand.push_back({i, m_sats[i].elevation});
    for (int a = 1; a < cand.size(); a++) {                   /* 插入排序（规模小） */
        IdxEl key = cand[a];
        int b = a - 1;
        while (b >= 0 && cand[b].el < key.el) { cand[b + 1] = cand[b]; b--; }
        cand[b + 1] = key;
    }
    int take = cand.size() < 12 ? cand.size() : 12;
    for (int a = 0; a < take; a++) m_sats[cand[a].i].in_use = true;
    m_hasFix = (take >= 4);
}

void SkSatelliteModel::advance(double dt)
{
    if (m_real) return;
    m_t += dt;
    simulate(m_t);
}

int SkSatelliteModel::visibleCount() const
{
    int n = 0;
    for (const SkSatellite &s : m_sats)
        if (s.elevation > 0.0 && s.snr > 0.0) n++;
    return n;
}

int SkSatelliteModel::inUseCount() const
{
    int n = 0;
    for (const SkSatellite &s : m_sats)
        if (s.in_use) n++;
    return n;
}

void SkSatelliteModel::setRealSatellites(const QVector<SkSatellite> &sats)
{
    m_real = true;
    m_sats = sats;
    int use = 0;
    for (const SkSatellite &s : sats)
        if (s.in_use) use++;
    m_hasFix = (use >= 4);
}

void SkSatelliteModel::setSimulation(bool on)
{
    if (on && m_real) {
        m_real = false;
        regenerate();
        simulate(m_t);
    }
}
