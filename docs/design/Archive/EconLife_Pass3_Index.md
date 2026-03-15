# EconLife — Pass 3 Planning Index
*Master overview: issue register, session map, readiness checklist*
*Pre-Bootstrapper fix and improvement plan*
*Status: PASS 3 COMPLETE — TDD v22, GDD v1.6 — Bootstrapper ready*
*Post-Pass-3 design decision (Session 22): WorldGen promoted to V1 scope — see Session Map and checklist notes below*

---

## What Pass 3 Is

Pass 2 resolved 13 of 18 blockers and 7 of 14 precision gaps. Three subsequent design sessions (Commodities v2, R&D & Technology, World Map & Geography) introduced new systems — and new gaps. The GDD's time-period setting is now in conflict with four other documents. A cross-cutting principle has been added: all data that can be moddable should be, which exposes a class of hardcoded constants that need to move to data files across all documents.

Pass 3 clears the remaining blockers, closes the precision gaps, resolves the conflicts, and applies the moddability principle throughout. After Pass 3, the Bootstrapper session can run.

---

## The Moddability Principle

**Logic is engine code. Constants are data.**

The distinction: a formula like `equilibrium = base_price × (demand / supply)` is logic — it belongs in the engine. The coefficients that tune it (the export floor multiplier, the import ceiling multiplier, the adjustment rate) are data — they belong in configuration files that modders can edit without touching the engine.

This principle applies everywhere. Every named constant that controls *how much* something happens (rates, thresholds, weights, multipliers) is a candidate for data files. Only constants that control *whether* something works at all (enum values, field counts, type definitions) belong in engine code.

A full audit of what is currently hardcoded versus what should be in data files is in the companion document **Pass3_Moddability_Audit.md**. That audit drives approximately 30 targeted changes across TDD v2, the Commodities document, and the R&D document. It is not a blocker for the Bootstrapper, but it is a prerequisite for the Bootstrapper generating the right file structure — data-file-backed constants need loader infrastructure, not `constexpr` declarations.

---

## Issue Register

### Category A — Bootstrapper Blockers
*The Bootstrapper cannot generate correct interface specs without these resolved.*

| ID | Issue | Source document | Session |
|---|---|---|---|
| A1 | GDD Section 2 still says 2040s–2060s setting; 4 other docs say Year 2000 | GDD v1.0 | 1 |
| A2 | Feature Tier List: procedural world gen still listed as V1 | Feature Tier List | 1 |
| A3 | Equilibrium price formula — `f()` is a placeholder | TDD v2 Section 5 | 3 |
| A4 | NPC expected_value calculation — formula undefined | TDD v2 Section 4 | 3 |
| A5 | Obligation escalation algorithm — marked UNRESOLVED in TDD | TDD v2 Section 7 | 3 |
| A6 | Worker satisfaction model — GDD references it, TDD doesn't have it | TDD v2 (missing) | 3 |
| A7 | Random event probability model — tick step 25, no model | TDD v2 (missing) | 3 |
| A8 | LOD 1 simplified tick — no specification exists | TDD v2 (missing) | 3 |
| A9 | LOD 2 annual batch job — no specification exists | TDD v2 (missing) | 3 |
| A10 | Province struct (rename + new fields) not in TDD v2 | TDD v2 Section 12 | 2 |
| A11 | WorldGenParameters must become WorldLoadParameters | TDD v2 Section 12 | 2 |
| A12 | GlobalCommodityPriceIndex + NationalTradeOffer missing from TDD v2 | TDD v2 (missing) | 2 |
| A13 | TariffSchedule + TradeAgreement missing from TDD v2 | TDD v2 (missing) | 2 |

### Category B — Precision Gaps
*Specified but not precisely enough for unambiguous implementation.*

| ID | Issue | Source document | Session |
|---|---|---|---|
| B1 | Informal economy / black market price formula absent from TDD | TDD v2 Section 5 | 2 |
| B2 | Tech tier efficiency differential — no % per tier defined | TDD v2 / R&D doc | 4 |
| B3 | Evidence actionability decay formula — `is_credible` effect unspecified | TDD v2 Section 6 | 4 |
| B4 | Legitimate NPC business decision matrix — only criminal version exists | TDD v2 Section 5 | 4 |
| B5 | DeadlineConsequence escalation behavior unspecified | TDD v2 Section 8 | 4 |
| B6 | Organic milestone list — no definition of what milestones exist | TDD v2 (missing) | 4 |
| B7 | Criminal entry conditions — no checkable states per operation type | GDD Section 12 | 4 |
| B8 | BASE_TRANSPORT_RATE not calibrated to real-world anchor | World Map doc | 2 |

