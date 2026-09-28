# -*- coding: utf-8 -*-
# 联机端到端冒烟: 以独立 Python 协议实现扮演客户端, 对 net_host_stub 驱动的
# 主机会话走完 发现 -> JOIN -> SNAP -> INP 移动 -> 命中扣血 -> busy -> 暂停。
# 用法: python tools/net_smoke.py [host_ip]  (默认 127.0.0.1)
import socket, struct, sys, time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = 52021
VER = 1

def disc():    return bytes([1, VER])
def join(name, ready=False):
    return bytes([4, VER, len(name)]) + name.encode() + bytes([1 if ready else 0])
def inp(bits, aim_deg, fire):
    a = int(round((aim_deg % 360.0) * 16)) % 5760
    return bytes([5, VER, bits]) + struct.pack("<H", a) + bytes([1 if fire else 0])
def keep():    return bytes([8, VER])

def parse_snap(b):
    assert b[0] == 6 and len(b) >= 31, "bad snap"  # 31B = 空导弹最小包
    tick, = struct.unpack_from("<I", b, 2)
    phase, hp0, hp1 = b[6], b[7], b[8]
    off = 9
    tanks = []
    for _ in range(2):
        x, y, d, t = struct.unpack_from("<hhHH", b, off)
        state, frame = b[off+8], b[off+9]
        tanks.append(dict(x=x/16.0, y=y/16.0, state=state, frame=frame))
        off += 10
    n = b[off]; off += 1
    missiles = []
    for _ in range(n):
        x, y, d = struct.unpack_from("<hhH", b, off)
        missiles.append(dict(x=x/16.0, y=y/16.0, owner=b[off+6]))
        off += 7
    return dict(tick=tick, phase=phase, hp=[hp0, hp1], tanks=tanks,
                missiles=missiles)

def expect(cond, msg):
    if not cond:
        print("FAIL:", msg); sys.exit(1)
    print("PASS:", msg)

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(0.5)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

# 1) 发现: 广播 DISC(本机回环也能收到 stub 的单播回应)
# 注: 对广播的回应源地址是本机出接口 IP(如 192.168.x.x)而非 HOST,
#     只有对直发 DISC 的回应源地址才等于 HOST —— 以直发回应作为发现成立判据。
s.sendto(disc(), ("255.255.255.255", PORT))
s.sendto(disc(), (HOST, PORT))
name = None
addr_ok = False
for _ in range(10):
    try:
        b, a = s.recvfrom(2048)
        if b[0] == 2:
            if name is None:
                name = b[3:3+b[2]].decode()
            if a[0] == HOST:
                addr_ok = True
                break
    except socket.timeout:
        s.sendto(disc(), (HOST, PORT))
if name is not None:
    expect(addr_ok, "discovered host %s room=%r" % (HOST, name))
expect(name is not None, "host responded to DISC")

# 2) JOIN 5Hz 直到第一个 SNAP(连接成功标志)
snap = None
t0 = time.time()
while time.time() - t0 < 5.0:
    s.sendto(join("smoke-client"), (HOST, PORT))
    try:
        b, _ = s.recvfrom(2048)
        if b[0] == 6:
            snap = parse_snap(b); break
    except socket.timeout:
        pass
expect(snap is not None, "first SNAP arrived (joined)")
expect(snap["phase"] == 0, "phase=countdown")
last_tick = snap["tick"]

def next_snap(timeout=3.0, keepalive=True, keep_sock=None):
    # keep_sock: 心跳发出的 socket, 默认主 socket s; 暂停测试传 s2
    # (同 IP 另一源端口), 练 spec §6.3 的 KEEP 任意源端口豁免
    global last_tick
    t0 = time.time()
    while time.time() - t0 < timeout:
        if keepalive:
            (s if keep_sock is None else keep_sock).sendto(keep(), (HOST, PORT))
        try:
            b, _ = s.recvfrom(2048)
            if b[0] == 6:
                sn = parse_snap(b)
                if sn["tick"] > last_tick:
                    last_tick = sn["tick"]
                    return sn
        except socket.timeout:
            pass
    return None

