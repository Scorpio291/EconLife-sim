# Calibration

## There is nothing left to calibrate in the climb.

`calib_rate.sh` is deleted, and so is the constant it fitted.

It bisected `KnowledgeConfig::knowledge_rate` — "the clock" — against a historical date, so
that the Bronze Age landed on 3300 BCE. That is steering toward a desired result. It made
the answer true by construction rather than as a consequence, and it had been fitted under a
broken integration stride besides, so it was steering toward a number produced by an
artefact.

How fast a society works things out now comes out of **what its people are**: mean years of
learning per adult, times the share of the year they are fit to work. Both are stocks with
long memories, both are measurable in the historical record (signature rates, age heaping,
guild indentures; skeletal stature and conscription records), and both are fed by flows the
model already computes. See `RegionCohortStats::{schooling, health, nutrition}` and
`HumanCapabilityConfig`.

## What is still regenerated, and why that is not calibration

`set_content_exponent.py` regenerates the era ladder (`knowledge_to_advance` in
`packages/base_game/eras/eras.csv`) as the running total of the technology tree's own node
weights, plus the four constants that are positions on that axis. It is a pure function of
content — edit the tree and the ladder follows — and a unit test asserts the two agree, so
forgetting to regenerate fails the build rather than drifting quietly.

    python3 tools/calibration/set_content_exponent.py 1.1

The one authored number in it is `difficulty_knowledge_exponent`: how much more learning
each further unit of a node's authored difficulty stands for. That is a statement about the
tree's content, not about when anything should happen.

## What the gates assert now

`[pacing]` asserts the SHAPE of a climb — eras arrive in order, none is skipped, none is
free, the dawn is long — and that different world classes get different distances. It does
not assert dates. How long a society takes over an era is its own business, and a model that
fails when a world is slow is a model with an opinion about the right answer.
