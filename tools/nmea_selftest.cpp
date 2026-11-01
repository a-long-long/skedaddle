/* NMEA-0183 解析单元自检：验证「外部定位信号接入」的解析核心。
 *
 * 纯 C++（只依赖 nmea_parser.{h,cpp}，不碰 Qt / 网络 / 窗口），可独立编译运行。
 * 覆盖：定位（RMC/GGA）、海拔、速度/航向换算、校验和拒绝坏句、星座映射
 * （GP→GPS / GL→GLO / BD→BDS / GA→GAL）、卫星计数与在用、GSV 缺方位回退。
 * 退出码 0 = 全过；非 0 = 有失败（stderr 打印细节）。regress 看标记 NMEA-OK。
 *
 * ★ 为什么单独一个可执行：解析正确性是正交关注，和整 App 的 --ui-test 不同轴——
 *   --ui-test 验"按钮→浮层"交互链，本工具验"NMEA 字节→定位/卫星"这条数据链。
 */
#include "ui_qt/nmea_parser.h"

#include <cstdio>
#include <cstring>
#include <string>

/* 给一句 NMEA「正文」（不含 '$' 与 '*XX'）算校验和并拼成完整句子。 */
static std::string mk_nmea(const char *body)
{
    unsigned char c = 0;
    for (const char *p = body; *p; p++) c ^= (unsigned char)*p;
    char tail[8];
    snprintf(tail, sizeof tail, "*%02X", c);
    return std::string("$") + body + tail;
}

/* 故意造一句校验和错误的句子（正文有效，但 checksum 改成 FF）。 */
static std::string mk_bad_nmea(const char *body)
{
    return std::string("$") + body + std::string("*FF");
}

static bool approx(double a, double b, double tol)
{
    double d = a - b; if (d < 0) d = -d;
    return d <= tol;
}

