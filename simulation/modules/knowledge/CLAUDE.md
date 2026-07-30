# knowledge — Developer Context

## What This Module Does
The engine that lets a society move forward. The food surplus frees a stratum from the
land; a few percent of it are the knowledge-keepers (elders, then scribes, then scholars
as the eras allow). What they accumulate raises the carrying ceiling and, at the frontier,
advances the era.

## Tier: mid (runs_after subsistence + technology; GLOBAL, not province-parallel)

## Critical Rules
- **KNOWLEDGE IS HELD PER PROVINCE (R6).** `cohort_stats->knowledge_level` is the stock;
  `technology.knowledge_level` is the MAXIMUM over provinces — the frontier, which is what
  an era is dated by. It was one global number, and that was the deepest reason no
  civilisation could fall: with a single figure there is no such thing as one society
  collapsing while another rises. Production, forgetting, decay and adversity are all
  local. Consumers (subsistence's ceiling, energy_base's mining technique) read the
  PROVINCE's level, never the frontier.
- **Knowledge is NOT conserved.** It diffuses along links from better-informed neighbours
  and the neighbour forgets nothing — copying a text leaves the original. Do not "transfer"
  it. This is what lets a dark region relearn (Greek mathematics via Arabic).
- Ideas get harder to find against what a place ALREADY knows (Jones), which is why
  catching up is faster than leading. It applies to genius leaps too.
- A genius leap is ONE MIND in ONE PLACE (most keepers, deterministic), carrying the local
  adversity and the era's institutions — NOT scaled by the size of the society. Dropping
  the adversity factor once let a single mind out-produce a two-million-person society.
- The written corpus is the floor under forgetting, bounded by what the province itself
  knows; the press (once invented anywhere) multiplies copying; polycentrism slows record
  loss by the number of independent jurisdictions holding records.
- Annual cadence. Regime-gated to the pre-market arc (modern eras: the technology module).
- Deterministic: provinces in index order; one year-seeded RNG draw for the genius arrival.

## Recalibrate after ANY change here
The era thresholds are fitted to this module's output. Run `tools/calibration/calib_seq.sh`
— sequentially, bottom-up, for the reason documented there — and check the `[pacing]` gate.

## Key Types
- KnowledgeConfig (core/config/package_config.h)
- writes RegionDelta.{province_knowledge,codified_knowledge}_delta + TechnologyDelta

## Interface Spec
- docs/interfaces/knowledge/INTERFACE.md
