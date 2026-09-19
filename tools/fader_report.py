"""Replace the fader wiggle/refresh logic in wholehog2.cpp with a delayed report: the console asks for fader positions
with 06 00 01 before its input processing is enabled, so answer 1.5 s and 4 s later as well.
usage: fader_report.py <wholehog2.cpp>"""
import sys

p = sys.argv[1]
s = open(p).read()

def rep(old, new):
    global s
    assert old in s, old[:70]
    s = s.replace(old, new, 1)

rep("""	case 0x06:  // fader reporting enable: answer with every fader position
		if (p[2])
			for (int i = 0; i < 10; i++)
				queue_packet(0x06, i, m_fader_state[i] ^ 1);   // the OS only acts on a change: the true value follows in the periodic refresh
		break;""",
"""	case 0x06:  // fader reporting enable: answer with every fader position, again 1.5 s and 4 s later (the first
		//         request arrives while the OS still drops input events)
		if (p[2])
		{
			report_faders();
			m_fader_report = 400;
		}
		break;""")

rep("""	// faders: report changes, and resend everything every 2 s (reports sent while the OS is still booting are dropped)
	m_fader_refresh++;
	const bool refresh = (m_fader_refresh % 200) == 0;
	for (int i = 0; i < 10; i++)
	{
		const uint8_t v = m_faders[i]->read();
		if (v != m_fader_state[i] || refresh)
		{
			m_fader_state[i] = v;
			// cmd 6 = fader position (OS posts events F4/F1/F2 for masters/xfader/GM).  The OS treats the first
			// position after a silence as a sync point and only moves the level on a change, so a refresh sends a
			// one-step wiggle followed by the true value (a real ADC jitters the same way).
			queue_packet(0x06, i, refresh ? (v ^ 1) : v);
		}
		else if (m_fader_settle == 1)
			queue_packet(0x06, i, v);   // true value, 50 ms after the wiggle
	}
	if (refresh) m_fader_settle = 6;
	if (m_fader_settle) m_fader_settle--;""",
"""	// faders: cmd 6 = fader position (OS posts events F4/F1/F2 for masters/xfader/GM)
	for (int i = 0; i < 10; i++)
	{
		const uint8_t v = m_faders[i]->read();
		if (v != m_fader_state[i])
		{
			m_fader_state[i] = v;
			queue_packet(0x06, i, v);
		}
	}
	if (m_fader_report)
	{
		m_fader_report--;
		if (m_fader_report == 250 || m_fader_report == 0)
			report_faders();
	}""")

rep("""	unsigned m_fader_refresh = 0;
	int m_fader_settle = 0;""", """	int m_fader_report = 0;""")

rep("""	void handle_packet(const uint8_t *p);""", """	void handle_packet(const uint8_t *p);
	void report_faders()
	{
		for (int i = 0; i < 10; i++)
			queue_packet(0x06, i, m_fader_state[i]);
	}""")

open(p, "w").write(s)
print("fader report rewritten")
