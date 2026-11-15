//! tutorial/rust · Rust 版演化链 —— 从最小框架长到迷你路网查看器
//!
//! 与 C 版 `tutorial/mini/` 同一条教学法的 Rust 重实现：
//!   - 固定骨架 + 每课一个增量段，**只增不减**；
//!   - 每步跑 0..N 全部课程并打印 RUST-MINI-STEP-NN-OK；
//!   - 末行 RUST-MINI-CHAIN-OK steps=N ink=… gates=ok/fail，退出码即门禁；
//!   - 零外部 crate（纯 std）——对应 C 版「零 SDL/Qt 依赖」。
//!
//! 2026-11-15：C 版补了二进制加载器语义校验、Router/MapSet 生命周期、
//! 算路期间冻结省份卸载、maxspeed mph/knots、下载 slug 等生产化修复。
//! 本链不复制这些状态机（无 mmap / 无 Qt / 无下载层）；逐条对照与练习见
//! `README.md` §七。
//!
//! 演化纪律（与 C 版 scripts/check_mini_chain.py 同精神，本题收进单文件）：
//!   - `// ============ stepNN: <主题> ============` 段只能**追加**；
//!   - `fn main` 里 `// @@CALL@@` 锚点之前只能**插入**一行 `step_NN(...)`；
//!   - 把本文件在各 stepNN 段边界截断，即得第 N 步的历史快照
//!     （单文件版锚点 = C 版的 stepNN 目录 + @@INCLUDE@@ 锚点）。
//!
//! 运行：cargo run            （在 tutorial/rust/ 下）
//!       cargo run --release  （判据与像素与优化级别无关，两档都必须过）
//!
//! 每一段的"Rust 语言点"注释是本课程与 C 版的核心差异：C 教你手管内存，
//! Rust 教你把同样的不变量（越界丢弃、截断报错、无悬垂）交给类型系统。

use std::cmp::Reverse;
use std::collections::BinaryHeap;
use std::fs;

const W: i32 = 320;
const H: i32 = 240;
const BG: u32 = 0xFFF4F1EA;

const COL_AXIS: u32 = 0xFF2A2A2A;
const COL_DIAG: u32 = 0xFF1F6FB0;
const COL_ROAD: u32 = 0xFF9A9A92;
const COL_NODE: u32 = 0xFF3D3D3D;
const COL_ROUTE: u32 = 0xFFD23F2E;

// =====================================================================
// step00: frame —— 像素缓冲 + 画点 + Bresenham + 24 位 BMP（最小框架）
// Rust 语言点：结构体 + &mut self（C 的 ctx 指针 → 借用检查替你保证
// 同一时间只有一个可变使用者，越界访问进 panic 而不是 UB）。
// =====================================================================
struct MiniCtx {
    buf: Vec<u32>,
    step_count: u32,
    gates_ok: u32,
    gates_fail: u32,
}

impl MiniCtx {
    fn new() -> Self {
        MiniCtx { buf: vec![0; (W * H) as usize], step_count: 0, gates_ok: 0, gates_fail: 0 }
    }

    fn cls(&mut self, color: u32) {
        for px in self.buf.iter_mut() {
            *px = color;
        }
    }

    /// 画点：边界外直接丢弃（与 C 版 put_pixel 同约定，防越界写）。
    fn put_pixel(&mut self, x: i32, y: i32, color: u32) {
        if x < 0 || x >= W || y < 0 || y >= H {
            return;
        }
        self.buf[(y * W + x) as usize] = color;
    }

    /// Bresenham 直线：raster 的最底层原子操作。
    fn draw_line(&mut self, mut x0: i32, mut y0: i32, x1: i32, y1: i32, color: u32) {
        let dx = (x1 - x0).abs();
        let dy = -(y1 - y0).abs();
        let sx: i32 = if x0 < x1 { 1 } else { -1 };
        let sy: i32 = if y0 < y1 { 1 } else { -1 };
        let mut err = dx + dy;
        loop {
            self.put_pixel(x0, y0, color);
            if x0 == x1 && y0 == y1 {
                break;
            }
            let e2 = 2 * err;
            if e2 >= dy {
                err += dy;
                x0 += sx;
            }
            if e2 <= dx {
                err += dx;
                y0 += sy;
            }
        }
    }

    /// 非背景像素数：与 C 版 mini_ink 同义的确定性量，作门禁输出。
    fn ink(&self) -> u64 {
        self.buf.iter().filter(|&&v| v != BG).count() as u64
    }

