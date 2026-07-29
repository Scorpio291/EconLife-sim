import pathlib, sys, re
vals = [float(x) for x in sys.argv[1:8]]  # eras 1..7 knowledge_to_advance
csv = pathlib.Path("packages/base_game/eras/eras.csv")
lines = csv.read_text().splitlines()
out = []
for line in lines:
    parts = line.split(",")
    if parts[0].isdigit() and 1 <= int(parts[0]) <= 7:
        parts[7] = ("%d" % round(vals[int(parts[0]) - 1]))
        line = ",".join(parts)
    out.append(line)
csv.write_text("\n".join(out) + "\n")

cpp = pathlib.Path("simulation/core/world_gen/era_catalog.cpp")
s = cpp.read_text()
for i, v in enumerate(vals, start=1):
    pat = re.compile(r'(\{%d, "[a-z_]+", "[^"]+", -?\d+, "[a-z_]+", (?:false|true), (?:false|true), )([0-9.]+f)' % i)
    m = pat.search(s)
    assert m, "era %d not found" % i
    s = pat.sub(lambda mm: mm.group(1) + ("%.1ff" % v), s, count=1)
cpp.write_text(s)
print("thresholds set:", [round(v) for v in vals])
