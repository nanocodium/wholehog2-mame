"""WSL side of the Art-Net bridge. Receives the Art-Net UDP frames MAME sends to 127.0.0.1:6454 inside WSL and
forwards each datagram (2-byte big-endian length prefix) to every TCP client connected on port 6455.
Windows cannot receive UDP sent by WSL (Hyper-V firewall) but Windows CAN open a TCP connection into WSL,
so the Windows side (artnet_bridge_win.py) connects here and re-emits the frames locally.
usage: artnet_relay_wsl.py [udp_port=6454] [tcp_port=6455]"""
import socket, select, struct, sys
udp_port = int(sys.argv[1]) if len(sys.argv) > 1 else 6454
tcp_port = int(sys.argv[2]) if len(sys.argv) > 2 else 6455
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
u.bind(("0.0.0.0", udp_port))
t = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
t.bind(("0.0.0.0", tcp_port)); t.listen(4)
clients = []
print(f"relay: udp {udp_port} -> tcp {tcp_port}", flush=True)
n = 0
while True:
    r, _, _ = select.select([u, t] + clients, [], [])
    for s in r:
        if s is t:
            c, a = t.accept(); c.setblocking(True); clients.append(c); print("client", a, flush=True)
        elif s is u:
            data, _ = u.recvfrom(2048); n += 1
            if n % 200 == 1: print(f"{n} frames, {len(clients)} clients", flush=True)
            pkt = struct.pack(">H", len(data)) + data
            for c in list(clients):
                try: c.sendall(pkt)
                except OSError: clients.remove(c); c.close(); print("client gone", flush=True)
        else:
            try:
                if not s.recv(64): raise OSError
            except OSError:
                clients.remove(s); s.close(); print("client gone", flush=True)