    /// 判据记账：门禁必须能失败，不是"没报错就算过"。
    fn gate(&mut self, ok: bool) {
        if ok {
            self.gates_ok += 1;
        } else {
            self.gates_fail += 1;
        }
    }

    /// 24 位 BMP：行从下往上、像素 BGR、每行 4 字节对齐。
    /// Rust 语言点：to_le_bytes + copy_from_slice —— 不允许 C 的
    /// *(uint32_t*)(hdr+2) 未对齐强转把 UB 带进来。
    fn write_bmp(&self, path: &str) -> std::io::Result<()> {
        use std::io::Write;
        let row_bytes = (W as usize) * 3;
        let pad = (4 - row_bytes % 4) % 4;
        let data_size = (row_bytes + pad) * (H as usize);
        let mut hdr = [0u8; 54];
        hdr[0] = b'B';
        hdr[1] = b'M';
        hdr[2..6].copy_from_slice(&((54 + data_size) as u32).to_le_bytes());
        hdr[10..14].copy_from_slice(&54u32.to_le_bytes());
        hdr[14..18].copy_from_slice(&40u32.to_le_bytes());
        hdr[18..22].copy_from_slice(&W.to_le_bytes());
        hdr[22..26].copy_from_slice(&H.to_le_bytes());
        hdr[26..28].copy_from_slice(&1u16.to_le_bytes());
        hdr[28..30].copy_from_slice(&24u16.to_le_bytes());
        hdr[34..38].copy_from_slice(&(data_size as u32).to_le_bytes());
        let mut f = fs::File::create(path)?;
        f.write_all(&hdr)?;
        let mut row = vec![0u8; row_bytes + pad];
        for y in (0..H).rev() {
            let mut p = 0;
            for x in 0..W {
                let v = self.buf[(y * W + x) as usize];
                row[p] = v as u8;
                row[p + 1] = (v >> 8) as u8;
                row[p + 2] = (v >> 16) as u8;
                p += 3;
            }
            f.write_all(&row)?;
        }
        Ok(())
    }
}

fn step_00(c: &mut MiniCtx) {
    c.cls(BG);
    c.draw_line(0, H / 2, W - 1, H / 2, COL_AXIS);
    c.draw_line(W / 2, 0, W / 2, H - 1, COL_AXIS);
    c.draw_line(0, 0, W - 1, H - 1, COL_DIAG);
    c.gate(c.ink() > 0); // 判据：真有像素落下
    println!("RUST-MINI-STEP-00-OK");
    c.step_count += 1;
}

// =====================================================================
// step01: coords —— 1e-7 度量化 + Web Mercator 正反算 + 经纬网
// Rust 语言点：f64↔i32 必须显式转换（as/round），无隐式截断。
// 不变量与 C 版 geo.c 相同：内部几何永远 WGS84；墨卡托只管显示不管测距。
// =====================================================================
const EARTH_R_M: f64 = 6371008.8;

/// 度 → 量化整数（1e-7 度）：落盘统一用它，规避浮点对齐/字节序问题。
fn to_e7(deg: f64) -> i32 {
    (deg * 1e7).round() as i32
}

fn from_e7(v: i32) -> f64 {
    v as f64 / 1e7
}

/// Web Mercator 正算：归一化 [0,1]²（原点左上，y 向南增大）。只用于显示。
fn mercator(lat: f64, lon: f64) -> (f64, f64) {
    let x = (lon + 180.0) / 360.0;
    let phi = lat.to_radians();
    let y = (1.0 - (phi.tan() + 1.0 / phi.cos()).ln() / std::f64::consts::PI) / 2.0;
    (x, y)
}

/// 反算：y 走 atan(sinh(·))，x 保持线性。
fn mercator_inv(wx: f64, wy: f64) -> (f64, f64) {
    let lon = wx * 360.0 - 180.0;
    let n = std::f64::consts::PI * (1.0 - 2.0 * wy);
    let lat = n.sinh().atan().to_degrees();
    (lat, lon)
}

/// 大圆距离（米）。真实距离一律用它，不用墨卡托像素距离。
fn haversine_m(lat1: f64, lon1: f64, lat2: f64, lon2: f64) -> f64 {
    let dlat = (lat2 - lat1).to_radians();
    let dlon = (lon2 - lon1).to_radians();
    let a = (dlat / 2.0).sin().powi(2)
        + lat1.to_radians().cos() * lat2.to_radians().cos() * (dlon / 2.0).sin().powi(2);
    2.0 * EARTH_R_M * a.sqrt().asin()
}

