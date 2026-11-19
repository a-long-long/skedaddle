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

// =====================================================================
// step08: pipeline —— 一帧的 9 阶段管线编排（C 第 7 课 07_pipeline）
// 对照 src/ui/window.c:paint_frame：真实工程每阶段吃真实图集并跨省统一
// 分层，mini/Rust 链只把「顺序 + 职责」钉住，阶段内容用前面各段的原子代跑。
// ★ 与真实工程唯一刻意的差别：① 底色阶段只校验、不重涂——否则每加一段
//   就会把前几段画的东西擦掉，「同一份工程逐段长大」就不成立。
// Rust 语言点：阶段表用 Vec<u8> 整体比对（stages == [1..=9]）；&mut self
// 的独占借用逼你把「画」与「读」排好序——C 里这类交错是静默 UB 温床。
// =====================================================================
const COL_CHROME: u32 = 0xFF3A4652;
const COL_LABEL: u32 = 0xFFE08020;
const COL_MASK: u32 = 0xFF304060;

impl MiniCtx {
    /// 矩形填充（step08 起的标注/叠加原子；越界由 put_pixel 裁剪）。
    fn fill_rect(&mut self, x0: i32, y0: i32, w: i32, h: i32, color: u32) {
        for y in y0..y0 + h {
            for x in x0..x0 + w {
                self.put_pixel(x, y, color);
            }
        }
    }
}

/// 简单 alpha 混合：a 底色、b 覆盖色，alpha ∈ [0,256]（只混 RGB，A 恒 FF）。
fn blend(a: u32, b: u32, alpha: u32) -> u32 {
    let mix = |sh: u32| ((a >> sh) & 0xFF) * (256 - alpha) / 256 + ((b >> sh) & 0xFF) * alpha / 256;
    0xFF000000 | (mix(16) << 16) | (mix(8) << 8) | mix(0)
}

/// 一帧的 9 阶段管线，阶段号按执行顺序记进 stages。返回执行过的阶段数。
fn paint_frame(c: &mut MiniCtx, vp: &Viewport, stages: &mut Vec<u8>) -> usize {
    let mut k = 0usize;
    // ① 底色：只校验，不重涂
    stages.push(1);
    k += 1;
    // ② 视口 bbox（真实工程反算可见 bbox，这里用视口对象本身代跑）
    let _ = (vp.clat, vp.clon, vp.span_lon);
    stages.push(2);
    k += 1;
    // ③ 按 bbox 决定图集进/出（span 宽度当确定性替身）
    let visible_tiles = (vp.span_lon * 1000.0) as i32;
    stages.push(3);
    k += 1;
    // ④ 跨省统一分层：面 → 路 → 线 → 等高线，顺序固定
    let layers = [1u8, 2, 3, 4];
    stages.push(4);
    k += 1;
    // ⑤ 光栅：裁剪线画一条「路」
    if let Some((x0, y0, x1, y1)) = clip_line(210, 20, 300, 20) {
        c.draw_line(x0, y0, x1, y1, COL_ROAD);
    }
    stages.push(5);
    k += 1;
    // ⑥ 标注：小方块代替字形
    for i in 0..3 {
        c.fill_rect(212 + i * 30, 26, 4, 4, COL_LABEL);
    }
    stages.push(6);
    k += 1;
    // ⑦ 叠加层：gpx → route → 车头 → 端点 → 解码占位（顺序固定）
    let overlay = [1u8, 2, 3, 4, 5];
    stages.push(7);
    k += 1;
    // ⑧ 昼夜蒙版：只在顶栏做一次半透明叠加，不改背景色约定
    let mut x = 0;
    while x < W {
        let old = c.buf[(6 * W + x) as usize];
        c.put_pixel(x, 6, blend(old, COL_MASK, 64));
        x += 2;
    }
    stages.push(8);
    k += 1;
    // ⑨ chrome 自绘界面：最后盖上
    c.fill_rect(0, 0, W, 3, COL_CHROME);
    stages.push(9);
    k += 1;
    // 内容判据（镜像 C 版 mini_step_07 的返回条件）
    c.gate(visible_tiles > 0 && layers[0] == 1 && overlay[4] == 5);
    k
}

