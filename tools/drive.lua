-- MAME autoboot script: drive the Wholehog II with a scripted sequence and take a snapshot after each step.
-- env HOG2_SCRIPT = ';' separated steps:
--   wait <sec>              wait
--   key <name>              press an AT keyboard key (field name as MAME shows it, e.g. "S", "Enter", "<--", "F1")
--   pkey <name>             press a front panel key field, e.g. "Panel key 5"
--   touch <screen> <x> <y>  touch LCD <screen> (1 or 2) at 8-bit coordinates (x*255/640, y*255/480)
--   snap                    take a snapshot (snap/wholehog2/NNNN.png)
-- default: wait 14; touch 2 151 138; wait 3; snap
local script = os.getenv("HOG2_SCRIPT") or "wait 14;touch 2 151 138;wait 3;snap"
local hold = tonumber(os.getenv("HOG2_HOLD") or "0.12")

local steps = {}
for item in string.gmatch(script, "[^;]+") do
	local words = {}
	for w in string.gmatch(item, "%S+") do words[#words + 1] = w end
	if #words > 0 then steps[#steps + 1] = words end
end

local function find_field(devtag, name)
	for tag, port in pairs(manager.machine.ioport.ports) do
		if tag:sub(1, #devtag) == devtag then
			for fname, f in pairs(port.fields) do
				if fname == name then return f, tag end
			end
		end
	end
	print("lua: no field '" .. name .. "' under " .. devtag)
	return nil
end

local function key_name(words)
	local n = words[2]
	for i = 3, #words do n = n .. " " .. words[i] end
	return n
end

local idx, phase, t_next = 1, 0, 0
local held = {}
local function step()
	local now = emu.time()
	if now < t_next then return end
	if idx > #steps then return end
	local s = steps[idx]
	if phase == 1 then
		for _, f in ipairs(held) do f:set_value(0) end
		held = {}
		phase = 0
		idx = idx + 1
		t_next = now + 0.25
		return
	end
	local cmd = s[1]
	print(string.format("lua: step %d: %s", idx, table.concat(s, " ")))
	if cmd == "wait" then
		t_next = now + tonumber(s[2])
		idx = idx + 1
	elseif cmd == "dumpplanes" then
		local sp = manager.machine.devices[":maincpu"].spaces["program"]
		for p = 0, 3 do
			sp:write_u8(0xaf0003ce, 4); sp:write_u8(0xaf0003cf, p)
			local f = io.open(string.format("plane%d.bin", p), "wb")
			for a = 0, 0xF000 - 1 do f:write(string.char(sp:read_u8(0xbf020000 + a))) end
			f:close()
		end
		print("lua: planes dumped")
		idx = idx + 1
	elseif cmd == "snap" then
		manager.machine.video:snapshot()
		print("lua: snapshot after step " .. (idx - 1))
		idx = idx + 1
	elseif cmd == "key" or cmd == "pkey" then
		local dev = (cmd == "key") and ":at_keyboard" or ":link:panel"
		local f = find_field(dev, key_name(s))
		if f then f:set_value(1); held = { f } end
		print("lua: " .. cmd .. " " .. key_name(s))
		phase = 1
		t_next = now + hold
	elseif cmd == "afield" then
		local nm = s[3]; for i = 4, #s do nm = nm .. " " .. s[i] end
		local f = find_field(":link:panel", nm)
		if f then f:set_value(tonumber(s[2])) end
		print("lua: afield " .. nm .. " = " .. s[2])
		idx = idx + 1
	elseif cmd == "fader" then
		local f = find_field(":link:panel", "Fader " .. s[2] .. ((tonumber(s[2]) == 9) and " (Grand Master)" or ""))
		if f then f:set_value(tonumber(s[3])) end
		print("lua: fader " .. s[2] .. " = " .. s[3])
		idx = idx + 1
	elseif cmd == "touch" then
		-- touch <screen 1|2> <x 0..255> <y 0..255> on that LCD; the panel has one pointer over both LCDs (12-bit)
		local scr = tonumber(s[2]) - 1
		local fx = find_field(":link:panel", "Touch pointer X")
		local fy = find_field(":link:panel", "Touch pointer Y")
		local fb = find_field(":link:panel", "Touch")
		local xt = (scr * 640 + tonumber(s[3]) * 640 / 255) * 4096 / 1280
		local yt = tonumber(s[4]) * 4096 / 255
		if fx then fx:set_value(math.floor(xt)) end
		if fy then fy:set_value(math.floor(yt)) end
		if fb then fb:set_value(1); held = { fb } end
		print("lua: touch " .. s[2] .. " at " .. s[3] .. "," .. s[4])
		phase = 1
		t_next = now + hold
	else
		print("lua: unknown step " .. cmd)
		idx = idx + 1
	end
end

emu.register_periodic(function()
	local ok, err = pcall(step)
	if not ok then
		print("lua: error: " .. tostring(err))
		idx = idx + 1
		phase = 0
	end
end)