/// 线性经纬度 → 像素（step04 建图渲染用；step05 起换墨卡托视口投影）。
fn ll_to_px_linear(lat: f64, lon: f64) -> (i32, i32) {
    const LAT0: f64 = 39.90;
    const LAT1: f64 = 40.02;
    const LON0: f64 = 116.33;
    const LON1: f64 = 116.44;
    let fx = (lon - LON0) / (LON1 - LON0);
    let fy = (lat - LAT0) / (LAT1 - LAT0);
    let x = (8.0 + fx * ((W - 16) as f64)).round() as i32;
    let y = ((H - 8) as f64 - fy * ((H - 16) as f64)).round() as i32;
    (x, y)
}

fn step_01(c: &mut MiniCtx) {
    // 判据 1：1e7 量化往返——误差必须 < 半个 LSB（5e-8 度），且整数域逐位可逆
    let (la, lo) = (39.904_217, 116.407_417);
    let q = to_e7(la);
    c.gate((from_e7(q) - la).abs() < 5e-8);
    c.gate(to_e7(from_e7(q)) == q);
    // 判据 2：墨卡托正反算往返 < 1e-9 度
    let (mx, my) = mercator(la, lo);
    let (la2, lo2) = mercator_inv(mx, my);
    c.gate((la2 - la).abs() < 1e-9 && (lo2 - lo).abs() < 1e-9);
    // 经纬网（bbox 内每 0.03° 纬线 / 0.02° 经线；整数循环避免浮点累加漂移）
    for i in 0..=4 {
        let lat = 39.90 + 0.03 * i as f64;
        let (x0, y) = ll_to_px_linear(lat, 116.33);
        let (x1, _) = ll_to_px_linear(lat, 116.44);
        c.draw_line(x0, y, x1, y, 0xFFC8C0B0);
    }
    for i in 0..=5 {
        let lon = 116.33 + 0.02 * i as f64;
        let (x, y0) = ll_to_px_linear(39.90, lon);
        let (_, y1) = ll_to_px_linear(40.02, lon);
        c.draw_line(x, y0, x, y1, 0xFFC8C0B0);
    }
    println!("RUST-MINI-STEP-01-OK");
    c.step_count += 1;
}

// =====================================================================
// step02: pbf —— PBF 线格式原子：varint + 字段号 + zigzag（自编自解）
// 真正的 OSM PBF = BlobHeader/Blob + PrimitiveBlock（容器层还有 zlib），
// 但其线格式原子就是这里的三件套；zlib 见练习。
// Rust 语言点：切片 slice + Result——截断/越界返回 Err 而不是 UB。
// =====================================================================
fn varint_encode(mut v: u64, out: &mut Vec<u8>) {
    loop {
        let b = (v & 0x7F) as u8;
        v >>= 7;
        if v == 0 {
            out.push(b);
            break;
        }
        out.push(b | 0x80);
    }
}

fn varint_decode(buf: &[u8], pos: &mut usize) -> Result<u64, String> {
    let mut v: u64 = 0;
    let mut shift = 0;
    loop {
        if *pos >= buf.len() {
            return Err("varint truncated".into());
        }
        let b = buf[*pos];
        *pos += 1;
        v |= ((b & 0x7F) as u64) << shift;
        if b & 0x80 == 0 {
            return Ok(v);
        }
        shift += 7;
        if shift >= 64 {
            return Err("varint too long".into());
        }
    }
}

/// zigzag：把有符号数折叠进无符号 varint（PBF 的 lat/lon 用 sint）。
fn zigzag(v: i64) -> u64 {
    ((v << 1) ^ (v >> 63)) as u64
}

fn unzigzag(v: u64) -> i64 {
    ((v >> 1) as i64) ^ -((v & 1) as i64)
}

