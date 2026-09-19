-- MAME autoboot script: after the console has found its panel, touch "New Show" on LCD 2, then hold a key.
-- usage: mame wholehog2 ... -autoboot_script /mnt/d/games/hog2/tools/touch_newshow.lua -autoboot_delay 0
local function field(port, name)
	local p = manager.machine.ioport.ports[port]
	if p == nil then print("no port " .. port) return nil end
	for k, f in pairs(p.fields) do if k == name then return f end end
	print("no field " .. name .. " in " .. port)
	return nil
end

local tx = field(":link:panel:TOUCH1_X", "Touch 2 X")
local ty = field(":link:panel:TOUCH1_Y", "Touch 2 Y")
local tb = field(":link:panel:TOUCH_BTN", "Touch screen 2")
local x8, y8 = tonumber(os.getenv("HOG2_TOUCH_X") or "35"), tonumber(os.getenv("HOG2_TOUCH_Y") or "80")

local start = manager.machine.time.seconds
local state = 0
emu.register_periodic(function()
	local t = manager.machine.time.seconds - start
	if state == 0 and t >= 14 then
		print(string.format("lua: touch down at %d,%d", x8, y8))
		if tx then tx:set_value(x8) end
		if ty then ty:set_value(y8) end
		if tb then tb:set_value(1) end
		state = 1
	elseif state == 1 and t >= 14.5 then
		print("lua: touch up")
		if tb then tb:set_value(0) end
		state = 2
	end
end)