fn step_08(c: &mut MiniCtx) {
    let vp = default_viewport();
    let mut stages = Vec::new();
    let k = paint_frame(c, &vp, &mut stages);
    c.gate(k == 9 && stages == [1, 2, 3, 4, 5, 6, 7, 8, 9]); // 9 个阶段按固定顺序执行
    c.gate(c.buf[0] == COL_CHROME);                          // chrome 层最后盖上
    c.gate(c.ink() > 0);                                     // 一帧画完仍有内容
    println!("RUST-MINI-STEP-08-OK stages={k}");
    c.step_count += 1;
}

// =====================================================================
// step09: search —— 地名子串检索与命中锚点（C 第 8 课 08_search）
// 对照 src/search/search.c：真实工程有 places_auto.db、拼音/前缀索引与
// 多路召回；mini 只保留「查询串 → 命中下标」这一层契约。
// Rust 语言点：str::contains 按 Unicode 字符匹配（C 的 strstr 按字节，
// 撞上 UTF-8 续字节会误命中）；返回 Vec<usize> 而非 out+cap——容量问题
// 从「调用方记得给大数组」变成类型保证。
// =====================================================================
const SEARCH_NAMES: [&str; 6] = ["北京市", "海淀区", "朝阳区", "中关村", "五道口", "天津市"];

fn search_hits(q: &str) -> Vec<usize> {
    (0..SEARCH_NAMES.len()).filter(|&i| SEARCH_NAMES[i].contains(q)).collect()
}

fn step_09(c: &mut MiniCtx) {
    let h1 = search_hits("区");
    let h2 = search_hits("中关村");
    let h3 = search_hits("上海");
    c.gate(h1 == [1usize, 2]);   // 「区」命中 海淀区 / 朝阳区
    c.gate(h2 == [3usize]);      // 精确子串命中与下标
    c.gate(h3.is_empty());       // 不存在的地名返回 0 条
    // 把命中项画成三个橙色锚点（真实工程里它们会带大字标签）
    for x in [30, 150, 250] {
        c.fill_rect(x, 208, 6, 6, COL_LABEL);
        c.draw_line(x + 3, 208, x + 3, 196, COL_LABEL);
    }
    println!("RUST-MINI-STEP-09-OK hits={}/{}/{}", h1.len(), h2.len(), h3.len());
    c.step_count += 1;
}

// =====================================================================
// step10: navvoice —— 机动指令 + 唯一发声出口的 15s 同文本冷却（C 第 10 课）
// 对照 src/route/maneuver.c、voice.c、voice_wire.c。真实工程里「到达帧先
// 播报后置 paused」的顺序是判据；mini 只钉「指令生成 + 冷却」两件事。
// Rust 语言点：C 的函数级 static（隐藏全局可变状态）→ 显式状态结构体
// &mut self——谁持有发声器、谁在改状态，类型系统一目了然。
// =====================================================================
const NAV_COOLDOWN_MS: i32 = 15000;

struct VoiceOut {
    last: String,
    last_ms: i32,
}

impl VoiceOut {
    fn new() -> Self {
        VoiceOut { last: String::new(), last_ms: -NAV_COOLDOWN_MS }
    }

    /// 同一文本 15s 内不重复播；换文本或冷却期满放行。true = 真播了。
    fn say(&mut self, text: &str, now_ms: i32) -> bool {
        if self.last == text && now_ms - self.last_ms < NAV_COOLDOWN_MS {
            return false;
        }
        self.last = text.to_string();
        self.last_ms = now_ms;
        true
    }
}

fn nav_build(path: &[usize]) -> Vec<String> {
    let mut out = Vec::new();
    if path.is_empty() {
        return out;
    }
    out.push("出发".to_string());
    for &p in &path[1..path.len() - 1] {
        out.push(format!("直行 至 {p} 号点"));
    }
    out.push("到达目的地".to_string());
    out
}