fn step_02(c: &mut MiniCtx) {
    // 编一条"迷你 dense node"消息：field1=id varint, field2=lat zigzag, field3=lon zigzag
    // protobuf 线格式键 = (field_number << 3) | wire_type；varint 的 wire_type = 0
    let id: u64 = 42;
    let (lat_e7, lon_e7): (i64, i64) = (399_042_170, 1_164_074_170);
    let mut msg = Vec::new();
    msg.push(1 << 3);
    varint_encode(id, &mut msg);
    msg.push(2 << 3);
    varint_encode(zigzag(lat_e7), &mut msg);
    msg.push(3 << 3);
    varint_encode(zigzag(lon_e7), &mut msg);

    // 解回来：字段号驱动，未知字段号判失败（真实 PBF 里是跳过，练习里改）
    let mut pos = 0usize;
    let mut got = (0u64, 0i64, 0i64);
    let mut ok = true;
    while pos < msg.len() {
        let key = match varint_decode(&msg, &mut pos) {
            Ok(v) => v,
            Err(_) => {
                ok = false;
                break;
            }
        };
        let v = match varint_decode(&msg, &mut pos) {
            Ok(v) => v,
            Err(_) => {
                ok = false;
                break;
            }
        };
        match key >> 3 {
            1 => got.0 = v,
            2 => got.1 = unzigzag(v),
            3 => got.2 = unzigzag(v),
            _ => ok = false,
        }
    }
    c.gate(ok);
    c.gate(got.0 == id && got.1 == lat_e7 && got.2 == lon_e7);
    // 判据：zigzag 往返（含负数）
    c.gate(unzigzag(zigzag(-1_234_567)) == -1_234_567);
    // 画：把消息的每个字节画成竖条（高度=字节值），给"线格式"一个可见形态
    for (i, &b) in msg.iter().enumerate() {
        let x = 20 + i as i32 * 6;
        let h = (b as i32 % 60) + 8;
        c.draw_line(x, H - 20, x, H - 20 - h, 0xFF7A9E7E);
    }
    println!("RUST-MINI-STEP-02-OK");
    c.step_count += 1;
}

// =====================================================================
// step03: binary —— 自描述二进制：magic + section 表 + 小端定长记录
// 与 C 版第 3 课同思路：文件 = magic + 段表(id,off,len) + 载荷。
// Rust 语言点：Vec<u8> 手工序列化、fs 读写、Result 传播、try_into 切片转数组。
// =====================================================================
const MAGIC: &[u8; 4] = b"SKRB";
const SEC_NODES: u32 = 1;
const SEC_EDGES: u32 = 2;
const SEC_NAMES: u32 = 3;

/// 样例路网：北京北部 10 个路口（经纬度，WGS84）。
const NODES: [(f64, f64); 10] = [
    (40.0050, 116.3500),
    (40.0120, 116.3830),
    (39.9960, 116.3920),
    (39.9880, 116.4130),
    (39.9700, 116.3980),
    (39.9580, 116.4180),
    (39.9420, 116.4300),
    (39.9650, 116.3700),
    (39.9350, 116.3950),
    (39.9200, 116.4150),
];
const NODE_NAMES: [&str; 10] =
    ["路口A", "路口B", "路口C", "路口D", "路口E", "路口F", "路口G", "路口H", "路口I", "路口J"];
const EDGES: [(usize, usize); 14] = [
    (0, 1),
    (1, 2),
    (2, 3),
    (3, 5),
    (5, 6),
    (0, 7),
    (7, 4),
    (4, 8),
    (8, 9),
    (4, 5),
    (3, 4),
    (1, 3),
    (2, 4),
    (8, 6),
];

/// 边权 = haversine 取整（真实工程里可再乘道路等级系数）。
fn edge_weight(a: usize, b: usize) -> u32 {
    let (la, lo) = NODES[a];
    haversine_m(la, lo, NODES[b].0, NODES[b].1).round() as u32
}

fn push_u32(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_le_bytes());
}

fn push_i32(out: &mut Vec<u8>, v: i32) {
    out.extend_from_slice(&v.to_le_bytes());
}

fn build_graph_bytes() -> Vec<u8> {
    let mut nodes_sec = Vec::new();
    for &(la, lo) in NODES.iter() {
        push_i32(&mut nodes_sec, to_e7(la));
        push_i32(&mut nodes_sec, to_e7(lo));
    }
    let mut edges_sec = Vec::new();
    for &(a, b) in EDGES.iter() {
        push_u32(&mut edges_sec, a as u32);
        push_u32(&mut edges_sec, b as u32);
        push_u32(&mut edges_sec, edge_weight(a, b));
    }
    let mut names_sec = Vec::new();
    for s in NODE_NAMES.iter() {
        push_u32(&mut names_sec, s.len() as u32);
        names_sec.extend_from_slice(s.as_bytes());
    }
    let secs = [(SEC_NODES, &nodes_sec), (SEC_EDGES, &edges_sec), (SEC_NAMES, &names_sec)];
    let mut out = Vec::new();
    out.extend_from_slice(MAGIC);
    push_u32(&mut out, 1); // version
    push_u32(&mut out, secs.len() as u32);
    let mut payload_off = 4 + 4 + 4 + secs.len() * 12;
    for &(id, body) in secs.iter() {
        push_u32(&mut out, id);
        push_u32(&mut out, payload_off as u32);
        push_u32(&mut out, body.len() as u32);
        payload_off += body.len();
    }
    for &(_, body) in secs.iter() {
        out.extend_from_slice(body);
    }
    out
}

