"""Tiny Art-Net monitor: prints ArtDMX packets (universe, slot count, first channels) arriving on UDP 6454.
usage: artnet_listen.py [bind_ip] [channels_to_show]"""
import socket, struct, sys, time

bind = sys.argv[1] if len(sys.argv) > 1 else "0.0.0.0"
show = int(sys.argv[2]) if len(sys.argv) > 2 else 16
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
port = int(sys.argv[3]) if len(sys.argv) > 3 else 6454
s.bind((bind, port))
print("listening on %s:%d" % (bind, port))
count = {}
seen = set()
last = time.time()
while True:
    data, addr = s.recvfrom(2048)
    if data[:8] != b"Art-Net\0" or len(data) < 18:
        continue
    op = struct.unpack_from("<H", data, 8)[0]
    if op != 0x5000:
        print("op %04x from %s" % (op, addr[0])); continue
    seq, phys, sub, net = data[12], data[13], data[14], data[15]
    length = (data[16] << 8) | data[17]
    uni = (net << 8) | sub
    count[uni] = count.get(uni, 0) + 1
    key = (uni, bytes(data[18:18 + show]))
    if key not in seen or time.time() - last > 0.5:
        seen.add(key)
        last = time.time()
        print("%s u%d seq %3d len %3d  %s  (%s)" % (addr[0], uni, seq, length, " ".join("%3d" % b for b in data[18:18 + show]),
              " ".join("u%d:%d" % kv for kv in sorted(count.items()))))