fn step_10(c: &mut MiniCtx, g: &Graph) {
    let csr = Csr::build(g.nodes_e7.len(), &g.edges);
    let (dist, prev) = dijkstra(g, &csr, 0);
    let mut path = Vec::new();
    let mut cur = Some(3usize);
    while let Some(u) = cur {
        path.push(u);
        cur = prev[u];
    }
    path.reverse();
    c.gate(dist[3] != u32::MAX && path.first() == Some(&0) && path.last() == Some(&3));
    let mans = nav_build(&path);
    c.gate(mans.len() == path.len()); // 指令数 = 出发 + 每个中间点直行 + 到达
    let mut vo = VoiceOut::new();
    let a = vo.say(&mans[0], 1000);
    let b = vo.say(&mans[0], 1500);
    let d = vo.say(&mans[1], 1600);
    let e = vo.say(&mans[0], 17000);
    c.gate(a && !b);                  // 15s 同文本冷却挡住重复播报
    c.gate(d && e);                   // 换文本 / 冷却后可再播
    // 车头：终点处画一个三角形（示意；真实工程用扫描线填充）
    let last = path[path.len() - 1];
    let (la, lo) = { let (la7, lo7) = g.nodes_e7[last]; (from_e7(la7), from_e7(lo7)) };
    let (ex, ey) = default_viewport().to_screen(la, lo);
    c.draw_line(ex, ey - 9, ex - 7, ey + 5, 0xFFE04030);
    c.draw_line(ex - 7, ey + 5, ex + 7, ey + 5, 0xFFE04030);
    c.draw_line(ex + 7, ey + 5, ex, ey - 9, 0xFFE04030);
    println!("RUST-MINI-STEP-10-OK maneuvers={}", mans.len());
    c.step_count += 1;
}

// =====================================================================
// step11: gates —— 判据框架与「负向对照必须能失败」（C 第 11 课 11_gates）
// 对照 scripts/regress.sh、src/render/audit.c。项目铁律：只跑通正常路径
// 不算验收，必须证明「把改动改回去，判据会 FAIL」——否则就是假门禁。
// Rust 语言点：闭包 Fn(i32)->bool 当判据一等公民传递（C 用函数指针 +
// void*；Rust 的闭包捕获与 trait 边界把「这判据依赖什么」写进签名）。
// =====================================================================
const GATE_THRESHOLD: i32 = 10;

fn gate_eval(name: &str, arg: i32, f: &dyn Fn(i32) -> bool, pass: &mut u32) -> bool {
    let r = f(arg);
    println!("  GATE {:<28} {}", name, if r { "PASS" } else { "FAIL" });
    if r {
        *pass += 1;
    }
    r
}

/// 返回 true = 已知坏输入确实让判据 FAIL（负向对照成立，不是假门禁）。
fn gate_negative(name: &str, bad_arg: i32, f: &dyn Fn(i32) -> bool) -> bool {
    let r = f(bad_arg);
    println!("  NEG  {:<28} {} (期望 FAIL)", name, if r { "PASS" } else { "FAIL" });
    !r
}

fn step_11(c: &mut MiniCtx) {
    let mut pass = 0u32;
    let ge = |x: i32| x >= GATE_THRESHOLD;
    let r1 = gate_eval("classify(15) 属高类", 15, &ge, &mut pass);
    let neg = gate_negative("classify(5) 应被挡下", 5, &ge);
    c.gate(r1 && pass == 1);   // 正向判据通过并计数
    c.gate(neg);               // 负向对照能失败（不是假门禁）
    // 阈值条：蓝=低类 / 红=高类
    for x in 0..100 {
        let col = if x >= GATE_THRESHOLD { 0xFFD04040 } else { 0xFF4080C0 };
        c.fill_rect(10 + x * 2, 222, 2, 4, col);
    }
    println!("RUST-MINI-STEP-11-OK gates={pass}");
    c.step_count += 1;
}

