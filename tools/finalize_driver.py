"""Final clean-up pass over wholehog2.cpp: quieter logging, Art-Net pacing, DMX end-of-frame word, named panel keys.
usage: finalize_driver.py <wholehog2.cpp>"""
import re, sys

p = sys.argv[1]
s = open(p).read()

def rep(old, new, count=1):
    global s
    assert old in s, old[:60]
    s = s.replace(old, new, count)

# 1. logging: only general messages by default
rep("#define VERBOSE ((1U << 0) | LOG_SYSCALL | LOG_LINK)", "#define VERBOSE (1U << 0)")

# 2. remove the per-5-second debug dumps (touch objects, event ring, port table) but keep the one-line status
start = s.index("\t\t{\n\t\t\taddress_space &sp = m_maincpu->space(AS_PROGRAM);\n\t\t\tconst uint32_t t0")
end = s.index("\t\tif ((m_ticks % (TICK_HZ * 20)) == 0)")
s = s[:start] + s[end:]
rep("\t\tif ((m_ticks % (TICK_HZ * 20)) == 0)\n\t\t\tdump_lcd_ram();\n", "")
rep("\tif ((m_ticks % (TICK_HZ * 5)) == 0)\n\t{\n\t\tlogerror(\"tick %u: interrupts %s, DMX frames %u %u %u %u, pc %08x\\n\", m_ticks, m_int_enabled ? \"on\" : \"off\", m_dmx_frames[0], m_dmx_frames[1], m_dmx_frames[2], m_dmx_frames[3], m_maincpu->pc());\n\t}",
    "\tif ((m_ticks % (TICK_HZ * 30)) == 0)\n\t\tLOG(\"tick %u: DMX frames %u %u %u %u\\n\", m_ticks, m_dmx_frames[0], m_dmx_frames[1], m_dmx_frames[2], m_dmx_frames[3]);")

# panel traffic logs -> LOGMASKED-style, off by default
rep('\tlogerror("panel: rx %02x %02x %02x\\n", p[0], p[1], p[2]);\n', "")
rep('\t\tlogerror("panel: tx %02x\\n", m_tx.front());\n', "")
rep('\t\tlogerror("panel: console command %02x %02x %02x\\n", p[0], p[1], p[2]);', '\t\tbreak;   // 08/09: display and misc commands, ignored')
rep('\t\tLOGMASKED(LOG_SYSCALL, "keyboard: scancode %02x\\n", sc);\n', "")
rep('\t\tLOGMASKED(LOG_SYSCALL, "keyboard: scancode %02x -> PLD code %03x\\n", sc, code & 0x1ff);\n', "")

# 3. DMX: FFFF terminates a frame; Art-Net pacing
rep("""	else if ((word & 0x000f) == 0x0007)
	{
		if (m_dmx_pos[port] >= 0 && m_dmx_pos[port] < 513)
			m_dmx_buf[port][m_dmx_pos[port]++] = (word >> 4) & 0xff;
		if (m_dmx_pos[port] == 513)
			dmx_frame_done(port);
	}""", """	else if (word == 0xffff)
	{
		// end of frame marker written by the OS after the last slot
		if (m_dmx_pos[port] > 0)
			dmx_frame_done(port);
		m_dmx_pos[port] = -1;
	}
	else if ((word & 0x000f) == 0x0007)
	{
		if (m_dmx_pos[port] >= 0 && m_dmx_pos[port] < 513)
			m_dmx_buf[port][m_dmx_pos[port]++] = (word >> 4) & 0xff;
		if (m_dmx_pos[port] == 513)
			dmx_frame_done(port);
	}""")
rep("""	if (m_dmx_buf[port][0] == 0)        // null start code: dimmer data
	{
		std::memcpy(m_dmx_last[port], &m_dmx_buf[port][1], slots);
		m_artnet.send_dmx(port, m_dmx_last[port], slots);
	}""", """	if (m_dmx_buf[port][0] == 0)        // null start code: dimmer data
	{
		// the OS refreshes its FIFOs far faster than a 250 kbit/s DMX line could carry (the PLD paces the real
		// hardware), so send Art-Net at most 40 times a second per universe, immediately on a change and at
		// least once a second otherwise
		const bool changed = std::memcmp(m_dmx_last[port], &m_dmx_buf[port][1], slots) != 0;
		const attotime now = machine().time();
		const attotime since = now - m_artnet_last[port];
		if ((changed && since >= attotime::from_msec(25)) || since >= attotime::from_seconds(1))
		{
			std::memcpy(m_dmx_last[port], &m_dmx_buf[port][1], slots);
			m_artnet.send_dmx(port, m_dmx_last[port], slots);
			m_artnet_last[port] = now;
		}
	}""")
