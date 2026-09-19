"""Windows side of the Art-Net bridge: connects to the WSL relay (TCP) and re-sends every Art-Net frame as UDP
to 127.0.0.1:6454 (or the host given), where ArtNetominator / any Art-Net node on this PC can see it.
Needs no admin rights and no firewall changes (outbound TCP from Windows into WSL is always allowed).
usage: py tools\artnet_bridge_win.py [wsl_host=auto] [dest=127.0.0.1] [dest_port=6454]"""
import socket, struct, subprocess, sys, time
def wsl_ip():
    out = subprocess.run(["wsl", "-e", "hostname", "-I"], capture_output=True, text=True).stdout
    return out.split()[0] if out.split() else "127.0.0.1"
host = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != "auto" else None
dest = (sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1", int(sys.argv[3]) if len(sys.argv) > 3 else 6454)
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
while True:
    h = host or wsl_ip()
    try:
        s = socket.create_connection((h, 6455), timeout=5); s.settimeout(None)
        print(f"connected to relay {h}:6455 -> udp {dest[0]}:{dest[1]}", flush=True)
        buf = b""; n = 0
        while True:
            chunk = s.recv(4096)
            if not chunk: raise OSError("relay closed")
            buf += chunk
            while len(buf) >= 2:
                ln = struct.unpack(">H", buf[:2])[0]
                if len(buf) < 2 + ln: break
                u.sendto(buf[2:2 + ln], dest); buf = buf[2 + ln:]; n += 1
                if n % 200 == 1: print(f"{n} frames relayed", flush=True)
    except OSError as e:
        print("relay not reachable (%s), retrying..." % e, flush=True); time.sleep(2)
