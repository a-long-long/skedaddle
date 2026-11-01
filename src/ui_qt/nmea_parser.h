#ifndef SKEDADDLE_QT_NMEA_PARSER_H
#define SKEDADDLE_QT_NMEA_PARSER_H

#include <cstdint>
#include <vector>
#include <string>
#include <map>

/* NMEA-0183 解析（纯 C++，不依赖 Qt/网络/窗口）。
 *
 * 设计参照 OsmAnd 的 AIS 插件「Use NMEA as location source」：
 *   从外部 GNSS / 蓝牙 GPS 接收机经 TCP/UDP/串口送来的 NMEA 流里，
 *   提取**定位**（RMC/GGA）+ **卫星天空图**（GSV/GSA），作为 skedaddle 的定位源。
 *   - $RMC / $GGA → 位置/海拔/速度/航向/定位质量；
 *   - $GSV（多段，按 talker 累积）→ 每颗星的 系统/PRN/方位/仰角/SNR；
 *   - $GSA → 在用星（in_use）与定位模式（2D/3D）。
 *   解析器只认"句子"，传输（socket/串口）由 SkNmeaSource（Qt）负责，便于单元测试。
 *
 * ★ 纯 C++：本文件不 include 任何 Qt 头，nmea_selftest 可单独编译验证。
 *   卫星结构用本项目自己的 SkNmeaSat（与 skyplot_draw.h 的 SkSatellite 字段同义），
 *   由 SkNmeaSource 在喂模型时原地转换成 SkSatellite。 */

/* 卫星系统编号（与 skyplot_draw.h 的 SkSatSystem 完全一致：0 GPS/1 BDS/2 GLO/3 GAL）。 */
enum SkNmeaSys {
    SKN_GPS = 0,
    SKN_BDS = 1,
    SKN_GLO = 2,
    SKN_GAL = 3,
    SKN_SYS_N
};

struct SkNmeaSat {
    int    prn;
    int    system;     /* SkNmeaSys */
    double azimuth;    /* 方位角 0..360，0=北，顺时针 */
    double elevation;  /* 仰角 0..90，0=地平线，90=天顶 */
    double snr;        /* 信号强度 0..99 dBHz（0=无） */
    bool   in_use;     /* 是否用于定位（来自 GSA） */
};

struct SkNmeaFix {
    bool   valid;       /* 有可用位置 */
    double lat, lon;    /* 度 */
    double alt_m;       /* 海拔（m），未知 = 0 */
    double speed_ms;    /* 对地速度 m/s（RMC knots→m/s） */
    double course_deg;  /* 对地航向 0..360 */
    int    quality;     /* GGA fix quality：0=无效 1=GPS 2=DGPS … */
    int    mode;        /* GSA fix mode：0/1=无 2=2D 3=3D */
    int    nsats_used;  /* GSA 列出的在用星数 */
    int    nsats_view;  /* 视野内星数（GGA/GSV） */
};

class SkNmeaParser {
public:
    SkNmeaParser();

    /* 喂一行（不含换行也可含；自动按 '$' 定位句首、'*' 取校验和）。
     * 返回 true：这一行刷新了定位或卫星集（供调用方按需重绘/重喂模型）。
     * 说明：GSV 多段，整组收齐才刷新卫星集；RMC/GGA 每行刷新定位；GSA 刷新在用星。 */
    bool feed(const char *line, size_t len);
    bool feed(const std::string &line) { return feed(line.data(), line.size()); }

    const SkNmeaFix            &fix()  const { return m_fix; }
    const std::vector<SkNmeaSat> &sats() const { return m_sats; }

    void reset();

    int  linesParsed()    const { return m_lines; }
    int  linesBadCksum() const { return m_bad; }

private:
    /* talker（GP/GL/GA/BD/GB/GN…）→ 系统。 */
    static int talkerToSys(const char *talker, size_t n);

    /* NMEA 坐标：ddmm.mmmmm(N/S) 或 dddmm.mmmmm(E/W) → 带符号度。
     * 返回 false：字段为空/非法。 */
    static bool parseCoord(const char *val, size_t n, char dir,
                          double *out_deg);

    /* 句子类型（GGA/RMC/GSV/GSA）+ 字段处理；返回是否刷新。 */
    bool handleGGA(const char *body, const char *end, int sys);
    bool handleRMC(const char *body, const char *end, int sys);
    bool handleGSV(const char *body, const char *end, int sys);
    bool handleGSA(const char *body, const char *end, int sys);

    SkNmeaFix m_fix;
    std::vector<SkNmeaSat> m_sats;
    /* (system,prn) → m_sats 下标，O(1) 更新卫星。 */
    std::map<uint64_t, size_t> m_idx;

    /* GSV 多段累积缓冲：按 talker 暂存本组已到的卫星，整组收齐再并入 m_sats。 */
    struct GsvBuf {
        int     total = 0;
        int     num   = 0;
        int     sys   = SKN_GPS;
        std::vector<SkNmeaSat> sats;
    };
    std::map<int, GsvBuf> m_gsv;   /* key = talker 系统 */

    int m_lines = 0;
    int m_bad   = 0;
};

#endif
