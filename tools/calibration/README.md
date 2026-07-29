# Era threshold calibration (F7)

The era `knowledge_to_advance` thresholds are the one calibration surface the grounding
doctrine allows: they are pure pacing dials and shape nothing mechanical. They are
anchored against EARTHLIKE, because it is Earth — measuring from the dawn of agriculture,
a world with Earth's hazards and Earth's bounty should reach each era at roughly the year
that era actually began. The spectrum (garden sooner, deathworld later or never) then
falls out of the same thresholds rather than being tuned per world.

## When to re-run

After any change that moves the knowledge trajectory: the knowledge engine itself, the
carrying ceiling, the specialist stratum, the wage valve, soil, or the energy base.

## How

    ./tools/calibration/calib_seq.sh

**Sequentially, bottom-up — this matters.** Each era transition itself accelerates
knowledge (better occupations, a higher specialist ceiling, tech multipliers), so fitting
all seven thresholds at once oscillates: the values measured while a society is stuck in
era 1 are meaningless once it reaches era 2. The script sets every later threshold
unreachable, calibrates one era against its historical date, fixes it, and moves up.

It writes both copies of the catalog — `packages/base_game/eras/eras.csv` (read at
runtime) and the builtin in `simulation/core/world_gen/era_catalog.cpp` — because
`era_catalog_test.cpp` requires them to agree, and a stale build reads one while the test
reads the other.

## Guards

- `era_catalog_test.cpp` — the thresholds must strictly increase. A non-monotone
  trajectory is what made this calibration impossible before ideas were made harder to
  find; the test makes that a failure rather than something to rediscover.
- `society_evolution_observe.cpp` `[pacing]` — the earthlike climb must reach every era
  within 1,500 years of its historical date. Runs in `ctest -L emergence`.

## Reading the measurement

    ./build/simulation/tests/integration/econlife_emergence_tests "[.society-threshold-calibration]"

prints the knowledge an earthlike world actually holds at each era's historical year, and
the peak. A threshold above the peak is an era no Earth can reach — which reads as a
failed civilisation, not a calibration error.