int main()
{
    int fail = 0;
    auto chk = [&](bool ok, const char *what) {
        printf("  %-52s %s\n", what, ok ? "OK" : "FAIL");
        if (!ok) fail++;
    };

    SkNmeaParser p;

    /* ① GGA：位置 / 海拔 / 定位质量。
     * $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,* */
    std::string gga = mk_nmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    p.feed(gga);
    {
        const SkNmeaFix &f = p.fix();
        chk(f.valid, "GGA：定位有效");
        chk(approx(f.lat, 48.0 + 7.038 / 60.0, 1e-4), "GGA：纬度解析正确");
        chk(approx(f.lon, 11.0 + 31.000 / 60.0, 1e-4), "GGA：经度解析正确");
        chk(approx(f.alt_m, 545.4, 1e-6), "GGA：海拔解析正确");
        chk(f.quality == 1, "GGA：定位质量 = 1");
        chk(f.nsats_view == 8, "GGA：视野内星数 = 8");
    }

    /* ② RMC：状态 A → 位置；速度 knots→m/s；航向。
     * $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W* */
    std::string rmc = mk_nmea("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
    p.feed(rmc);
    {
        const SkNmeaFix &f = p.fix();
        chk(f.valid, "RMC：状态 A → 定位有效");
        chk(approx(f.lat, 48.0 + 7.038 / 60.0, 1e-4), "RMC：纬度解析正确");
        chk(approx(f.lon, 11.0 + 31.000 / 60.0, 1e-4), "RMC：经度解析正确");
        chk(approx(f.speed_ms, 22.4 * 0.514444, 1e-3), "RMC：速度 knots→m/s 换算正确");
        chk(approx(f.course_deg, 84.4, 1e-6), "RMC：航向解析正确");
    }

    /* ③ RMC 状态 V（无效）→ 不刷新定位（保留 GGA 给的有效位）。 */
    std::string rmcV = mk_nmea("GPRMC,123519,V,9999.999,N,99999.999,E,000.0,000.0,230394,,,");
    p.feed(rmcV);
    {
        const SkNmeaFix &f = p.fix();
        chk(f.valid, "RMC(V)：不覆盖已有有效定位");
        chk(approx(f.lat, 48.0 + 7.038 / 60.0, 1e-4), "RMC(V)：有效纬度仍保留");
    }

    /* ④ 校验和错误：应被拒、不刷新、linesBadCksum 计数 +1。 */
    int badBefore = p.linesBadCksum();
    std::string bad = mk_bad_nmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    bool fed = p.feed(bad);
    chk(!fed, "坏校验和：feed 返回 false");
    chk(p.linesBadCksum() == badBefore + 1, "坏校验和：linesBadCksum 计数 +1");

    /* ⑤ GSV 多段（GPS）：两段共 8 颗，系统归 GPS。
     * $GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*
     * $GPGSV,2,2,08,03,03,111,00,04,15,270,00,06,01,047,00,13,06,292,00* */
    p.feed(mk_nmea("GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45"));
    p.feed(mk_nmea("GPGSV,2,2,08,03,03,111,00,04,15,270,00,06,01,047,00,13,06,292,00"));
    {
        const auto &s = p.sats();
        int gps = 0;
        for (const SkNmeaSat &x : s) if (x.system == SKN_GPS) gps++;
        chk((int)s.size() == 8, "GSV(GPS)：累计 8 颗卫星");
        chk(gps == 8, "GSV(GPS)：系统全部归 GPS");
        /* 验证其中一颗的坐标（PRN 01：仰角40 方位83 SNR46） */
        bool found = false;
        for (const SkNmeaSat &x : s)
            if (x.prn == 1 && x.system == SKN_GPS)
                found = approx(x.elevation, 40, 1e-9) && approx(x.azimuth, 83, 1e-9)
                        && approx(x.snr, 46, 1e-9);
        chk(found, "GSV(GPS)：PRN01 仰角/方位/SNR 解析正确");
    }

    /* ⑥ 星座映射：GL→GLO、BD→BDS、GA→GAL（各一段，独立累积）。 */
    p.feed(mk_nmea("GLGSV,1,1,03,71,30,100,40,72,45,200,38,73,10,050,30"));
    p.feed(mk_nmea("BDGSV,1,1,02,201,20,045,35,202,55,120,33"));
    p.feed(mk_nmea("GAGSV,1,1,02,55,33,080,25,56,12,200,20"));
    {
        const auto &s = p.sats();
        int glo = 0, bds = 0, gal = 0;
        for (const SkNmeaSat &x : s) {
            if (x.system == SKN_GLO) glo++;
            if (x.system == SKN_BDS) bds++;
            if (x.system == SKN_GAL) gal++;
        }
        chk(glo == 3, "映射：GL→GLO 3 颗");
        chk(bds == 2, "映射：BD→BDS 2 颗");
        chk(gal == 2, "映射：GA→GAL 2 颗");
        chk(bds == 2 && s.size() == 8 + 3 + 2 + 2, "卫星总数累计正确");
    }

    /* ⑦ GSA：定 3D 模式 + 标记在用星（PRN 01/02/12/14/03/04/06/13 共 8 颗 GPS）。
     * $GPGSA,A,3,01,02,12,14,03,04,06,13,,,,* */
    p.feed(mk_nmea("GPGSA,A,3,01,02,12,14,03,04,06,13,,,,"));
    {
        const SkNmeaFix &f = p.fix();
        chk(f.mode == 3, "GSA：定位模式 = 3（3D）");
        chk(f.nsats_used == 8, "GSA：在用星数 = 8");
        int inUse = 0;
        for (const SkNmeaSat &x : p.sats())
            if (x.in_use) inUse++;
        chk(inUse == 8, "GSA：8 颗 GPS 星被标记为在用");
    }

    /* ⑧ GSV 缺方位回退：某颗星方位字段为空 → 优雅取默认 0，不崩、不丢其它字段。
     * $GAGSV,1,1,01,55,080,,25  （PRN55，仰角80，方位空，SNR25） */
    p.feed(mk_nmea("GAGSV,1,1,01,55,080,,25"));
    {
        bool found = false;
        for (const SkNmeaSat &x : p.sats())
            if (x.prn == 55 && x.system == SKN_GAL)
                found = approx(x.elevation, 80, 1e-9) && approx(x.azimuth, 0.0, 1e-9)
                        && approx(x.snr, 25, 1e-9);
        chk(found, "GSV 缺方位：回退 0 且不丢仰角/SNR");
    }

    /* ⑨ 行统计：共 11 次 feed，其中坏校验和那句不计入 linesParsed
     * → 10 条有效行（GGA/RMC/RMC(V)/GSV×4/GL/BD/GA/GSA/GAGSV）。 */
    chk(p.linesParsed() == 10, "统计：有效行累计数 = 10（坏校验和不计入）");

    if (fail == 0) {
        printf("NMEA-OK\n");
        return 0;
    }
    printf("NMEA-FAIL：%d 项不符\n", fail);
    return 1;
}
