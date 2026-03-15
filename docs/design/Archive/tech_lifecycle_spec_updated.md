# Technology Lifecycle Model — Specification
*INTEGRATION COMPLETE — This spec is now retired as a standalone document.*
*Contents are canonical in: R&D & Technology v2.2 §Part 3.5 (lifecycle model, CommercializationDecision) and §Part 9 (GlobalTechnologyState, ActorTechnologyState, RnDFacility, TechnologyNode structs)*
*TDD v19 §3.3 contains the commercialize_technology WorkType and maturation_project_advance WorkType.*
*Commodities & Factories v2.3 contains the updated ComputeOutputQuality function and FacilityRecipe struct.*
*This file is retained for historical reference and scenario test stubs only. Do not use for Bootstrapper generation — use the companion documents above.*

---

## The Problem with the Current Model

The current design treats `GlobalTechnologyState.unlocked_nodes` as `map<string, bool>` — a global flag that flips once when *any* actor researches a node. This collapses three distinct things:

1. **Who has it** — only the researching actor, until diffusion occurs
2. **Whether it's on the market** — a deliberate business decision, not automatic
3. **How good it is** — a function of continued investment, not a one-time step

The iPhone launched in 2007. It was a `product_unlock` for `mobile_phone_smartphone`. But the 2007 iPhone and the 2026 iPhone are not the same product. The unlock opened the category; maturation is what turned it into the dominant computing platform. A simulation that collapses these into one event cannot model competitive dynamics in technology markets.

---

## The Three-Stage Technology Lifecycle

Every technology node, once researched, moves through three stages **per actor**. These are not global — different actors hold the same technology at different stages simultaneously.

```
Stage 1: RESEARCHED
    The actor has demonstrated the technology works.
    They can use it internally (private production, supply chain).
    Quality ceiling is low — first-generation capability.
    Not visible to other actors as a market product.
    Patent may be filed (begins 20-year clock).

Stage 2: COMMERCIALIZED
    The actor decides to bring the product to open market.
    Product becomes observable — other actors can reverse-engineer.
    Market demand signals are generated.
    NPC consumers and businesses can purchase.
    Patent protection is still active (if filed).

Stage 3: MATURED (ongoing, never fully complete)
    Continued investment raises maturation_level (0.0 → 1.0).
    maturation_level raises the quality ceiling of products made using this technology.
    maturation_level is era-capped — some improvements require later-era dependent tech.
    Competing actors can close the maturation gap by investing heavily post-acquisition.
```

**Key invariant:** A technology can only move forward through stages. An actor who has commercialized a technology cannot un-commercialize it. An actor who has researched a technology cannot lose the research (only the patents can expire or be stolen).

---

## Data Structures

### TechHolding — Per-Actor Technology Record

```cpp
enum class TechStage : uint8_t {
    researched,      // actor has the capability; can produce internally
    commercialized,  // product is on the open market
};

struct TechHolding {
    std::string  node_key;
    uint32_t     holder_id;            // business entity
    TechStage    stage;
    float        maturation_level;     // 0.0–1.0; rises with continued investment
    float        maturation_ceiling;   // era-gated max; rises as era advances
    uint32_t     researched_tick;      // when Stage 1 was reached
    uint32_t     commercialized_tick;  // when Stage 2 was reached; 0 if not yet
    bool         has_patent;           // true if patent was filed on research completion
    float        internal_use_only;    // if true, product exists in supply chain but not on market
                                       // (commercialized stage but trade restricted; e.g. vertical integration only)
};
```

### ActorTechnologyState — Per-Business Tech Portfolio

```cpp
struct ActorTechnologyState {
    uint32_t  business_id;
    std::map<std::string, TechHolding> holdings;  // node_key → holding

    // Helpers:
    bool      has_researched(const std::string& node_key) const;
    bool      has_commercialized(const std::string& node_key) const;
    float     maturation_of(const std::string& node_key) const;  // 0.0 if not held
};
```

### GlobalTechnologyState — Revised

The global state no longer tracks "who has what." It tracks only what is needed for domain knowledge and patent arbitration:

```cpp
struct GlobalTechnologyState {
    // Which nodes have been researched by ANYONE — used for domain knowledge contributions
    // and to signal that a tech category is now possible in the world.
    // Value: first holder_id to research it (for domain knowledge attribution).
    std::map<std::string, uint32_t>  first_researched_by;   // node_key → holder_id

    // Which nodes have been commercialized by ANYONE — used to determine if a
    // product type is visible in the market and eligible for reverse engineering.
    std::map<std::string, uint32_t>  first_commercialized_by;  // node_key → holder_id

    // Per-domain knowledge (unchanged)
    std::map<std::string, float>     domain_knowledge_levels;

    std::vector<Patent>              active_patents;
    std::vector<SchedulingProcess>   active_scheduling_reviews;
    SimulationEra                    current_era;
    float                            global_co2_index;
    ClimateState                     climate_state;
};
```

