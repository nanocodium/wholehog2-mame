"""Join a C string literal that a sed step split across two lines (the newline should have been an escaped \\n).
usage: fix_trace_line.py <file>"""
import re, sys

p = sys.argv[1]
s = open(p).read()
# a line ending inside a string literal: ...%08x<newline>", args);
s2 = re.sub(r'(logerror\("[^"\n]*)\n(", )', lambda m: m.group(1) + '\\n' + m.group(2), s)
print("fixed" if s2 != s else "nothing to fix")
open(p, "w").write(s2)
