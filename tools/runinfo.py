"""Reads a PS5MG job log (out/ps5_<name>.log, the stamped log of one job) into stage times.
usage: python runinfo.py out/ps5_b05_sword_tex.log
Stages are the '[n/N] ...' lines of trellis-cli; the seconds are those the console's clock measured in the job."""
import re
import sys


def parse(path):
    stamp = re.compile(r"^\[\s*([0-9.]+)\] (.*)$")
    rows = []
    for line in open(path, errors="replace"):
        m = stamp.match(line.rstrip("\n"))
        if m:
            rows.append((float(m.group(1)), m.group(2)))
    info = {"stages": {}, "total": None, "faces": None, "voxels": None, "load_s": 0.0, "steps": {}}
    start = next((t for t, s in rows if "JOB" in s and "START" in s), rows[0][0] if rows else 0.0)
    marks = []  # (time, label)
    for t, s in rows:
        m = re.match(r"\[(\d)/(\d)\] (.*)", s)
        if m:
            marks.append((t, m.group(3).split(" (")[0].split(" /data")[0].strip()))
        if "decimate_qem" in s:
            m2 = re.search(r"F (\d+)->(\d+)", s)
            if m2:
                info["faces"] = int(m2.group(2))
        m3 = re.search(r"decoded voxels @res\d+ = (\d+)", s)
        if m3:
            info["voxels"] = int(m3.group(1))
        m4 = re.search(r"load \S+\.gguf\s+([0-9.]+) ms", s)
        if m4:
            info["load_s"] += float(m4.group(1)) / 1000.0
        m5 = re.search(r"\[flow\] (\d+) steps, (\d+) forwards, ([0-9.]+)s", s)
        if m5:
            info["steps"].setdefault("flows", []).append(float(m5.group(3)))
        m6 = re.search(r"done in ([0-9.]+)s", s)
        if m6:
            info["total"] = float(m6.group(1))
            end = t
    if marks:
        end = locals().get("end", rows[-1][0])
        for i, (t, label) in enumerate(marks):
            nxt = marks[i + 1][0] if i + 1 < len(marks) else end
            info["stages"][label] = round(nxt - t, 1)
    return info


if __name__ == "__main__":
    import json
    print(json.dumps(parse(sys.argv[1]), indent=1))