/// 解析产物：nodes（1e7 度）/ names / edges(a,b,w)。
struct Graph {
    nodes_e7: Vec<(i32, i32)>,
    names: Vec<String>,
    edges: Vec<(u32, u32, u32)>,
}

fn parse_graph_bytes(buf: &[u8]) -> Result<Graph, String> {
    if buf.len() < 12 || &buf[0..4] != MAGIC {
        return Err("bad magic".into());
    }
    let rd_u32 = |p: &mut usize| -> Result<u32, String> {
        if *p + 4 > buf.len() {
            return Err("truncated".into());
        }
        let v = u32::from_le_bytes(buf[*p..*p + 4].try_into().unwrap());
        *p += 4;
        Ok(v)
    };
    let mut p = 4usize;
    let ver = rd_u32(&mut p)?;
    if ver != 1 {
        return Err("unsupported version".into());
    }
    let n_sec = rd_u32(&mut p)?;
    let mut secs = Vec::new();
    for _ in 0..n_sec {
        let id = rd_u32(&mut p)?;
        let off = rd_u32(&mut p)? as usize;
        let len = rd_u32(&mut p)? as usize;
        if off + len > buf.len() {
            return Err("section out of range".into());
        }
        secs.push((id, off, len));
    }
    let mut g = Graph { nodes_e7: Vec::new(), names: Vec::new(), edges: Vec::new() };
    for (id, off, len) in secs {
        let body = &buf[off..off + len];
        match id {
            SEC_NODES => {
                let mut q = 0;
                while q + 8 <= body.len() {
                    let la = i32::from_le_bytes(body[q..q + 4].try_into().unwrap());
                    let lo = i32::from_le_bytes(body[q + 4..q + 8].try_into().unwrap());
                    g.nodes_e7.push((la, lo));
                    q += 8;
                }
            }
            SEC_EDGES => {
                let mut q = 0;
                while q + 12 <= body.len() {
                    let a = u32::from_le_bytes(body[q..q + 4].try_into().unwrap());
                    let b = u32::from_le_bytes(body[q + 4..q + 8].try_into().unwrap());
                    let w = u32::from_le_bytes(body[q + 8..q + 12].try_into().unwrap());
                    g.edges.push((a, b, w));
                    q += 12;
                }
            }
            SEC_NAMES => {
                let mut q = 0;
                while q + 4 <= body.len() {
                    let l = u32::from_le_bytes(body[q..q + 4].try_into().unwrap()) as usize;
                    q += 4;
                    if q + l > body.len() {
                        return Err("name out of range".into());
                    }
                    g.names.push(String::from_utf8_lossy(&body[q..q + l]).into_owned());
                    q += l;
                }
            }
            _ => {}
        }
    }
    Ok(g)
}

fn step_03(c: &mut MiniCtx) -> Graph {
    let bytes = build_graph_bytes();
    c.gate(fs::write("mini_graph.bin", &bytes).is_ok());
    let read_back = fs::read("mini_graph.bin").unwrap_or_default();
    let g = match parse_graph_bytes(&read_back) {
        Ok(g) => g,
        Err(e) => {
            eprintln!("parse: {e}");
            Graph { nodes_e7: Vec::new(), names: Vec::new(), edges: Vec::new() }
        }
    };
    c.gate(g.nodes_e7.len() == NODES.len());
    c.gate(g.edges.len() == EDGES.len());
    c.gate(g.names.len() == NODE_NAMES.len());
    // 判据：落盘边权与重算 haversine 一致（±2m 来自取整）
    let w_ok = g.edges.iter().all(|&(a, b, w)| {
        (w as f64 - edge_weight(a as usize, b as usize) as f64).abs() <= 2.0
    });
    c.gate(w_ok);
    // 负向判据：魔数被破坏必须被拒收（证明解析器真有判别力）
    let mut bad = bytes.clone();
    bad[0] = b'X';
    c.gate(parse_graph_bytes(&bad).is_err());
    println!("RUST-MINI-STEP-03-OK");
    c.step_count += 1;
    g
}

