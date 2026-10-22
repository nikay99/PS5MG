"""Build-time patch of trellis.cpp's src/birefnet.cpp: interp() replaced by ps5/birefnet_interp.inc. Idempotent."""
import re, sys
src, inc = sys.argv[1], sys.argv[2]
s = open(src, newline="").read()
if "PS5MG interp" in s or "PS5MG's version of trellis.cpp's interp" in s:
    sys.exit(0)
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
a = s.index("static Feat interp(const Feat& x, int Ho, int Wo) {")
b = s.index("    return o;\n}\n", a) + len("    return o;\n}\n")
new = open(inc).read()
s = s[:a] + new + s[b:]
if "#include <atomic>" not in s:
    s = s.replace("#include", "#include <atomic>\n#include <thread>\n#include", 1)
if crlf:
    s = s.replace("\n", "\r\n")
open(src, "w", newline="").write(s)
print("patched interp in", src)