`ActorTechnologyState` is a field on `BusinessEntity`, not on `WorldState`. Each business owns its technology portfolio.

---

## Maturation Mechanics

### Investment Model

After Stage 1 (research complete), the actor can run **maturation projects** — a distinct project type that does not unlock new nodes but raises the `maturation_level` of a held technology.

```cpp
struct MaturationProject {
    std::string  node_key;           // which technology to mature
    uint32_t     facility_id;        // same facility types as research projects
    uint32_t     researchers_assigned;
    float        funding_per_tick;
    float        progress;           // accumulated toward next maturation threshold
};
```

**Maturation progress per tick:**
```
maturation_progress_per_tick = researchers_assigned
                              × researcher_quality_avg
                              × facility_quality_modifier
                              × domain_knowledge_bonus
                              × funding_adequacy
                              × MATURATION_RATE_COEFF    // slower than original research

maturation_level += maturation_progress_per_tick / MATURATION_DIFFICULTY_PER_LEVEL
maturation_level  = min(maturation_level, maturation_ceiling)
```

**Named constants (in `rnd_config.json`):**
```
MATURATION_RATE_COEFF         = 0.40   // maturation is slower per researcher than original research
MATURATION_DIFFICULTY_PER_LEVEL = 2.0  // effort points to raise maturation_level by 0.1
```

### Era-Gated Maturation Ceiling

A technology cannot be matured beyond what the current era's dependent technologies support. The 2007 iPhone couldn't become a 2026 iPhone through software investment alone — it needed OLED, 5G, ML chips, and a decade of mobile SoC scaling.

```cpp
float compute_maturation_ceiling(
    const std::string& node_key,
    SimulationEra current_era,
    const ActorTechnologyState& actor_state
) {
    float base_ceiling = MATURATION_ERA_CEILING_BASE[node_key][current_era];

    // Ceiling can rise if the actor also holds dependent enhancement nodes.
    // e.g., mobile_phone_smartphone ceiling rises when the actor holds:
    //   - process_node_7nm (Era 4 chip density)
    //   - oled_display (Era 2+)
    //   - 5g_base_station (Era 4)
    float enhancement_bonus = 0.0f;
    for (const auto& enhancer : MATURATION_ENHANCERS[node_key]) {
        if (actor_state.has_researched(enhancer.node_key)) {
            enhancement_bonus += enhancer.ceiling_bonus;
        }
    }

    return std::min(1.0f, base_ceiling + enhancement_bonus);
}
```

**`MATURATION_ERA_CEILING_BASE` example values:**

| node_key | Era 1 | Era 2 | Era 3 | Era 4 | Era 5 |
|---|---|---|---|---|---|
| electric_vehicle | 0.10 | 0.35 | 0.55 | 0.75 | 0.90 |
| mobile_phone_smartphone | — | 0.25 | 0.50 | 0.70 | 0.85 |
| solar_panel_cost_competitive | 0.20 | 0.40 | 0.60 | 0.80 | 0.95 |
| large_language_model | — | — | — | 0.40 | 0.75 |
| mrna_therapeutics | — | — | — | 0.35 | 0.65 |

*"—" means the tech is not researchable that era; ceiling is N/A.*

These values are data-file defined (`/data/technology/maturation_ceilings.csv`), fully moddable.

**Design note:** The era ceiling does not prevent investment — it caps the return. An actor can over-invest in maturation, but they will plateau until the era advances. This models diminishing returns accurately: you can keep spending on 3G network quality in 2010, but without 4G infrastructure, you'll never reach 2026 mobile capability.

---

## Effect on Product Quality

### Revised ComputeOutputQuality

`ComputeOutputQuality` in Commodities & Factories must be updated to incorporate actor-specific maturation level alongside facility tech tier.

**Current formula:**
```
quality_ceiling = TECH_QUALITY_CEILING_BASE + TECH_QUALITY_CEILING_STEP × (facility_tech_tier - recipe.min_tech_tier)
```

