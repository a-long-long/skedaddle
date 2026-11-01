#include "nmea_parser.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>

/* ------------------------------------------------------------------ */
/* 静态工具                                                            */
/* ------------------------------------------------------------------ */

static int hex1(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 安全解析 double（空字段 → 0；解析失败返回 false）。文件作用域自由函数，
 * 供 parseCoord（成员）/parseFieldD（自由函数）共用。 */
static bool parseDouble(const char *s, size_t n, double *out);

int SkNmeaParser::talkerToSys(const char *t, size_t n)
{
    if (n < 2) return SKN_GPS;
    char a = t[0], b = t[1];
    if (a == 'G' && b == 'P') return SKN_GPS;
    if (a == 'G' && b == 'L') return SKN_GLO;
    if (a == 'G' && b == 'A') return SKN_GAL;
    if (a == 'B' && b == 'D') return SKN_BDS;   /* BD = 北斗 */
    if (a == 'G' && b == 'B') return SKN_BDS;   /* GB = 北斗（部分接收机） */
    /* GN = 多系统混合：默认归 GPS 着色（GN GSV 不携带系统信息）。 */
    return SKN_GPS;
}

bool SkNmeaParser::parseCoord(const char *v, size_t n, char dir, double *out)
{
    double val;
    if (!parseDouble(v, n, &val)) return false;
    double deg = std::floor(val / 100.0);
    double min = val - deg * 100.0;
    *out = deg + min / 60.0;
    if (dir == 'S' || dir == 'W') *out = -*out;
    return true;
}

bool parseDouble(const char *s, size_t n, double *out)
{
    *out = 0.0;
    if (!s || n == 0) return false;
    size_t i = 0;
    while (i < n && std::isspace((unsigned char)s[i])) i++;
    if (i >= n) return false;
    char buf[32];
    size_t k = 0;
    while (i < n && k + 1 < sizeof(buf)) {
        char c = s[i++];
        if (c == ',' || c == '*' || c == '\r' || c == '\n') break;
        buf[k++] = c;
    }
    if (k == 0) return false;
    buf[k] = '\0';
    char *end = nullptr;
    double d = strtod(buf, &end);
    if (end == buf) return false;
    *out = d;
    return true;
}

/* 把 body（从句子类型之后、到 '*' 或结尾）按逗号切成字段。 */
static void tokenize(const char *p, const char *end, std::vector<std::string> &out)
{
    out.clear();
    const char *seg = p;
    while (p <= end) {
        if (p == end || *p == ',') {
            out.emplace_back(seg, (size_t)(p - seg));
            seg = p + 1;
        }
        p++;
    }
}

/* 取第 i 个字段的 (ptr,len)，越界返回 (NULL,0)。 */
static const char *field(const std::vector<std::string> &f, size_t i, size_t *n)
{
    if (i >= f.size()) { *n = 0; return nullptr; }
    *n = f[i].size();
    return f[i].data();
}

/* 便捷：直接解析第 i 个字段为 double。
 * ★ 注意：不能写成 parseDouble(field(f,i,&n), n, out) —— 两个实参对 n 的
 *   "写(field 经指针)" 与 "读(第二实参)" 在单条调用中是 unsequenced，编译器可能
 *   先读未初始化的 n（拿到垃圾值）再让 field 写它，导致长度错乱、数值被截断。
 *   分两步：先让 field 把长度写进 n，再把 n 传给 parseDouble，保证定序。 */
static bool parseFieldD(const std::vector<std::string> &f, size_t i, double *out)
{
    size_t n = 0;
    const char *p = field(f, i, &n);
    return parseDouble(p, n, out);
}

/* ------------------------------------------------------------------ */
/* 公开接口                                                            */
/* ------------------------------------------------------------------ */

SkNmeaParser::SkNmeaParser()
{
    reset();
}

void SkNmeaParser::reset()
{
    memset(&m_fix, 0, sizeof(m_fix));
    m_sats.clear();
    m_idx.clear();
    m_gsv.clear();
    m_lines = 0;
    m_bad = 0;
}

bool SkNmeaParser::feed(const char *line, size_t len)
{
    if (!line || len == 0) return false;
    const char *dollar = nullptr;
    for (size_t i = 0; i < len; i++)
        if (line[i] == '$') { dollar = line + i; break; }
    if (!dollar) return false;
    const char *end = line + len;
    const char *star = nullptr;
    for (const char *p = dollar + 1; p < end; p++)
        if (*p == '*') { star = p; break; }
    const char *bodyEnd = star ? star : end;
    if (star && star + 3 <= end) {
        unsigned char cks = 0;
        for (const char *p = dollar + 1; p < star; p++)
            cks ^= (unsigned char)*p;
        int hi = hex1(star[1]), lo = hex1(star[2]);
        if (hi < 0 || lo < 0 || ((hi << 4) | lo) != cks) {
            m_bad++;
            return false;
        }
    }

    const char *hdr = dollar + 1;
    size_t hdrLeft = (size_t)(bodyEnd - hdr);
    if (hdrLeft < 5) return false;
    int sys = talkerToSys(hdr, 2);
    const char *type = hdr + 2;
    bool isGGA = (std::strncmp(type, "GGA", 3) == 0);
    bool isRMC = (std::strncmp(type, "RMC", 3) == 0);
    bool isGSV = (std::strncmp(type, "GSV", 3) == 0);
    bool isGSA = (std::strncmp(type, "GSA", 3) == 0);
    if (!isGGA && !isRMC && !isGSV && !isGSA) return false;

    m_lines++;
    /* body 从「句子类型后的逗号之后」开始（hdr 已越过 talker(2)+type(3)，
     * 再 +1 跳过逗号），这样 field[0] 才是第一真实字段（时间/总句数/模式…）。
     * 漏掉这 1 位会把逗号当 field[0]，后面所有字段整体 +1 偏移。 */
    const char *body = hdr + 6;

    if (isGGA) return handleGGA(body, bodyEnd, sys);
    if (isRMC) return handleRMC(body, bodyEnd, sys);
    if (isGSV) return handleGSV(body, bodyEnd, sys);
    if (isGSA) return handleGSA(body, bodyEnd, sys);
    return false;
}

/* ------------------------------------------------------------------ */
/* 各句子处理                                                          */
/* ------------------------------------------------------------------ */

bool SkNmeaParser::handleGGA(const char *body, const char *end, int sys)
{
    std::vector<std::string> f;
    tokenize(body, end, f);
    size_t n1, n2, n3, n4;
    const char *la    = field(f, 1, &n1);
    const char *laDir = field(f, 2, &n2);
    const char *lo    = field(f, 3, &n3);
    const char *loDir = field(f, 4, &n4);
    double quality = 0; parseFieldD(f, 5, &quality);
    double nsat    = 0; parseFieldD(f, 6, &nsat);
    double alt     = 0; parseFieldD(f, 8, &alt);

    double lat = 0, lon = 0;
    bool gotPos = false;
    if (la && laDir && parseCoord(la, n1, laDir[0], &lat)) {
        if (lo && loDir && parseCoord(lo, n3, loDir[0], &lon))
            gotPos = true;
    }
    if (gotPos) {
        m_fix.lat = lat; m_fix.lon = lon; m_fix.alt_m = alt;
        if (quality > 0) m_fix.valid = true;
    }
    m_fix.quality = (int)quality;
    if (nsat > 0) m_fix.nsats_view = (int)nsat;
    (void)sys;
    return gotPos;
}

bool SkNmeaParser::handleRMC(const char *body, const char *end, int sys)
{
    std::vector<std::string> f;
    tokenize(body, end, f);
    size_t n1, n2, n3;
    const char *st    = field(f, 1, &n1);   /* [1]=状态 A/V */
    const char *la    = field(f, 2, &n2);   /* [2]=纬度 */
    const char *laDir = field(f, 3, &n3);   /* [3]=纬度方向 N/S */
    double spd = 0; parseFieldD(f, 6, &spd);  /* [6]=对地速度 knots */
    double crs = 0; parseFieldD(f, 7, &crs);  /* [7]=对地航向 */

    bool active = (st && n1 > 0 && (st[0] == 'A' || st[0] == 'a'));
    double lat = 0, lon = 0;
    bool gotPos = false;
    if (active && la && laDir && parseCoord(la, n2, laDir[0], &lat)) {
        size_t n4, n5;
        const char *lo2 = field(f, 4, &n4);     /* [4]=经度 */
        const char *loDir2 = field(f, 5, &n5);  /* [5]=经度方向 E/W */
        if (lo2 && loDir2 && parseCoord(lo2, n4, loDir2[0], &lon))
            gotPos = true;
    }
    if (gotPos) {
        m_fix.lat = lat; m_fix.lon = lon;
        m_fix.speed_ms = spd * 0.514444;   /* 1 knot = 0.514444 m/s */
        m_fix.course_deg = crs;
        m_fix.valid = true;
    }
    (void)sys;
    return gotPos;
}

bool SkNmeaParser::handleGSV(const char *body, const char *end, int sys)
{
    std::vector<std::string> f;
    tokenize(body, end, f);
    double dt = 0, dn = 0;
    parseFieldD(f, 0, &dt);
    parseFieldD(f, 1, &dn);
    int total = (int)dt, num = (int)dn;
    if (total <= 0) total = 1;
    if (num <= 0) num = 1;

    GsvBuf &gb = m_gsv[sys];
    if (num == 1) gb.sats.clear();
    gb.total = total; gb.num = num; gb.sys = sys;

    /* 从字段 3 起，每 4 个一组：PRN, elev, az, snr。
     * NMEA GSV 字段布局（body 已去掉 "GPGSV," 头）：
     *   [0]=本组总句数 [1]=当前第几句 [2]=视野内星数
     *   [3]=SV1 PRN [4]=SV1 仰角 [5]=SV1 方位 [6]=SV1 SNR
     *   [7]=SV2 PRN [8]=SV2 仰角 …… 即卫星数据从字段 3 开始。 */
    for (int i = 3; i + 3 < (int)f.size(); i += 4) {
        double prn = 0; parseFieldD(f, (size_t)i + 0, &prn);
        double el = 0;  parseFieldD(f, (size_t)i + 1, &el);
        double az = 0;  parseFieldD(f, (size_t)i + 2, &az);
        double snr = 0; parseFieldD(f, (size_t)i + 3, &snr);
        if (prn <= 0) continue;
        SkNmeaSat s;
        s.prn = (int)prn; s.system = sys; s.elevation = el; s.azimuth = az;
        s.snr = snr; s.in_use = false;
        gb.sats.push_back(s);
    }

    if (num < total) return false;     /* 整组未收齐 */

    for (const SkNmeaSat &s : gb.sats) {
        uint64_t key = ((uint64_t)s.system << 32) | (uint64_t)(uint32_t)s.prn;
        auto it = m_idx.find(key);
        if (it == m_idx.end()) {
            m_idx[key] = m_sats.size();
            m_sats.push_back(s);
        } else {
            m_sats[it->second] = s;
        }
    }
    gb.sats.clear();
    if ((int)m_sats.size() > m_fix.nsats_view)
        m_fix.nsats_view = (int)m_sats.size();
    return true;
}

bool SkNmeaParser::handleGSA(const char *body, const char *end, int sys)
{
    std::vector<std::string> f;
    tokenize(body, end, f);
    double mode2 = 0; parseFieldD(f, 1, &mode2);
    m_fix.mode = (int)mode2;
    if (m_fix.mode >= 2) m_fix.valid = true;

    for (SkNmeaSat &s : m_sats) s.in_use = false;
    int used = 0;
    for (int i = 2; i <= 13 && i < (int)f.size(); i++) {
        double prn = 0; parseFieldD(f, (size_t)i, &prn);
        if (prn <= 0) continue;
        used++;
        int iprn = (int)prn;
        for (SkNmeaSat &s : m_sats) {
            if (s.prn == iprn && (sys == SKN_GPS || s.system == sys)) {
                s.in_use = true; break;
            }
        }
    }
    m_fix.nsats_used = used;
    return true;
}
