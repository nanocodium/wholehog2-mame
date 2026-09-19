-- dump both LCD RAM shares to lcd1.bin / lcd2.bin at 25 s
local done = false
emu.register_periodic(function()
	if done or emu.time() < 25 then return end
	done = true
	for i = 1, 2 do
		local sh = manager.machine.memory.shares[":lcdram" .. i]
		local f = io.open("lcd" .. i .. ".bin", "wb")
		for a = 0, sh.size - 1, 4 do f:write(string.pack("<I4", sh:read_u32(a))) end
		f:close()
	end
	print("lua: lcd dumped")
end)
