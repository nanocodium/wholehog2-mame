"""Generate a Wholehog II user fixture library (_LIB.LIB) for every fixture of the Minecraft mod
"Theatrical Extra Lights" (github.com/dumann089/TheatricalExtraLights, ver/1.20.1).

Input : tools/tel_personalities.txt (dumped from the mod's fixtures/*.java by the grep in the README) plus the
        hand-written overrides below for the fixtures whose Java builds the personality in a loop.
Output: a _LIB.LIB in the Hog II text format (handbook appendix "Fixture Library"): one Hog fixture per DMX
        personality, named "<Fixture> <N>ch", manufacturer 0 (Generic) with a unique product code each.
usage: tel_library.py <tel_personalities.txt> <out _LIB.LIB>
"""
import re, sys

src, out = sys.argv[1], sys.argv[2]

# ---------------------------------------------------------------- parse the dump: "## Name" / "P n label" / "  SLOT comment"
fixtures = {}          # name -> list of (nch, label, [(slot, comment), ...])
cur = None
for line in open(src, encoding="utf-8"):
    line = line.rstrip("\n")
    if line.startswith("## "):
        cur = line[3:].strip(); fixtures[cur] = []
    elif re.match(r"^(DMXPersonality \w+ = )?P \d+ ", line):
        m = re.search(r"P (\d+) (.*)", line)
        fixtures[cur].append([int(m.group(1)), m.group(2).rstrip(" ,;"), []])
    elif line.startswith("  ") and fixtures.get(cur) and re.match(r"^  [A-Z]+", line):
        parts = line.strip().split(None, 1)
        slot = parts[0].rstrip(",;")
        comment = parts[1] if len(parts) > 1 else ""
        comment = re.sub(r"^[,;]?\s*(//\s*)?", "", comment).strip(" ,;")
        comment = re.sub(r"^\d+:\s*", "", comment)          # "16: Pan" -> "Pan"
        fixtures[cur][-1][2].append((slot, comment))

# ---------------------------------------------------------------- overrides for loop-built personalities
cell = lambda n, slots: [s for _ in range(n) for s in slots]
fixtures["AtomicStrobe"] = [[34, "34-Channel Atomic", cell(8, [("RED", ""), ("GREEN", ""), ("BLUE", "")]) + cell(9, [("INTENSITY", "")]) + [("FOCUS", "")]]]
fixtures["PyroFan"] = [[3, "3-Channel Pyro Fan", [("INTENSITY", ""), ("TILT", ""), ("FOCUS", "")]],
                       [10, "10-Channel Pyro Fan", [("INTENSITY", "Tube %d" % i) for i in range(1, 11)]]]
fixtures["MovingMiniBar"] = [[5, "5ch - Unite Beam", [("INTENSITY", ""), ("TILT", ""), ("RED", ""), ("GREEN", ""), ("BLUE", "")]],
                             [35, "35ch - Alone Beam", cell(7, [("INTENSITY", ""), ("TILT", ""), ("RED", ""), ("GREEN", ""), ("BLUE", "")])]]
# the par64 racks list nine single-channel colour variants plus an iRGB mode: one dimmer personality covers the nine
for f in ("a1x1par64", "a2x8par64", "a6x3par64_vertical"):
    fixtures[f] = [[1, "1-Channel Mode", [("INTENSITY", "")]], [4, "iRGB", [("INTENSITY", ""), ("RED", ""), ("GREEN", ""), ("BLUE", "")]]]
# label says 2-Channel but three slots are declared; the mod sends 3
for f in ("WaltzCurtain", "WaltzesWaterJet"):
    fixtures[f][0][0] = 3

