	for (int i = 0; i < 16; i++)
		logerror("i960 r%-2d = %08x   g%-2d = %08x\n", i, m_r[i], i, m_r[16 + i]);
	logerror("i960 PC = %08x AC = %08x IMSK = %08x rcache_pos = %d\n", m_PC, m_AC, m_sfr[1], m_rcache_pos);
	for (int i = 0; i < 0x40; i += 4)
		logerror("i960 mem %03x = %08x\n", 0xc0 + i, m_program.read_dword(0xc0 + i));
