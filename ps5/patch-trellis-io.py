"""Build-time patch of trellis.cpp's src/trellis_model.cpp: the weight loading reads with mg_pread_full() instead of
fseek + fread (ps5/ps5mg_threads.h). Idempotent."""
import sys

path = sys.argv[1]
s = open(path, newline="").read()
if "mg_pread_full" in s:
    sys.exit(0)
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
old = ("        if (trellis_fseek64(f, (int64_t)off, SEEK_SET) != 0 ||\n"
       "            fread(staging.data(), 1, nbytes, f) != nbytes) {\n")
new = "        if (!mg_pread_full(fileno(f), staging.data(), nbytes, (int64_t)off)) {\n"
if old not in s:
    sys.exit("patch-trellis-io: the read loop is not where it was expected")
s = s.replace(old, new)
if crlf:
    s = s.replace("\n", "\r\n")
open(path, "w", newline="").write(s)
print("patched the weight reads in", path)