# ---------------------------------------------------------------- Hog parameter templates
#            name        kind type      default highlight
SLOT = {"INTENSITY": ("Intensity", "i", "htp8bit", 0, 255),
        "RED":       ("Red",       "c", "ltp8bit", 255, 255),
        "GREEN":     ("Green",     "c", "ltp8bit", 255, 255),
        "BLUE":      ("Blue",      "c", "ltp8bit", 255, 255),
        "PAN":       ("Pan",       "f", "ltp8bit", 128, 128),
        "TILT":      ("Tilt",      "f", "ltp8bit", 128, 128),
        "FOCUS":     ("Focus",     "b", "ltp8bit", 128, 128),
        "STROBE":    ("Strobe",    "b", "ltp8bit", 0, 0),
        "EFFECT":    ("Effect",    "b", "ltp8bit", 0, 0)}
# comments on FOCUS slots name the real function; map them onto Hog parameter names (max 10 chars)
COMMENT = {"Prism": "Prism", "Prism Zoom": "Zoom", "Prism Rotation": "Prism rot", "Prism Beams": "Prism",
           "Gobo Slots": "Gobo", "Gobo Zoom": "Zoom", "Gobo Rot": "Gobo rot",
           "Pattern": "Gobo", "Size": "Zoom", "Amplitude": "Fxdepth", "Speed": "Speed", "Rotation": "Gobo rot",
           "Focus": "Focus", "Persistence": "Trails", "Intensity": "Intensity", "Pan": "Pan", "Tilt": "Tilt"}

def hog_params(slots):
    """turn the slot list into unique Hog parameter definitions"""
    params = []
    counts = {}
    for slot, comment in slots:
        name, kind, typ, dflt, hl = SLOT[slot]
        if comment:
            c = re.sub(r"^[RGB]\d$", lambda m: {"R": "Red", "G": "Green", "B": "Blue"}[m.group(0)[0]] + m.group(0)[1], comment)
            name = COMMENT.get(comment, c)[:10]
        params.append([name, kind, typ, dflt, hl])
    # number duplicates: "Red", "Red 2", ...  (Hog names must be unique within a fixture, max ~10 chars)
    seen = {}
    for p in params:
        seen[p[0]] = seen.get(p[0], 0) + 1
    idx = {}
    for p in params:
        if seen[p[0]] > 1:
            idx[p[0]] = idx.get(p[0], 0) + 1
            if idx[p[0]] > 1:
                base = {"Intensity": "Int"}.get(p[0], p[0])[:10 - len(" %d" % idx[p[0]])]
                p[0] = "%s %d" % (base, idx[p[0]])
    return params

# ---------------------------------------------------------------- emit
lines = []
entries = []
product = 900
for fname in sorted(fixtures, key=str.lower):
    for nch, label, slots in fixtures[fname]:
        if len(slots) != nch:
            print("warning: %s %s declares %d channels but lists %d slots" % (fname, label, nch, len(slots)), file=sys.stderr)
        short = re.sub(r"[^A-Za-z0-9]", "", fname)[:9] + str(nch)          # fixture label: no spaces
        name = ("%s %dch" % (re.sub(r"^a(\d)", r"\1", fname), nch))[:16]     # shown in Add Fixtures
        params = hog_params(slots)
        e = ["fixture = %s" % short, "manufacturer = 0", "product = %d" % product, "name = %s" % name]
        product += 1
        if any(p[1] == "f" for p in params):
            e.append("yoke = yes")
        e.append("output = dmx")
        for pname, kind, typ, dflt, hl in params:
            e += ["parameter = %s" % pname, "kind = %s" % kind, "type = %s" % typ, "default = %d" % dflt,
                  "highlight = %d" % hl, "crossfade = %d" % (1 if kind in "bc" and pname not in ("Red", "Green", "Blue", "Focus", "Zoom") and not pname.startswith(("Red", "Green", "Blue")) else 0),
                  "range = 0, 255, %"]
        entries.append(e)

lines = ["version =  40", "count =  %d" % len(entries), ""]
for e in entries:
    lines += e + [""]
open(out, "w", newline="\r\n").write("\n".join(lines))
print("%d Hog fixtures from %d mod fixtures -> %s" % (len(entries), len(fixtures), out))
