-- MAME autoboot script: press a sequence of input fields on the Wholehog II after boot.
-- env HOG2_KEYS = comma separated field names, each optionally prefixed with a device tag and '|'
--   e.g. HOG2_KEYS="Tab,Enter" or HOG2_KEYS=":link:panel|Panel key 5,Enter"
-- env HOG2_KEY_AT = seconds after start for the first key (default 14), HOG2_KEY_GAP = seconds between keys (default 1)
local list = os.getenv("HOG2_KEYS") or "Enter"
local at = tonumber(os.getenv("HOG2_KEY_AT") or "14")
local gap = tonumber(os.getenv("HOG2_KEY_GAP") or "1")

local keys = {}
for item in string.gmatch(list, "[^,]+") do
	local dev, name = item:match("^(:[^|]*)|(.*)$")
	if not dev then dev, name = ":at_keyboard", item end
	keys[#keys + 1] = { dev = dev, name = name }
end

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

local function list_fields(devtag)
	for tag, port in pairs(manager.machine.ioport.ports) do
		if tag:sub(1, #devtag) == devtag then
			local names = {}
			for fname, _ in pairs(port.fields) do names[#names + 1] = fname end
			print(tag .. ": " .. table.concat(names, ", "))
		end
	end
end

local start = manager.machine.time.seconds
local idx, pressed = 1, false
emu.register_periodic(function()
	if idx > #keys then return end
	local t = manager.machine.time.seconds - start
	local due = at + (idx - 1) * gap
	local k = keys[idx]
	if not pressed and t >= due then
		local f, tag = find_field(k.dev, k.name)
		if f then
			print(string.format("lua: press %s on %s", k.name, tag))
			f:set_value(1)
		else
			print("lua: no field " .. k.name .. " under " .. k.dev)
			list_fields(k.dev)
		end
		pressed = true
	elseif pressed and t >= due + 0.25 then
		local f = find_field(k.dev, k.name)
		if f then f:set_value(0) end
		pressed = false
		idx = idx + 1
	end
end)