// =====================================================================
// step12: capstone —— 综合闭环：改一个点 → 加一条判据 → 负向对照（C 第 12 课）
// 对照 tutorial/12_capstone/。不新增模块，只把 step11 的判据框架用起来：
// 「改 → 测 → 守」三步，缺一步都不算闭环。
// Rust 语言点：阈值用 Cell<i32> 在闭包间共享可变状态（C 直接改全局变量；
// Rust 把「谁能改」限定进单个 Cell，作用域一目了然；&Cell 也能 set，
// 这正是内部可变性存在的意义）。
// =====================================================================
fn step_12(c: &mut MiniCtx) {
    use std::cell::Cell;
    let mut pass = 0u32;
    let thresh = Cell::new(10i32);
    let lock_change = |x: i32| x < thresh.get(); // 新判据：改动后 15 必须落进低类
    // ① 改一个点
    thresh.set(20);
    let locked = gate_eval("classify(15) 属低类(新判据)", 15, &lock_change, &mut pass);
    // ② 负向对照：改回去——旧阈值下新判据必须 FAIL
    thresh.set(10);
    let neg = gate_negative("改回 10 后新判据应 FAIL", 15, &lock_change);
    // ③ 恢复改动
    thresh.set(20);
    let restored = thresh.get() == 20;
    c.gate(locked && pass == 1); // 新判据锁住改动
    c.gate(neg && restored);     // 负向对照证明判据有效
    // 改动前 / 改动后的阈值条，上下两行对照
    for x in 0..100 {
        c.fill_rect(10 + x * 2, 230, 2, 3, if x >= 10 { 0xFFD04040 } else { 0xFF4080C0 });
        c.fill_rect(10 + x * 2, 235, 2, 3, if x >= 20 { 0xFFD04040 } else { 0xFF4080C0 });
    }
    println!("RUST-MINI-STEP-12-OK closed-loop={locked}/{neg}/{restored}");
    c.step_count += 1;
}

// =====================================================================
// step13: datum —— 大地基准：只换椭球 ≠ 基准转换（C 第 13 课 13_datum）
// 对照 src/utils/datum.c。结论与项目一致：CGCS2000→WGS84 平移 0；西安 1980
// 用全国三参数 (−104.57, −45.94, +5.30)；北京 1954 登记「有 TOWGS84」。
// Rust 语言点：查不到返回 Option<&Datum> 而不是 NULL——调用方必须
// unwrap 或匹配，「忘了判空」在编译期就消失。
// =====================================================================
struct Datum {
    name: &'static str,
    a: f64,
    invf: f64,
    dx: f64,
    dy: f64,
    dz: f64,
    has_towgs84: bool,
}

const DATUMS: [Datum; 4] = [
    Datum { name: "CGCS2000",    a: 6378137.0, invf: 298.257222101, dx: 0.0,     dy: 0.0,    dz: 0.0,  has_towgs84: false },
    Datum { name: "WGS84",       a: 6378137.0, invf: 298.257223563, dx: 0.0,     dy: 0.0,    dz: 0.0,  has_towgs84: false },
    Datum { name: "Beijing1954", a: 6378245.0, invf: 298.3,         dx: 0.0,     dy: 0.0,    dz: 0.0,  has_towgs84: true },
    Datum { name: "Xian1980",    a: 6378140.0, invf: 298.257,       dx: -104.57, dy: -45.94, dz: 5.30, has_towgs84: false },
];

fn datum_find(name: &str) -> Option<&'static Datum> {
    DATUMS.iter().find(|d| d.name == name)
}

fn datum_shift(d: &Datum, x: f64, y: f64, z: f64) -> (f64, f64, f64) {
    (x + d.dx, y + d.dy, z + d.dz)
}

