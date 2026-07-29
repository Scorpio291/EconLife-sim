#!/bin/bash
# F7 sequential bottom-up calibration.
#
# Each era transition accelerates knowledge (better occupations, a higher specialist
# ceiling, tech multipliers), so calibrating all seven thresholds at once oscillates:
# the values measured while a society is stuck in era 1 are meaningless once it reaches
# era 2. Calibrate one at a time instead, with every LATER threshold set unreachable so
# the society genuinely sits in the era being calibrated.
set -e
cd /home/user/EconLife-sim
SP=tools/calibration
INF=1000000000
# Calibrated so far (era 1 measured at 1218 -> Bronze Age lands at year 6701, target 6700)
T=(1218 $INF $INF $INF $INF $INF $INF)

for E in 2 3 4 5 6 7; do
    ARGS="${T[@]}"
    python3 tools/calibration/set_thresholds.py $ARGS > /dev/null
    cmake --build build -j6 2>&1 | grep -E " error|Error [0-9]" | head || true
    OUT=$(timeout 3500 ./build/simulation/tests/integration/econlife_emergence_tests \
          "[.society-threshold-calibration]" 2>&1)
    # The row for era E+1 is the target year the era being calibrated must reach.
    ROW=$(echo "$OUT" | grep -E "^ +$((E+1)) \|" || true)
    if [ -z "$ROW" ]; then
        echo "era $E: target year never reached — stopping"; echo "$OUT" | tail -12; exit 1
    fi
    K=$(echo "$ROW" | awk -F'|' '{print $4}' | tr -d ' ')
    echo "era $E threshold <- $K   ($ROW)"
    T[$((E-1))]=$K
done
echo "FINAL: ${T[@]}"
python3 tools/calibration/set_thresholds.py ${T[@]}
cmake --build build -j6 2>&1 | grep -E " error|Error [0-9]" | head || true