**Revised formula:**
```cpp
float compute_quality_ceiling(
    const FacilityRecipe&       recipe,
    uint8_t                     facility_tech_tier,
    const ActorTechnologyState& actor_state
) {
    // Base ceiling from facility tier (unchanged)
    float tier_ceiling = TECH_QUALITY_CEILING_BASE
        + TECH_QUALITY_CEILING_STEP × (facility_tech_tier - recipe.min_tech_tier);

    // Maturation ceiling from the actor's technology holding for the key node
    // recipe.key_technology_node identifies which TechHolding governs this recipe.
    // If the actor does not hold the tech at all, they cannot run the recipe.
    float maturation_ceiling = 1.0f;  // default: no maturation cap if no key node
    if (!recipe.key_technology_node.empty()) {
        maturation_ceiling = actor_state.maturation_of(recipe.key_technology_node);
        // maturation_of() returns 0.0 if not held → cannot produce
    }

    // Final ceiling: the more restrictive of the two
    float quality_ceiling = std::min(tier_ceiling, maturation_ceiling);
    quality_ceiling = std::min(quality_ceiling, 1.0f);

    return quality_ceiling;
}

float compute_output_quality(
    const std::vector<RecipeInput>& inputs,
    const std::vector<float>&       input_qualities,
    const FacilityRecipe&           recipe,
    uint8_t                         facility_tech_tier,
    const ActorTechnologyState&     actor_state
) {
    float weighted_input_quality = /* existing weighted average logic */;
    float ceiling = compute_quality_ceiling(recipe, facility_tech_tier, actor_state);
    return std::min(weighted_input_quality, ceiling);
}
```

**`FacilityRecipe` gains one field:**
```cpp
std::string key_technology_node;  // node_key whose maturation_level caps this recipe's quality
                                   // empty string = no maturation cap (mature/commodity production)
```

**Effect:** Two producers using identical facilities and inputs will produce different quality output if their maturation levels differ. The first company to commercialize EVs produces low-quality vehicles (maturation 0.25). A decade later, with continued R&D investment, the same factory produces quality 0.75. A late entrant who licenses the technology from the pioneer gets the pioneer's current maturation snapshot — not zero, but not equal to decades of the pioneer's cumulative investment either.

---

## Commercialization as a Business Decision

Commercialization is not automatic. It is a **BusinessAction** that the actor (player or NPC) must take.

```cpp
enum class CommercializationDecision : uint8_t {
    hold_proprietary,      // produce internally only; not sold on open market
    commercialize_open,    // sell on open market; product observable to all
    commercialize_licensed, // sell only to specific licensees; not generally observable
    commercialize_regional, // sell in specific regions only (regulatory or strategic choice)
};
```

**When an actor holds a technology in `Stage 1` (researched, not commercialized):**
- They can use the product in their own supply chain
- The product does not appear in market price feeds
- Other actors cannot observe the product directly (no reverse engineering target)
- NPC investigators may detect it via facility output signals (OPSEC mechanic)
- The actor's own products may implicitly reveal the technology (e.g., their vehicles get better range → competitors infer battery breakthrough)

**When an actor moves to `Stage 2` (commercialized):**
- Product appears in market
- Market price signals for the product type are generated
- Product becomes a reverse engineering target for competitors
- Patent protection (if active) prevents competitors from legally using the tech — but not from observing it

**NPC commercialization logic:**

NPC decision to commercialize is governed by business profile:

| Profile | Commercialization trigger |
|---|---|
| `fast_expander` | Commercializes as soon as production capacity exists; wants market share |
| `defensive_incumbent` | Delays commercialization; prefers licensing negotiations over open market |
| `cost_cutter` | Commercializes once internal supply chain benefit is extracted; then sells |
| `innovation_leader` | Commercializes immediately; uses market presence as competitive signal |
| `criminal_organization` | Never formally commercializes; distributes through grey/black channels |

---

## Technology Acquisition and Maturation Transfer

When an actor acquires technology through diffusion (licensing, reverse engineering, espionage, talent hire), they do not get the technology at maturation 0.0. They get a **snapshot** of the technology at a transferred maturation level.

```
Acquisition mechanism → Transferred maturation level
─────────────────────────────────────────────────────
Licensing (willing)        → licensor's current maturation × LICENSE_TRANSFER_RATE (0.80)
Reverse engineering        → observed product's implied maturation × REVERSE_ENG_TRANSFER (0.50)
Patent expiry (public)     → global average maturation at expiry × PUBLIC_DOMAIN_TRANSFER (0.60)
Corporate espionage        → target's current maturation × ESPIONAGE_TRANSFER (0.90)
Talent hire (researcher)   → researcher's domain_skill translates to maturation bonus
Academic publication       → contributes to domain_knowledge only; not direct maturation
```

**Named constants (in `rnd_config.json`):**
```
LICENSE_TRANSFER_RATE       = 0.80
REVERSE_ENG_TRANSFER        = 0.50
PUBLIC_DOMAIN_TRANSFER      = 0.60
ESPIONAGE_TRANSFER          = 0.90
```

**Maturation transfer is still subject to the era ceiling.** A competitor who steals bleeding-edge technology in Era 2 gets the maturation snapshot, but if the maturation ceiling for that technology in Era 2 is 0.35, they cannot use the transferred maturation above 0.35 until the era advances.