fn step_13(c: &mut MiniCtx) {
    let cg = datum_find("CGCS2000");
    let wg = datum_find("WGS84");
    let bj = datum_find("Beijing1954");
    let xa = datum_find("Xian1980");
    c.gate(cg.is_some() && wg.is_some() && bj.is_some() && xa.is_some()); // 四个基准都登记在册
    let (ox, oy, oz) = datum_shift(cg.unwrap(), 100.0, 200.0, 30.0);
    c.gate(ox == 100.0 && oy == 200.0 && oz == 30.0);                     // CGCS2000 平移为 0
    // 长半轴相同但扁率不同 ⇒ 只换椭球不等于基准转换
    c.gate(cg.unwrap().a == wg.unwrap().a && cg.unwrap().invf != wg.unwrap().invf);
    let xa = xa.unwrap();
    c.gate((xa.dx, xa.dy, xa.dz) == (-104.57, -45.94, 5.30) && bj.unwrap().has_towgs84);
    // 平移前 / 平移后两个点（偏移量按像素缩放示意）
    c.fill_rect(270, 200, 5, 5, 0xFF4080C0);
    c.fill_rect(270 + (xa.dx / 8.0).round() as i32, 200 - (xa.dy / 8.0).round() as i32, 5, 5, 0xFFD04040);
    println!("RUST-MINI-STEP-13-OK datums={}", DATUMS.len());
    c.step_count += 1;
}

// =====================================================================
// step14: i18n —— 极简 i18n：中文 msgid 是安全的（C 第 14 课 14_i18n）
// 对照 src/i18n/i18n.c、lang/en.xml。真实工程从 XML 加载词条并做 XML 转义；
// 这里只保留「查表 + 未登记回退原串 + 实体解码」三件事。
// Rust 语言点：返回 &str 借用自静态表，绝不返回空指针；解码产物 String
// 自己拥有自己的内存——C 版 out+cap 的缓冲约定整个消失。
// =====================================================================
const TR_TABLE: [(&str, &str); 5] = [
    ("退出", "Quit"),
    ("搜索", "Search"),
    ("设置", "Settings"),
    ("起点", "Start"),
    ("终点", "End"),
];

/// 未登记一律回退原串（中文 msgid 本身可读），绝无「查不到返回空」。
fn tr(id: &str) -> &str {
    TR_TABLE.iter().find(|(k, _)| *k == id).map(|(_, v)| *v).unwrap_or(id)
}

/// XML 实体解码：&amp; → &、&lt; → <。单遍扫描、不解已展开的实体
/// （"A&amp;lt;B" 必须得 "A&lt;B" 而不是 "A<B"——两次 replace 会二次解码）。
fn xml_unescape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut rest = s;
    while let Some(p) = rest.find('&') {
        out.push_str(&rest[..p]);
        let tail = &rest[p..];
        if tail.starts_with("&amp;") {
            out.push('&');
            rest = &tail[5..];
        } else if tail.starts_with("&lt;") {
            out.push('<');
            rest = &tail[4..];
        } else {
            out.push('&');
            rest = &tail[1..];
        }
    }
    out.push_str(rest);
    out
}

fn step_14(c: &mut MiniCtx) {
    c.gate(tr("退出") == "Quit");                     // 中文 msgid 能查到英文
    c.gate(tr("未登记") == "未登记");                 // 未登记词条回退原串
    c.gate(xml_unescape("A&amp;B") == "A&B");         // XML 实体解码
    c.gate(xml_unescape("A&amp;lt;B") == "A&lt;B");   // 已转义实体不被二次解码
    // 语言切换小框（不用字体，纯几何示意）
    c.fill_rect(286, 12, 28, 12, 0xFFF8F8F8);
    c.draw_line(286, 12, 313, 12, COL_CHROME);
    c.draw_line(286, 23, 313, 23, COL_CHROME);
    println!("RUST-MINI-STEP-14-OK entries={}", TR_TABLE.len());
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
    step_08(&mut c);
    step_09(&mut c);
    step_10(&mut c, &g);
    step_11(&mut c);
    step_12(&mut c);
    step_13(&mut c);
    step_14(&mut c);
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
