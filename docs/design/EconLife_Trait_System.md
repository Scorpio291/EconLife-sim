# EconLife — Trait System
## Companion Document to Technical Design Document §11

**Version:** 0.4
**Status:** Draft
**Depends on:** GDD §4.1–4.3, TDD §11 (PlayerCharacter), Character Creation spec
**Referenced by:** TDD §11.2–11.3, simulation.json `trait_system` block

---

## 1. Overview

Traits are persistent character attributes that modify how the simulation evaluates actions, what options are available in engagements, and how NPCs respond over time. They are earned through sustained behavior, role adoption, and drastic events, then awarded probabilistically when an accumulator threshold is crossed.

There is no cap on the number of traits a character can hold. Once earned, a trait is permanent unless displaced by a hard-incompatible trait via the Displacement mechanic (§4.3). Natural accumulator decay does not affect held traits.

**Incompatibility grounding:** Hard incompatibilities are mechanical contradictions — two traits whose in-game effects cannot both be active simultaneously. They are not psychological impossibilities. The one exception is Empathetic ↔ Ruthless, a deliberate design position: affective care for people and willingness to harm them without friction are treated as mutually exclusive expressions for a single character.

**Affective vs. cognitive empathy:** `Empathetic` represents affective empathy (sharing others' emotional states). `Manipulative` represents high cognitive empathy with low affective empathy (reading others precisely and using that information instrumentally). These are in soft tension, not hard conflict.

---

## 2. Trait Enum

```cpp
enum class Trait : uint8_t {
    // Cognitive
    Analytical          = 0,
    Creative            = 1,
    Scholarly           = 2,
    Intuitive           = 3,

    // Social
    Charismatic         = 4,
    Empathetic          = 5,
    Connected           = 6,
    Manipulative        = 7,

    // Volitional
    Disciplined         = 8,
    Ambitious           = 9,
    Patient             = 10,
    Impulsive           = 11,
    Resilient           = 12,
    Independent         = 13,

    // Risk / Moral
    Cautious            = 14,
    Ruthless            = 15,
    RiskTolerant        = 16,
    Paranoid            = 17,

    // Adaptive
    Adaptable           = 18,
    Stoic               = 19,
    Chameleon           = 20,

    // Physical / Domain
    PhysicallyRobust    = 21,
    StreetSmart         = 22,
    PoliticallyAstute   = 23,

    // Character
    Principled          = 24,
};

static constexpr uint8_t TRAIT_COUNT = 25;
```

**NPC traits:** NPCs use this enum for behavioral characterization. NPC trait assignment at world generation draws from the full 25-value set using population frequency weights from `simulation.json → npc_trait_distribution`. NPCs do not run the accumulator system; traits function as behavioral profile tags.

---

## 3. Trait Definitions

Each entry specifies: behavioral description, mechanical effects, NPC observability (what memory entry is created, when, and at what visibility scope), hard incompatibilities, soft incompatibilities, and primary accumulator sources.

All modifier constants reference `simulation.json → trait_system`. Per-trait threshold overrides are defined in §5.5.

---

### 3.1 ANALYTICAL
**Description:** Processes information methodically; finds signal in noise through systematic reasoning.

**Mechanical effects:**
- Skill acquisition rate +15%: Finance, Engineering, Intelligence
- KnowledgeMap confidence builds 20% faster from the same evidence volume
- Engagements: `assess_probability` sub-option unlocked, surfacing rough outcome odds

**NPC observability:** None.

**Hard incompatibilities:** none

**Soft incompatibilities:** Intuitive (−10% to both)

**Accumulator sources:** Finance/Engineering/Intelligence skill use; `assess_probability` engagement resolution; extended multi-variable strategic planning

---

### 3.2 CREATIVE
**Description:** Finds solutions others don't see; approaches problems sideways.

**Mechanical effects:**
- Engagements: unconventional option present where Creative applies
- Business innovation: product development cycles −15% time
- Persuasion: creative framing paths available in negotiation

**NPC observability:** `creative_reputation` (visibility: `industry`) — creative-field and close-relationship NPCs, after sustained interaction.

**Hard incompatibilities:** none

**Soft incompatibilities:** Disciplined (−5% to both)

**Accumulator sources:** Selecting unconventional engagement options; business innovation and pivot actions; creative-adjacent NPC relationships

---

### 3.3 SCHOLARLY
**Description:** Carries deep formal knowledge; learns fastest from structured bodies of knowledge.

**Mechanical effects:**
- Education level treated one tier higher for skill acquisition calculations
- Skill acquisition rate +20%: Finance, Politics, Engineering, Intelligence
- Unlocks academic NPC pool (professors, researchers, think-tank contacts)

**NPC observability:** `scholarly_recognition` (visibility: `personal`) — academic NPCs, 30% less interaction history required.

**Hard incompatibilities:** none

**Soft incompatibilities:** StreetSmart (−5% to both)

**Accumulator sources:** University Stage 3 choice; sustained knowledge-domain skill investment; academic NPC relationships; formal research actions

---

### 3.4 INTUITIVE
**Description:** Reads situations and people from minimal information; pattern-matches where others need more data.

**Mechanical effects:**
- NPC motivation estimates formed with 30% less interaction history
- NPC loyalty and stress indicators visible one tier earlier in engagement reads
- Market price trend signals readable without complete data set

**NPC observability:** None.

**Hard incompatibilities:** none

**Soft incompatibilities:** Analytical (−10% to both)

**Accumulator sources:** High-volume NPC negotiation on short timelines; high-frequency trading under pressure; repeated engagement resolution under incomplete information

---

### 3.5 CHARISMATIC
**Description:** Magnetically draws people in; persuasive through force of presence.

**Mechanical effects:**
- Persuasion engagements: +15% success probability on social resolution paths
- `trust_ceiling` +0.1 across all NPCs
- New NPC relationships start with `trust_level` 0.1 higher than baseline

**NPC observability:** `charismatic_impression` (visibility: `public_info` for public figures, `personal` for private interactions) — single direct engagement.

**Hard incompatibilities:** Paranoid (`trust_ceiling` contradictory direction)

**Soft incompatibilities:** none

**Accumulator sources:** Successful Persuasion engagements; public platform actions; movement leadership; high-volume NPC social investment

---

### 3.6 EMPATHETIC
**Description:** Affective empathy — genuinely shares others' emotional states. Builds the deepest trust; creates recorded friction when directed to cause harm.

**Mechanical effects:**
- NPC trust builds 25% faster across all relationship types
- `relationship_depth_ceiling` +0.15 (distinct field from `trust_ceiling` — see §3.20 note)
- Coercive action causing direct harm to an NPC with `trust_level >= 0.5`: `internal_friction` flag set on the action record; action is entered into the legacy log; Ruthless accumulator receives `TRAIT_REPEATED_ACTION_INCREMENT`

**NPC observability:** `empathetic_recognition` (visibility: `personal`) — sustained close relationships only.

**Hard incompatibilities:** Ruthless

**Soft incompatibilities:**
- Manipulative: Manipulative effectiveness −15%
- Stoic: Empathetic `trust_build_rate` −10%; Stoic `emotional_buffer` −10%
- Paranoid: Empathetic `trust_build_rate` −10%

**Accumulator sources:** Personal life scene cards engaged honestly; caregiver/mentor NPC roles sustained over time; responding to NPC suffering in consequence queue; trust_level ≥ 0.7 relationships sustained 6+ months

---

### 3.7 CONNECTED
**Description:** Naturally builds networks; generates social capital through reciprocity.

**Mechanical effects:**
- Starting province: +3 significant NPCs (acquaintance, trust_level: 0.2)
- Referral-based NPC introductions: 20% more frequent
- "Through a contact" opportunity events: 25% more frequent

**NPC observability:** `well_connected_reputation` (visibility: `public_info`) — once network exceeds threshold size.

**Hard incompatibilities:** Paranoid (`trust_ceiling` contradictory direction)

**Soft incompatibilities:**
- Independent: each −10%
- Ruthless: Connected NPCs' `trust_ceiling` −15% for those holding `ruthless_awareness`

**Accumulator sources:** Calendar slots dedicated to NPC maintenance; large-scale social events; movement building; referral-chain NPC introductions

---

### 3.8 MANIPULATIVE
**Description:** High cognitive empathy, low affective empathy — reads others precisely and uses that information instrumentally.

**Mechanical effects:**
- Leverage held over NPCs: +20% effectiveness on coercive engagements
- NPC vulnerability assessment: motivation weaknesses visible with 40% less history
- Obligations the character holds over others decay 20% slower

**NPC observability:** `manipulation_suspicion` (visibility: `personal`) — only NPCs who recognized they were manipulated; Intuitive and PoliticallyAstute NPCs detect faster.

**Hard incompatibilities:** none

**Soft incompatibilities:**
- Empathetic: Manipulative effectiveness −15%
- Principled: Manipulative effectiveness −20%

**Accumulator sources:** Coercive leverage actions; obligation exploitation under duress; sustained deceptive NPC management; deliberate information asymmetry in negotiations

---

### 3.9 DISCIPLINED
**Description:** Consistent habits and structured execution; resists entropy in skills and schedule.

**Mechanical effects:**
- Skill `decay_rate` × 0.6 across all domains (40% slower rust)
- Calendar capacity: +1 slot per in-game week
- Exhaustion accumulator builds 15% slower

**NPC observability:** `disciplined_reputation` (visibility: `industry`) — professional NPCs after sustained interaction.

**Hard incompatibilities:** Impulsive

**Soft incompatibilities:** Creative (−5% to both)

**Accumulator sources:** Consistent calendar use without skipped engagements; sustained skill practice without neglect; long-running business operations without delegation failures; Military Stage 3 choice

---

### 3.10 AMBITIOUS
**Description:** Driven toward achievement; pushes further and faster than circumstances warrant, at personal cost.

**Mechanical effects:**
- Skill acquisition rate +15% across all domains
- Exhaustion accumulator builds 20% faster
- Certain high-risk, high-reward action variants unlock

**NPC observability:** `ambitious_reputation` (visibility: `industry`) — professional NPCs over time.

**Hard incompatibilities:** none

**Soft incompatibilities:**
- Cautious: Ambitious upside cap −15%; Cautious protection bonus −15%
- Patient: context-dependent — see interaction note below

**Patient + Ambitious interaction:** Real psychology documents this as a functional high-performing combination (the "calculated climber"). No mutual penalty applies. Full benefit of both traits applies when an action is simultaneously high-ambition AND long-horizon. Single-axis actions receive the applicable trait's bonus; the other trait contributes nothing to that specific action.

**Accumulator sources:** Selecting high-stakes action variants over moderate alternatives; rapid business scaling; persistent pursuit of dominant positions; operating above calendar capacity at exhaustion cost

---

### 3.11 PATIENT
**Description:** Plays long games; derives return from compounding rather than immediacy.

**Mechanical effects:**
- Long-term investments: return +10% per additional in-game year held beyond expected timeline
- Relationship trust builds 20% faster
- Political influence campaigns: +15% effectiveness

**NPC observability:** `patient_operator_reputation` (visibility: `personal`, upgrades to `industry` after extended observation).

**Hard incompatibilities:** Impulsive

**Soft incompatibilities:** Ambitious — see §3.10 interaction note

**Accumulator sources:** Holding investments through downturns without reversal; multi-year political campaigns to completion; maintaining relationships through non-engagement periods; declining to act when acting is available

---

### 3.12 IMPULSIVE
**Description:** Acts on immediate information without deliberation. High-variance outcomes in both directions; certain slow-burn strategies structurally unavailable.

**Mechanical effects:**
- High-variance action variants always available; outcome variance ×1.4
- Time-sensitive engagements: reaction-speed bonus +10% success probability
- **Unavailable:** any action typed `slow_burn_strategy`; multi-year influence campaigns; systematic evidence building over >30 ticks

**NPC observability:** `unpredictable_reputation` (visibility: `personal`).

**Hard incompatibilities:** Disciplined, Cautious, Patient

**Soft incompatibilities:** none

**Accumulator sources:** Immediate-response action selection when deliberate alternatives are available; high-frequency trading with rapid position changes; reactive criminal operations; drastic event responses executed within the same tick

---

### 3.13 RESILIENT
**Description:** Recovers faster from damage across all systems. Built from adversity; each major setback survived strengthens the next recovery.

**Mechanical effects:**
- Health recovery rate +25%
- Reputation damage decay rate +20% faster
- Post-bankruptcy restructuring timeline −25%
- Relationship damage from betrayal or conflict heals 15% faster

**NPC observability:** `comeback_story` (visibility: `public_info`) — only for publicized events; NPCs who witnessed collapse and recovery.

**Hard incompatibilities:** none

**Soft incompatibilities:** none

**Accumulator sources:** Every drastic negative event survived contributes. Health event recovery; bankruptcy recovery; reputation crisis navigation. Built specifically by adversity.

---

### 3.14 INDEPENDENT
**Description:** Generates direction and momentum from within; functions at full capacity without external support infrastructure.

**Mechanical effects:**
- Social isolation health penalty: −50%
- Self-directed business operations: −10% management overhead
- Personal life neglect health penalty: −25%

**NPC observability:** `self_reliant_read` (visibility: `personal`) — NPCs who attempted sustained relationship-building and were consistently rebuffed.

**Hard incompatibilities:** none

**Soft incompatibilities:** Connected (each −10%)

**Accumulator sources:** Sustained operation without significant NPC relationships; sole business ownership; Self-taught Stage 3 choice; solo criminal operation without lieutenants

---

### 3.15 CAUTIOUS
**Description:** Systematic risk management; avoids worst outcomes at the cost of maximum upside.

**Mechanical effects:**
- Exposure generation: −20% on risky actions
- Worst-case outcome probability: −15% on high-risk engagements
- Maximum upside on aggressive actions capped at 85% of theoretical maximum

**NPC observability:** `risk_averse_reputation` (visibility: `industry`) — business and finance NPCs over time.

**Hard incompatibilities:** Impulsive

**Soft incompatibilities:**
- Ambitious: each −15%
- RiskTolerant: each −15%

**Accumulator sources:** Selecting lower-risk options when higher-risk alternatives are available; consistent evidence management; sustained clean business operation over multi-year periods

---

### 3.16 RUTHLESS
**Description:** Willing to cause direct harm without internal friction when it serves the character's goals.

**Mechanical effects:**
- Unlocked action types: direct threat termination, wage suppression without negotiation, coercive leverage without relationship prerequisite
- NPCs holding `ruthless_awareness`: fear-compliance behavior — negotiation difficulty −15%, loyalty risk +20%
- Evidence rate: Ruthless actions generate evidence tokens at 1.3× normal rate

**NPC observability:** `ruthless_awareness` (visibility: `personal`, propagates to `public_info` for widely-witnessed events) — NPCs who witness or directly experience ruthless actions.

**Hard incompatibilities:** Empathetic

**Soft incompatibilities:**
- Connected: NPCs holding `ruthless_awareness` have `trust_ceiling` −15%
- Principled: each −15%

**Accumulator sources:** Coercive actions involving direct harm or threat of violence; NPC eliminations; sustained exploitation involving coercion; drastic harm events executed without reversal. Financial and deceptive criminal actions do not reinforce Ruthless.

---

### 3.17 RISKTOLERANT
**Description:** Comfortable with uncertainty and variance in outcomes. A considered orientation, not reactive impulsivity.

**Mechanical effects:**
- EV calculation: `variance_bonus = 0.05` added to expected value of high-risk actions
- `inaction_threshold`: −0.05
- Debt tolerance: leverage 15% above config maximum without stress penalty

**NPC observability:** `risk_tolerant_profile` (visibility: `industry`) — finance and investment NPCs observing sustained high-risk positions.

**Hard incompatibilities:** none

**Soft incompatibilities:** Cautious (each −15%)

**Accumulator sources:** High-risk financial positions held to resolution; criminal expansion into contested territory; multi-market business risk; major financial loss survived without behavioral reversal

---

### 3.18 PARANOID
**Description:** Heightened threat detection at cost of relationship openness. Catches real threats others miss; generates false positives that damage relationships.

**Mechanical effects:**
- Exposure detection: +40%
- Countersurveillance action types unlocked
- `trust_ceiling`: −0.15 with all NPCs

**NPC observability:** `paranoid_read` (visibility: `personal`) — NPCs who experience sustained suspicion or unusual security behavior.

**Hard incompatibilities:**
- Connected (`trust_ceiling` contradictory direction)
- Charismatic (`trust_ceiling` contradictory direction)

**Soft incompatibilities:**
- Empathetic: Empathetic `trust_build_rate` −10%

**Per-trait threshold override:** `award_threshold = 0.50`

**Accumulator sources:** Survived assassination attempts or serious physical threats; NPC betrayal (trust_level ≥ 0.6); sustained high-scrutiny criminal operation; near-arrest events

---

### 3.19 ADAPTABLE
**Description:** Thrives in unfamiliar environments; where others see disruption as setback, this character finds it rebalancing.

**Mechanical effects:**
- Province-unfamiliarity penalty: −50%
- Sector-unfamiliarity penalty: −50%
- Disruption event negative magnitude: −20%

**NPC observability:** None.

**Hard incompatibilities:** none

**Soft incompatibilities:** none

**Accumulator sources:** Sustained multi-province operations; repeated sector pivots; major disruption events navigated without business failure; frequent residence changes across different region types

---

### 3.20 STOIC
**Description:** Emotional regulation under sustained pressure; events that break other characters leave this one functioning. The cost is a ceiling on emotional depth.

**Mechanical effects:**
- Negative event health impact: −25%
- Reputation damage recovery: +15% faster
- `relationship_depth_ceiling`: −0.1

**`trust_ceiling` vs. `relationship_depth_ceiling`:** These are distinct fields. `trust_ceiling` caps how much an NPC trusts the character (willingness to act on their behalf, share information). `relationship_depth_ceiling` caps emotional investment (how much the relationship shapes NPC decisions, how much an NPC will sacrifice). A Charismatic + Stoic character builds widely-trusted but emotionally shallow relationships.

**NPC observability:** `unshakeable_read` (visibility: `personal`) — NPCs who observed the character through major crises.

**Hard incompatibilities:** none

**Soft incompatibilities:**
- Empathetic: Empathetic `trust_build_rate` −10%; Stoic `emotional_buffer` −10%

**Accumulator sources:** Navigating personal loss without operational disengagement; sustained high-stress operations over multiple in-game years; personal life losses without withdrawal or collapse; drastic negative events without behavioral change in the following 30 ticks

---

### 3.21 CHAMELEON
**Description:** Reads and mirrors the social context they inhabit — not without identity, but with fluid expression of it.

**Mechanical effects:**
- Social class movement penalty: removed entirely
- NPC background detection: 2× longer for NPCs to read the character's actual origin
- Cross-context trust: no foreign-context trust penalty when operating outside background strata
- Infiltration operations: +25% effectiveness [V1 note: UndercoverInfiltration skill domain is [EX] scope in TDD §11.1. This bonus applies to all actions typed `infiltration_operation` independently of that skill. When UndercoverInfiltration ships, the bonus stacks multiplicatively.]

**NPC observability:** `surface_read_uncertain` (visibility: `personal`) — suspicious NPCs require 3× normal interaction volume to form even an uncertain read.

**Hard incompatibilities:** none

**Soft incompatibilities:** none

**Accumulator sources:** Cross-class operations simultaneously active; undercover NPC infiltration; cover identity maintained 30+ ticks; frequent movement between social contexts within single in-game weeks

---

### 3.22 PHYSICALLY_ROBUST
**Description:** Higher baseline physical health; physical decline comes later and more slowly.

**Mechanical effects:**
- `health.current_health` baseline: +0.1
- Physical decline: begins at in-game age 60 instead of 50
- Physically demanding engagement types available for 10 additional in-game years

**NPC observability:** `physical_presence_noted` (visibility: `public_info`) — immediate first impression.

**Hard incompatibilities:** none

**Soft incompatibilities:** none

**Accumulator sources:** Stage 2 `Physical and competitive` choice; healthcare investment calendar entries; physical maintenance calendar actions; Stage 1 outdoor or labor-intensive upbringing choices

---

### 3.23 STREET_SMART
**Description:** Reads grey and black market situations accurately; the informal economy is legible where academic knowledge doesn't reach.

**Mechanical effects:**
- CriminalOperations skill acquisition: +15%
- Grey/black market NPC motivation reads: 40% less history required
- Exposure from criminal activity: −10%
- Informal economy participation unlocked

**NPC observability:** `street_credibility` (visibility: `concealed`) — street-level NPCs already in grey/black network only.

**Hard incompatibilities:** none

**Soft incompatibilities:** Scholarly (−5% to both)

**Accumulator sources:** Stage 2 `Trouble and mischief` + Stage 3 `Criminal apprenticeship`; sustained criminal operations; grey market trading; street-level NPC network building

---

### 3.24 POLITICALLY_ASTUTE
**Description:** Reads power dynamics, institutional behavior, and political motivations accurately.

**Mechanical effects:**
- Politics skill effectiveness: +15%
- Institutional NPC behavior prediction: readable with 30% less history
- Electoral campaign polling accuracy: +20%

**NPC observability:** `political_operator_read` (visibility: `institutional`) — political and institutional NPCs.

**Hard incompatibilities:** none

**Soft incompatibilities:** none

**Accumulator sources:** Stage 2 `Community or civic involvement` + Stage 3 `Political / activist`; sustained political campaign management; multi-cycle regulatory navigation; senior institutional NPC relationships

---

### 3.25 PRINCIPLED
**Description:** Behaves according to consistent personal values regardless of cost. Creates a specific type of trust — NPCs trust not just the character's goodwill but the predictability of their behavior under pressure.

**Mechanical effects:**
- NPCs holding `principled_reputation` have `trust_ceiling` +0.15 for this character specifically
- When bribery, betrayal, or deception options are declined when profitable: `principled_refusal` event logged; contributes to `principled_reputation` in observing NPCs
- Reputation damage from scandal events: −30% magnitude (consistent behavior is harder to frame as hypocritical)
- Obligation fulfilled to NPC at net personal cost: `trust_level` increase 2× normal

**NPC observability:** `principled_reputation` (visibility: `personal`, upgrades to `public_info` for publicly witnessed refusals) — after observing the character honor commitments at personal cost, or refuse profitable value-violating options.

**Hard incompatibilities:** none

**Soft incompatibilities:**
- Ruthless: each −15%
- Manipulative: Manipulative effectiveness −20%

**Accumulator sources:** Declining bribery or corruption when available and profitable; honoring obligations when defaulting would be advantageous; maintaining commitments through adversity; whistleblowing or principled resignation actions

---

## 4. Incompatibility Rules

### 4.1 Hard Incompatibilities

| Trait A | Trait B | Mechanical reason |
|---|---|---|
| Disciplined | Impulsive | Calendar and skill decay outcomes directly contradict |
| Cautious | Impulsive | Risk management and high-variance behavior directly contradict |
| Patient | Impulsive | Available strategy types mutually exclusive |
| Empathetic | Ruthless | Affective care ↔ willingness to harm without friction; design position (§1) |
| Connected | Paranoid | `trust_ceiling` raised ↔ `trust_ceiling` lowered; same field |
| Charismatic | Paranoid | `trust_ceiling` raised ↔ `trust_ceiling` lowered; same field |

### 4.2 Soft Incompatibilities

| Trait A | Trait B | Effect on A | Effect on B |
|---|---|---|---|
| Analytical | Intuitive | −10% all bonuses | −10% all bonuses |
| Ambitious | Cautious | Upside cap −15% | Protection bonus −15% |
| Ambitious | Patient | Context-dependent — §3.10 | Context-dependent — §3.10 |
| Connected | Independent | −10% all bonuses | −10% all bonuses |
| Disciplined | Creative | −5% all bonuses | −5% all bonuses |
| Empathetic | Manipulative | No change | Effectiveness −15% |
| Empathetic | Paranoid | `trust_build_rate` −10% | No change |
| Empathetic | Stoic | `trust_build_rate` −10% | `emotional_buffer` −10% |
| Principled | Ruthless | Each −15% | Each −15% |
| Principled | Manipulative | No change | Effectiveness −20% |
| RiskTolerant | Cautious | `variance_bonus` −15%; threshold reduction halved | Protection bonus −15% |
| Ruthless | Connected | No change | `trust_ceiling` −15% for NPCs holding `ruthless_awareness` |
| Scholarly | StreetSmart | −5% all bonuses | −5% all bonuses |

### 4.3 Displacement Mechanic

When a hard-incompatible trait's accumulator crosses `award_threshold` while the conflicting trait is held, a Displacement Roll fires. Probability scales with distance above threshold.

```
excess = accumulator - effective_threshold
displacement_probability = clamp(
    excess × config.trait_system.displacement_probability_scale,  // default: 0.80
    0.0,
    config.trait_system.displacement_max_probability              // default: 0.75
)
// excess = 0.0 → probability = 0%; displacement cannot fire at threshold
// excess = 0.25 → probability = 20%
// excess = 0.94+ → probability = 75% (max)

if random_float(seed) < displacement_probability:
    traits.erase(conflicting_trait)
    traits.push_back(candidate_trait)
    trait_accumulators[candidate_trait].accumulator = 0.0
    record MilestoneType::trait_displacement in milestone_log
else:
    trait_accumulators[candidate_trait].accumulator -= TRAIT_DISPLACEMENT_FAILED_DECAY  // default: 0.05
    // accumulator drops below threshold; must rebuild before roll re-fires
```

**Multiple displacement:** When a character holds multiple traits that are all hard-incompatible with the same candidate, conflicts are evaluated sequentially in the order they appear in `traits`. The first successful displacement terminates the loop; the candidate trait is awarded. If the first evaluation fails, subsequent conflicts are not evaluated — the accumulator resets and the cycle restarts.

**Trait permanence:** Held traits are permanent outside of displacement. Natural decay affects only candidate accumulators, not held traits.

### 4.4 Active Counter-Behavior Drainage

Actions that directly oppose a trait's behavioral pattern drain that trait's candidate accumulator by `TRAIT_COUNTER_BEHAVIOR_DRAIN` (default: 0.03). `last_reinforced_tick` is NOT updated — counter-behavior does not reset the decay timer. Counter-behavior pairings are listed in §7.2.

---

## 5. Accumulator System

### 5.1 TraitAccumulator Struct

```cpp
struct TraitAccumulator {
    Trait    candidate_trait;
    float    accumulator;         // -TRAIT_ACCUMULATOR_MIN_FLOOR to 1.0
                                  // negative = friction from background seeds
    float    award_threshold;     // 0 = use global default (§5.5 for per-trait overrides)
    float    decay_rate;          // 0 = use global default
    uint32_t last_reinforced_tick;
};
```

**Initialization:** All 25 entries created at character creation with `accumulator = 0.0`. Background stage seeds (§6) are applied immediately after. Per-trait threshold overrides (§5.5) set at initialization. The award roll runs on all accumulators after seeding.

### 5.2 Effective Threshold

```cpp
float effective_threshold(const TraitAccumulator& ta) {
    return (ta.award_threshold != 0.0f)
        ? ta.award_threshold
        : config.trait_system.award_threshold;  // default: 0.65
}
```

### 5.3 Input Types

**Repeated actions** — qualifying action processed (§7.1):
```
accumulator = clamp(accumulator + TRAIT_REPEATED_ACTION_INCREMENT, -MIN_FLOOR, 1.0)
last_reinforced_tick = current_tick
```

**Skill threshold bumps** — once per domain per threshold crossing (0.3, 0.5, 0.7, 0.9):
```
primary_accumulator   = clamp(primary_accumulator   + TRAIT_SKILL_THRESHOLD_BUMP,    -MIN_FLOOR, 1.0)
secondary_accumulator = clamp(secondary_accumulator + TRAIT_SKILL_SECONDARY_BUMP,    -MIN_FLOOR, 1.0)
// last_reinforced_tick updated for both
```

**Drastic event spikes** — single event direct injection (§7.4):
```
accumulator = clamp(accumulator + spike_magnitude, -MIN_FLOOR, 1.0)
```

**Counter-behavior drain** (§4.4):
```
accumulator = clamp(accumulator - TRAIT_COUNTER_BEHAVIOR_DRAIN, -MIN_FLOOR, 1.0)
// last_reinforced_tick NOT updated
```

### 5.4 Award Roll

Fires only when a reinforcing input (action, skill raise, or drastic event) is processed AND the resulting accumulator is ≥ `effective_threshold`. Does not fire on ticks with no input. Does not fire from counter-behavior drain.

```cpp
void check_trait_award(Trait candidate, PlayerCharacter& pc) {
    const TraitAccumulator& ta = pc.trait_accumulators[candidate];
    if (ta.accumulator < effective_threshold(ta)) return;
    if (pc.traits contains candidate) return;

    float award_probability = clamp(ta.accumulator,
                                    effective_threshold(ta),
                                    TRAIT_MAX_AWARD_PROBABILITY);  // default: 0.95

    if (random_float(seed) < award_probability) {
        auto conflicts = get_hard_incompatible_held_traits(candidate, pc.traits);
        if (conflicts.empty()) {
            pc.traits.push_back(candidate);
            pc.trait_accumulators[candidate].accumulator = 0.0f;
        } else {
            run_displacement_sequence(candidate, conflicts, pc);  // §4.3
        }
    }
    // Failed: accumulator unchanged; next reinforcing input re-triggers
}
```

### 5.5 Per-Trait Threshold Overrides

| Trait | Override | Rationale |
|---|---|---|
| Paranoid | 0.50 | Drastic traumatic events sufficient without extended behavioral runway |
| Resilient | 0.55 | Earned through adversity; shorter runway than learned traits |
| Impulsive | 0.70 | Requires strong and sustained pattern |
| Scholarly | 0.75 | Requires genuine long-term academic investment |
| Chameleon | 0.75 | Requires proven cross-context history |

### 5.6 Decay

Each tick, for every `TraitAccumulator` where `current_tick - last_reinforced_tick > TRAIT_DECAY_GRACE_PERIOD` (default: 60 ticks):

```cpp
float decay = (ta.decay_rate != 0.0f) ? ta.decay_rate
                                       : config.trait_system.accumulator_decay_rate;  // 0.0001

if      (ta.accumulator > 0.0f) ta.accumulator = max(0.0f, ta.accumulator - decay);
else if (ta.accumulator < 0.0f) ta.accumulator = min(0.0f, ta.accumulator + decay * 0.5f);
// negative accumulators (friction) decay toward 0 at half rate
```

---

## 6. Background Stage Seeds

Background stage choices add initial values to `TraitAccumulator.accumulator` at character creation. Negative values set friction — the accumulator starts below 0.0, requiring more reinforcing inputs before reaching threshold.

### 6.1 Stage 1 — Early Childhood Seeds

| Stage 1 Choice | Accumulator Seeds |
|---|---|
| Stable, loving household | Resilient +0.15, Empathetic +0.20, Stoic −0.05 |
| Chaotic / unstable household | Resilient +0.25, Independent +0.20, Adaptable +0.15, Empathetic −0.05 |
| Loss of a parent or primary caregiver | Empathetic +0.20, Resilient +0.20, Independent +0.15 |
| Religious or highly traditional household | Disciplined +0.20, PoliticallyAstute +0.10, Stoic +0.10, Principled +0.10, Adaptable −0.05 |
| Family that moved frequently | Adaptable +0.25, Chameleon +0.15, Connected +0.10 |
| Close-knit small community | Connected +0.20, Empathetic +0.15, Principled +0.10, Adaptable −0.10 |
| State-raised (orphanage / care system) | Independent +0.25, Resilient +0.20, Adaptable +0.10, Connected −0.10 |

**Economic circumstance sub-selector:**

| Sub-choice | Accumulator Seeds |
|---|---|
| Impoverished | StreetSmart +0.10, Resilient +0.10 |
| Modest | no modifier |
| Comfortable | Scholarly +0.05 |
| Well-off | Scholarly +0.10, StreetSmart −0.05 |

### 6.2 Stage 2 — Youth Seeds

| Stage 2 Choice | Accumulator Seeds |
|---|---|
| Academic achiever | Analytical +0.25, Scholarly +0.20, StreetSmart −0.05 |
| Physical and competitive | PhysicallyRobust +0.25, Ambitious +0.15, Disciplined +0.10 |
| Creative and artistic | Creative +0.25, Intuitive +0.10, Empathetic +0.10 |
| Social leader / popular | Charismatic +0.25, Connected +0.20, Manipulative +0.05 |
| Solitary / self-directed | Independent +0.20, Analytical +0.15, Patient +0.15, Charismatic −0.05 |
| Early worker / entrepreneurial | Ambitious +0.20, Disciplined +0.15, StreetSmart +0.10 |
| Trouble and mischief | StreetSmart +0.25, Resilient +0.10, Disciplined −0.10 |
| Community or civic involvement | PoliticallyAstute +0.20, Empathetic +0.15, Connected +0.15, Principled +0.10 |

Ruthless is not seeded at any Stage 2 choice.

### 6.3 Stage 3 — Teenage Years Seeds

| Stage 3 Choice | Accumulator Seeds |
|---|---|
| Academic / university track | Scholarly +0.25, Analytical +0.15, Ambitious +0.10 |
| Vocational / apprenticeship | Disciplined +0.20, Patient +0.15 |
| Military service | Disciplined +0.25, PhysicallyRobust +0.15, Stoic +0.15, Independent +0.10 |
| Self-taught entrepreneur | Ambitious +0.20, Creative +0.15, RiskTolerant +0.15, Independent +0.10 |
| Political / activist | PoliticallyAstute +0.25, Charismatic +0.10, Patient +0.10, Principled +0.10 |
| Criminal apprenticeship | StreetSmart +0.25, Paranoid +0.10, Cautious −0.10 |
| Early family responsibility | Resilient +0.20, Empathetic +0.15, Patient +0.15, Ambitious −0.05 |
| Artistic / bohemian | Creative +0.25, Independent +0.15, Adaptable +0.10, Disciplined −0.05 |

Ruthless is not seeded at Stage 3 `Criminal apprenticeship`. Criminal work teaches wariness and street knowledge — not willingness to harm.

### 6.4 Cross-Stage Combination Bonuses

| Stage 1 | Stage 2 | Stage 3 | Bonus |
|---|---|---|---|
| Any | Trouble and mischief | Criminal apprenticeship | StreetSmart +0.15 |
| Any | Academic achiever | Academic / university track | Scholarly +0.15, Analytical +0.10 |
| Any | Physical and competitive | Military service | PhysicallyRobust +0.15, Disciplined +0.10 |
| Chaotic / unstable household | Any | Early family responsibility | Resilient +0.20 |
| Close-knit small community | Community or civic involvement | Political / activist | PoliticallyAstute +0.15, Connected +0.10, Principled +0.05 |
| State-raised | Solitary / self-directed | Self-taught entrepreneur | Independent +0.20, Adaptable +0.10 |
| Religious or traditional | Community or civic involvement | Political / activist | Principled +0.15, PoliticallyAstute +0.10 |

---

## 7. Trait-Action Mapping

### 7.1 Reinforcing Actions

Each qualifying action adds `TRAIT_REPEATED_ACTION_INCREMENT` (0.02) to listed traits.

Criminal operation note: Financial and deceptive criminal actions reinforce StreetSmart only. Ruthless requires direct harm or coercion.

| Action Type | Primary Trait | Secondary Trait |
|---|---|---|
| Persuasion engagement — social path | Charismatic | Manipulative |
| Persuasion engagement — unconventional path | Creative | Intuitive |
| Coercive leverage (non-violent) | Manipulative | — |
| Coercive leverage (harm or threat of violence) | Ruthless | Manipulative |
| Coercive leverage against NPC with trust_level ≥ 0.5 | Ruthless | — |
| Bribery or corruption declined when available and profitable | Principled | — |
| Obligation honored at net personal cost | Principled | Empathetic |
| NPC relationship maintained 6+ in-game months | Connected | Empathetic |
| NPC relationship depth ceiling reached | Empathetic | Connected |
| Criminal operation — financial / deceptive | StreetSmart | — |
| Criminal operation — coercive / violent | StreetSmart | Ruthless |
| Criminal operation with active evidence management | Cautious | StreetSmart |
| Finance skill engagement resolved | Analytical | Scholarly |
| Business innovation action | Creative | Ambitious |
| Business scaled to next tier | Ambitious | RiskTolerant |
| High-risk investment held to positive resolution | RiskTolerant | Patient |
| High-risk investment held through loss without reversal | Resilient | Stoic |
| Slow-burn strategy to multi-year completion | Patient | Disciplined |
| Calendar fully utilized with no missed slots | Disciplined | Ambitious |
| Lower-risk option selected when higher-risk available | Cautious | — |
| Evidence management action | Cautious | Paranoid |
| Countersurveillance action | Paranoid | StreetSmart |
| Action in province/sector with <30 ticks of history | Adaptable | — |
| Cross-context social engagement (different strata in same week) | Chameleon | Connected |
| Political campaign action | PoliticallyAstute | Patient |
| Institutional NPC engagement (regulator, senior politician) | PoliticallyAstute | Analytical |
| Physical maintenance or healthcare investment | PhysicallyRobust | Disciplined |
| Personal life scene card engaged honestly | Empathetic | Connected |
| Personal life scene card skipped or disengaged | Stoic | Independent |
| NPC mentor or caregiver role maintained | Empathetic | Patient |
| NPC betrayal survived without operational collapse | Resilient | Paranoid |
| Assassination attempt or serious threat survived | Paranoid | Resilient |

### 7.2 Counter-Behavior Drain

These actions drain the listed trait's accumulator by `TRAIT_COUNTER_BEHAVIOR_DRAIN` (0.03). `last_reinforced_tick` NOT updated.

| Action Type | Drains Accumulator For |
|---|---|
| Coercive action against NPC with trust_level ≥ 0.5 | Empathetic |
| Bribery or corruption accepted | Principled |
| Obligation to NPC defaulted when honoring was feasible | Principled |
| High-risk investment reversed at loss before resolution | RiskTolerant |
| Evidence management or operational security neglected | Cautious |
| NPC relationship calendar slots consistently skipped | Connected, Empathetic |
| Personal life scene cards engaged dishonestly | Empathetic |
| Social isolation sustained without necessity | Charismatic, Connected |

### 7.3 Skill Threshold Bumps

Fires once per domain per threshold crossing (0.3, 0.5, 0.7, 0.9):

| Skill Domain | Primary (+0.12) | Secondary (+0.06) |
|---|---|---|
| Finance | Analytical | Ambitious |
| Engineering | Analytical | Disciplined |
| Intelligence | Analytical | Paranoid |
| Politics | PoliticallyAstute | Charismatic |
| Management | Disciplined | Connected |
| Trade | StreetSmart | Adaptable |
| Persuasion | Charismatic | Manipulative |
| CriminalOperations | StreetSmart | Ruthless |
| Business | Ambitious | Creative |
| SpecialtyCulinary | — | — |
| SpecialtyChemistry | — | — |
| SpecialtyCoding | Analytical | — |
| SpecialtyAgriculture | — | Resilient |
| SpecialtyConstruction | — | Disciplined |

### 7.4 Drastic Event Spikes

| Event | Trait | Magnitude | Secondary |
|---|---|---|---|
| Survived assassination attempt | Paranoid | +0.40 | — |
| Survived near-death health event | Resilient | +0.30 | — |
| NPC trusted ally betrayal (trust_level ≥ 0.6) | Paranoid | +0.25 | — |
| Major financial collapse (>50% net worth) survived | Resilient | +0.25 | RiskTolerant +0.15 |
| Direct serious harm to NPC with trust_level ≥ 0.5 | Ruthless | +0.30 | Empathetic drain −0.10 |
| Personal life major loss without behavioral collapse | Stoic | +0.35 | — |
| Movement reaches 10,000 followers | Charismatic | +0.25 | PoliticallyAstute +0.15 |
| Bankruptcy and full recovery | Resilient | +0.40 | — |
| Cover identity maintained 30+ ticks | Chameleon | +0.30 | — |
| Near-arrest event | Paranoid | +0.20 | Cautious +0.15 |
| Obligation honored publicly at major personal cost | Principled | +0.30 | — |

---

## 8. Trait Synergies

When a character holds specific trait combinations, emergent mechanical effects activate beyond what each trait provides individually.

### 8.1 THE BELOVED (Charismatic + Empathetic)
- NPC follower loyalty threshold: −20%
- Trust build rate: Empathetic's and Charismatic's bonuses both apply fully (no cap conflict)
- Movement scene cards unlock requiring both traits — followers who identify personally with the character rather than just the cause

### 8.2 THE CHESS PLAYER (Analytical + Patient)
- Multi-step strategic engagement options unlock: probability chains 3+ moves ahead
- KnowledgeMap projections extend to predicted future NPC states (~2 in-game months forward at base)

### 8.3 THE PREDATOR (Ruthless + Manipulative)
- Coercive engagement effectiveness: +35% (vs. Manipulative's standalone +20%)
- Coercive actions require no prior relationship foundation

### 8.4 THE EXPERT (Disciplined + Scholarly)
- Knowledge domain skill `decay_rate`: × 0.5 (below Disciplined's standalone ×0.6)
- Knowledge domain skill acquisition rate: +35%

### 8.5 THE CON (Chameleon + Manipulative)
- Cover identity: NPCs take 3× longer to detect inconsistencies (vs. Chameleon's standalone 2×)
- Manipulation within cover: leverage effectiveness +30%

### 8.6 THE FIXER (StreetSmart + Connected)
- `network_criminal_opportunity` event tier unlocked — grey/black market opportunities requiring both knowledge and network
- Criminal NPC referral rate doubles in grey/black contexts

### 8.7 THE GAMBLER (Ambitious + RiskTolerant)
- Outcome variance on high-risk actions: ×1.6
- `all_in` action variant unlocked on major decisions: doubles both downside and upside

### 8.8 THE KINGMAKER (PoliticallyAstute + Patient)
- `behind_the_scenes_operation` action type unlocked: influence political outcomes without running for office
- Political influence campaigns: +25% effectiveness

### 8.9 THE SURVIVOR (PhysicallyRobust + Resilient)
- Single-event health floor: cannot be reduced to 0 health by one event that begins above 10% health
- Active engagement availability extended 5 additional in-game years

### 8.10 THE HONEST BROKER (Principled + Charismatic)
- `principled_reputation` memory propagates through the NPC social graph at higher rate than normal memory propagation
- `trusted_intermediary` role unlocked in multi-party negotiations: broker deals between NPCs who otherwise won't deal with each other

### 8.11 THE INTELLIGENCE ANALYST (Paranoid + Analytical)
- Intelligence skill acquisition: +25%
- Counter-intelligence operations: identifying compromised assets requires 50% less evidence

---

## 9. Mental Health Crisis States

Mental health crises are temporary states that layer on top of the trait system, modifying how traits express rather than altering which traits are held. If untreated, a crisis can escalate to permanent trait modification — bypassing the accumulator system and applying changes directly.

A character after a severe crisis is recognizably themselves: same relationships, same skills, same history. But they carry marks that don't go away.

### 9.1 Structs

```cpp
enum class MentalHealthCrisisType : uint8_t {
    None              = 0,
    PsychoticBreak    = 1,
    SevereDepression  = 2,
    ManicEpisode      = 3,
    AddictionSpiral   = 4,
    SevereTrauma      = 5,
};

enum class CrisisResolutionState : uint8_t {
    Nominal      = 0,   // no active crisis
    AcutePhase   = 1,   // crisis active; effects at full severity
    Stabilizing  = 2,   // intervention received; severity decrementing
    Residual     = 3,   // crisis resolved; permanent marks applied; no active modifiers
};

struct ResidualMark {
    Trait   affected_trait;
    float   effectiveness_modifier;  // multiplicative; applied to trait's bonuses each tick
                                     // <1.0 = suppression; >1.0 = amplification
    float   accumulator_offset;      // one-time permanent adjustment applied when mark is created
};

struct MentalHealthCrisisState {
    MentalHealthCrisisType  type;                    // None if Nominal
    CrisisResolutionState   resolution_state;
    float                   severity;                // 0.0–1.0; drives effect magnitude
    uint32_t                onset_tick;
    uint32_t                last_intervention_tick;  // 0 if no intervention received
    bool                    intervention_received;
    std::vector<ResidualMark> residual_marks;        // permanent; persist through Residual into future Nominal
};
```

Added to `PlayerCharacter`:
```cpp
MentalHealthCrisisState mental_health;
```

### 9.2 Crisis Triggers

| Trigger Condition | Crisis Type |
|---|---|
| `exhaustion_accumulator > 0.9` sustained for 30+ ticks without recovery | SevereDepression or ManicEpisode (determined by character trait profile: Ambitious + RiskTolerant lean toward Manic; Stoic + Independent lean toward Depression) |
| Addiction stage ≥ 3 (per GDD §12.11 addiction mechanics) | AddictionSpiral |
| 3+ high-severity drastic events within 30 ticks | SevereTrauma |
| Near-death health event while `exhaustion_accumulator > 0.7` | SevereTrauma or PsychoticBreak |
| Sustained social isolation (no NPC interactions > 0.3 trust for 90+ ticks) while under high stress | SevereDepression |
| `health.current_health` drops below 0.2 | SevereTrauma |

### 9.3 Trait Effectiveness Modifiers During AcutePhase

These are multiplicative modifiers applied to trait bonuses during the crisis. They do not alter which traits are held — only how those traits express.

**PsychoticBreak:**
| Trait | Modifier | Note |
|---|---|---|
| Paranoid | ×2.0 if held; else +0.05/tick to accumulator | Threat perception overclocked |
| Analytical | ×0.3 | Systematic reasoning severely impaired |
| Empathetic | alternates ×0.0 / ×1.5 every 7 ticks | Affect becomes erratic |
| Charismatic | ×0.5 | Social coherence degraded |
| Principled | ×0.5 | Values feel distant and abstract |

**SevereDepression:**
| Trait | Modifier |
|---|---|
| Ambitious | ×0.1 |
| Disciplined | ×0.3 |
| All skill acquisition rates | ×0.4 |
| Resilient | ×0.5 |
| Stoic | ×0.0 (no emotional buffer — character is more raw, not less) |
| Patient | ×0.5 |

**ManicEpisode:**
| Trait | Modifier | Note |
|---|---|---|
| Ambitious | ×2.5 | |
| RiskTolerant | ×2.0 | |
| Cautious | ×0.0 | |
| Disciplined | ×0.0 | |
| Impulsive | Full Impulsive behavior available for duration even if trait not held | Temporary behavioral state, not trait award |
| Exhaustion accumulator | Does not update normally | Character does not feel fatigue |

**AddictionSpiral:**
| Trait | Modifier |
|---|---|
| Disciplined | ×0.2 |
| Patient | ×0.2 |
| Cautious | ×0.3 |
| RiskTolerant | ×1.5 (specific to obtaining the substance) |

**SevereTrauma:**
| Trait | Modifier | Note |
|---|---|---|
| Paranoid | Immediate accumulator +0.30 spike | |
| Stoic | Initially ×0.0 → if resolving: ×1.5 | Post-traumatic stoicism builds during Stabilizing |
| Empathetic | ×0.5 | Emotional numbing |
| Connected | ×0.3 | Withdrawal |

### 9.4 Crisis Escalation (No Intervention)

Each tick in AcutePhase where `intervention_received == false`:

```
severity += config.mental_health.escalation_rate  // default: 0.005/tick

if severity >= config.mental_health.permanent_threshold:  // default: 0.75
    apply_permanent_crisis_modifications(crisis_type, character)
    // This bypasses the accumulator system entirely (§9.6)
```

### 9.5 Crisis Resolution (With Intervention)

Intervention sources: healthcare NPC, high-trust personal relationship engagement, rest (no calendar engagement for 7+ ticks), specialist facility.

```
if intervention received this tick:
    intervention_received = true
    last_intervention_tick = current_tick
    resolution_state = Stabilizing

each tick in Stabilizing:
    severity -= config.mental_health.recovery_rate  // default: 0.01/tick

    if severity <= config.mental_health.resolved_threshold:  // default: 0.20
        resolution_state = Residual
        apply_residual_marks(crisis_type, character)
        severity = 0.0
```

### 9.6 Permanent Crisis Modifications (Escalated Without Intervention)

Applied directly to the trait system, bypassing accumulator thresholds. These are not displacement events — traits are added or modified without requiring accumulator buildup.

| Crisis Type | Permanent Modifications |
|---|---|
| PsychoticBreak (escalated) | Paranoid directly awarded if not held; Analytical effectiveness ×0.7 permanent; if Empathetic held: Displacement Roll fires immediately |
| SevereDepression (escalated) | Resilient accumulator +0.20; Ambitious effectiveness ×0.7 permanent; if not recovered: character enters terminal health phase |
| ManicEpisode (escalated) | Impulsive directly awarded if not held; RiskTolerant directly awarded if not held; if Cautious or Disciplined held: Displacement Roll fires immediately |
| AddictionSpiral (escalated) | Disciplined effectiveness ×0.7 permanent; per GDD §12.11 long-term health consequences |
| SevereTrauma (escalated) | Paranoid directly awarded if not held; Empathetic effectiveness ×0.8 permanent |

### 9.7 Residual Marks (Resolved With Intervention)

Applied when `resolution_state` transitions to `Residual`. Smaller than escalated modifications; permanent.

| Crisis Type | Residual Marks |
|---|---|
| PsychoticBreak | Paranoid accumulator +0.15; Analytical effectiveness ×0.9; `exhaustion_tolerance` −0.05 |
| SevereDepression | Patient effectiveness ×0.9; Resilient accumulator +0.10 |
| ManicEpisode | RiskTolerant accumulator +0.10; Cautious effectiveness ×0.95 |
| AddictionSpiral | Disciplined effectiveness ×0.9; substance-specific health modifiers per GDD §12.11 |
| SevereTrauma | Paranoid accumulator +0.10; Empathetic effectiveness ×0.95 |

### 9.8 Named Constants (`simulation.json → mental_health`)

```json
"mental_health": {
    "escalation_rate":      0.005,
    "recovery_rate":        0.010,
    "permanent_threshold":  0.75,
    "resolved_threshold":   0.20
}
```

---

## 10. Trait Synergy Unlocked Action Types

The following action types are created by trait synergies (§8) and must be defined in the engagement system:

| Action Type | Unlocked By |
|---|---|
| `network_criminal_opportunity` | StreetSmart + Connected |
| `behind_the_scenes_operation` | PoliticallyAstute + Patient |
| `all_in` | Ambitious + RiskTolerant |
| `trusted_intermediary` | Principled + Charismatic |

Full specification for each belongs in the engagement or action system documentation.

---

## 11. Named Constants (`simulation.json → trait_system`)

```json
"trait_system": {
    "award_threshold":                      0.65,
    "max_award_probability":                0.95,
    "accumulator_decay_rate":               0.0001,
    "accumulator_min_floor":                0.30,
    "decay_grace_period_ticks":             60,
    "repeated_action_increment":            0.02,
    "counter_behavior_drain":               0.03,
    "skill_threshold_bump":                 0.12,
    "skill_secondary_bump":                 0.06,
    "drastic_spike_min":                    0.20,
    "drastic_spike_max":                    0.45,
    "displacement_probability_scale":       0.80,
    "displacement_max_probability":         0.75,
    "displacement_failed_decay":            0.05
}
```

---

## 12. TDD Integration Points

**§11.2 — Trait enum:** Replace 8-value enum with 25-value enum (§2). `TRAIT_COUNT = 25`.

**§11.3 — PlayerCharacter struct:**
```cpp
// Replace:
std::array<Trait, 3> traits;

// With:
std::vector<Trait>              traits;
std::vector<TraitAccumulator>   trait_accumulators;   // all 25 entries; see §5.1
MentalHealthCrisisState         mental_health;        // see §9.1
```

**§11.4 — CharacterCreationHistory struct:**
```cpp
struct CharacterCreationHistory {
    Stage1Choice    childhood;
    EconomicTier    childhood_wealth;   // Impoverished | Modest | Comfortable | WellOff
    Stage2Choice    youth;
    Stage3Choice    teenage;
    EducationLevel  education_level;    // derived; see Character Creation spec
};
```

**MemoryType enum additions required:**
```cpp
charismatic_impression, empathetic_recognition, disciplined_reputation,
ambitious_reputation, patient_operator_reputation, unpredictable_reputation,
comeback_story, self_reliant_read, risk_averse_reputation, risk_tolerant_profile,
ruthless_awareness, paranoid_read, unshakeable_read, surface_read_uncertain,
physical_presence_noted, well_connected_reputation, scholarly_recognition,
creative_reputation, manipulation_suspicion, political_operator_read,
principled_reputation, principled_refusal, street_credibility,
```

**NPC trait distribution:**
```json
"npc_trait_distribution": {
    "Resilient": 0.10, "Empathetic": 0.09, "Disciplined": 0.09,
    "Analytical": 0.08, "Ambitious": 0.08, "Cautious": 0.07,
    "Patient": 0.06, "Connected": 0.06, "Creative": 0.05,
    "Principled": 0.05, "Independent": 0.05, "Charismatic": 0.04,
    "Stoic": 0.04, "RiskTolerant": 0.03, "Adaptable": 0.03,
    "Intuitive": 0.03, "Scholarly": 0.02, "PoliticallyAstute": 0.02,
    "StreetSmart": 0.02, "PhysicallyRobust": 0.02, "Impulsive": 0.02,
    "Manipulative": 0.02, "Ruthless": 0.01, "Paranoid": 0.01,
    "Chameleon": 0.01
}
```

**Heir trait inheritance:** The heir is their own person — shaped by their formation, not a continuation of the character who raised them. The heir NPC arrives with trait accumulators that developed during the mentorship period: a character who invested in personal life scene cards, designated an heir early, and modeled specific behaviors created an NPC whose accumulators reflect that formation. What the heir absorbed from their upbringing is encoded in their starting accumulator values, some of which may cross threshold on their first ticks of play. What they make of that formation — which traits actually award, how they develop further — is theirs to determine. The heir also carries whatever the mentorship missed. Neglected formation produces gaps that show up under pressure.

---

## 13. Open Questions

**[AMBIGUITY]** When Ruthless is displaced by Empathetic, `ruthless_awareness` memory entries already in NPCs' logs persist. A reformed character carries a reputation that no longer matches their current trait profile. Confirm this is intended behavior.

**[SCOPE]** `Stage1Choice`, `Stage2Choice`, `Stage3Choice`, `EconomicTier`, `EducationLevel` enums must be defined in the Character Creation spec. Enum values must exactly match the choice names in §6.

**[SCOPE]** Mental health crisis triggers (§9.2) reference `exhaustion_tolerance` as a field on `PlayerCharacter`. This field must be added to the TDD §11 struct if not already present.

**[SCOPE]** The ManicEpisode escalation applies Displacement Rolls to Cautious and Disciplined. If the character holds both and both conflict with the now-awarded Impulsive, the multiple displacement rule (§4.3) applies sequentially.
