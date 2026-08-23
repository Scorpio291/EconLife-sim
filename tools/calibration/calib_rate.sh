#!/bin/bash
# Single-dial calibration: KnowledgeConfig::knowledge_rate — THE CLOCK.
#
# The era ladder is CONTENT — the running total of the technology tree's own weights
# (TechnologyCatalog::derive_era_thresholds), asserted against eras.csv by a unit test.
# It is never fitted. The only fitted quantity in the knowledge engine is the RATE, and a
# rate decides nothing about the SHAPE of the climb: it slides every date together.
#
# So this bisects one number against ONE anchor date — the Bronze Age, the first
# transition and the one every later date compounds from — and then reports where the
# other six land. Those six are a PREDICTION of the model, not a fit, and the [pacing]
# gate reads them as such.
#
# This replaces calib_seq.sh, which fitted seven thresholds to seven dates and therefore
# could not be wrong about any of them.
set -e
cd /home/user/EconLife-sim
CFG=simulation/core/config/package_config.h
ANCHOR_ERA=2
ANCHOR_YEAR=6700          # 3300 BCE, the Bronze Age
LO=${1:-0.0000001}
HI=${2:-1.0}

run_at() {
    python3 - "$1" <<'PY'
import sys, pathlib, re
v = float(sys.argv[1])
p = pathlib.Path("simulation/core/config/package_config.h")
s = p.read_text()
s2 = re.sub(r'float knowledge_rate = [0-9.eE+-]+f;', 'float knowledge_rate = %.9gf;' % v, s, count=1)
assert s2 != s, "knowledge_rate not found"
p.write_text(s2)
PY
    cmake --build build --target econlife_emergence_tests -j6 2>&1 | grep -E " error|Error [0-9]" | head -3 || true
    timeout 3500 ./build/simulation/tests/integration/econlife_emergence_tests \
        "[.rate-calibration]" 2>&1 | grep -E "^ERA "
}

for i in $(seq 1 14); do
    MID=$(python3 -c "import math;print(math.sqrt($LO*$HI))")   # geometric: the dial is a rate
    OUT=$(run_at "$MID")
    Y=$(echo "$OUT" | awk -v e=$ANCHOR_ERA '$2==e {print $4}')
    if [ -z "$Y" ] || [ "$Y" = "-1" ]; then
        echo "scalar $MID -> era $ANCHOR_ERA never reached; raising"
        LO=$MID
        continue
    fi
    echo "scalar $MID -> era $ANCHOR_ERA at year $Y (target $ANCHOR_YEAR)"
    if [ "$Y" -gt "$ANCHOR_YEAR" ]; then LO=$MID; else HI=$MID; fi
done

FINAL=$(python3 -c "import math;print(math.sqrt($LO*$HI))")
echo "FINAL knowledge_rate: $FINAL"
run_at "$FINAL"