// =====================================================================
// step04: graph —— 前向星（CSR）建图 + 度数 + BFS 可达性 + 画路网
// Rust 语言点：迭代器与切片（windows/iter/all）；VecDeque BFS。
// =====================================================================
struct Csr {
    off: Vec<u32>,        // len = n + 1
    adj: Vec<(u32, u32)>, // (to, w_m)；无向图 ⇒ 每条边存两份
}

impl Csr {
    fn build(n: usize, edges: &[(u32, u32, u32)]) -> Csr {
        let mut deg = vec![0u32; n];
        for &(a, b, _) in edges {
            deg[a as usize] += 1;
            deg[b as usize] += 1;
        }
        let mut off = Vec::with_capacity(n + 1);
        let mut acc = 0u32;
        for &d in deg.iter() {
            off.push(acc);
            acc += d;
        }
        off.push(acc);
        let mut cur: Vec<u32> = off[..n].to_vec();
        let mut adj = vec![(0u32, 0u32); edges.len() * 2];
        for &(a, b, w) in edges {
            let (a, b) = (a as usize, b as usize);
            adj[cur[a] as usize] = (b as u32, w);
            cur[a] += 1;
            adj[cur[b] as usize] = (a as u32, w);
            cur[b] += 1;
        }
        Csr { off, adj }
    }

    fn neighbors(&self, u: usize) -> &[(u32, u32)] {
        &self.adj[self.off[u] as usize..self.off[u + 1] as usize]
    }

    fn bfs_reachable(&self, src: usize) -> Vec<bool> {
        let n = self.off.len() - 1;
        let mut seen = vec![false; n];
        let mut queue = std::collections::VecDeque::new();
        seen[src] = true;
        queue.push_back(src);
        while let Some(u) = queue.pop_front() {
            for &(v, _) in self.neighbors(u) {
                let v = v as usize;
                if !seen[v] {
                    seen[v] = true;
                    queue.push_back(v);
                }
            }
        }
        seen
    }
}

fn step_04(c: &mut MiniCtx, g: &Graph) {
    let csr = Csr::build(g.nodes_e7.len(), &g.edges);
    c.gate(csr.adj.len() == g.edges.len() * 2); // 无向图：每边双份
    let seen = csr.bfs_reachable(0);
    c.gate(seen.iter().all(|&s| s)); // 样例路网必须连通
    // 画路网（线性投影；step05 起换视口投影）
    for &(a, b, _) in g.edges.iter() {
        let (x0, y0) =
            ll_to_px_linear(from_e7(g.nodes_e7[a as usize].0), from_e7(g.nodes_e7[a as usize].1));
        let (x1, y1) =
            ll_to_px_linear(from_e7(g.nodes_e7[b as usize].0), from_e7(g.nodes_e7[b as usize].1));
        c.draw_line(x0, y0, x1, y1, COL_ROAD);
    }
    for &(la, lo) in g.nodes_e7.iter() {
        let (x, y) = ll_to_px_linear(from_e7(la), from_e7(lo));
        for dy in -1..=1 {
            for dx in -1..=1 {
                c.put_pixel(x + dx, y + dy, COL_NODE);
            }
        }
    }
    println!("RUST-MINI-STEP-04-OK");
    c.step_count += 1;
}

// =====================================================================
// step05: viewport —— 视口（中心 + 经度跨度）；墨卡托 → 屏幕投影
// Rust 语言点：方法语法；坐标变换写成纯函数，判据才能逐条核对。
// 归一化墨卡托 y 向南增大，与屏幕 y 向下同向 ⇒ 无需翻转。
// =====================================================================
struct Viewport {
    clat: f64,
    clon: f64,
    span_lon: f64,
}

impl Viewport {
    fn to_screen(&self, lat: f64, lon: f64) -> (i32, i32) {
        let (mx, my) = mercator(lat, lon);
        let (cx, cy) = mercator(self.clat, self.clon);
        let scale = (W as f64) / (self.span_lon / 360.0); // px / mercator-x 单位
        let x = ((mx - cx) * scale + (W as f64) / 2.0).round() as i32;
        let y = ((my - cy) * scale + (H as f64) / 2.0).round() as i32;
        (x, y)
    }
}

fn default_viewport() -> Viewport {
    Viewport { clat: 39.965, clon: 116.390, span_lon: 0.12 }
}

