"""Instrument LCD 2's sequencer/graphics-controller registers and log writes into the stale bytes.
usage: gc_trace.py <wholehog2.cpp>"""
import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, 1)

rep("""	map(0xaf0003b0, 0xaf0003df).m(m_lcd[1], FUNC(f65535_vga_device::io_map));""",
"""	map(0xaf0003b0, 0xaf0003df).m(m_lcd[1], FUNC(f65535_vga_device::io_map));
	map(0xaf0013b0, 0xaf0013df).m(m_lcd[1], FUNC(f65535_vga_device::io_map));   // mirror used by the register tracer
	map(0xaf0003c4, 0xaf0003c5).w(FUNC(wholehog2_state::lcd2_seq_w));
	map(0xaf0003ce, 0xaf0003cf).w(FUNC(wholehog2_state::lcd2_gc_w));""")

rep("""	uint32_t m_lcd_stats[10] = {};""",
"""	uint32_t m_lcd_stats[10] = {};
	void lcd2_seq_w(offs_t offset, uint8_t data);
	void lcd2_gc_w(offs_t offset, uint8_t data);
	uint8_t m_seq_idx = 0, m_gc_idx = 0, m_seq_regs[32] = {}, m_gc_regs[16] = {};
	int m_trace_left = 200;""")

rep("""// LCD planar window: byte lanes go to the VGA device one at a time (statistics on access widths for now)""",
"""void wholehog2_state::lcd2_seq_w(offs_t offset, uint8_t data)
{
	if (offset == 0) m_seq_idx = data & 31; else m_seq_regs[m_seq_idx] = data;
	m_maincpu->space(AS_PROGRAM).write_byte(0xaf0013c4 + offset, data);
}

void wholehog2_state::lcd2_gc_w(offs_t offset, uint8_t data)
{
	if (offset == 0) m_gc_idx = data & 15; else m_gc_regs[m_gc_idx] = data;
	m_maincpu->space(AS_PROGRAM).write_byte(0xaf0013ce + offset, data);
}

// LCD planar window: byte lanes go to the VGA device one at a time (statistics on access widths for now)""")

rep("""	int lanes = 0;
	for (int i = 0; i < 4; i++)
		if (mem_mask & (0xffU << (i * 8)))
		{""",
"""	int lanes = 0;
	if (N == 1 && m_trace_left > 0)
	{
		const int row = (offset * 4) / 128, col = (offset * 4) % 128;
		if (row == 200 && col >= 32 && col < 68)
		{
			m_trace_left--;
			logerror("lcd2 write row %d col %d data %08x mask %08x | seq map %02x mode %02x | gc sr %02x esr %02x rot %02x rmap %02x mode %02x bitmask %02x | t=%s pc=%08x\\n",
					row, col, data, mem_mask, m_seq_regs[2], m_seq_regs[4], m_gc_regs[0], m_gc_regs[1], m_gc_regs[3], m_gc_regs[4], m_gc_regs[5], m_gc_regs[8],
					machine().time().as_string(3), m_maincpu->pc());
		}
	}
	for (int i = 0; i < 4; i++)
		if (mem_mask & (0xffU << (i * 8)))
		{""")
open(p, "w").write(s)
print("trace patch applied")
