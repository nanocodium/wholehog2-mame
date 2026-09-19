"""Route the LCD frame buffers through the C&T/VGA device's planar write logic and render the four planes as
16 grey levels.  usage: planar_lcd.py <wholehog2.cpp>"""
import sys, re

p = sys.argv[1]
s = open(p).read()

def rep(old, new):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, 1)

rep("""	// the OS drives the LCD frame buffers linearly; keep them as plain RAM and render them ourselves (lcd_update)
	map(0xb7000000, 0xb707ffff).flags(i960_cpu_device::BURST).ram().share("lcdram1");
	map(0xbf000000, 0xbf07ffff).flags(i960_cpu_device::BURST).ram().share("lcdram2");""",
"""	// LCD frame buffers: the OS uses the 64 KB at linear offset 0x20000 of each controller in VGA 16-colour planar
	// mode (sequencer memory mode 06), writing planes through the map mask.  Route the window through the VGA
	// device's planar write logic; lcd_update decodes the four planes into 16 grey levels.
	map(0xb7020000, 0xb702ffff).rw(m_lcd[0], FUNC(f65535_vga_device::mem_r), FUNC(f65535_vga_device::mem_w));
	map(0xbf020000, 0xbf02ffff).rw(m_lcd[1], FUNC(f65535_vga_device::mem_r), FUNC(f65535_vga_device::mem_w));""")

rep("""		, m_lcdram(*this, "lcdram%u", 1U)
""", "")
rep("""	required_shared_ptr_array<uint32_t, 2> m_lcdram;
""", "")

start = s.index("	const uint8_t *vram = reinterpret_cast<const uint8_t *>(m_lcdram[N].target()) + 0x20000;")
end = s.index("	// the touch pointer: a cross on the LCD it is over", start)
s = s[:start] + """	// 640x480, 128 bytes per plane row, 4 planes = 16 grey levels.  Plane bits set drive the pixel dark on the
	// STN panel (the handbook shows light buttons with dark text); the polarity is configurable.
	const bool invert = BIT(m_conf->read(), 0);
	uint32_t shades[16];
	for (int v = 0; v < 16; v++)
	{
		const int level = invert ? v : 15 - v;                // 0 = dark ... 15 = light
		const int r = 0x1c + (0xdc - 0x1c) * level / 15, g = 0x24 + (0xe0 - 0x24) * level / 15, b = 0x30 + (0xdc - 0x30) * level / 15;
		shades[v] = 0xff000000 | (r << 16) | (g << 8) | b;
	}
	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		uint32_t *dst = &bitmap.pix(y, 0);
		for (int bx = 0; bx < 80; bx++)
		{
			const offs_t off = y * 128 + bx;
			const uint8_t p0 = m_lcd[N]->mem_linear_r(off), p1 = m_lcd[N]->mem_linear_r(off + 0x10000),
					p2 = m_lcd[N]->mem_linear_r(off + 0x20000), p3 = m_lcd[N]->mem_linear_r(off + 0x30000);
			for (int bit = 0; bit < 8; bit++)
			{
				const int sh = 7 - bit;
				const int v = BIT(p0, sh) | (BIT(p1, sh) << 1) | (BIT(p2, sh) << 2) | (BIT(p3, sh) << 3);
				dst[bx * 8 + bit] = shades[v];
			}
		}
	}
""" + s[end:]

# dump helper no longer has the shares
start = s.index("void wholehog2_state::dump_lcd_ram()")
end = s.index("\n}\n", start) + 3
s = s[:start] + """void wholehog2_state::dump_lcd_ram()
{
	for (int n = 0; n < 2; n++)
	{
		FILE *f = fopen(n ? "lcd2.bin" : "lcd1.bin", "wb");
		if (!f) continue;
		for (offs_t a = 0; a < 0x40000; a++) fputc(m_lcd[n]->mem_linear_r(a), f);
		fclose(f);
	}
}
""" + s[end:]

open(p, "w").write(s)
print("planar LCD patch applied")