# 3) 倒计时 3s 后进战斗
sn = None
for _ in range(120):
    s.sendto(inp(0, 0.0, False), (HOST, PORT))
    sn = next_snap(0.5)
    if sn and sn["phase"] == 1:
        break
expect(sn and sn["phase"] == 1, "countdown -> battle (phase self-heal)")

# 4) 移动: 按住 W 1 秒, y 必须上升(位移同步生效)
y0 = sn["tanks"][1]["y"]
for _ in range(30):
    s.sendto(inp(1, 0.0, False), (HOST, PORT))
    sn = next_snap(0.5)
y1 = sn["tanks"][1]["y"]
expect(y1 > y0 + 5.0, "own tank moved y %.1f -> %.1f" % (y0, y1))

# 5) 命中扣血: 客户端坦克朝下瞄准主机(它在 (0,-120) 原地), 炮塔 180 度连发
for _ in range(90):
    s.sendto(inp(0, 180.0, True), (HOST, PORT))
    sn = next_snap(0.5)
    if sn["hp"][0] < 3:
        break
expect(sn["hp"][0] < 3, "host tank hit, hp0=%d" % sn["hp"][0])

# 6) busy: 第二个 socket 在战斗中 JOIN, 必须收到 ERR{reason=2}
s2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s2.settimeout(2.0)
s2.sendto(join("intruder"), (HOST, PORT))
b, _ = s2.recvfrom(2048)
expect(b[0] == 3 and b[2] == 2, "busy ERR for third-party JOIN")

# 6.5) 源过滤: 陌生端口(s2)伪造 SNAP/BYE 直发主机, 必须被无反响地丢弃
#      (非对端 IP 维度本机回环测不了, 显式移交 Task 8 双机清单)
for fake in (bytes([6, VER]) + b"\xff" * 40, bytes([7, VER])):
    s2.sendto(fake, (HOST, PORT))
sn = None
for _ in range(30):
    s.sendto(inp(0, 0.0, False), (HOST, PORT))
    sn = next_snap(0.5)
    if sn and sn["phase"] == 1:
        break
expect(sn and sn["phase"] == 1, "forged SNAP/BYE ignored (source filter)")

# 7) 暂停(N1): 停发 INP(KEEP 照发), 必须收到 phase=3 且 tick 前进。
#    心跳只从 s2 发(同 IP 另一源端口, 正是 §6.3 KEEP 豁免定义的形态)。
#    观察窗必须盖过 5s 失活点(合并门评审 Important#1: 旧 3s 窗够不着
#    lastAny_ 失活, 对 KEEP 豁免零守护): 若豁免被删, s2 心跳全被源过滤
#    丢弃, 最后一个 INP 后 5s lastAny_ 失活 -> remoteInputStale 转假,
#    主机退出暂停(stub 形态: phase 跳回 1; 真 Game 形态: Disconnected
#    散会断流)。故断言四件套: 进得去暂停 / 入停后 phase 不回退 /
#    失活点后仍有快照到达 / 暂停期 tick 单调前进
t0 = time.time()
samples = []                               # (接收时刻, phase, tick)
while time.time() - t0 < 8.0:
    sn7 = next_snap(0.5, keep_sock=s2)     # 内部只发 keep 不发 inp
    if sn7 is not None:
        samples.append((time.time() - t0, sn7["phase"], sn7["tick"]))
p3 = next((i for i, x in enumerate(samples) if x[1] == 3), None)
expect(p3 is not None, "pause visible (phase=3 within window)")
expect(all(x[1] == 3 for x in samples[p3:]),
       "phase stays 3 after entry (%d samples)" % (len(samples) - p3))
expect(samples[-1][0] > 6.0,
       "snaps still arriving past 5s expiry horizon (last t+%.1fs)" %
       samples[-1][0])
expect(all(b[2] > a[2] for a, b in zip(samples[p3:], samples[p3 + 1:])),
       "tick advances while paused (%d samples)" % (len(samples) - p3))

# 8) 恢复: 继续发 INP, phase 回 1
for _ in range(30):
    s.sendto(inp(0, 0.0, False), (HOST, PORT))
    sn = next_snap(0.5)
    if sn and sn["phase"] == 1:
        break
expect(sn and sn["phase"] == 1, "resumed after INP resumed")

print("ALL SMOKE CHECKS PASSED")