**After acquisition, the receiving actor can invest independently to advance maturation further.** The head start from the pioneer's work is real but not permanent — with sufficient investment, a late entrant can close the gap.

---

## Scenario Assertions (for test stubs)

```
[SCENARIO S-TL-01]: When actor A researches electric_vehicle and does NOT commercialize,
    then electric_vehicle does NOT appear in any regional market price feed.
    AND actor A can produce electric_vehicle for internal use (fleet, supply chain).
    AND other actors cannot initiate a reverse_engineering project targeting electric_vehicle
        (no observable product to disassemble).

[SCENARIO S-TL-02]: When actor A commercializes electric_vehicle at maturation_level = 0.30,
    then electric_vehicle appears in market with quality ceiling = min(tier_ceiling, 0.30).
    AND actor B can initiate reverse_engineering project targeting electric_vehicle.
    AND reverse_engineering success grants actor B a TechHolding at maturation = 0.30 × 0.50 = 0.15.

[SCENARIO S-TL-03]: When actor A holds electric_vehicle at maturation 0.30 in Era 2,
    AND era advances to Era 3 (MATURATION_ERA_CEILING_BASE[electric_vehicle][Era 3] = 0.55),
    then actor A's maturation_ceiling rises to 0.55 immediately (no action required).
    AND actor A can now invest to raise maturation_level beyond 0.30 up to 0.55.

[SCENARIO S-TL-04]: When actor A licenses electric_vehicle to actor B,
    AND actor A's current maturation_level = 0.60,
    then actor B receives TechHolding at maturation = 0.60 × 0.80 = 0.48.
    AND actor B's maturation_ceiling is still era-gated (not actor A's ceiling).
    AND actor B can invest to advance maturation independently.

[SCENARIO S-TL-05]: When actor A holds electric_vehicle at maturation = 0.25,
    AND actor A's facility tech_tier = 4,
    AND a competitor's factory (same tier, same inputs, different actor) holds maturation = 0.60,
    then actor A's output quality ceiling = min(tier_4_ceiling, 0.25) = 0.25.
    AND competitor's output quality ceiling = min(tier_4_ceiling, 0.60) = 0.60.
    AND the quality difference is visible in market prices (quality 0.25 vs 0.60 products).

[SCENARIO S-TL-06]: When actor A holds electric_vehicle proprietary (Stage 1),
    AND actor A also holds process_node_7nm (an MATURATION_ENHANCER for electric_vehicle),
    then actor A's maturation_ceiling for electric_vehicle rises by the enhancer bonus,
    even before commercialization.
    AND the ceiling bonus applies to internal production immediately.

[SCENARIO S-TL-07]: When patent for electric_vehicle expires (20-year term),
    AND patent holder had maturation_level = 0.80 at expiry,
    then GlobalTechnologyState.first_commercialized_by is unchanged (still original holder).
    AND any actor can now produce electric_vehicle WITHOUT a license.
    AND new entrants start at PUBLIC_DOMAIN_TRANSFER × 0.80 = 0.48 maturation.
    AND they are still era-capped.
```

---

## Impact on Existing Document Sections

### RnD_and_Technology_v2.0 — Changes Required

| Section | Change |
|---|---|
| Part 3 design philosophy | Add: "Technology unlock is per-actor, not global. The tech tree defines *what can be researched*; the technology lifecycle model defines *who has it, at what quality, and whether it's on the market*." |
| Part 3 node registry outcome_type | `product_unlock` outcome is now "grants Stage 1 TechHolding to researching actor." Not a global unlock. |
| Part 6 Diffusion | Each mechanism now specifies transferred maturation rate (table above replaces prose description). |
| Part 9 GlobalTechnologyState | Replace `map<string, bool> unlocked_nodes` with `map<string, uint32_t> first_researched_by` and `first_commercialized_by`. Add `ActorTechnologyState` to BusinessEntity. |

### Commodities_and_Factories_v2.1 — Changes Required

| Section | Change |
|---|---|
| ComputeOutputQuality | Add `actor_state` parameter; add maturation ceiling as second ceiling constraint. |
| FacilityRecipe struct | Add `key_technology_node` field (string; empty for commodity production). |

### TDD v16 — Changes Required

| Section | Change |
|---|---|
| BusinessEntity struct | Add `ActorTechnologyState actor_tech_state` field. |
| Tick step 26 | Maturation projects advance each tick alongside standard research projects. |
| BusinessAction enum | Add `commercialize_technology(node_key, CommercializationDecision)`. |

---

*Technology Lifecycle Model — spec complete*
*Three stages: researched → commercialized → matured (ongoing)*
*Maturation is per-actor, era-gated, and transferable at a discount*
