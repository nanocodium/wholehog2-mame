"""Switch the on-screen keypad from MAME clickable elements to pointer handling inside the layout script (press on
pointer-down over a key, release on pointer-up/leave), so releases can never get lost.
usage: lua_keys.py <gen_layout.py>"""
import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, 1)

# items: no inputtag any more (drawn only); geometry goes into the script
rep("""    items.append('		<element ref="%s" inputtag="%s" inputmask="0x%x"><bounds x="%d" y="%d" width="%d" height="%d" /></element>' % (name, tag, mask, x, y, w, h))""",
"""    items.append('		<element ref="%s"><bounds x="%d" y="%d" width="%d" height="%d" /></element>' % (name, x, y, w, h))
    keytable.append('			{ x = %d, y = %d, w = %d, h = %d, port = "%s", mask = 0x%x },' % (x, y, w, h, tag, mask))""")
rep("""elements = []
items = []""", """elements = []
items = []
keytable = []""")

rep("""				hook("Console", 0, 1280, VIEWH)""",
"""				-- on-screen keys: press the input field while the pointer is down over a key
				local keys = {
KEYTABLE
				}
				local function keyfield(k)
					local port = ports[k.port]
					if port == nil then return nil end
					for _, f in pairs(port.fields) do
						if f.mask == k.mask then return f end
					end
					return nil
				end
				local held = nil
				local function keys_update(type, id, dev, x, y, btn, dn, up, cnt)
					x = x * 1280
					y = y * VIEWH
					local down = (btn & 1) ~= 0
					if down and held == nil then
						for _, k in ipairs(keys) do
							if x >= k.x and x < k.x + k.w and y >= k.y and y < k.y + k.h then
								local f = keyfield(k)
								if f then f:set_value(1); held = f end
								break
							end
						end
					elseif not down and held ~= nil then
						held:set_value(0)
						held = nil
					end
				end
				local function keys_release()
					if held ~= nil then held:set_value(0); held = nil end
				end
				local cview = file.views["Console"]
				if cview ~= nil then
					local touch_update, touch_left, touch_aborted
					hook("Console", 0, 1280, 1280, VIEWH)
					-- chain: the hook installed the touch handler; wrap it so keys are handled too
					local view = cview
					local prev = nil
					view:set_pointer_updated_callback(function(type, id, dev, x, y, btn, dn, up, cnt)
						if y * VIEWH < 480 then
							touch_dispatch(type, id, dev, x, y, btn, dn, up, cnt)
						else
							keys_update(type, id, dev, x, y, btn, dn, up, cnt)
						end
						if (btn & 1) == 0 then keys_release() end
					end)
					view:set_pointer_left_callback(function(...) keys_release(); touch_release() end)
					view:set_pointer_aborted_callback(function(...) keys_release(); touch_release() end)
				end""")

# expose the touch handler pieces from hook() so the Console view can chain them
rep("""					view:set_pointer_updated_callback(update)
					view:set_pointer_left_callback(function(type, id, dev, x, y, up, cnt) fb:set_value(0) end)
					view:set_pointer_aborted_callback(function(type, id, dev, x, y, up, cnt) fb:set_value(0) end)
				end""",
"""					view:set_pointer_updated_callback(update)
					view:set_pointer_left_callback(function(type, id, dev, x, y, up, cnt) fb:set_value(0) end)
					view:set_pointer_aborted_callback(function(type, id, dev, x, y, up, cnt) fb:set_value(0) end)
					touch_dispatch = update
					touch_release = function() if fb then fb:set_value(0) end end
				end""")
rep("""				local function hook(viewname, xoff, xmax, vw, vh)""",
"""				local touch_dispatch, touch_release = function() end, function() end
				local function hook(viewname, xoff, xmax, vw, vh)""")

# fill the key table into the script
rep("""script.replace("VIEWH", str(total_h))""", """script.replace("VIEWH", str(total_h)).replace("KEYTABLE", "\\n".join(keytable))""")
open(p, "w").write(s)
print("lua keys patch applied")