fn step_05(c: &mut MiniCtx, g: &Graph) {
    let vp = default_viewport();
    // 判据 1：中心必投到画布正中
    let (hx, hy) = vp.to_screen(vp.clat, vp.clon);
    c.gate(hx == W / 2 && hy == H / 2);
    // 判据 2：东边的点投到中心右侧、北边的点投到中心上方
    let (ex, _) = vp.to_screen(vp.clat, vp.clon + 0.01);
    c.gate(ex > hx);
    let (_, ny) = vp.to_screen(vp.clat + 0.01, vp.clon);
    c.gate(ny < hy);
    // 用视口投影重画一遍节点（3px 蓝点），与 step04 的线性投影做形态对照
    for &(la, lo) in g.nodes_e7.iter() {
        let (x, y) = vp.to_screen(from_e7(la), from_e7(lo));
        c.put_pixel(x, y, COL_DIAG);
        c.put_pixel(x + 1, y, COL_DIAG);
        c.put_pixel(x, y + 1, COL_DIAG);
    }
    println!("RUST-MINI-STEP-05-OK");
    c.step_count += 1;
}

// =====================================================================
// step06: raster —— Cohen–Sutherland 线段裁剪
// Rust 语言点：模式匹配 + Option——"可能没有结果"进类型系统，
// 调用方被迫处理"完全在画布外"的分支（C 版靠返回值约定）。
// =====================================================================
const INS: u8 = 0;
const OUT_L: u8 = 1;
const OUT_R: u8 = 2;
const OUT_T: u8 = 4; // 屏幕 y 向下：y<0 在上
const OUT_B: u8 = 8; // y>=H 在下

fn out_code(x: i32, y: i32) -> u8 {
    let mut code = INS;
    if x < 0 {
        code |= OUT_L;
    } else if x >= W {
        code |= OUT_R;
    }
    if y < 0 {
        code |= OUT_T;
    } else if y >= H {
        code |= OUT_B;
    }
    code
}

fn clip_line(x0: i32, y0: i32, x1: i32, y1: i32) -> Option<(i32, i32, i32, i32)> {
    let (mut ax, mut ay, mut bx, mut by) = (x0 as f64, y0 as f64, x1 as f64, y1 as f64);
    for _ in 0..8 {
        let ca = out_code(ax as i32, ay as i32);
        let cb = out_code(bx as i32, by as i32);
        if ca | cb == 0 {
            return Some((
                ax.round() as i32,
                ay.round() as i32,
                bx.round() as i32,
                by.round() as i32,
            ));
        }
        if ca & cb != 0 {
            return None; // 完全在同一越界侧 ⇒ 全部丢弃
        }
        let outside = if ca != 0 { ca } else { cb };
        let (dx, dy) = (bx - ax, by - ay);
        // P(t) = a + t*(b-a)；按越界的那条边解 t
        let (nx, ny) = if outside & OUT_T != 0 {
            let t = (0.0 - ay) / dy;
            (ax + t * dx, 0.0)
        } else if outside & OUT_B != 0 {
            let t = ((H - 1) as f64 - ay) / dy;
            (ax + t * dx, (H - 1) as f64)
        } else if outside & OUT_L != 0 {
            let t = (0.0 - ax) / dx;
            (0.0, ay + t * dy)
        } else {
            let t = ((W - 1) as f64 - ax) / dx;
            ((W - 1) as f64, ay + t * dy)
        };
        if outside == ca {
            ax = nx;
            ay = ny;
        } else {
            bx = nx;
            by = ny;
        }
    }
    None
}

fn step_06(c: &mut MiniCtx, g: &Graph) {
    // 判据 1：完全在画布外的线段必被丢弃
    c.gate(clip_line(-100, -100, -10, -50).is_none());
    // 判据 2：横穿左边界 ⇒ 裁剪后起点 x == 0、y 不变
    let (x0, y0, _, _) = clip_line(-40, 120, 200, 120).unwrap();
    c.gate(x0 == 0 && y0 == 120);
    // 视口 + 裁剪重画全部边——收敛到真实地图渲染的路径
    let vp = default_viewport();
    for &(a, b, _) in g.edges.iter() {
        let (x0, y0) = vp.to_screen(from_e7(g.nodes_e7[a as usize].0), from_e7(g.nodes_e7[a as usize].1));
        let (x1, y1) = vp.to_screen(from_e7(g.nodes_e7[b as usize].0), from_e7(g.nodes_e7[b as usize].1));
        if let Some((cx0, cy0, cx1, cy1)) = clip_line(x0, y0, x1, y1) {
            c.draw_line(cx0, cy0, cx1, cy1, COL_ROAD);
        }
    }
    println!("RUST-MINI-STEP-06-OK");
    c.step_count += 1;
}

