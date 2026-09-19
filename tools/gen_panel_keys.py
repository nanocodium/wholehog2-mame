"""Regenerate the front panel key input ports in wholehog2.cpp: 13 ports x 8 keys = 104 keys (the OS key table has
104 console entries; 104+ are expansion wing masters).  usage: gen_panel_keys.py <wholehog2.cpp>"""
import re, sys

p = sys.argv[1]
s = open(p).read()

codes = ["KEYCODE_0_PAD", "KEYCODE_1_PAD", "KEYCODE_2_PAD", "KEYCODE_3_PAD", "KEYCODE_4_PAD", "KEYCODE_5_PAD", "KEYCODE_6_PAD", "KEYCODE_7_PAD",
         "KEYCODE_8_PAD", "KEYCODE_9_PAD", "KEYCODE_Q", "KEYCODE_W", "KEYCODE_E", "KEYCODE_R", "KEYCODE_T", "KEYCODE_Y",
         "KEYCODE_U", "KEYCODE_I", "KEYCODE_O", "KEYCODE_P", "KEYCODE_A", "KEYCODE_S", "KEYCODE_D", "KEYCODE_F",
         "KEYCODE_G", "KEYCODE_H", "KEYCODE_J", "KEYCODE_K", "KEYCODE_L", "KEYCODE_Z", "KEYCODE_X", "KEYCODE_C",
         "KEYCODE_V", "KEYCODE_B", "KEYCODE_N", "KEYCODE_M", "KEYCODE_1", "KEYCODE_2", "KEYCODE_3", "KEYCODE_4",
         "KEYCODE_5", "KEYCODE_6", "KEYCODE_7", "KEYCODE_8", "KEYCODE_9", "KEYCODE_0", "KEYCODE_MINUS", "KEYCODE_EQUALS",
         "KEYCODE_F1", "KEYCODE_F2", "KEYCODE_F3", "KEYCODE_F4", "KEYCODE_F5", "KEYCODE_F6", "KEYCODE_F7", "KEYCODE_F8",
         "KEYCODE_F9", "KEYCODE_F10", "KEYCODE_F11", "KEYCODE_F12", "KEYCODE_ENTER_PAD", "KEYCODE_PLUS_PAD", "KEYCODE_MINUS_PAD", "KEYCODE_DEL_PAD"]

NPORTS = 13
lines = []
for port in range(NPORTS):
    lines.append('\tPORT_START("KEYS%d")' % port)
    for bit in range(8):
        k = port * 8 + bit
        code = (" PORT_CODE(%s)" % codes[k]) if k < len(codes) else ""
        lines.append('\tPORT_BIT(0x%02x, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key %d")%s' % (1 << bit, k, code))
block = "\n".join(lines) + "\n"

start = s.index('\tPORT_START("KEYS0")')
end = s.index('\tPORT_START("FADER0")')
s = s[:start] + block + "\n" + s[end:]
s = s.replace("required_ioport_array<8> m_keys;", "required_ioport_array<%d> m_keys;" % NPORTS)
s = s.replace("uint8_t m_key_state[8] = {};", "uint8_t m_key_state[%d] = {};" % NPORTS)
s = s.replace("for (int i = 0; i < 8; i++) m_key_state[i] = 0;", "for (int i = 0; i < %d; i++) m_key_state[i] = 0;" % NPORTS)
s = s.replace("\tfor (int port = 0; port < 8; port++)\n\t{\n\t\tconst uint8_t now = m_keys[port]->read();",
              "\tfor (int port = 0; port < %d; port++)\n\t{\n\t\tconst uint8_t now = m_keys[port]->read();" % NPORTS)
open(p, "w").write(s)
print("panel keys regenerated: %d ports" % NPORTS)
