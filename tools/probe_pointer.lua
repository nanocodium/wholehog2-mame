-- print the touch pointer field values once a second
local function field(name)
	for tag, port in pairs(manager.machine.ioport.ports) do
		if tag:sub(1, 11) == ":link:panel" then
			for fname, f in pairs(port.fields) do if fname == name then return f end end
		end
	end
end
local fx, fy, fb = field("Touch pointer X"), field("Touch pointer Y"), field("Touch")
local last = -1
emu.register_periodic(function()
	local t = math.floor(emu.time())
	if t ~= last then
		last = t
		local px = fx and fx.port:read() or -1
		print(string.format("pointer t=%d x=%d y=%d btn=%d", t, fx.port:read() & 0xfff, fy.port:read() & 0xfff, fb.port:read() & 1))
	end
end)