rep("\tuint32_t m_dmx_frames[4] = {};\n", "\tuint32_t m_dmx_frames[4] = {};\n\tattotime m_artnet_last[4];\n")
rep("\tfor (int i = 0; i < 4; i++) m_dmx_pos[i] = -1;\n", "\tfor (int i = 0; i < 4; i++) { m_dmx_pos[i] = -1; m_artnet_last[i] = attotime::zero; }\n")

# 4. named panel keys (decoded from the OS key table and command line echoes)
names = {}
rows = ["Go", "Halt", "Flash", "Choose"]   # rows of eight master keys: codes 0100, 00f5, 00f6, 00f7 (order uncertain)
for r in range(4):
    for m in range(8):
        names[r * 8 + m] = "Master %d %s" % (m + 1, rows[r])
names.update({36: "Group", 37: "Position", 38: "Colour", 39: "Beam",
              40: "Master 1 List", 41: "Master 2 List", 42: "Master 3 List", 43: "Master 4 List",
              44: "Master 5 List", 45: "Master 6 List", 46: "Master 7 List", 47: "Master 8 List",
              56: "Master 9 List", 57: "Master 10 List",
              61: "Next", 64: "Copy", 65: "Move", 66: "Delete", 68: "Backspace", 69: "/", 70: "-", 71: "+",
              72: "Active", 73: "Load", 75: "Record", 76: "7", 77: "8", 78: "9", 79: "Thru", 81: "Goto",
              85: "0", 86: ".", 88: "Macro", 89: "Page", 91: "List", 92: "4", 93: "5", 94: "6", 95: "Full",
              98: "Clear", 99: "Set", 100: "1", 101: "2", 102: "3", 103: "@"})
for i in range(48, 56):
    names[i] = "Window control %d" % (i - 47)
names[58] = "Window control 9"; names[59] = "Window control 10"
keycodes = {76: "KEYCODE_7_PAD", 77: "KEYCODE_8_PAD", 78: "KEYCODE_9_PAD", 92: "KEYCODE_4_PAD", 93: "KEYCODE_5_PAD", 94: "KEYCODE_6_PAD",
            100: "KEYCODE_1_PAD", 101: "KEYCODE_2_PAD", 102: "KEYCODE_3_PAD", 85: "KEYCODE_0_PAD", 86: "KEYCODE_DEL_PAD",
            103: "KEYCODE_ASTERISK", 79: "KEYCODE_SLASH_PAD", 95: "KEYCODE_ENTER_PAD", 99: "KEYCODE_EQUALS", 98: "KEYCODE_BACKSPACE",
            70: "KEYCODE_MINUS_PAD", 71: "KEYCODE_PLUS_PAD", 75: "KEYCODE_R", 73: "KEYCODE_L", 64: "KEYCODE_C", 65: "KEYCODE_M",
            66: "KEYCODE_D", 72: "KEYCODE_A", 81: "KEYCODE_G", 88: "KEYCODE_F5", 89: "KEYCODE_F6", 91: "KEYCODE_F8",
            36: "KEYCODE_F1", 37: "KEYCODE_F2", 38: "KEYCODE_F3", 39: "KEYCODE_F4", 61: "KEYCODE_N",
            0: "KEYCODE_Q", 1: "KEYCODE_W", 2: "KEYCODE_E", 3: "KEYCODE_R", 4: "KEYCODE_T", 5: "KEYCODE_Y", 6: "KEYCODE_U", 7: "KEYCODE_I"}
def fix(m):
    k = int(m.group(1))
    name = names.get(k, "Panel key %d" % k)
    if k not in names:
        name = "Panel key %d" % k
    code = (" PORT_CODE(%s)" % keycodes[k]) if k in keycodes else ""
    return 'PORT_NAME("%s")%s' % (name, code)
s = re.sub(r'PORT_NAME\("Panel key (\d+)"\)(?: PORT_CODE\([A-Z0-9_]+\))?', fix, s)
# keep a comment block describing the key numbering
rep("static INPUT_PORTS_START( hog2_panel )", """// Panel key numbering (key = port*8 + bit) follows the OS key table at flash 0x5016be50; names come from the command
// line echoes of each key.  Unnamed keys did not produce visible output on the command line.
static INPUT_PORTS_START( hog2_panel )""")

open(p, "w").write(s)
print("finalized")