// =====================================================================
// step07: routing —— Dijkstra + 路径回溯 + 画路线
// Rust 语言点：BinaryHeap + Reverse 组成最小堆；图只读借用（&Graph），
// 结果独立分配——所有权的边界就是数据的边界。
// =====================================================================
fn dijkstra(g: &Graph, csr: &Csr, src: usize) -> (Vec<u32>, Vec<Option<usize>>) {
    let n = g.nodes_e7.len();
    let mut dist = vec![u32::MAX; n];
    let mut prev: Vec<Option<usize>> = vec![None; n];
    let mut heap = BinaryHeap::new();
    dist[src] = 0;
    heap.push(Reverse((0u32, src)));
    while let Some(Reverse((d, u))) = heap.pop() {
        if d > dist[u] {
            continue; // 懒删除：过期堆项直接跳过
        }
        for &(v, w) in csr.neighbors(u) {
            let v = v as usize;
            let nd = d + w;
            if nd < dist[v] {
                dist[v] = nd;
                prev[v] = Some(u);
                heap.push(Reverse((nd, v)));
            }
        }
    }
    (dist, prev)
}

fn step_07(c: &mut MiniCtx, g: &Graph) {
    let csr = Csr::build(g.nodes_e7.len(), &g.edges);
    let src = 0usize;
    let dst = 6usize;
    let (dist, prev) = dijkstra(g, &csr, src);
    c.gate(dist[dst] != u32::MAX);
    // 回溯路径：dst 沿 prev 一路走回 src，再倒序
    let mut path = Vec::new();
    let mut cur = Some(dst);
    while let Some(u) = cur {
        path.push(u);
        cur = prev[u];
    }
    path.reverse();
    c.gate(path.first() == Some(&src) && path.last() == Some(&dst));
    // 判据：路程 ≥ 直线距离（三角不等式；0.999 容取整误差）
    let straight = haversine_m(
        from_e7(g.nodes_e7[src].0),
        from_e7(g.nodes_e7[src].1),
        from_e7(g.nodes_e7[dst].0),
        from_e7(g.nodes_e7[dst].1),
    );
    c.gate(dist[dst] as f64 >= straight * 0.999);
    // 判据：路径连续——相邻节点必是真边
    let cont_ok = path.windows(2).all(|w| {
        let (a, b) = (w[0], w[1]);
        g.edges.iter().any(|&(ea, eb, _)| {
            (ea as usize == a && eb as usize == b) || (ea as usize == b && eb as usize == a)
        })
    });
    c.gate(cont_ok);
    // 画路线：视口投影 + 裁剪 + 纵向 3 线加粗
    let vp = default_viewport();
    for w in path.windows(2) {
        let (x0, y0) = vp.to_screen(from_e7(g.nodes_e7[w[0]].0), from_e7(g.nodes_e7[w[0]].1));
        let (x1, y1) = vp.to_screen(from_e7(g.nodes_e7[w[1]].0), from_e7(g.nodes_e7[w[1]].1));
        if let Some((cx0, cy0, cx1, cy1)) = clip_line(x0, y0, x1, y1) {
            for off in -1..=1 {
                c.draw_line(cx0, cy0 + off, cx1, cy1 + off, COL_ROUTE);
            }
        }
    }
    println!(
        "RUST-MINI-STEP-07-OK dist={}m straight={}m path={}",
        dist[dst],
        straight.round() as u32,
        path.len()
    );
    c.step_count += 1;
}

// @@STEP@@

fn main() {
    let mut c = MiniCtx::new();
    step_00(&mut c);
    step_01(&mut c);
    step_02(&mut c);
    let g = step_03(&mut c);
    step_04(&mut c, &g);
    step_05(&mut c, &g);
    step_06(&mut c, &g);
    step_07(&mut c, &g);
    // @@CALL@@
    if let Err(e) = c.write_bmp("out.bmp") {
        eprintln!("write out.bmp failed: {e}");
        std::process::exit(2);
    }
    println!(
        "RUST-MINI-CHAIN-OK steps={} ink={} gates={}/{}",
        c.step_count,
        c.ink(),
        c.gates_ok,
        c.gates_fail
    );
    if c.gates_fail > 0 {
        std::process::exit(1);
    }
}
