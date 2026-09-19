-- MAME autoboot script: press one AT keyboard key on the Wholehog II after boot.
-- env HOG2_KEY = field name as MAME shows it (default "Enter"), HOG2_KEY_AT = seconds after start (default 14)
local wanted = os.getenv("HOG2_KEY") or "Enter"
local devtag = os.getenv("HOG2_KEY_DEV") or ":at_keyboard"
local at = tonumber(os.getenv("HOG2_KEY_AT") or "14")

local function find_field(devtag, name)
	for tag, port in pairs(manager.machine.ioport.ports) do
		if tag:sub(1, #devtag) == devtag then
			for fname, f in pairs(port.fields) do
				if fname == name then return f, tag end
			end
		end
	end
	return nil
end

local start = manager.machine.time.seconds
local state = 0
emu.register_periodic(function()
	local t = manager.machine.time.seconds - start
	if state == 0 and t >= at then
		local f, tag = find_field(devtag, wanted)
		if f then
			print("lua: press " .. wanted .. " on " .. tag)
			f:set_value(1)
		else
			print("lua: no key field named " .. wanted)
			for tag, port in pairs(manager.machine.ioport.ports) do
				if tag:sub(1, #devtag) == devtag then
					local names = {}
					for fname, _ in pairs(port.fields) do names[#names + 1] = fname end
					print(tag .. ": " .. table.concat(names, ", "))
				end
			end
		end
		state = 1
	elseif state == 1 and t >= at + 0.3 then
		local f = find_field(devtag, wanted)
		if f then f:set_value(0) end
		print("lua: release")
		state = 2
	end
end)
