"""Generate mame/wholehog2.lay: a front-panel view modelled on the Wholehog II desk (two touch LCDs, view and window
control buttons above them, masters with faders and Go/Halt keys, three parameter wheels, function keys and keypad).
Touch, keys, faders and wheels are all driven from the layout script's pointer events.
usage: gen_layout.py <out.lay>"""
import sys

out = sys.argv[1]

def pk(idx): return ':link:panel:KEYS%d' % (idx // 8), 1 << (idx % 8)
KB0, KB1, KB2, KB3 = [':at_keyboard:pc_keyboard_%d' % i for i in range(4)]

W = 1680
LCD_Y = 70
PANEL_H = 1090

buttons = []    # x, y, w, h, label, tag, mask, style
def key(x, y, w, h, label, tagmask, style="key"):
    tag, mask = tagmask
    buttons.append((x, y, w, h, label, tag, mask, style))

# ---- rows above the LCDs ------------------------------------------------------------------------------------
for i, idx in enumerate([32, 34, 60, 62, 63, 80, 83, 84, 90, 96]):          # view buttons (function unknown)
    key(60 + i * 66, 14, 60, 46, "V%d" % (i + 1), pk(idx), "view")
for i, idx in enumerate([48, 49, 50, 51, 52, 53, 54, 55, 58, 59]):          # window controls
    key(1000 + i * 66, 14, 60, 46, ["Close", "Pg Up", "Pg Dn", "Left", "Right", "Split", "Toggle", "Shuffle", "W9", "W10"][i], pk(idx), "view")

# ---- masters (left, below LCD 1) ----------------------------------------------------------------------------
MX, MY = 40, LCD_Y + 480 + 30
MW, MH, MG = 64, 42, 10
for m in range(8):
    x = MX + 60 + m * (MW + MG)
    key(x, MY, MW, MH, "List %d" % (m + 1), pk(40 + m), "master")
    key(x, MY + 52, MW, MH, "Go %d" % (m + 1), pk(0 + m), "go")
    key(x, MY + 104, MW, MH, "Halt %d" % (m + 1), pk(8 + m), "master")
    key(x, MY + 156, MW, MH, "Flash %d" % (m + 1), pk(16 + m), "master")
    key(x, MY + 208, MW, MH, "Choose %d" % (m + 1), pk(24 + m), "master")
key(MX, MY, 50, MH, "Key 33", pk(33), "key")
key(MX, MY + 208, 50, MH, "Key 35", pk(35), "key")
key(MX + 60 + 8 * (MW + MG) + 6, MY + 52, 56, MH, "Next", pk(61), "key")
key(MX + 60 + 8 * (MW + MG) + 6, MY + 104, 56, MH, "Key 67", pk(67), "key")
key(MX + 60 + 8 * (MW + MG) + 6, MY + 156, 56, MH, "Key 74", pk(74), "key")
key(MX + 60 + 8 * (MW + MG) + 6, MY + 208, 56, MH, "Key 82", pk(82), "key")

# faders: 8 masters + crossfader + grand master, drawn as tracks with a knob animated by the fader value
faders = []     # x, y, w, h, port, name
FY, FH = MY + 270, 230
for m in range(8):
    x = MX + 60 + m * (MW + MG) + MW // 2 - 14
    faders.append((x, FY, 28, FH, ":link:panel:FADER%d" % m, "Fader %d" % (m + 1)))
faders.append((MX + 60 + 8 * (MW + MG) + 66, FY, 28, FH, ":link:panel:FADER9", "Grand Master (A)"))
faders.append((MX + 6, FY, 28, FH, ":link:panel:FADER8", "Fader 9 (crossfader)"))
key(MX + 60 + 8 * (MW + MG) + 62, FY + 20, 70, 50, "Main\nGo", pk(87), "go")        # uses Enter-like key; unknown real key
key(MX + 60 + 8 * (MW + MG) + 62, FY + 110, 70, 50, "Main\nHalt", pk(97), "key")

# ---- right side ---------------------------------------------------------------------------------------------
RX = 1000
wheels = []     # cx, cy, r, port
for i in range(3):
    wheels.append((RX + 90 + i * 200, LCD_Y + 480 + 85, 50, ":link:panel:WHEEL%d" % i))
# parameter / group keys
GY = LCD_Y + 480 + 165
for i, (label, idx) in enumerate([("Group", 36), ("Position", 37), ("Colour", 38), ("Beam", 39)]):
    key(RX + i * 70, GY, 64, 44, label, pk(idx), "param")
for i, (label, idx) in enumerate([("Macro", 88), ("Page", 89), ("List", 91), ("Active", 72)]):
    key(RX + 340 + i * 70, GY, 64, 44, label, pk(idx), "param")

# function block (4 x 4)
FX, FBY = RX, GY + 64
funcs = [
    [("Record", pk(75)), ("Load", pk(73)), ("Copy", pk(64)), ("Move", pk(65))],
    [("Delete", pk(66)), ("Goto", pk(81)), ("Setup", (KB1, 0x8000)), ("Pig", (KB1, 0x2000))],
    [("Undo", (KB1, 0x0100)), ("Blind", (KB3, 0x0008)), ("Set", pk(99)), ("Clear", pk(98))],
    [("Esc", (KB0, 0x0002)), ("Tab", (KB0, 0x8000)), ("Shift", (KB2, 0x4000)), ("Key 87", pk(87))],
]
for r, row in enumerate(funcs):
    for c, (label, tm) in enumerate(row):
        key(FX + c * 70, FBY + r * 56, 64, 48, label, tm, "func")

# keypad (4 x 5) at the far right
KX = RX + 340
keypad = [
    [("←", pk(68)), ("/", pk(69)), ("-", pk(70)), ("+", pk(71))],
    [("7", pk(76)), ("8", pk(77)), ("9", pk(78)), ("Thru", pk(79))],
    [("4", pk(92)), ("5", pk(93)), ("6", pk(94)), ("@", pk(103))],
    [("1", pk(100)), ("2", pk(101)), ("3", pk(102)), ("Full", pk(95))],
    [("0", pk(85)), (".", pk(86)), ("Enter", pk(87))],
]
for r, row in enumerate(keypad):
    for c, (label, tm) in enumerate(row):
        w = 64 if not (r == 4 and c == 2) else 64 * 2 + 6
        key(KX + c * 70, FBY + r * 56, w, 48, label, tm, "pad")

styles = {
    "key":    ("0.55", "0.57", "0.60", "0.05", "0.05", "0.08"),
    "view":   ("0.62", "0.64", "0.66", "0.05", "0.05", "0.08"),
    "master": ("0.58", "0.56", "0.52", "0.05", "0.05", "0.08"),
    "go":     ("0.30", "0.62", "0.34", "0.05", "0.08", "0.05"),
    "param":  ("0.50", "0.55", "0.66", "0.05", "0.05", "0.08"),
    "func":   ("0.60", "0.60", "0.60", "0.05", "0.05", "0.08"),
    "pad":    ("0.72", "0.73", "0.75", "0.05", "0.05", "0.08"),
}

elements, items, keytable = [], [], []
for n, (x, y, w, h, label, tag, mask, style) in enumerate(buttons):
    br, bg, bb, tr, tg, tb = styles[style]
    name = "b%d" % n
    lines = label.split("\n")
    texts = ""
    if len(lines) == 1:
        texts = '<text string="%s"><color red="%s" green="%s" blue="%s" /><bounds x="0.04" y="0.25" width="0.92" height="0.5" /></text>' % (
            lines[0].replace("&", "&amp;").replace("<", "&lt;"), tr, tg, tb)
    else:
        for i, ln in enumerate(lines):
            texts += '<text string="%s"><color red="%s" green="%s" blue="%s" /><bounds x="0.04" y="%.2f" width="0.92" height="0.4" /></text>' % (
                ln, tr, tg, tb, 0.08 + i * 0.45)
    elements.append('''	<element name="%s" defstate="0">
		<rect state="0"><color red="%s" green="%s" blue="%s" /></rect>
		<rect state="1"><color red="0.95" green="0.55" blue="0.25" /></rect>
		%s
	</element>''' % (name, br, bg, bb, texts))
    items.append('		<element ref="%s"><bounds x="%d" y="%d" width="%d" height="%d" /></element>' % (name, x, y, w, h))
    keytable.append('			{ x = %d, y = %d, w = %d, h = %d, port = "%s", mask = 0x%x },' % (x, y, w, h, tag, mask))

# faders: track + knob animated by the analog value (0 at the bottom, 255 at the top)
fadertable = []
for n, (x, y, w, h, port, name) in enumerate(faders):
    items.append('		<element ref="track"><bounds x="%d" y="%d" width="%d" height="%d" /></element>' % (x + w // 2 - 3, y, 6, h))
    items.append('''		<element ref="knob" inputtag="%s" inputmask="0xff">
			<animate inputtag="%s" mask="0xff" />
			<bounds state="0" x="%d" y="%d" width="%d" height="14" />
			<bounds state="255" x="%d" y="%d" width="%d" height="14" />
		</element>''' % (port, port, x, y + h - 14, w, x, y, w))
    fadertable.append('			{ x = %d, y = %d, w = %d, h = %d, port = "%s" },' % (x - 8, y - 10, w + 16, h + 20, port))
wheeltable = []
for cx, cy, r, port in wheels:
    items.append('		<element ref="wheel"><bounds x="%d" y="%d" width="%d" height="%d" /></element>' % (cx - r, cy - r, 2 * r, 2 * r))
    items.append('		<element ref="wheeldot"><bounds x="%d" y="%d" width="10" height="10" /></element>' % (cx - 5, cy - r + 6))
    wheeltable.append('			{ x = %d, y = %d, w = %d, h = %d, port = "%s" },' % (cx - r, cy - r, 2 * r, 2 * r, port))

script = r'''
	<script><![CDATA[
		file:set_resolve_tags_callback(
			function()
				local ports = machine.ioport.ports
				local function field(port, name)
					local p = ports[port]
					if p == nil then return nil end
					for fname, f in pairs(p.fields) do
						if fname == name then return f end
					end
					return nil
				end
				local function field_by_mask(port, mask)
					local p = ports[port]
					if p == nil then return nil end
					for _, f in pairs(p.fields) do
						if f.mask == mask then return f end
					end
					return nil
				end
				local function analog_field(port)
					local p = ports[port]
					if p == nil then return nil end
					for _, f in pairs(p.fields) do return f end
					return nil
				end
				local fx = field(":link:panel:TOUCH_X", "Touch pointer X")
				local fy = field(":link:panel:TOUCH_Y", "Touch pointer Y")
				local fb = field(":link:panel:TOUCH_BTN", "Touch")
				local keys = {
KEYTABLE
				}
				local faders = {
FADERTABLE
				}
				local wheels = {
WHEELTABLE
				}
				local held_key, held_fader, held_wheel, wheel_last_y = nil, nil, nil, 0

				local function release_all()
					if fb then fb:set_value(0) end
					if held_key then held_key:set_value(0); held_key = nil end
					held_fader = nil
					held_wheel = nil
				end

				-- geometry of the LCDs in each view: {x, y, width} for LCD 1 and LCD 2, plus view size
				local function make_handler(vw, vh, lcd1, lcd2)
					return function(type, id, dev, x, y, btn, dn, up, cnt)
						x = x * vw
						y = y * vh
						local down = (btn & 1) ~= 0
						-- touch screens
						for si, lcd in ipairs({lcd1, lcd2}) do
							if lcd and x >= lcd[1] and x < lcd[1] + 640 and y >= lcd[2] and y < lcd[2] + 480 then
								if fx then
									fx:set_value(math.floor(((si - 1) * 640 + (x - lcd[1])) * 4096 / 1280))
									fy:set_value(math.floor((y - lcd[2]) * 4096 / 480))
									fb:set_value(down and 1 or 0)
								end
								if not down then release_all() end
								return
							end
						end
						if fb then fb:set_value(0) end
						if down then
							if held_fader then
								local fld = held_fader
								local v = math.floor((fld.y + fld.h - 10 - y) * 255 / (fld.h - 20))
								if v < 0 then v = 0 elseif v > 255 then v = 255 end
								fld.f:set_value(v)
								return
							end
							if held_wheel then
								local dy = wheel_last_y - y
								if math.abs(dy) >= 3 then
									local v = (held_wheel.f.port:read() & 0xff)
									held_wheel.f:set_value((v + math.floor(dy / 3)) & 0xff)
									wheel_last_y = y
								end
								return
							end
							if held_key == nil then
								for _, k in ipairs(keys) do
									if x >= k.x and x < k.x + k.w and y >= k.y and y < k.y + k.h then
										local f = field_by_mask(k.port, k.mask)
										if f then f:set_value(1); held_key = f end
										return
									end
								end
								for _, fd in ipairs(faders) do
									if x >= fd.x and x < fd.x + fd.w and y >= fd.y and y < fd.y + fd.h then
										local f = analog_field(fd.port)
										if f then
											held_fader = { f = f, y = fd.y, h = fd.h }
											local v = math.floor((fd.y + fd.h - 10 - y) * 255 / (fd.h - 20))
											if v < 0 then v = 0 elseif v > 255 then v = 255 end
											f:set_value(v)
										end
										return
									end
								end
								for _, wh in ipairs(wheels) do
									if x >= wh.x and x < wh.x + wh.w and y >= wh.y and y < wh.y + wh.h then
										local f = analog_field(wh.port)
										if f then held_wheel = { f = f }; wheel_last_y = y end
										return
									end
								end
							end
						else
							release_all()
						end
					end
				end
				local function hook(viewname, vw, vh, lcd1, lcd2)
					local view = file.views[viewname]
					if view == nil then return end
					view:set_pointer_updated_callback(make_handler(vw, vh, lcd1, lcd2))
					view:set_pointer_left_callback(function(...) release_all() end)
					view:set_pointer_aborted_callback(function(...) release_all() end)
				end
				hook("Console", 1700, PANELH, {60, LCDY}, {1000, LCDY})
				hook("Touch LCDs", 1280, 480, {0, 0}, {640, 0})
				hook("Left LCD", 640, 480, {0, 0}, nil)
				hook("Right LCD", 640, 480, nil, {0, 0})
			end)
	]]></script>
'''

lay = '''<?xml version="1.0"?>
<!--
license:CC0-1.0
Wholehog II front panel: touch LCDs (screens 0 and 1), console keys, faders and wheels; VGA monitors are screens 2/3.
Generated by tools/gen_layout.py; interaction is handled by the embedded script (needs MAME's "layout" plugin).
-->
<mamelayout version="2">
	<element name="bg"><rect><color red="0.16" green="0.18" blue="0.30" /></rect></element>
	<element name="bezel"><rect><color red="0.08" green="0.08" blue="0.10" /></rect></element>
	<element name="track"><rect><color red="0.05" green="0.05" blue="0.06" /></rect></element>
	<element name="knob" defstate="0"><rect><color red="0.85" green="0.85" blue="0.82" /></rect><rect><color red="0.3" green="0.3" blue="0.3" /><bounds x="0" y="0.4" width="1" height="0.2" /></rect></element>
	<element name="wheel"><disk><color red="0.20" green="0.20" blue="0.22" /></disk><disk><color red="0.35" green="0.35" blue="0.38" /><bounds x="0.1" y="0.1" width="0.8" height="0.8" /></disk></element>
	<element name="wheeldot"><disk><color red="0.9" green="0.9" blue="0.9" /></disk></element>
%s

	<view name="Console">
		<element ref="bg"><bounds x="0" y="0" width="%d" height="%d" /></element>
		<element ref="bezel"><bounds x="48" y="%d" width="664" height="504" /></element>
		<element ref="bezel"><bounds x="988" y="%d" width="664" height="504" /></element>
		<screen index="0"><bounds x="60" y="%d" width="640" height="480" /></screen>
		<screen index="1"><bounds x="1000" y="%d" width="640" height="480" /></screen>
%s
	</view>
	<view name="Touch LCDs">
		<screen index="0"><bounds x="0" y="0" width="640" height="480" /></screen>
		<screen index="1"><bounds x="640" y="0" width="640" height="480" /></screen>
	</view>
	<view name="LCDs and monitors">
		<screen index="2"><bounds x="0" y="0" width="640" height="480" /></screen>
		<screen index="3"><bounds x="650" y="0" width="640" height="480" /></screen>
		<screen index="0"><bounds x="0" y="490" width="640" height="480" /></screen>
		<screen index="1"><bounds x="650" y="490" width="640" height="480" /></screen>
	</view>
	<view name="Left LCD">
		<screen index="0"><bounds x="0" y="0" width="640" height="480" /></screen>
	</view>
	<view name="Right LCD">
		<screen index="1"><bounds x="0" y="0" width="640" height="480" /></screen>
	</view>
%s
</mamelayout>
''' % ("\n".join(elements), W + 20, PANEL_H, LCD_Y - 12, LCD_Y - 12, LCD_Y, LCD_Y, "\n".join(items),
       script.replace("KEYTABLE", "\n".join(keytable)).replace("FADERTABLE", "\n".join(fadertable))
             .replace("WHEELTABLE", "\n".join(wheeltable)).replace("PANELH", str(PANEL_H)).replace("LCDY", str(LCD_Y)))

open(out, "w", encoding="utf-8").write(lay)
print("layout written: %d buttons, %d faders, %d wheels" % (len(buttons), len(faders), len(wheels)))
