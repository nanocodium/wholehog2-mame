-- at 33 s dump LCD 2's four planes via the CPU address space (read map select through the GC index/data ports)
local done = false
emu.register_periodic(function()
	if done or emu.time() < 33 then return end
	done = true
	local sp = manager.machine.devices[":maincpu"].spaces["program"]
	for p = 0, 3 do
		sp:write_u8(0xaf0003ce, 4)      -- GC index: read map select
		sp:write_u8(0xaf0003cf, p)
		local f = io.open(string.format("plane%d.bin", p), "wb")
		for a = 0, 0xF000 - 1 do f:write(string.char(sp:read_u8(0xbf020000 + a))) end
		f:close()
	end
	print("lua: planes dumped")
end)
