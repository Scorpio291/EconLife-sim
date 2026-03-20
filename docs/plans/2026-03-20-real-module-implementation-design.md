# Real Module Implementation — Design Document

**Date:** 2026-03-20
**Status:** Approved

## Problem

The bootstrap phase created 45 module skeletons across Tiers 0-12 with types, headers, static utility tests, and interface specs. However, most `execute()` methods are stubs or have significant gaps vs their INTERFACE.md specifications. The simulation doesn't actually run — critically, no `apply_deltas()` function exists to apply DeltaBuffer changes between tick steps.

## Goal

Transform skeletons into a running simulation where all 83 scenario tests pass, determinism is verified, and tick performance meets the <200ms target at 2,000 NPCs.

## Approach

Bottom-up tier-by-tier implementation with scenario tests as validation gates at tier boundaries. 28 sessions across 8 phases.

## Phases

1. **Core Infrastructure Wiring (Sessions 1-3):** Test world factory, apply_deltas(), DeferredWorkQueue integration
2. **Economic Foundation (Sessions 4-7):** Production, supply chain, prices, labor — Gate 1: economy_scenarios pass
3. **NPC Behavior Engine (Sessions 8-11):** Decision overhaul, memory, migration — Gate 2: npc_scenarios pass
4. **Tier 6 Evidence + Community (Sessions 12-14):** Evidence lifecycle, obligations, community — Gate 3
5. **Criminal Economy Pipeline (Sessions 15-18):** Criminal ops through legal process — Gate 4: criminal_scenarios pass
6. **Political + Social (Sessions 19-21):** Elections, influence, media — Gate 5: political_social_scenarios pass
7. **Tier 11-12 Completion (Sessions 22-25):** Population aging, LOD, persistence, determinism/benchmarks
8. **Integration + Polish (Sessions 26-28):** Full-year integration, edge cases, final audit

## Key Decisions

- **apply_deltas() first:** Nothing works without WorldState mutation between steps
- **Scenario-gated progression:** Don't advance to next phase until current phase's scenario tests pass
- **Autonomous execution:** Same model as bootstrap — commit per session, full auto
- **INTERFACE.md is authoritative:** Implementation must match spec; divergences are bugs

See full plan at: `.claude/plans/transient-yawning-wigderson.md`
