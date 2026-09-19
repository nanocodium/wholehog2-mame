"""Single mouse-driven touch point over both LCDs + default layout showing only the LCDs.
usage: touch_layout.py <wholehog2.cpp>"""
import re, sys

p = sys.argv[1]
s = open(p).read()

def rep(old, new):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, 1)

# --- panel: one touch point -------------------------------------------------------------------------------------
rep("""		, m_touch_x(*this, "TOUCH%u_X", 0U)
		, m_touch_y(*this, "TOUCH%u_Y", 0U)
		, m_touch_btn(*this, "TOUCH_BTN")""",
"""		, m_touch_x(*this, "TOUCH_X")
		, m_touch_y(*this, "TOUCH_Y")
		, m_touch_btn(*this, "TOUCH_BTN")""")
rep("""	required_ioport_array<2> m_touch_x;
	required_ioport_array<2> m_touch_y;
	required_ioport m_touch_btn;""",
"""	required_ioport m_touch_x;
	required_ioport m_touch_y;
	required_ioport m_touch_btn;""")
rep("""	bool m_touch_down[2] = {};
	uint8_t m_touch_last[2][2] = {};""",
"""	bool m_touch_down = false;
	int m_touch_screen = 0;         // LCD under the pointer (0/1)
	int m_touch_px = 0, m_touch_py = 0;   // pointer position on that LCD in pixels""")
rep("""	virtual void input_txd(int state) override { device_serial_interface::rx_w(state); }
""", """	virtual void input_txd(int state) override { device_serial_interface::rx_w(state); }

	// pointer position for the LCD renderer: returns true when the pointer is over LCD 'screen'
	bool pointer(int screen, int &x, int &y, bool &down) const
	{
		x = m_touch_px; y = m_touch_py; down = m_touch_down;
		return screen == m_touch_screen;
	}
""")
rep("""	PORT_START("TOUCH0_X") PORT_BIT(0xff, 0x80, IPT_LIGHTGUN_X) PORT_CROSSHAIR(X, 1.0, 0.0, 0) PORT_SENSITIVITY(50) PORT_KEYDELTA(8) PORT_NAME("Touch 1 X") PORT_PLAYER(1)
	PORT_START("TOUCH0_Y") PORT_BIT(0xff, 0x80, IPT_LIGHTGUN_Y) PORT_CROSSHAIR(Y, 1.0, 0.0, 0) PORT_SENSITIVITY(50) PORT_KEYDELTA(8) PORT_NAME("Touch 1 Y") PORT_PLAYER(1)
	PORT_START("TOUCH1_X") PORT_BIT(0xff, 0x80, IPT_LIGHTGUN_X) PORT_CROSSHAIR(X, 1.0, 0.0, 0) PORT_SENSITIVITY(50) PORT_KEYDELTA(8) PORT_NAME("Touch 2 X") PORT_PLAYER(2)
	PORT_START("TOUCH1_Y") PORT_BIT(0xff, 0x80, IPT_LIGHTGUN_Y) PORT_CROSSHAIR(Y, 1.0, 0.0, 0) PORT_SENSITIVITY(50) PORT_KEYDELTA(8) PORT_NAME("Touch 2 Y") PORT_PLAYER(2)
	PORT_START("TOUCH_BTN")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_NAME("Touch screen 1") PORT_PLAYER(1) PORT_CODE(MOUSECODE_BUTTON1)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_NAME("Touch screen 2") PORT_PLAYER(2)""",
"""	// One pointer over both LCDs laid side by side (0..1279 x 0..479), moved by the mouse (run MAME with -mouse);
	// the driver draws it as a cross on the LCD it is over, button 1 is the finger.
	PORT_START("TOUCH_X") PORT_BIT(0xfff, 0x400, IPT_LIGHTGUN_X) PORT_MINMAX(0, 0xfff) PORT_SENSITIVITY(40) PORT_KEYDELTA(24) PORT_NAME("Touch pointer X") PORT_CODE_INC(KEYCODE_RIGHT) PORT_CODE_DEC(KEYCODE_LEFT)
	PORT_START("TOUCH_Y") PORT_BIT(0xfff, 0x800, IPT_LIGHTGUN_Y) PORT_MINMAX(0, 0xfff) PORT_SENSITIVITY(40) PORT_KEYDELTA(24) PORT_NAME("Touch pointer Y") PORT_CODE_INC(KEYCODE_DOWN) PORT_CODE_DEC(KEYCODE_UP)
	PORT_START("TOUCH_BTN")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_NAME("Touch") PORT_CODE(MOUSECODE_BUTTON1) PORT_CODE(KEYCODE_RCONTROL)""")