### Category C — Moddability Violations
*Hardcoded values that should be in data files per the moddability principle.*

See **Pass3_Moddability_Audit.md** for full details. High-priority items that affect Bootstrapper file structure generation:

| ID | Issue | Current location | Should be |
|---|---|---|---|
| C1 | OPSEC signal weights (W_POWER, W_CHEMICAL, etc.) hardcoded as `constexpr` | TDD v2 Section 16 | Per-facility-type in facility_types.csv |
| C2 | Community response stage thresholds hardcoded in enum comments | TDD v2 Section 14 | simulation_config.json |
| C3 | Investigator meter status thresholds hardcoded | TDD v2 Section 6 | simulation_config.json |
| C4 | BRAND_BUILD_RATE, BRAND_DAMAGE_RATE etc. as named constants in spec | Commodities doc | Per-good-category in goods CSV |
| C5 | NPC behavioral constants (FEAR_DECAY_RATE, COMMUNITY_EMA_ALPHA, etc.) | TDD v2 various | simulation_config.json |
| C6 | Climate sensitivity constants embedded in spec prose | R&D doc | climate_config.json |

### Category D — Document Updates
*Documents needing edits to reflect already-made decisions.*

| ID | Document | What changes | Session |
|---|---|---|---|
| D1 | GDD v1.0 Section 2 | Rewrite: Year 2000 start; conditions become emergent outcomes | 1 + 5 |
| D2 | GDD v1.0 Section 21 | Update WorldGenParameters refs; Region → Province | 6 |
| D3 | Feature Tier List | 3 row edits; 1 new row | 1 |
| D4 | TDD v2 header | Add World Map doc to companion list; update pass note | 6 |
| D5 | R&D doc | Close answered open questions; add moddability corrections | 6 |
| D6 | Commodities doc | Close answered open questions; update moddability section | 6 |
| D7 | All documents | Region → Province terminology; 4 regions → 6 provinces | 6 |

### Category E — Decisions Required Before Specs Can Be Written
*Design questions that must be answered first.*

| ID | Question | Affects | Session |
|---|---|---|---|
| E1 | LOD 1 trade offer frequency: monthly? weekly by category? | A8 | 3 |
| E2 | LOD 1 nation set: global list or per-starting-province mapping? | A8 | 3 |
| E3 | Political boundary changes: static 2000 map or dynamic border events? | GDD rewrite | 1 |
| E4 | simulation_config.json scope: which constants live there vs per-entity data files? | C-category | Moddability session |

---

## Session Map

```
Session 1  — Document Conflicts                     COMPLETE  TDD v1
Session 2  — TDD World Map Integration              COMPLETE  TDD v2
Session 3  — TDD Algorithmic Specs                  COMPLETE  TDD v3
Session 4  — Precision Gap Completions              COMPLETE  TDD v4
Session 5  — GDD Section 2 Rewrite                  COMPLETE  GDD v1.1–1.2
Session 6  — Moddability + Hygiene                  COMPLETE  TDD v6
Session 7  — Architecture (Packages, Parallelism,   COMPLETE  TDD v7
             Transit, Persistence)
Session 8  — Physics Consistency Pass               COMPLETE  TDD v8
Session 9  — Structural Fixes                       COMPLETE  TDD v9
Session 10 — Missing Struct Definitions             COMPLETE  TDD v10
Session 11 — Precision Gap Completions              COMPLETE  TDD v11
Session 12 — Mechanical Fixes                       COMPLETE  TDD v12
Session 13 — Universal Facility Signals and         COMPLETE  TDD v13
             Scrutiny System
Session 14 — Grey Zone Compliance and               COMPLETE  TDD v14
             Universal Community Grievance
Session 15 — Player-Special Fixes                   COMPLETE  TDD v15
Session 16 — Constants Homing                       COMPLETE  TDD v16
Session 17 — Technology Lifecycle Integration       COMPLETE  TDD v17
Session 18 — Cross-Doc Audit Fixes                  COMPLETE  TDD v18
             (commercialize_technology, farm runtime
             fields, permafrost dual-gate)
Session 19 — [internal pass]                        COMPLETE  TDD v19
Session 20 — Economy Consolidation and              COMPLETE  TDD v20→v21
             Universal OPSEC                                   GDD v1.4
             (VisibilityScope, informal economy
             reframed, OPSEC universalised)
Session 21 — Missing Struct Definitions             COMPLETE  TDD v21
             and Blocker Fixes                                 GDD v1.5
             (KnowledgeMap, ConsumerDemand,
             B-02 sign fix, stale ref cleanup)
Session 22 — WorldGen V1 Scope Promotion            COMPLETE  TDD v22
             (post-Pass-3 design decision)                    GDD v1.6
             (H3Index, LinkType, ProvinceLink,
             WorldGenParameters; Province struct
             updated; WorldGen doc EX→V1)
```

