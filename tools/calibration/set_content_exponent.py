#!/usr/bin/env python3
"""Set TechnologyAdoptionConfig::difficulty_knowledge_exponent and re-derive everything
that hangs off it: the era ladder (eras.csv + the builtin default, which a unit test
asserts against the tree) and the four constants that live on the knowledge axis.

The ladder is CONTENT — the running total of the tech tree's own weights — so it is never
fitted. This regenerates it; it does not calibrate anything."""
import math, pathlib, re, sys

k = float(sys.argv[1])
rows = []
for line in open('packages/base_game/technology/technology_nodes.csv'):
    if line.startswith('#') or not line.strip():
        continue
    f = line.rstrip('\n').split(',')
    if f[0] == 'node_key' or len(f) < 12:
        continue
    try:
        era = int(f[3]); d = float(f[4])
    except ValueError:
        continue
    rows.append((era, max(d, float(era))))

w = lambda d: math.exp(k * (d - 1.0))
T, cum = [], 0.0
for era in range(1, 8):
    cum += sum(w(d) for e, d in rows if e == era)
    T.append(round(cum))

p = pathlib.Path('simulation/core/config/package_config.h'); s = p.read_text()
s = re.sub(r'float difficulty_knowledge_exponent = [0-9.eE+-]+f;',
           'float difficulty_knowledge_exponent = %.2ff;' % k, s, count=1)
# the four constants that are positions on the knowledge axis, kept at their
# ladder-relative positions so re-deriving the ladder carries them along
for name, val in [("sustainable_yield_technique_halfsat", round(T[0] * 1.2)),
                  ("knowledge_productivity_halfsat", round(T[1] * 1.4)),
                  ("discovery_difficulty_halfsat", round(T[1] * 2.4)),
                  ("printing_knowledge_halfsat", round(T[4] * 2.2)),
                  # mechanised farming is an industrial technique: gate it at the era-6 rung
                  ("machine_leverage_knowledge_halfsat", round(T[5]))]:
    s = re.sub(r'float %s = [0-9.eE+-]+f;' % name, 'float %s = %d.0f;' % (name, val), s, count=1)
p.write_text(s)

p = pathlib.Path('packages/base_game/eras/eras.csv'); out = []
for line in p.read_text().splitlines():
    f = line.split(',')
    if f[0].isdigit() and 1 <= int(f[0]) <= 7:
        f[7] = str(T[int(f[0]) - 1]); line = ','.join(f)
    out.append(line)
p.write_text("\n".join(out) + "\n")

p = pathlib.Path('simulation/core/world_gen/era_catalog.cpp'); s = p.read_text()
for i, v in enumerate(T, start=1):
    pat = re.compile(r'(\{%d, "[a-z_]+", "[^"]+", -?\d+, "[a-z_]+", (?:false|true), (?:false|true), )([0-9.]+f)' % i)
    s = pat.sub(lambda m: m.group(1) + ("%.1ff" % v), s, count=1)
p.write_text(s)
print("exponent %.2f -> ladder %s" % (k, T))