rep("""	save_item(NAME(m_touch_down));
	save_item(NAME(m_touch_last));""",
"""	save_item(NAME(m_touch_down));
	save_item(NAME(m_touch_screen));
	save_item(NAME(m_touch_px));
	save_item(NAME(m_touch_py));""")

start = s.index("\t// touch screens: x then y while pressed")
end = s.index("\n}\n", start)
s = s[:start] + """	// touch: the pointer covers both LCDs (1280 x 480); the LCD under it receives 8-bit raw coordinates, streamed
	// while the finger is down, and a full-scale pair (which the OS maps outside the display) when it lifts
	{
		const int xt = m_touch_x->read() * 1280 / 0x1000, yt = m_touch_y->read() * 480 / 0x1000;
		const bool down = BIT(m_touch_btn->read(), 0);
		if (!m_touch_down)
		{
			// the pointer may move between LCDs only while lifted, so a press always belongs to one LCD
			m_touch_screen = xt >= 640 ? 1 : 0;
		}
		m_touch_px = std::clamp(xt - m_touch_screen * 640, 0, 639);
		m_touch_py = std::clamp(yt, 0, 479);
		if (down)
		{
			queue_packet(0x08, m_touch_screen * 2 + 0, m_touch_px * 255 / 639);
			queue_packet(0x08, m_touch_screen * 2 + 1, m_touch_py * 255 / 479);
		}
		else if (m_touch_down)
		{
			queue_packet(0x08, m_touch_screen * 2 + 0, 0xff);
			queue_packet(0x08, m_touch_screen * 2 + 1, 0xff);
		}
		m_touch_down = down;
	}""" + s[end:]

# --- driver: pointer drawn on the LCD, LCDs first, layout -------------------------------------------------------
rep("""	required_shared_ptr_array<uint32_t, 2> m_lcdram;""",
"""	required_shared_ptr_array<uint32_t, 2> m_lcdram;
	optional_device<hog2_panel_device> m_panel;""")
rep("""		, m_lcdram(*this, "lcdram%u", 1U)""", """		, m_lcdram(*this, "lcdram%u", 1U)
		, m_panel(*this, "link:panel")""")
rep("""		for (int x = 0; x < 640; x++)
			dst[x] = BIT(src[x >> 3], 7 - (x & 7)) ? 0xffc8d8c0 : 0xff183020;
	}
	return 0;""",
"""		for (int x = 0; x < 640; x++)
			dst[x] = BIT(src[x >> 3], 7 - (x & 7)) ? 0xffc8d8c0 : 0xff183020;
	}
	// the touch pointer: a cross on the LCD it is over, filled while the finger is down
	int px, py;
	bool down;
	if (m_panel && m_panel->pointer(N, px, py, down))
	{
		const uint32_t col = down ? 0xffff4040 : 0xff2060ff;
		for (int d = -7; d <= 7; d++)
		{
			if (px + d >= 0 && px + d < 640 && py >= cliprect.top() && py <= cliprect.bottom()) bitmap.pix(py, px + d) = col;
			if (py + d >= cliprect.top() && py + d <= cliprect.bottom() && px >= 0 && px < 640) bitmap.pix(py + d, px) = col;
		}
	}
	return 0;""")

# screens: LCDs first so they are screen 0/1, then the VGAs; default layout shows the LCDs
vga_start = s.index("\t// two external VGA monitors")
lcd_start = s.index("\t// two built-in touch LCDs")
lcd_end = s.index("\n}\n", lcd_start)
vga_block = s[vga_start:lcd_start]
lcd_block = s[lcd_start:lcd_end]
s = s[:vga_start] + lcd_block + "\n" + vga_block.rstrip("\n") + "\n\n\tconfig.set_default_layout(layout_wholehog2);" + s[lcd_end:]
rep('#include "screen.h"\n', '#include "screen.h"\n\n#include "wholehog2.lh"\n')

open(p, "w").write(s)
print("touch/layout patch applied")