---

## Bootstrapper Readiness Checklist

The Bootstrapper can run when every item below is checked.

**Setting and scope:**
- [x] GDD Section 2 describes Year 2000 setting (Session 1 + 5)
- [x] Feature Tier List entries consistent with Year 2000 and GIS-seeded world (Session 1)

**World and trade infrastructure (TDD v22):**
- [x] Province struct defined with GeographyProfile, ClimateProfile, LOD fields (Session 2)
- [x] Province struct updated: H3Index h3_index added; ProvinceLink adjacency replaces bare id list (Session 22)
- [x] H3Index type alias, LinkType enum, ProvinceLink struct defined in §12 (Session 22)
- [x] WorldGenParameters struct defined in §12 (Session 22 — V1 scope; 11-stage procedural pipeline)
- [x] WorldLoadParameters replaces old WorldGenParameters runtime struct (Session 2)
- [x] NationalTradeOffer struct in TDD (Session 2)
- [x] GlobalCommodityPriceIndex struct in TDD (Session 2)
- [x] TariffSchedule and TradeAgreement structs in TDD (Session 2)
- [x] Transport cost formula with named constants including BASE_TRANSPORT_RATE (Session 2)
- [x] Black market price formula in TDD (Session 2)

**Algorithms (TDD v22):**
- [x] Equilibrium price formula with named constants (Session 3)
- [x] NPC expected_value calculation with outcome type table (Session 3)
- [x] Obligation escalation algorithm with named constants (Session 3)
- [x] Worker satisfaction model as TDD subsection (Session 3 + 14C)
- [x] Random event probability model with Poisson parameters (Session 3)
- [x] LOD 1 simplified tick as TDD section with step list (Session 3)
- [x] LOD 2 annual batch job as TDD section with step list (Session 3)

**Moddability infrastructure (affects what the Bootstrapper generates):**
- [x] simulation_config.json defined as a data file the engine reads (Session 6 + 16)
- [x] OPSEC signal weights moved to facility_types.csv (Session 6 → renamed FacilitySignals in Session 13)
- [x] All `constexpr` simulation constants identified as data-file-backed (Sessions 6, 11, 16)

**Cross-references and hygiene:**
- [x] All TDD section references valid after renumbering (Session 6)
- [x] Region → Province terminology consistent across all docs (Session 6)
- [x] TDD header lists all companion documents (Session 6; updated through Session 22)

---

## What Is Not in Pass 3 Scope

The following are real gaps but are not required before the Bootstrapper:

- Scene card template library (content, not spec)
- NPC name generation tables per culture archetype (content)
- Facility design module spec (marked EX)
- Full diplomatic relationships system (marked EX)
- Human trafficking expansion systems (deliberately EX)
- World editor tool (EX)
- GIS pipeline tool implementation (pre-game tool, not game binary)
- WorldGen procedural pipeline *implementation* (spec is complete in WorldGen v0.17 and TDD v22; pipeline is V1 scope but the pipeline *tool* is pre-game tooling, not the game binary itself)
- NPC R&D investment decision matrix (open question in R&D doc, flagged for later)
- Technology upgrade scheduling for NPC businesses (open question in R&D doc)
- Climate tipping points model (open question in R&D doc, flagged EX)

These are flagged for a Pass 4 that runs after prototype validation.

---

## Companion Documents in This Series

| Document | Contents |
|---|---|
| **Pass3_Index.md** (this file) | Master overview, issue register, checklist |
| **Pass3_Moddability_Audit.md** | Moddability principle, full constant audit, data file spec |
| **Pass3_Sessions_1_2.md** | Session 1 (conflict resolution) + Session 2 (TDD world map) |
| **Pass3_Session_3_Algorithms.md** | Session 3: all 7 algorithm specs |
| **Pass3_Sessions_4_5_6.md** | Sessions 4 (precision gaps) + 5 (GDD rewrite) + 6 (hygiene) |
