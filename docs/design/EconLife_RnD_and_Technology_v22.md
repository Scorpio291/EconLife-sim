# EconLife — Research, Development & Technology Progression
*Companion document to GDD v1.7, Technical Design Document v29, and Commodities & Factories v2.3*
*Version 2.3 — Pass 5 Session 8: Era transition mechanics, climate feedback non-linearity, NPC climate politics, researcher quality modifier, GlobalCommodityPriceIndex LOD weighting, all TDD references updated to v29*
*Pass 5 Session 9 (Scenario Tests) applied: 2 R&D scenario stubs added (RWA-S1 R&D Failure Modes, RWA-S2 Climate Regulation Adoption)*

---

## Purpose

This document specifies three deeply connected systems:

1. **The Era System** — the game begins in the year 2000 and time advances, changing what technology exists, what regulations apply, and how the world economy is structured. This framework now extends to Era 10 (~2250) with full scope markers for V1 and EX content.

2. **Research & Development** — the mechanism by which technology advances. Without R&D, manufacturing is static. With it, players can be ahead of history or behind it.

3. **The Climate System** — a slow-accumulating global condition driven by industrial activity, modeled from 2000 baselines. Its effects compound across all 250+ years of simulation time.

These three systems are documented together because they cannot be designed in isolation. The era system defines the timeline. R&D is how players navigate it. Climate is the long-run consequence they cannot entirely escape.

**Scope conventions used in this document:**
- `[V1]` — Required for the V1 release; Bootstrapper must generate scaffolding
- `[EX]` — Extended content; engine must support but gameplay content is post-V1
- `[CORE]` — Cross-cutting engine primitive; must exist regardless of content scope
- No marker on a section means V1 unless it falls within an EX era

---

## Part 1 — The Era System

### Design Intent

The game starts in **January 2000**. This starting point was chosen deliberately:

- 25+ years of documented economic history serves as the baseline simulation trajectory — the world will unfold roughly as it did if players do nothing extraordinary
- Players have enormous tech progression ahead of them — smartphones, social media, EVs, renewable energy, AI hardware, mRNA medicine, fusion, space economy, cognitive technology
- Climate change is still in its early policy phase — the consequences are coming, the window to influence them is open
- The world is still highly globalized and optimistic — the 2008 financial crisis, COVID, and deglobalization are future shocks players will either navigate or cause

Simulated time passes at a configurable rate. The default: **1 game year ≈ 6 real hours of play** (very rough; tuned during prototype phase). This gives a full 25-year V1 campaign timeline of roughly 150 real hours. Extended campaigns (EX) allow progression through the full 250-year arc.

---

### Eras

The simulation divides history into ten eras. Era transitions are not hard cutoffs — they are thresholds on accumulated global conditions that trigger new events, regulations, and technology unlocks. Player choices can accelerate or delay transitions.

```cpp
enum class SimulationEra : uint8_t {
    // V1 ERAS — fully specified; Bootstrapper generates all content
    era_1_turn_of_millennium,  // 2000–2007: Globalization peak, pre-crisis, pre-smartphone
    era_2_disruption,          // 2007–2013: Financial crisis, smartphone revolution, social media
    era_3_acceleration,        // 2013–2019: App economy, early EVs, shale revolution, gig economy
    era_4_fracture,            // 2019–2024: COVID shock, deglobalization, EV mainstream, AI emergence
    era_5_transition,          // 2024–2035: Energy transition, AI integration, supply chain rebalancing

    // EX ERAS — engine must handle; content scaffolding is post-V1
    era_6_convergence,         // 2035–2050: AI ubiquity, renewables dominant, genetic medicine
    era_7_reckoning,           // 2050–2075: Climate consequences peak, fusion arrives, longevity medicine
    era_8_synthesis,           // 2075–2100: Space economy, biological manufacturing, post-scarcity goods
    era_9_expansion,           // 2100–2150: Interplanetary economy, radical materials, new governance
    era_10_divergence,         // 2150–2250+: Civilization bifurcation, extreme technology, new paradigms
};
```

**Era transition triggers** (all eras):
- Simulated calendar year (primary threshold)
- `global_climate_stress` accumulation (can accelerate Era 4/5/7 thresholds)
- Global economic conditions (severe financial crises can trigger disruption eras early)
- Cumulative technology unlock density (if players or NPCs rapidly advance a domain, era-level effects can trigger early)
- Player actions (monopolizing or crashing a sector can accelerate structural change)

#### Era Transition Mechanical System [CORE]

Era transitions are evaluated each tick via a weighted scoring system. When accumulated score exceeds the threshold, the era advances.

```cpp
era_transition_check(current_era):
    score = 0.0
    for each trigger in era_triggers[current_era + 1]:
        if trigger.condition_met(world_state):
            score += trigger.weight
    if score >= ERA_TRANSITION_THRESHOLD (default: 0.70):
        advance_era()
        publish_era_change_event()
```

**Era Trigger Table** (Pass 5 Session 8):

| Era Transition | Trigger | Weight | Condition |
|---|---|---|---|
| 1→2 | smartphone_adoption | 0.30 | technology_node('smartphone') maturation > 0.5 |
| 1→2 | financial_crisis | 0.25 | global_debt_ratio > 1.5 × GDP |
| 1→2 | social_media_penetration | 0.20 | social_media_users > 0.15 × population |
| 1→2 | broadband_coverage | 0.25 | broadband_penetration > 0.40 |
| 2→3 | cloud_computing | 0.25 | cloud_adoption > 0.30 |
| 2→3 | renewable_cost_parity | 0.30 | renewable_cost <= fossil_cost × 1.1 |
| 2→3 | ev_early_adoption | 0.20 | ev_market_share > 0.02 |
| 2→3 | mobile_internet | 0.25 | mobile_internet_users > 0.50 × population |
| 3→4 | ev_mainstream | 0.25 | ev_market_share > 0.10 |
| 3→4 | renewable_dominant | 0.30 | renewable_generation > coal_generation |
| 3→4 | semiconductor_shortage | 0.20 | chip_supply_index < 0.7 |
| 3→4 | genai_emergence | 0.25 | genai_models_published > 3 |
| 4→5 | renewables_parity | 0.30 | renewable_generation > fossil_generation |
| 4→5 | ev_penetration | 0.25 | ev_market_share > 0.50 |
| 4→5 | ai_integration | 0.25 | ai_adoption_index > 0.4 |
| 4→5 | smr_deployment | 0.20 | small_modular_reactors_online > 0 |

Additional eras (5→10) follow the same pattern with era-appropriate triggers (fusion milestones, AI economic agency, space economy thresholds, climate tipping points, longevity treatments).

---

### Era Descriptions

#### Era 1 — Turn of the Millennium (2000–2007) [V1]
Peak globalization. The internet is maturing. Feature phones dominate. Oil is cheap. China joins the WTO. Pharmaceutical patents are highly profitable. Climate change is a political talking point, not yet an economic force. The player enters a world of enormous opportunity and low disruption pressure.

**Dominant industries:** Petroleum refining, automotive, consumer electronics, telecommunications, pharmaceuticals.
**Key shock events:** 9/11 security response, early 2000s recession, dot-com hangover, China WTO accession (2001), early shale experimentation.

#### Era 2 — Disruption (2007–2013) [V1]
The global financial crisis fractures the pre-2008 model. Smartphones arrive and destroy incumbent consumer electronics. Social media platforms begin capturing advertising spend. The app economy is born. First EVs appear (niche). Austerity reshapes public sector demand in many regions. Shale revolution transforms energy geopolitics.

**Dominant industries:** Financial services (restructuring), mobile, social media, shale oil, pharmaceutical generics (patent cliffs hit).
**Key shock events:** 2008 financial crisis, TARP, smartphone proliferation, Arab Spring (social media role), shale oil boom, Eurozone crisis.

#### Era 3 — Acceleration (2013–2019) [V1]
The app economy matures. Cloud computing becomes the dominant IT model. Early EVs expand from luxury to premium mass market. Renewable energy costs decline sharply — solar and wind become competitive without subsidy in some regions. Gig economy disrupts labor markets. Machine learning begins producing commercial applications.

**Dominant industries:** Cloud services, EV early supply chain, renewable energy equipment, gig platforms, e-commerce logistics.
**Key shock events:** Paris Agreement (2015), first commercial EV mass market, trade tensions (US/China decoupling beginning), GDPR, early autonomous vehicle development.

#### Era 4 — Fracture (2019–2024) [V1]
COVID delivers a supply chain shock that exposes the fragility of hyperglobalization. Deglobalization accelerates. Semiconductor shortage reveals strategic vulnerability. EVs cross the mainstream threshold. Generative AI emerges as a commercial force. Geopolitical blocks harden. Inflation and rate shocks reshape capital allocation.

**Dominant industries:** Onshore manufacturing, defense supply chains, AI software, EV mass market, pharmaceutical mRNA platforms.
**Key shock events:** COVID-19 pandemic, global chip shortage, Russia-Ukraine war and energy shock, AI breakthrough (LLMs), EV adoption inflection.

#### Era 5 — Transition (2024–2035) [V1]
Energy transition becomes the dominant economic story. Renewables are the default new generation capacity. EV penetration crosses 50% in leading markets. AI is integrated into almost every industry. Supply chains are regionalized. Carbon pricing spreads. First commercial SMRs come online. Space economy begins.

**Dominant industries:** Renewable energy, battery manufacturing, AI infrastructure, onshored semiconductor fabrication, reusable launch.
**Key shock events:** First commercial fusion milestone (private), AGI precursor systems, SMR deployment, Antarctic ice sheet instability confirmed, carbon border adjustments.

#### Era 6 — Convergence (2035–2050) [EX]
AI becomes ubiquitous infrastructure, like electricity. Renewables provide the majority of global electricity. Lab-grown protein disrupts agriculture. Gene therapies become routine. First permanent lunar base. Designer drugs shift from chemistry to synthetic biology. The first generation of significant longevity treatments enters clinical use.

**Dominant industries:** AI services, biological manufacturing, space infrastructure, precision medicine, green hydrogen.
**Key shock events:** First fusion power plant (commercial), lab-grown food reaches price parity, lunar economy formalized, cognitive enhancement market emerges.

#### Era 7 — Reckoning (2050–2075) [EX]
Climate consequences hit maximum economic intensity regardless of mitigation efforts to date (30–50 year lag from emissions). Fusion power begins structural disruption of energy markets. Longevity medicine extends working lives by decades, reshaping labor and pension economics. Asteroid mining delivers first non-Earth metal supply. The first AI systems with genuine autonomous economic agency operate in regulated frameworks.

**Dominant industries:** Climate adaptation infrastructure, fusion energy, longevity medicine, space mining, autonomous AI agents.
**Key shock events:** First Category 6 hurricane season, fusion grid parity confirmed, first multi-decade life extension treatment approved, Mars colony becomes economically self-sustaining.

#### Era 8 — Synthesis (2075–2100) [EX]
Post-scarcity begins to arrive in specific goods — energy abundance from fusion, automated manufacturing, biological food production. But scarcity concentrates in new forms: cognitive property, longevity access inequality, space resource rights. Brain-computer interfaces cross the medical threshold into consumer use. Biological factories produce most bulk chemicals.

**Dominant industries:** Space resources, biological manufacturing, cognitive augmentation, quantum computing applications, longevity services.
**Key shock events:** First space elevator operational (materials-dependent), BCI adoption crosses 5% global population, Mars GDP surpasses smaller Earth nations.

#### Era 9 — Expansion (2100–2150) [EX]
Interplanetary trade creates genuinely new economic geographies. Outer solar system resource extraction begins. The political structures of the early 21st century are straining under the weight of life extension, AI economic agency, and interplanetary sovereignty claims. Molecular-level manufacturing arrives in specialized domains.

**Dominant industries:** Interplanetary logistics, molecular manufacturing, cognitive labor markets (AI and human hybrid), outer planet resource extraction.
**Key shock events:** First interplanetary financial market, asteroid belt property rights treaty (contested), first post-biological human legal case.

#### Era 10 — Divergence (2150–2250+) [EX]
Civilizational bifurcation. Some factions have access to life extension, cognitive augmentation, and space resources. Others do not. New economic paradigms — post-scarcity, post-scarcity conflict, extraction-based colonialism in space — coexist with rump industrial economies on Earth. The simulation's long-horizon consequence space is fully open.

**Dominant industries:** Highly faction-dependent. Energy: fusion and space-based solar near-free. Matter: molecular assembly widely available. Labor: AI dominant in most domains; human labor in niche, premium, or contested applications.
**Key shock events:** First polity with majority post-biological citizens, molecular assembler commercialization, interplanetary war (economic disruption), digital consciousness legal personhood.

---

### What Changes Between Eras

| Condition | Era 1 | Era 2 | Era 3 | Era 4 | Era 5 | Era 6 [EX] | Era 7 [EX] | Era 8 [EX] | Era 9 [EX] | Era 10 [EX] |
|---|---|---|---|---|---|---|---|---|---|---|
| Mobile/compute | Feature phones | Smartphones | App economy | Mobile+AI | AI-native | Ubiquitous AI | Cognitive AI | BCI early | BCI mainstream | Digital-physical merge |
| Energy | Coal/oil dominant | Shale revolution | Renewables emerging | EV tipping point | Renewables dominant | Fusion demo | Fusion scale | Fusion abundance | Post-scarcity energy | Near-free energy |
| EV / transport | ICE only | Early EV niche | EV premium | EV mainstream | EV >50% | Autonomous default | Air mobility | Suborbital routine | Space elevator | Interplanetary |
| Climate regulation | Kyoto (weak) | Carbon markets | Paris targets | Net-zero mandates | Carbon tax | Geoengineering | Mandatory adaptation | Planetary management | Terraforming policy | Stellar-scale |
| Pharma / bio | Small molecules | Biologics | Gene therapy | mRNA | Synthetic bio early | Gene editing routine | Longevity medicine | Life extension decade+ | Century longevity | Post-biological |
| Space | Satellites | Commercial launch | Reusable launch | Lunar economy | Mars colony | Asteroid mining | Mars self-sustaining | Space elevator | Outer planets | Interplanetary trade |
| AI / automation | Early ML | Data economy | Cloud AI | Gen AI | AGI precursors | Autonomous agents | AI economic agents | Post-AGI | AI persons (legal) | AI dominant economy |
| Finance/governance | Pre-crisis light | Crisis response | Fintech | Digital assets | CBDCs | DAO governance | AI-managed economies | Interplanetary finance | Post-national orgs | New paradigms |
| Supply chains | Hyperglobalized | Stressed | Re-established | Fracturing | Regionalized | Localized bio-production | Distributed fusion | In-situ space | Interplanetary | Molecular assembly |

---

### The Living World

Era transitions are not instant. They propagate through the simulation as:

- **New NPC behaviors** — NPC businesses adopt new strategies; some obsolete themselves, new entrants appear
- **Regulatory changes** — new laws governing emissions, financial instruments, data, narcotics, cognitive enhancement, space resources
- **Market structure shifts** — demand for fossil fuels peaks; renewables become competitive; EV sub-assembly markets appear; longevity medicine creates new consumer categories
- **Stranded assets** — businesses built on prior-era assumptions face declining revenue
- **Tech unlock events** — certain goods and recipes become researchable (see Technology Tree)

The world does not wait for the player. If the player does nothing in Era 1, NPC businesses will advance technology, build market share, and establish dominance. The player can enter any industry at any time, but the earlier they move, the more they pay for uncertainty — and the more they gain if they're right.

---

## Part 2 — R&D System Design

### Core Concepts

**R&D converts time and money into technology.** More specifically, it produces one or more of:

- **Technology advancements** — increases tech tier for a facility type, unlocking higher quality ceilings and efficiency bonuses
- **Product unlocks** — makes new goods and recipes available that were previously inaccessible
- **Process improvements** — reduces energy consumption, waste output, or labor requirement for a specific recipe
- **Patents** — creates an exclusive commercial right over a technology advancement for a defined period
- **Publications** — non-patentable results that advance the state of knowledge (academia) and eventually flow into the public technology pool
- **Failures** — partial results or dead ends; not wasted (see "Productive Failure" in Part 4)

**R&D is probabilistic, not deterministic.** Every research project has a probability distribution over outcomes. The same project, run twice, may succeed spectacularly once and produce a partial result the other time.

**R&D has externalities.** Published research raises the global knowledge level in a domain, eventually unlocking advancements for all players and NPCs. Patents grant temporary exclusivity that slows diffusion. Stealing research (corporate espionage) is faster than doing it — but carries severe consequences if detected.

---

### R&D Facility Types

```cpp
enum class RnDFacilityType : uint8_t {
    corporate_lab,         // Funded by a single company; output stays proprietary
                           // Fast, focused, expensive
    independent_institute, // Contracted research; output can be sold to multiple buyers
                           // Slower, cheaper, results go to highest bidder
    university_department, // Academic research; publications, talent pipeline
                           // Very slow, very cheap, results eventually go public
    criminal_lab,          // Illicit R&D; designer drugs, synthesis improvements
                           // No patents; races the scheduling clock; OPSEC applies
    skunkworks,            // Hidden from official records; classified as another business type
                           // Combines corporate_lab secrecy with independent flexibility
    orbital_lab,           // [EX] Space-based; microgravity research; outside most jurisdictions
                           // Unlocked when space infrastructure exists; special OPSEC profile
    biological_foundry,    // [EX] Programmable biological production; synthetic biology domain
                           // Dual-use: legitimate pharma or criminal biotech
};
```

**Facility quality affects:**
- `base_research_rate` — progress points per tick per researcher
- `success_probability_modifier` — better facilities raise the odds of success vs. partial result
- `discovery_probability_modifier` — better facilities are more likely to produce unexpected breakthroughs
- `patent_quality` — better-resourced labs produce stronger, harder-to-design-around patents

---

### Research Domains

Research is organized into **domains** that map to the goods and facility categories in the commodities document. Each domain has a current global knowledge level (0.0–1.0) that rises over simulated time from published research. Projects within a domain benefit from the accumulated domain knowledge.

```cpp
enum class ResearchDomain : uint8_t {
    // V1 DOMAINS — fully active in base game
    materials_science,         // Steel alloys, composites, advanced materials
    semiconductor_physics,     // Chip architecture, process nodes, quantum effects
    chemical_synthesis,        // Industrial chemistry, drug synthesis, polymer science
    energy_systems,            // Combustion, electrical, battery, renewable
    mechanical_engineering,    // Engines, precision manufacturing, robotics
    software_systems,          // Operating systems, applications, protocols
    biotechnology,             // Pharmaceutical, agricultural genetics, food science
    climate_and_environment,   // Carbon capture, renewable integration, environmental
    information_security,      // Encryption, network security, exploit development
    social_media_platforms,    // Network effects, engagement, content algorithms
    financial_instruments,     // Derivatives, risk models, digital assets
    illicit_chemistry,         // Criminal-specific domain; not in academic knowledge pool

    // EX DOMAINS — engine must register; content is post-V1
    nuclear_engineering,       // [EX] Fission advances, SMRs, fusion research, nuclear medicine
    advanced_transportation,   // [EX] Hypersonic, suborbital, space access, novel propulsion
    space_systems,             // [EX] Launch, orbital manufacturing, asteroid mining, planetary ops
    cognitive_science,         // [EX] Neurotechnology, BCI, consciousness research, enhancement
    synthetic_biology,         // [EX] Organism design, biological factories, gene drives, ecology
    geoengineering,            // [EX] DAC, ocean alkalinity, stratospheric management, terraforming
    quantum_systems,           // [EX] Quantum computing, QKD, quantum sensing, quantum internet
};
```

**Domain knowledge accumulation:**
```
domain_knowledge_level += publications_this_tick × PUBLICATION_KNOWLEDGE_INCREMENT
domain_knowledge_level += technology_transfer_events_this_tick × TRANSFER_INCREMENT
domain_knowledge_level decays at KNOWLEDGE_DECAY_RATE per tick (obsolescence)
```

A project in a high-knowledge domain has a higher base success probability. Early in the game, most domains are at low-to-medium levels (matching real-world 2000 baselines).

**Cross-domain prerequisites:** Some technology nodes require knowledge levels in multiple domains. Example: `electric_vehicle` requires both `energy_systems` (battery) and `mechanical_engineering` (powertrain). This is specified per node in the tech tree (Part 3) and in the node registry (Part 12).

---

## Part 3 — Technology Tree

### Design Philosophy

The technology tree is not a static branching diagram. It is a set of **technology nodes** that become *researchable* when:
1. The era conditions are met (calendar year + global conditions)
2. Prerequisite nodes are unlocked
3. A facility with sufficient domain expertise exists
4. A research project targeting that node is funded and succeeds

**Unlocks are per-actor, not global.** When a node is researched, the researching actor gains a `TechHolding` at Stage 1 (researched). Other actors do not automatically gain access. Diffusion occurs through the mechanisms in Part 6. The tech tree defines what *can* be researched; the Technology Lifecycle Model (Part 3.5) defines who has it, at what quality, and whether it is on the market.

Technology nodes are defined in data files (moddable). Each node specifies:
- **Era available** (earliest possible — can't research smartphones in 2000)
- **Prerequisites** (what must already be unlocked by the same actor)
- **Domain** (which research domain the project falls in)
- **Difficulty** (effort points required; see scale note below)
- **Outcome type** (product unlock, tech tier advance, process improvement, etc.)
- **Patentable** (yes/no)
- **Key technology node** (for `FacilityRecipe`: which holding governs this recipe's quality ceiling via maturation)

**Difficulty scale:** `0.1` = days of effort at a well-staffed lab; `1.0` = months; `10.0` = years; `100.0` = decades of global R&D effort. Most commercial product unlocks fall in `0.5`–`5.0`. Foundational domain breakthroughs (fusion ignition, AGI) fall in `20.0`–`100.0`.

---

### Era 1 Starting State (Year 2000) [V1]

What exists at game start — no research required:

**Available Tech Tiers by Category:**
- Basic extraction (mining, farming, logging): Tier 2 (mature)
- Steel and metals processing: Tier 2 (mature industry)
- Petroleum refining: Tier 2 (mature)
- Chemical processing: Tier 2 (mature)
- Basic manufacturing (vehicles, appliances): Tier 2
- Advanced manufacturing (precision machinery): Tier 3 (frontier)
- Semiconductor fabrication: Tier 3 (frontier, expensive; ~130nm process node)
- Consumer electronics: Tier 2–3
- Nuclear fission power: Tier 2 (Gen II/III reactors, mature)

**Available Products at Year 2000:**
- All geological and biological raw resources ✓
- All Tier 1 processed materials ✓
- Most mechanical sub-assemblies ✓
- Internal combustion engine vehicles ✓
- CRT televisions, early flat-panel displays (LCD Tier 3) ✓
- Desktop and laptop computers (Tier 3) ✓
- Feature phones (not smartphones) ✓
- Basic pharmaceuticals ✓
- Cannabis, cocaine, heroin, methamphetamine (criminal goods) ✓
- Satellite communications equipment ✓
- Gen II nuclear reactors (operational in sim; construction requires capital, not R&D) ✓

**NOT available at Year 2000 (must be researched or unlocked by era):**
- Smartphones and app platforms
- Lithium-ion batteries at EV scale
- Electric vehicles
- OLED displays
- Competitive solar panels
- Large-scale wind generation
- mRNA pharmaceuticals
- Social media platforms as goods
- AI hardware accelerators
- Shale oil extraction (fracking improvements — near-term unlock, Era 1)
- Designer drugs (era 1 analogue frameworks allow research)
- Advanced synthetic opioids
- Small modular reactors
- Fusion power (Era 6+ EX)
- Gene editing therapies
- Reusable orbital launch

---

### Technology Tree — Complete Domain Chains

Each chain shows the full progression from Year 2000 to Era 10. V1 nodes are untagged; EX nodes are marked `[EX]`. Scope-limited nodes carry their scope marker.

---

#### Domain: energy_systems

**Chain: Conventional Energy — Mature to Stranded**
```
[Era 1] Hydraulic fracturing improvements  (difficulty: 1.0, patentable: yes)
    → product unlock: shale_oil_extraction, shale_gas_extraction
    → WorldGen note: shale resource deposits are era_available = 2 in WorldGen §8.7.
      The tech node is researchable Era 1 (modelling real pre-2000 fracking work),
      but the resource deposits remain invisible until Era 2 regardless of tech status.
      Tech and resource gate are decoupled: research early = first-mover on Era 2 production start.
    → process improvement: petroleum_extraction_cost –25%
        → [Era 1] Directional drilling optimization  (difficulty: 0.8)
            → process improvement: shale_yield +15%
        → [Era 2] Heavy oil processing  (difficulty: 1.2)
            → process improvement: oil_sands_extraction unlocked; heavy_crude_refining_cost –20%
            → WorldGen note: oil_sands resource era_available = 2; aligns with this tech
        → [Era 3] Enhanced oil recovery — CO₂ injection  (difficulty: 1.5)
            → dual outcome: yield +20%, generates CO₂ sequestration byproduct credit
        → [Era 3] Arctic offshore drilling  (difficulty: 2.0)
            → facility unlock: arctic_offshore_platform
            → WorldGen note: Arctic offshore oil era_available = 3; climate-gated (sea ice recession)
              Tech alone is insufficient — resource deposit also requires regional_climate_stress threshold
```
*Strategic note: Shale investment maximizes cash flow through Era 1–3. Carbon costs begin stranding shale in Era 4–5.*

**Chain: Lithium-Ion Battery**
```
[Era 1] Li-ion cell chemistry optimization  (difficulty: 1.5, patentable: yes)
    → product unlock: battery_cell_liion (higher energy density)
        → [Era 2] Battery management system  (difficulty: 1.0)
            → product unlock: battery_management_system
                → [Era 2] EV powertrain integration  (difficulty: 2.0)
                    → product unlock: ev_drivetrain
                        → [Era 2] Electric vehicle  (difficulty: 3.0)
                            → product unlock: electric_vehicle
                                → [Era 3] Fast charging infrastructure  (difficulty: 1.5)
                                    → product unlock: ev_charging_station
                                        → [Era 3] Grid-scale battery storage  (difficulty: 4.0)
                                            → product unlock: grid_battery_array
                                                → [Era 4] Vehicle-to-grid integration  (difficulty: 1.0)
                                                    → process improvement: grid_stability +X

[Era 3] Solid-state battery research  (difficulty: 3.0, patentable: yes)
    → [Era 4] Solid-state battery cell  (difficulty: 5.0)
        → process improvement: energy_density +40%, charge_time –50%
        → product unlock: battery_cell_solid_state
            → [Era 5] Structural battery materials  (difficulty: 4.0) [EX eligible]
                → product unlock: structural_battery_composite
                    (vehicle chassis doubles as energy storage; enables lighter EVs)
```

**Chain: Solar Power**
```
[Era 1] Photovoltaic cell efficiency research  (difficulty: 1.0)
    → [Era 2] Cost-competitive solar panel  (difficulty: 2.0, patentable: yes)
        → process improvement: solar_panel_unit_cost –40%
            → [Era 2] Solar farm  (difficulty: 1.5)
                → facility unlock: solar_farm
                    → [Era 3] Grid-scale solar  (difficulty: 2.0)
                        → process improvement: solar_grid_lcoe –30%
                            → [Era 4] Perovskite solar cells  (difficulty: 3.0)
                                → process improvement: conversion_efficiency +8pp
                                    → [Era 5] Tandem solar cells  (difficulty: 4.0)
                                        → conversion_efficiency +12pp; cost parity achieved
                                            → [Era 6] Solar-battery integrated storage  (difficulty: 2.0) [EX]
                                                → product unlock: solar_storage_hybrid_unit
                                                    → [Era 8] Space-based solar power collection  (difficulty: 30.0) [EX]
                                                        → requires: space_systems.orbital_construction
                                                        → product unlock: spacebased_solar_transmitter
                                                            (effectively infinite energy density; strands all ground generation)
```

**Chain: Wind Power**
```
[Era 1] Wind turbine blade efficiency research  (difficulty: 1.0)
    → [Era 2] Offshore wind platform  (difficulty: 2.5, patentable: yes)
        → facility unlock: offshore_wind_farm
            → [Era 3] Grid-scale wind  (difficulty: 2.0)
                → [Era 4] Floating offshore wind  (difficulty: 4.0) [EX eligible]
                    → enables deep-water installation; opens new geographies
                        → [Era 6] Airborne wind energy systems  (difficulty: 5.0) [EX]
                            → product unlock: airborne_wind_generator
```

**Chain: Hydrogen Economy**
```
[Era 3] Green hydrogen electrolysis  (difficulty: 2.0, patentable: yes)
    → product unlock: green_hydrogen
        → [Era 4] Hydrogen fuel cell — industrial  (difficulty: 3.0)
            → product unlock: industrial_fuel_cell
                → [Era 5] Hydrogen fuel cell vehicle  (difficulty: 3.5) [EX eligible]
                    → product unlock: hydrogen_vehicle
                        → [Era 6] Hydrogen pipeline infrastructure  (difficulty: 4.0) [EX]
                            → facility unlock: hydrogen_distribution_network
                                → [Era 7] Hydrogen-ammonia fuel synthesis  (difficulty: 3.0) [EX]
                                    → product unlock: green_ammonia
                                        (disrupts fossil fertilizer completely)
```

**Chain: Nuclear — Fission to Fusion**
```
[Era 1] Gen III+ reactor design (available; no R&D needed for construction)
    → [Era 2] Small modular reactor design  (difficulty: 5.0, patentable: yes)
        → facility unlock: small_modular_reactor
            → [Era 3] SMR commercial deployment  (difficulty: 3.0)
                → process improvement: nuclear_capex –40% vs. large plants
                    → [Era 4] Gen IV fast reactor  (difficulty: 8.0, patentable: yes) [EX eligible]
                        → facility unlock: gen4_fast_reactor
                        → product unlock: reactor_byproduct_plutonium (regulated)
                            → [Era 5] Thorium fuel cycle  (difficulty: 6.0) [EX]
                                → process improvement: nuclear_waste_volume –90%

[Era 4] Fusion ignition research  (difficulty: 25.0)  [EX]
    → requires: nuclear_engineering domain knowledge ≥ 0.6
    → [Era 5] First private fusion demonstration  (difficulty: 15.0) [EX]
        → product unlock: fusion_demonstration_plant
            → [Era 6] Fusion pilot power plant  (difficulty: 20.0) [EX]
                → facility unlock: fusion_pilot_plant
                    → [Era 7] Commercial fusion grid  (difficulty: 30.0) [EX]
                        → process improvement: electricity_generation_cost –70%
                        → stranded_asset trigger: all_fossil_fuel_generation
                            → [Era 8] Fusion abundance  (difficulty: 10.0) [EX]
                                → energy_cost approaches near-zero for simulation purposes
                                → new economic bottleneck: materials and cognitive labor
```

**Chain: Carbon Capture**
```
[Era 3] Carbon capture and storage (CCS) — industrial  (difficulty: 2.0, patentable: yes)
    → process improvement: point_source_emissions –60% for heavy industry
        → [Era 4] Direct air capture — prototype  (difficulty: 4.0) [EX eligible]
            → product unlock: direct_air_capture_unit
                → [Era 5] DAC at scale  (difficulty: 6.0) [EX]
                    → process improvement: dac_unit_cost –50%
                        → [Era 6] Enhanced weathering  (difficulty: 3.0) [EX]
                            → agricultural byproduct: soil_alkalization_agent
                                → [Era 7] Ocean alkalinity enhancement  (difficulty: 5.0) [EX]
                                    → requires: geoengineering domain
                                    → modifies: global_co2_index removal rate
```

---

#### Domain: semiconductor_physics

**Chain: Process Node Scaling**
```
[Era 1] 130nm process node (available at game start; Tier 3)
    → [Era 1] 90nm process node  (difficulty: 3.0, ~2003)
        → [Era 2] 65nm process node  (difficulty: 4.0, ~2006)
            → [Era 2] 45nm process node  (difficulty: 5.0, ~2008)
                → [Era 3] 22nm process node  (difficulty: 6.0, ~2012)
                    → [Era 4] 7nm process node  (difficulty: 8.0, ~2018)
                        → [Era 4] 5nm process node  (difficulty: 9.0, ~2020)
                            → [Era 5] 3nm / angstrom era  (difficulty: 10.0, ~2022)
                                → [Era 5] Gate-all-around transistor  (difficulty: 8.0)
                                    → [Era 5] 2nm process  (difficulty: 12.0)
```
*Each process node: reduces energy consumption per transistor, increases output quality ceiling, reduces per-unit cost. Massive capex per generation makes semiconductors a natural monopoly business.*

**Chain: Beyond Silicon**
```
[Era 3] GaN power electronics research  (difficulty: 2.0, patentable: yes)
    → product unlock: gan_power_chip
    → process improvement: power_electronics_efficiency +15%
        → [Era 4] SiC power electronics  (difficulty: 2.5)
            → enables high-voltage EV inverters; EV efficiency +8%
                → [Era 5] Wide-bandgap semiconductor platform  (difficulty: 4.0)
                    → process improvement: power_chip_switching_loss –30%
```

**Chain: Advanced Chip Architectures**
```
[Era 3] 3D chip stacking (TSV technology)  (difficulty: 3.0, patentable: yes)
    → process improvement: chip_compute_density +40%
        → [Era 4] Monolithic 3D integration  (difficulty: 5.0)
            → [Era 5] Photonic computing interconnects  (difficulty: 6.0) [EX eligible]
                → process improvement: chip_bandwidth +300%
                    → [Era 6] Photonic computing processor  (difficulty: 8.0) [EX]
                        → product unlock: photonic_processor
                            → [Era 6] Neuromorphic chip architecture  (difficulty: 7.0) [EX]
                                → product unlock: neuromorphic_chip
                                → process improvement: ai_inference_energy –95%
                                    → [Era 7] Neuromorphic general-purpose computing  (difficulty: 10.0) [EX]
```

**Chain: Quantum Computing** (cross-domain with quantum_systems)
```
[Era 3] Quantum bit (qubit) demonstration  (difficulty: 5.0) [EX eligible]
    → [Era 4] NISQ quantum processor — 50–100 qubits  (difficulty: 8.0) [EX]
        → product unlock: nisq_quantum_processor
        → limited utility; primarily research tool
            → [Era 5] Quantum error correction  (difficulty: 15.0) [EX]
                → [Era 6] Fault-tolerant quantum computer  (difficulty: 20.0) [EX]
                    → product unlock: fault_tolerant_quantum_computer
                    → unlocks: quantum_chemistry_simulation (enables new drug discovery)
                        → [Era 7] Quantum supremacy for commercial optimization  (difficulty: 15.0) [EX]
                            → process improvement: logistics_optimization –30%
                            → process improvement: financial_modeling_speed ×1000
                                → [Era 8] Quantum internet node  (difficulty: 12.0) [EX]
                                    → product unlock: quantum_network_node
                                    → enables unhackable communication
```

**Chain: Molecular Computing** [EX]
```
[Era 8] Molecular electronics research  (difficulty: 20.0) [EX]
    → requires: materials_science.atomically_precise_fabrication
    → [Era 9] Molecular logic gate  (difficulty: 25.0) [EX]
        → [Era 9] DNA computing substrate  (difficulty: 20.0) [EX]
            → product unlock: dna_computing_array
                → [Era 10] Molecular-scale processor  (difficulty: 30.0) [EX]
```

---

#### Domain: materials_science

**Chain: Advanced Composites**
```
[Era 1] Carbon fiber manufacturing process optimization  (difficulty: 1.0, patentable: yes)
    → process improvement: carbon_fiber_cost –20%
        → [Era 2] Aerospace-grade carbon fiber composite  (difficulty: 2.0)
            → product unlock: carbon_fiber_composite
                → [Era 3] Automotive carbon fiber integration  (difficulty: 1.5)
                    → process improvement: vehicle_weight –15%, fuel_efficiency +10%
                        → [Era 4] Carbon fiber mass production  (difficulty: 3.0)
                            → product unlock: carbon_fiber_panel (consumer vehicle grade)
```

**Chain: Advanced Alloys and Coatings**
```
[Era 1] High-strength low-alloy (HSLA) steel optimization  (difficulty: 0.8)
    → process improvement: structural_steel_yield_strength +20%
        → [Era 2] Advanced high-strength steel (AHSS)  (difficulty: 1.5)
            → automotive weight reduction; EV range benefit
                → [Era 3] High-entropy alloys research  (difficulty: 3.0, patentable: yes)
                    → product unlock: high_entropy_alloy
                    → extreme temperature and corrosion resistance
                        → [Era 5] Refractory high-entropy alloys  (difficulty: 4.0) [EX eligible]
                            → enables hypersonic vehicle components
                            → enables fusion reactor first-wall materials
```

**Chain: Polymers and Nanomaterials**
```
[Era 1] Advanced polymer science  (difficulty: 1.0)
    → [Era 2] High-performance thermoplastics  (difficulty: 1.5)
        → product unlock: engineering_thermoplastic
            → [Era 3] Graphene research  (difficulty: 3.0, patentable: yes)
                → [Era 4] Graphene composite material  (difficulty: 4.0)
                    → product unlock: graphene_composite
                    → conductivity and strength far exceed steel at fractions of weight
                        → [Era 5] Carbon nanotube fiber  (difficulty: 6.0) [EX eligible]
                            → product unlock: cnt_fiber_material
                                → [Era 7] CNT macrofiber — structural cable  (difficulty: 10.0) [EX]
                                    → required prerequisite for: space_systems.space_elevator
                                    → product unlock: space_elevator_cable_material
                                        → [Era 8] Space elevator construction  (difficulty: 40.0) [EX]
                                            → requires: cnt_fiber_material + orbital_construction
                                            → facility unlock: space_elevator

[Era 3] Aerogel manufacturing optimization  (difficulty: 2.0, patentable: yes)
    → product unlock: aerogel_insulation
    → process improvement: building_insulation_energy –25%
        → [Era 4] Aerogel structural applications  (difficulty: 3.0)
            → product unlock: structural_aerogel_panel
```

**Chain: Metamaterials and Smart Materials**
```
[Era 3] Metamaterial electromagnetic research  (difficulty: 3.0) [EX eligible]
    → [Era 4] Acoustic metamaterial panel  (difficulty: 2.0)
        → product unlock: acoustic_metamaterial (industrial noise reduction)
            → [Era 5] Optical metamaterial  (difficulty: 5.0) [EX]
                → product unlock: metamaterial_optical_component
                    → [Era 6] Programmable metamaterial surface  (difficulty: 6.0) [EX]
                        → product unlock: programmable_surface_panel
                        → reconfigurable antenna, structural, optical properties

[Era 4] Self-healing polymer research  (difficulty: 3.0, patentable: yes) [EX eligible]
    → [Era 5] Self-healing coating  (difficulty: 2.5) [EX]
        → product unlock: self_healing_paint_coating
            → [Era 6] Structural self-healing composite  (difficulty: 5.0) [EX]
                → process improvement: maintenance_cost –30% for composite structures
                    → [Era 7] Autonomous repair material  (difficulty: 8.0) [EX]
```

**Chain: Atomically Precise Manufacturing** [EX]
```
[Era 6] Scanning probe lithography at molecular scale  (difficulty: 10.0) [EX]
    → research tool only; no production unlock
        → [Era 7] Atomically precise small-molecule synthesis  (difficulty: 15.0) [EX]
            → product unlock: atomically_precise_molecule
                → [Era 8] Atomically precise fabrication (nano-scale)  (difficulty: 25.0) [EX]
                    → enables: molecular_electronics, synthetic_biology.programmable_proteins
                        → [Era 9] Molecular assembler prototype  (difficulty: 40.0) [EX]
                            → product unlock: molecular_assembler
                            → MAJOR economic disruption: manufacturing cost structure collapses
                                → [Era 10] Generalized molecular assembly  (difficulty: 50.0) [EX]
                                    → process improvement: any_manufactured_good_cost –80%
                                    → stranded_asset trigger: all_conventional_manufacturing
```

---

#### Domain: mechanical_engineering

**Chain: Manufacturing Automation**
```
[Era 1] CNC machining optimization  (difficulty: 0.5)
    → process improvement: precision_manufacturing_cost –10%
        → [Era 1] Industrial robot programming advances  (difficulty: 1.0)
            → process improvement: assembly_line_labor_requirement –15%
                → [Era 2] Additive manufacturing — polymer  (difficulty: 1.5, patentable: yes)
                    → product unlock: fdm_3d_printer
                        → [Era 3] Additive manufacturing — metal  (difficulty: 2.5)
                            → product unlock: metal_3d_printer
                            → enables low-volume custom metal parts
                                → [Era 4] Industrial additive manufacturing  (difficulty: 3.0)
                                    → process improvement: custom_part_lead_time –80%
                                        → [Era 5] Mass-customization manufacturing  (difficulty: 3.0) [EX eligible]
                                            → product unlock: mass_customization_factory_module
```

**Chain: Robotics**
```
[Era 2] Collaborative robot (cobot) platform  (difficulty: 2.0, patentable: yes)
    → product unlock: cobot_unit
        → [Era 3] Autonomous mobile robot (warehouse/logistics)  (difficulty: 2.5)
            → product unlock: amr_unit
                → [Era 4] General-purpose humanoid robot — prototype  (difficulty: 8.0) [EX eligible]
                    → requires: software_systems.computer_vision + mechanical_engineering
                        → [Era 5] Humanoid robot — commercial  (difficulty: 6.0) [EX]
                            → product unlock: humanoid_robot_unit
                            → labor market disruption: displaces 20–40% of physical labor NPCs
                                → [Era 6] Adaptive manufacturing robot  (difficulty: 5.0) [EX]
                                    → process improvement: retooling_time –70%
                                        → [Era 7] Self-replicating manufacturing system  (difficulty: 20.0) [EX]
                                            → facility can partially construct copies of itself
                                            → exponential capital formation capability
```

**Chain: Vehicles and Transport**
(See also: advanced_transportation domain for hypersonic+)
```
[Era 1] Hybrid powertrain  (difficulty: 1.0, patentable: yes)
    → product unlock: hybrid_vehicle
        → [Era 3] Autonomous vehicle sensor suite  (difficulty: 3.0)
            → product unlock: lidar_sensor_array, av_compute_unit
                → [Era 4] Autonomous vehicle — L4  (difficulty: 5.0)
                    → product unlock: autonomous_vehicle
                        → [Era 5] Autonomous logistics fleet  (difficulty: 3.0)
                            → process improvement: logistics_cost –30%
                            → NPC truck driver employment collapses

[Era 4] Electric vertical takeoff and landing (eVTOL)  (difficulty: 4.0) [EX eligible]
    → product unlock: evtol_aircraft
        → [Era 5] Urban air mobility service  (difficulty: 3.0) [EX]
            → product unlock: uam_service_route
```

**Chain: Precision Manufacturing**
```
[Era 1] Six-sigma manufacturing processes  (difficulty: 0.5)
    → process improvement: defect_rate –20%
        → [Era 2] Statistical process control — digital  (difficulty: 1.0)
            → [Era 3] AI-assisted quality control  (difficulty: 2.0)
                → process improvement: quality_inspection_labor –60%
                    → [Era 5] Zero-defect adaptive manufacturing  (difficulty: 3.0) [EX eligible]
                        → process improvement: defect_rate approaches zero
```

**Chain: Deep Sea Resource Extraction** [EX]
```
[Era 3] Deep sea mining platform  (difficulty: 2.5, patentable: yes) [EX]
    → facility unlock: deep_sea_mining_platform
    → WorldGen note: deep sea mineral deposits seeded at Stage 8, era_available = 3;
      this tech is the required unlock condition (WorldGen §8.7)
    → product unlock: polymetallic_nodule_ore (manganese, nickel, cobalt)
        → [Era 4] Deep sea mineral processing  (difficulty: 2.0) [EX]
            → process improvement: deep_sea_ore_yield +30%
                → [Era 5] Autonomous underwater vehicle — industrial  (difficulty: 3.0) [EX]
                    → process improvement: deep_sea_extraction_cost –40%
                    → product unlock: auv_mining_unit
```

---

#### Domain: software_systems

**Chain: Platform and Infrastructure**
```
[Era 1] Broadband internet infrastructure  (available at start, advancing)
    → [Era 1] Content delivery network  (difficulty: 1.0, patentable: yes)
        → product unlock: cdn_infrastructure
            → [Era 2] Cloud computing platform  (difficulty: 3.0)
                → product unlock: cloud_compute_service
                    → [Era 3] Microservices architecture  (difficulty: 1.5)
                        → process improvement: software_development_velocity +30%
                            → [Era 3] Serverless computing platform  (difficulty: 2.0)
                                → [Era 4] Edge computing network  (difficulty: 3.0)
                                    → product unlock: edge_compute_node
                                        → [Era 4] 5G network equipment  (difficulty: 3.5)
                                            → product unlock: 5g_base_station
                                                → [Era 6] 6G/integrated sensing+comm  (difficulty: 5.0) [EX]
```

**Chain: Artificial Intelligence**
```
[Era 2] Machine learning research  (difficulty: 2.0)
    → [Era 3] Deep learning framework  (difficulty: 3.0, patentable: yes)
        → product unlock: ml_inference_service
            → [Era 3] Computer vision system  (difficulty: 2.5)
                → process improvement: quality_control_automation; manufacturing labor –30%
                    → [Era 3] Natural language processing  (difficulty: 3.0)
                        → product unlock: nlp_service
                            → [Era 4] Large language model  (difficulty: 8.0)
                                → product unlock: llm_api_service
                                    → [Era 4] AI hardware accelerator (GPU cluster)  (difficulty: 5.0)
                                        → product unlock: gpu_compute_cluster
                                            → [Era 5] Autonomous AI agent platform  (difficulty: 8.0) [EX eligible]
                                                → product unlock: autonomous_ai_agent_service
                                                → labor market disruption: knowledge worker demand –40%
                                                    → [Era 6] AGI precursor system  (difficulty: 30.0) [EX]
                                                        → requires: semiconductor_physics.neuromorphic_chip
                                                        → MAJOR economic event: knowledge labor market collapses
                                                            → [Era 7] Economically autonomous AI  (difficulty: 20.0) [EX]
                                                                → AI systems can hold contracts, own IP
                                                                → new economic actor class in simulation
                                                                    → [Era 8] Post-AGI industrial AI  (difficulty: 15.0) [EX]
                                                                        → process improvement: ALL manufacturing and logistics –50%
```

**Chain: Security and Privacy**
```
[Era 1] Firewall and intrusion detection  (difficulty: 0.5)
    → [Era 2] Advanced persistent threat (APT) detection  (difficulty: 1.5)
        → [Era 3] AI-assisted threat detection  (difficulty: 2.0)
            → [Era 4] Quantum-resistant cryptography  (difficulty: 4.0, patentable: yes)
                → product unlock: post_quantum_crypto_library
                → defensive: protects against Era 5+ quantum decryption
                    → [Era 5] Quantum key distribution network  (difficulty: 6.0) [EX]
                        → product unlock: qkd_network_node
                            → [Era 7] Quantum internet security fabric  (difficulty: 10.0) [EX]
```

---

#### Domain: social_media_platforms

**Chain: Platform Evolution**
```
[Era 1] Early internet forums and communities  (available at start)
    → [Era 2] Social network platform  (difficulty: 2.0, patentable: no)
        → product unlock: social_media_platform
        → NPC behavior change: time_on_platform displaces traditional media
            → [Era 2] Content algorithm v1  (difficulty: 1.5)
                → process improvement: user_retention +X%
                    → [Era 3] Targeted advertising inventory  (difficulty: 2.0, patentable: yes)
                        → product unlock: targeted_ad_inventory
                            → [Era 3] Influencer economy platform  (difficulty: 1.5)
                                → product unlock: creator_monetization_layer
                                    → [Era 4] AI-generated content at scale  (difficulty: 3.0) [EX eligible]
                                        → product unlock: synthetic_content_feed
                                        → regulatory pressure: deepfake laws
                                            → [Era 5] Immersive social platform (AR/VR)  (difficulty: 4.0) [EX]
                                                → product unlock: immersive_social_platform
                                                    → [Era 6] Persistent digital-physical social layer  (difficulty: 6.0) [EX]
                                                        → [Era 7] Direct neural social interface  (difficulty: 10.0) [EX]
                                                            → requires: cognitive_science.bidirectional_bci
```

---

#### Domain: biotechnology

**Chain: Pharmaceutical Development**
```
[Era 1] Combinatorial drug screening  (difficulty: 1.0)
    → [Era 1] Targeted small molecule drugs  (difficulty: 2.0, patentable: yes)
        → product unlock: pharmaceutical_rx_targeted
            → [Era 2] Biologic drugs (protein-based)  (difficulty: 3.0)
                → product unlock: biologic_drug
                    → [Era 3] Gene therapy — ex vivo  (difficulty: 5.0, patentable: yes)
                        → product unlock: gene_therapy_treatment
                            → [Era 4] mRNA therapeutics  (difficulty: 6.0, patentable: yes)
                                → product unlock: mrna_pharmaceutical
                                → mRNA vaccine platform: rapid response to novel pathogens
                                    → [Era 5] In vivo gene editing (CRISPR 2.0)  (difficulty: 7.0) [EX eligible]
                                        → product unlock: in_vivo_gene_editing_therapy
                                            → [Era 6] Whole-genome therapeutic replacement  (difficulty: 12.0) [EX]
                                                → product unlock: genome_therapeutic
                                                    → [Era 6] Polygenic disease correction  (difficulty: 8.0) [EX]
```

**Chain: Life Extension Medicine** [EX]
```
[Era 5] Senolytic therapy research  (difficulty: 5.0) [EX]
    → product unlock: senolytic_drug
    → effect: NPC lifespan extension +5 years in simulation
        → [Era 6] Telomere extension therapy  (difficulty: 8.0) [EX]
            → product unlock: telomere_therapy
            → NPC lifespan +10-15 years
                → [Era 7] Comprehensive longevity treatment  (difficulty: 15.0) [EX]
                    → product unlock: longevity_package
                    → NPC lifespan +30 years
                    → MAJOR economic event: retirement age shifts, pension system stress
                        → [Era 8] Radical life extension  (difficulty: 25.0) [EX]
                            → product unlock: radical_longevity_treatment
                            → NPC lifespan effectively open-ended for wealthy NPCs
                            → longevity inequality becomes major political driver
                                → [Era 9] Post-biological substrate  (difficulty: 50.0) [EX]
                                    → requires: cognitive_science.mind_upload
```

**Chain: Agricultural Biotechnology**
```
[Era 1] Genetically modified crops (existing; regulatory context varies)
    → [Era 2] Drought-resistant crop variants  (difficulty: 1.5, patentable: yes)
        → process improvement: agricultural_yield_climate_resilience +15%
            → [Era 3] Precision fermentation — proteins  (difficulty: 2.0)
                → product unlock: fermentation_protein
                    → [Era 4] Lab-grown meat — prototype  (difficulty: 4.0) [EX eligible]
                        → product unlock: cultivated_meat (expensive)
                            → [Era 5] Lab-grown meat — cost parity  (difficulty: 5.0) [EX]
                                → process improvement: cultivated_meat_cost –70%
                                → MAJOR disruption: livestock farming demand collapses in some regions
                                    → [Era 6] Synthetic ecosystem food production  (difficulty: 8.0) [EX]
                                        → product unlock: synthetic_agricultural_system
                                        → climate-proof food supply; decoupled from land/water
```

---

#### Domain: information_security

**Chain: Offensive and Defensive Cyber**
```
[Era 1] Vulnerability research platform  (difficulty: 1.0, patentable: no)
    → product unlock: zero_day_exploit (criminal/intelligence good)
        → [Era 2] Advanced persistent threat toolkit  (difficulty: 2.0)
            → product unlock: apt_toolkit (criminal/state actor good)
                → [Era 3] AI-assisted vulnerability discovery  (difficulty: 3.0, patentable: yes)
                    → product unlock: ai_vuln_scanner
                        → [Era 4] Autonomous offensive cyber system  (difficulty: 5.0) [EX eligible]
                            → product unlock: autonomous_cyberweapon (heavily regulated)
                                → [Era 5] Infrastructure cyber-physical attack capability  (difficulty: 8.0) [EX]
                                    → can target power grids, water systems, manufacturing
                                        → [Era 6] AI-native cyberwar platform  (difficulty: 10.0) [EX]
                                            → economic warfare between polities

[Era 3] Defensive deception technology (honeypots, active defense)  (difficulty: 2.0)
    → [Era 5] AI-managed cyber defense  (difficulty: 4.0) [EX eligible]
        → process improvement: intrusion_detection_rate +40%
```

---

#### Domain: financial_instruments

**Chain: Complex Financial Products**
```
[Era 1] Structured financial products (CDOs, MBS)  (difficulty: 1.0, patentable: no)
    → product unlock: structured_credit_instrument
    → risk: systemic exposure accumulates
        → [Era 2] Algorithmic trading system  (difficulty: 2.0, patentable: yes)
            → product unlock: algorithmic_trading_engine
                → [Era 2] High-frequency trading platform  (difficulty: 2.5)
                    → product unlock: hft_colocation_service
                        → [Era 3] Predictive market model — AI  (difficulty: 3.0)
                            → process improvement: trading_alpha +X%
                                → [Era 4] Decentralized finance protocol  (difficulty: 2.5) [EX eligible]
                                    → product unlock: defi_protocol
                                        → [Era 4] Central bank digital currency  (difficulty: 3.0)
                                            → product unlock: cbdc_platform
                                            → regulatory event: crypto frameworks enacted
                                                → [Era 5] AI portfolio management service  (difficulty: 3.0) [EX]
                                                    → product unlock: ai_investment_service
                                                        → [Era 6] Autonomous economic agent (DAO)  (difficulty: 5.0) [EX]
                                                            → product unlock: dao_governance_structure
                                                                → [Era 7] Interplanetary financial instrument  (difficulty: 8.0) [EX]
                                                                    → product unlock: interplanetary_bond
                                                                        → [Era 9] Post-scarcity economics research  (difficulty: 20.0) [EX]
                                                                            → publication only; raises global_knowledge_level
```

---

#### Domain: illicit_chemistry

**Chain: Designer Drug Progression (Scheduling Race)**
```
[Era 1] Novel synthetic cannabinoid  (difficulty: 0.5, patentable: no)
    → product unlock: synthetic_cannabinoid (legal until scheduled)
    → scheduling_clock starts on detection
        → [Era 1] Synthetic stimulant (bath salts class)  (difficulty: 0.5)
            → product unlock: synthetic_stimulant_nps
                → [Era 2] Synthetic opioid analogs — fentanyl class  (difficulty: 1.5)
                    → product unlock: synthetic_opioid_analog
                    → high risk/reward: extreme LD50 proximity, law enforcement focus
                        → [Era 2] Synthetic psychedelic analogs  (difficulty: 1.0)
                            → product unlock: synthetic_psychedelic_nps
                                → [Era 3] Structure-activity relationship (SAR) modeling — illicit  (difficulty: 2.0)
                                    → process improvement: analog_synthesis_speed +40%
                                    → designer_drug_pipeline_capacity +2 slots
                                        → [Era 4] Synthetic biology for drug synthesis  (difficulty: 4.0) [EX eligible]
                                            → yeast/bacteria engineered for alkaloid production
                                            → replaces chemical synthesis for some compounds
                                                → [Era 5] Neurochemical precision drug  (difficulty: 5.0) [EX]
                                                    → product unlock: precision_neuro_drug
                                                    → grey market: enhancement vs. scheduled
                                                        → [Era 6] Black market longevity compound  (difficulty: 8.0) [EX]
                                                            → criminal version of Era 6 longevity therapy
                                                                → [Era 7] Illicit cognitive enhancement  (difficulty: 6.0) [EX]
                                                                    → product unlock: illicit_bci_enhancement
                                                                    → requires: cognitive_science.bci unlocked (legal version)
                                                                        → [Era 8] Criminal synthetic biology  (difficulty: 15.0) [EX]
                                                                            → biological weapon / mass-casualty agent risk
                                                                            → biosecurity regulatory response
```

*See Part 7 for scheduling race mechanics applicable to all nodes in this domain.*

---

#### Domain: climate_and_environment

**Chain: Climate Tech**
```
[Era 2] Industrial emissions monitoring  (difficulty: 0.5)
    → product unlock: emissions_sensor_array
        → [Era 2] Carbon credit verification system  (difficulty: 1.0, patentable: yes)
            → product unlock: carbon_credit_instrument
                → [Era 3] Carbon offset project — forestry  (difficulty: 1.0)
                    → product unlock: carbon_offset_forestry_credit
                        → [Era 4] Corporate net-zero audit system  (difficulty: 1.5)
                            → regulatory unlock: enables carbon_tax_compliance_service

[Era 3] Second-generation biofuel  (difficulty: 2.0, patentable: yes)
    → product unlock: cellulosic_ethanol
        → [Era 4] Sustainable aviation fuel (SAF)  (difficulty: 2.5)
            → product unlock: sustainable_aviation_fuel
                → [Era 5] Green ammonia fertilizer  (difficulty: 3.0) [EX eligible]
                    → process improvement: fertilizer_co2_intensity –80%

[Era 4] Climate adaptation infrastructure  (difficulty: 2.0)
    → product unlock: flood_barrier_system
    → product unlock: drought_resilient_irrigation
        → [Era 5] Urban heat island mitigation  (difficulty: 1.5) [EX eligible]
            → process improvement: city_cooling_energy –20%
                → [Era 6] Ecosystem restoration at scale  (difficulty: 5.0) [EX]
                    → modifies: regional_forest_coverage
                    → modifies: global_co2_index (negative feedback)
```

**Chain: Industrial Gas Separation** (chemical_synthesis domain)
```
[Era 1] Industrial gas separation — cryogenic distillation  (difficulty: 0.5, available at start)
    → used for nitrogen, oxygen; no unlock needed
        → [Era 3] Helium separation plant  (difficulty: 1.5, patentable: yes)
            → product unlock: extracted_helium
            → WorldGen note: helium_fraction field is set per gas deposit at Stage 8 gen time;
              this tech is the required unlock (WorldGen §8.7 "Helium extraction from gas, era 3")
            → helium applications: superconductor cooling, MRI, semiconductor fab purge gas
                → [Era 4] Helium recycling system  (difficulty: 1.0)
                    → process improvement: helium_consumption –60% per unit of use
                    → strategic: limits depletion of finite helium reserves
```

---

#### Domain: nuclear_engineering [EX]

```
[Era 2] Small modular reactor design  (difficulty: 5.0, patentable: yes) [EX eligible; node in energy_systems]
    → facility unlock: small_modular_reactor
        → [Era 3] Thorium fuel cycle  (difficulty: 3.5, patentable: yes)  [V1; energy_systems domain]
            → process improvement: nuclear_waste_volume –90% vs uranium cycle
            → WorldGen note: thorium ore is seeded from Era 1; fuel use unlocked here at Era 3
                → [Era 3] Advanced reactor fuel cycle  (difficulty: 4.0) [EX]
                    → process improvement: nuclear_fuel_utilization +30%
                        → [Era 4] Gen IV fast reactor  (difficulty: 8.0) [EX]
                            → facility unlock: gen4_fast_reactor
                                → [Era 4] Isotope separation  (difficulty: 3.0) [EX]
                                    → product unlock: isotope_separation
                                    → enables K-40 fuel use (WorldGen §8.7), nuclear medicine isotopes
                                        → [Era 5] Fusion plasma physics  (difficulty: 20.0) [EX]
                                            → requires: quantum_systems domain knowledge ≥ 0.3
                                                → [Era 6] Fusion ignition  (difficulty: 20.0) [EX]
                                                    → see energy_systems.fusion chain

[Era 5] Nuclear medicine — advanced imaging  (difficulty: 3.0) [EX]
    → product unlock: pet_ct_scanner_advanced
        → [Era 6] Targeted radionuclide therapy  (difficulty: 4.0) [EX]
            → product unlock: targeted_radionuclide_drug
```

---

#### Domain: advanced_transportation [EX]

```
[Era 3] Supersonic business jet research  (difficulty: 4.0, patentable: yes) [EX eligible]
    → product unlock: supersonic_business_jet
        → [Era 5] Hypersonic aircraft research  (difficulty: 8.0) [EX]
            → requires: materials_science.high_entropy_alloys
            → product unlock: hypersonic_vehicle_platform
                → [Era 6] Commercial hypersonic airliner  (difficulty: 10.0) [EX]
                    → product unlock: hypersonic_commercial_route
                    → strategic implication: point-to-point travel anywhere < 2 hours
                        → [Era 7] Suborbital point-to-point transport  (difficulty: 15.0) [EX]
                            → product unlock: suborbital_transport_route
                                → [Era 8] Space elevator  (difficulty: 40.0) [EX]
                                    → requires: materials_science.space_elevator_cable_material
                                    → facility unlock: space_elevator
                                    → strategic implication: launch cost drops to near-zero; all orbital economics change
                                        → [Era 9] Non-rocket space access  (difficulty: 20.0) [EX]
                                            → electromagnetic launch, laser propulsion variants

[Era 4] eVTOL aircraft  (difficulty: 4.0) [EX eligible]
    → see mechanical_engineering.vehicles chain
        → [Era 5] Urban air mobility service platform  (difficulty: 3.0) [EX]
            → product unlock: uam_service_route
```

---

#### Domain: space_systems [EX]

**Chain: Launch and Orbital Access**
```
[Era 3] Reusable orbital launch vehicle  (difficulty: 5.0, patentable: yes) [EX eligible]
    → product unlock: reusable_launch_vehicle
    → process improvement: launch_cost –70% vs. expendable
        → [Era 4] Crewed commercial spaceflight  (difficulty: 6.0) [EX]
            → product unlock: crewed_spacecraft
                → [Era 4] Commercial space station  (difficulty: 8.0) [EX]
                    → facility unlock: commercial_orbital_station
                        → [Era 5] Lunar cargo delivery service  (difficulty: 6.0) [EX]
                            → product unlock: lunar_delivery_service
                                → [Era 5] Lunar base construction  (difficulty: 10.0) [EX]
                                    → facility unlock: lunar_base
                                        → [Era 6] Lunar helium-3 extraction  (difficulty: 8.0) [EX]
                                            → product unlock: helium3_fuel
                                            → enables next-gen fusion fuel
                                                → [Era 6] Lunar regolith manufacturing  (difficulty: 6.0) [EX]
                                                    → product unlock: lunar_manufactured_good
                                                        → [Era 7] Mars colony — early  (difficulty: 15.0) [EX]
                                                            → facility unlock: mars_base
```

**Chain: Asteroid Mining**
```
[Era 5] Asteroid prospecting — robotic  (difficulty: 6.0) [EX]
    → product unlock: asteroid_survey_data
        → [Era 6] Asteroid mining — near-Earth  (difficulty: 12.0) [EX]
            → product unlock: asteroid_metal_ore
            → economic event: rare earth prices collapse if successful
                → [Era 7] Industrial-scale asteroid mining  (difficulty: 15.0) [EX]
                    → product unlock: asteroid_bulk_metal (iron, nickel, platinum-group)
                    → MAJOR economic disruption: metals markets
                        → [Era 8] Outer belt mining operations  (difficulty: 20.0) [EX]
                            → product unlock: outer_belt_resources
                                → [Era 9] Asteroid belt economy  (difficulty: 30.0) [EX]
                                    → new SimulationRegion: asteroid_belt (EX)
```

**Chain: In-Space Manufacturing**
```
[Era 6] Microgravity manufacturing research  (difficulty: 5.0) [EX]
    → requires: commercial_orbital_station
    → product unlock: microgravity_manufactured_crystal (pharmaceutical use)
        → [Era 7] In-space factory  (difficulty: 10.0) [EX]
            → facility unlock: orbital_manufacturing_station
                → [Era 8] In-space resource utilization  (difficulty: 8.0) [EX]
                    → process improvement: space_construction_cost –60%
                        → [Era 9] Orbital megastructure construction  (difficulty: 40.0) [EX]
                            → product unlock: orbital_megastructure_module
```

---

#### Domain: cognitive_science [EX]

```
[Era 4] Consumer neurofeedback device  (difficulty: 2.0, patentable: yes) [EX eligible]
    → product unlock: consumer_neurofeedback_headset
        → [Era 5] Medical BCI — motor cortex  (difficulty: 6.0) [EX]
            → product unlock: medical_bci_motor
            → regulatory: FDA/equiv approval path ~5 years in-sim
                → [Era 5] High-bandwidth neural recording array  (difficulty: 5.0) [EX]
                    → research tool only; not commercial
                        → [Era 6] Cognitive enhancement implant  (difficulty: 8.0) [EX]
                            → product unlock: cognitive_enhancement_bci
                            → grey market exists before regulatory approval
                            → labor market: enhanced workers command wage premium
                                → [Era 6] Memory augmentation BCI  (difficulty: 8.0) [EX]
                                    → product unlock: memory_augmentation_bci
                                        → [Era 7] Bidirectional BCI — read/write  (difficulty: 15.0) [EX]
                                            → product unlock: bidirectional_bci
                                            → enables: direct neural communication
                                                → [Era 8] Neural mesh — persistent  (difficulty: 20.0) [EX]
                                                    → product unlock: neural_mesh_implant
                                                    → cognitive labor market transformation
                                                        → [Era 9] Mind upload prototype  (difficulty: 50.0) [EX]
                                                            → requires: quantum_systems.fault_tolerant_quantum_computer
                                                            → product unlock: digital_consciousness_substrate
                                                            → new legal and economic entity class
                                                                → [Era 10] Substrate-independent existence  (difficulty: 30.0) [EX]
                                                                    → post-biological human NPC class enters simulation
```

---

#### Domain: synthetic_biology [EX]

```
[Era 4] CRISPR-Cas9 therapeutic application  (difficulty: 4.0, patentable: yes) [EX eligible]
    → product unlock: crispr_therapy
        → [Era 5] Metabolic engineering for industrial chemicals  (difficulty: 5.0) [EX]
            → product unlock: bio_manufactured_chemical
            → process improvement: chemical_synthesis_energy –40%
                → [Era 6] Programmable microbiome  (difficulty: 6.0) [EX]
                    → product unlock: therapeutic_microbiome
                    → agricultural application: soil microbiome enhancement
                        → [Era 6] Whole-genome synthesis  (difficulty: 8.0) [EX]
                            → product unlock: synthetic_genome
                                → [Era 7] Gene drive — contained  (difficulty: 10.0) [EX]
                                    → product unlock: contained_gene_drive
                                    → application: malaria elimination, invasive species
                                    → regulatory risk: international biosafety treaties
                                        → [Era 7] Programmable organism — industrial  (difficulty: 12.0) [EX]
                                            → facility unlock: biological_factory
                                            → produces: any organic compound at low cost
                                                → [Era 8] Synthetic ecosystem design  (difficulty: 20.0) [EX]
                                                    → product unlock: synthetic_ecosystem_module
                                                        → [Era 9] Designer organism — general  (difficulty: 30.0) [EX]
                                                            → product unlock: bespoke_organism
                                                                → [Era 10] Post-biological ecology  (difficulty: 40.0) [EX]
                                                                    → climate recovery or new equilibrium
```

---

#### Domain: geoengineering [EX]

**Scope Note** (Pass 5 Session 8):
[EX] Geoengineering is expansion content. V1 data structure requirements:
- `climate_stress_index` must support negative delta (cooling effects)
- `ResourceDeposit.accessible` dual-gate (era + tech) already supports geoengineering resource unlocks
- No additional V1 data structures needed; geoengineering reuses existing climate and resource systems

```
[Era 5] Stratospheric aerosol injection research  (difficulty: 5.0) [EX]
    → controversial; accelerates regulatory response globally
    → no product unlock (research only for Era 5)
        → [Era 6] SAI pilot deployment  (difficulty: 8.0) [EX]
            → modifies: global_temperature_delta (small reduction)
            → triggers: geopolitical conflict events
                → [Era 7] Coordinated SAI program  (difficulty: 15.0) [EX]
                    → requires: political_pressure_req = very high (global consensus)
                    → modifies: global_temperature_delta –0.5°C
                    → risk: termination_shock (if stopped abruptly)

[Era 6] Ocean iron fertilization  (difficulty: 4.0) [EX]
    → modifies: ocean_co2_absorption +5%
    → risk: ocean_ecosystem_disruption event probability +0.02/tick

[Era 7] Marine cloud brightening  (difficulty: 6.0) [EX]
    → modifies: regional_albedo (regional effect)

[Era 8] Planetary albedo management  (difficulty: 25.0) [EX]
    → requires: space_systems.orbital_megastructure_construction
    → modifies: global_temperature_delta (significant)

[Era 9] Deliberate climate control  (difficulty: 30.0) [EX]
    → set global_temperature_delta as policy variable
    → only achievable with multiple geoengineering techs combined
        → [Era 10] Planetary atmospheric engineering  (difficulty: 50.0) [EX]
            → climate is now a managed system, not an external force
```

---

#### Domain: quantum_systems [EX]

```
[Era 3] Quantum key distribution — point-to-point  (difficulty: 3.0, patentable: yes) [EX eligible]
    → product unlock: qkd_link
        → [Era 4] Quantum random number generator — commercial  (difficulty: 1.5) [EX eligible]
            → product unlock: quantum_rng_module
                → [Era 5] Quantum sensor — navigation  (difficulty: 4.0) [EX]
                    → product unlock: quantum_inertial_sensor
                    → enables GPS-free precision navigation
                        → [Era 5] Quantum sensor — gravitational  (difficulty: 5.0) [EX]
                            → product unlock: quantum_gravimeter
                            → enables mineral/oil survey without drilling
                                → [Era 6] Quantum communication network  (difficulty: 8.0) [EX]
                                    → product unlock: quantum_repeater_node
                                        → [Era 7] Continental quantum internet  (difficulty: 12.0) [EX]
                                            → process improvement: financial_transaction_security → unbreakable
                                                → [Era 8] Global quantum internet  (difficulty: 20.0) [EX]
                                                    → product unlock: quantum_internet_node

[Era 4] NISQ quantum processor  (difficulty: 8.0) [EX eligible]
    → see semiconductor_physics.quantum chain
```

---

### Smartphones and Platform Economy Chain (Preserved + Extended)

```
[Era 1] Mobile chipsets (Tier 3 semiconductors)
    → [Era 2] ARM processor architecture license
        → [Era 2] Mobile operating system
            → [Era 2] Smartphone (product unlock: mobile_phone_smartphone)
                → [Era 2] App platform (product unlock: app_platform)
                    → [Era 3] App economy infrastructure
                        → [Era 3] Gig platform (product unlock: gig_economy_platform)
                            → [Era 4] Super-app platform  (product unlock: super_app)
                                → [Era 5] AI-native app platform  [EX eligible]
                                    → [Era 6] Spatial computing platform (AR/MR)  [EX]
                                        → [Era 7] Neural interface app platform  [EX]
                                            → requires: cognitive_science.bidirectional_bci
```

---

## Part 3.5 — Technology Lifecycle Model [CORE]

### Overview

A technology node, once researched, moves through **two discrete stages** followed by **one continuous maturation track** — all **per actor**. These are not global — different actors hold the same technology at different stages simultaneously, and that gap is itself a competitive asset.

**Implementation note for AI Bootstrapper:** `TechStage` enum has exactly **two values** (`researched`, `commercialized`). Stage 3 "MATURED" in the description below is not a third enum value — it is the `maturation_level` float (0.0–1.0) on `TechHolding`, which continues rising after commercialization. Do not generate a `TechStage::matured` enum value.

```
Stage 1: RESEARCHED
    Actor demonstrated the technology works internally.
    Can produce for own supply chain; not visible on the market.
    Quality ceiling is low — first-generation capability only.
    Patent may be filed (begins 20-year clock).

Stage 2: COMMERCIALIZED
    Actor decides to bring the product to open market.
    A BusinessAction — not automatic on research completion.
    Product is now observable; reverse engineering becomes possible.
    NPC consumers and businesses can purchase.

Stage 3: MATURED  (continuous, not a discrete stage)
    Ongoing investment raises maturation_level (0.0 → 1.0).
    maturation_level sets the quality ceiling for recipes using this technology.
    Ceiling is era-gated — some improvements require dependent tech in later eras.
    Competing actors can close the gap by investing post-acquisition.
```

**Key invariant:** Stages advance forward only. Commercialization is irreversible. Research cannot be lost (only patents expire).

---

### Data Structures

```cpp
enum class TechStage : uint8_t {
    researched,      // actor has the capability; internal production only
    commercialized,  // product is on the open market
};

struct TechHolding {
    std::string  node_key;
    uint32_t     holder_id;
    TechStage    stage;
    float        maturation_level;     // 0.0–1.0; rises with continued investment
    float        maturation_ceiling;   // era-gated max; recomputed each tick
    uint32_t     researched_tick;
    uint32_t     commercialized_tick;  // 0 if still Stage 1
    bool         has_patent;
    bool         internal_use_only;    // true = actor does NOT list on open market despite stage = commercialized.
                                      // P-02 clarification: "commercialized" stage means commercial-scale production
                                      // for own supply chain is unlocked. internal_use_only = true means production
                                      // stays internal — no GoodOffer generated, other actors cannot purchase.
                                      // Reverse-engineering risk applies only via physical observation (facility
                                      // inspection, product forensics) — NOT via market observation. Models vertical
                                      // integration strategy. When false (default at commercialization), the product
                                      // enters the open market and market-observation reverse-engineering applies.
};

struct ActorTechnologyState {
    uint32_t  business_id;
    std::map<std::string, TechHolding> holdings;  // node_key → holding

    bool      has_researched(const std::string& node_key) const;
    bool      has_commercialized(const std::string& node_key) const;
    float     maturation_of(const std::string& node_key) const;  // 0.0 if not held
};
```

`ActorTechnologyState` is a field on `BusinessEntity` (TDD). Each business owns its technology portfolio.

```cpp
struct MaturationProject {
    std::string  node_key;
    uint32_t     facility_id;
    uint32_t     researchers_assigned;
    float        funding_per_tick;
    float        progress;
};
```

**Maturation progress per tick:**
```
maturation_progress_per_tick = researchers_assigned
                              × researcher_quality_avg
                              × facility_quality_modifier
                              × domain_knowledge_bonus
                              × funding_adequacy
                              × MATURATION_RATE_COEFF

maturation_level += maturation_progress_per_tick / MATURATION_DIFFICULTY_PER_LEVEL
maturation_level  = min(maturation_level, maturation_ceiling)
```

**Named constants (in `rnd_config.json`):**
```
MATURATION_RATE_COEFF          = 0.40   // maturation is slower per researcher than original research
MATURATION_DIFFICULTY_PER_LEVEL = 2.0  // effort points to raise maturation_level by 0.1
```

---

### Era-Gated Maturation Ceiling

A technology cannot be matured beyond what the current era's dependent infrastructure supports. The 2007 iPhone could not become a 2026 iPhone through software investment alone — it needed OLED, 5G, and a decade of mobile SoC scaling that did not exist yet.

```cpp
float compute_maturation_ceiling(
    const std::string&          node_key,
    SimulationEra               current_era,
    const ActorTechnologyState& actor_state
) {
    float base_ceiling = MATURATION_ERA_CEILING_BASE[node_key][current_era];

    // Ceiling rises if the actor also holds dependent enhancement nodes.
    float enhancement_bonus = 0.0f;
    for (const auto& enhancer : MATURATION_ENHANCERS[node_key]) {
        if (actor_state.has_researched(enhancer.node_key))
            enhancement_bonus += enhancer.ceiling_bonus;
    }

    return std::min(1.0f, base_ceiling + enhancement_bonus);
}
```

`MATURATION_ERA_CEILING_BASE` and `MATURATION_ENHANCERS` are data-file defined (`/data/technology/maturation_ceilings.csv`, `/data/technology/maturation_enhancers.csv`). Example values:

| node_key | Era 1 | Era 2 | Era 3 | Era 4 | Era 5 |
|---|---|---|---|---|---|
| electric_vehicle | 0.10 | 0.35 | 0.55 | 0.75 | 0.90 |
| mobile_phone_smartphone | — | 0.25 | 0.50 | 0.70 | 0.85 |
| solar_panel_cost_competitive | 0.20 | 0.40 | 0.60 | 0.80 | 0.95 |
| large_language_model | — | — | — | 0.40 | 0.75 |
| mrna_therapeutics | — | — | — | 0.35 | 0.65 |

*"—" = not yet researchable that era.*

**The era ceiling does not prevent investment — it caps the return.** An actor can over-invest in maturation but will plateau until the era advances.

---

### Effect on Product Quality

`ComputeOutputQuality` (Commodities & Factories) is revised to incorporate actor-specific maturation alongside facility tech tier. The more restrictive of the two ceilings applies.

```cpp
float compute_quality_ceiling(
    const FacilityRecipe&       recipe,
    uint8_t                     facility_tech_tier,
    const ActorTechnologyState& actor_state
) {
    // Existing: facility tier sets a ceiling
    float tier_ceiling = TECH_QUALITY_CEILING_BASE
        + TECH_QUALITY_CEILING_STEP × (facility_tech_tier - recipe.min_tech_tier);

    // New: actor's maturation level on the recipe's key technology node
    float maturation_ceiling = 1.0f;  // no cap if no key node (commodity recipes)
    if (!recipe.key_technology_node.empty()) {
        maturation_ceiling = actor_state.maturation_of(recipe.key_technology_node);
        // maturation_of() returns 0.0 if not held → actor cannot run the recipe
    }

    return std::min({tier_ceiling, maturation_ceiling, 1.0f});
}
```

`FacilityRecipe` gains one field:
```cpp
std::string key_technology_node;  // node_key whose maturation_level caps quality
                                   // empty = no maturation cap (mature/commodity recipes)
```

**Practical effect:** Two factories with identical tier and inputs produce different quality if their maturation levels differ. The pioneer's decade of investment is a persistent quality advantage that competitors must either buy (licensing), steal (espionage), or earn (independent maturation investment).

---

### Commercialization as a Business Decision

```cpp
enum class CommercializationDecision : uint8_t {
    hold_proprietary,           // produce internally only; product invisible to market
    commercialize_open,         // sell on open market; product observable to all
    commercialize_licensed,     // sell only to specific licensees
    commercialize_regional,     // sell in specific regions only
};
```

**NPC commercialization logic by profile:**

| Profile | Trigger |
|---|---|
| `fast_expander` | Commercializes as soon as production capacity exists |
| `defensive_incumbent` | Delays; prefers licensing negotiations over open market |
| `cost_cutter` | Commercializes after internal supply chain benefit is extracted |
| `innovation_leader` | Commercializes immediately; market presence is the competitive signal |
| `criminal_organization` | Never formally commercializes; grey/black channel distribution only |

---

### Maturation Transfer on Acquisition

When an actor acquires technology through diffusion, they receive a snapshot maturation level, not zero.

| Acquisition mechanism | Transferred maturation |
|---|---|
| Licensing (willing) | `licensor_maturation × 0.80` |
| Reverse engineering | `observed_product_implied_maturation × 0.50` |
| Patent expiry (public domain) | `global_avg_maturation_at_expiry × 0.60` |
| Corporate espionage | `target_maturation × 0.90` |
| Talent hire | Researcher's `domain_skill` translates to maturation bonus (formula TBD) |
| Academic publication | Raises `domain_knowledge_level` only; no direct maturation transfer |

**Named constants (in `rnd_config.json`):**
```
MATURATION_TRANSFER_LICENSE     = 0.80
MATURATION_TRANSFER_REVERSE_ENG = 0.50
MATURATION_TRANSFER_PUBLIC      = 0.60
MATURATION_TRANSFER_ESPIONAGE   = 0.90
```

Transferred maturation is still era-capped. A late entrant who licenses cutting-edge technology gets the snapshot, but cannot exceed the current era ceiling until the era advances.

---

### Scenario Assertions

```
[SCENARIO S-TL-01]: When actor A researches electric_vehicle and does NOT commercialize,
    then electric_vehicle does NOT appear in any RegionalMarket price feed.
    AND actor A can produce electric_vehicle for internal use.
    AND other actors cannot initiate reverse_engineering (no observable product).

[SCENARIO S-TL-02]: When actor A commercializes electric_vehicle at maturation_level = 0.30,
    then quality_ceiling of actor A's EV recipes = min(tier_ceiling, 0.30).
    AND actor B can initiate reverse_engineering.
    AND reverse_engineering success grants actor B maturation = 0.30 × 0.50 = 0.15.

[SCENARIO S-TL-03]: When era advances from Era 2 to Era 3,
    AND MATURATION_ERA_CEILING_BASE[electric_vehicle][Era 3] = 0.55,
    then actor A's maturation_ceiling rises to 0.55 immediately (no action required).
    AND actor A can now invest maturation beyond 0.30 up to 0.55.

[SCENARIO S-TL-04]: When actor A licenses electric_vehicle to actor B at maturation 0.60,
    then actor B receives TechHolding at maturation = 0.60 × 0.80 = 0.48.
    AND actor B's maturation_ceiling is era-gated (not actor A's ceiling).
    AND actor B can invest to advance maturation independently.

[SCENARIO S-TL-05]: When actor A holds electric_vehicle at maturation = 0.25 (tier 4 facility),
    AND competitor holds electric_vehicle at maturation = 0.60 (same tier, same inputs),
    then actor A quality_ceiling = min(tier_4_ceiling, 0.25) = 0.25.
    AND competitor quality_ceiling = min(tier_4_ceiling, 0.60) = 0.60.
    AND quality difference is reflected in market prices for each actor's output.
```

---

## Part 4 — Project Mechanics

### The Research Project Lifecycle

```cpp
struct ResearchProject {
    std::string project_key;
    uint32_t    facility_id;
    ResearchDomain domain;
    std::string target_node_key;
    float       difficulty;
    float       progress;
    uint32_t    researchers_assigned;
    float       funding_per_tick;
    float       success_probability;
    uint32_t    started_tick;
    bool        is_secret;

    ResearchOutcome expected_outcome;
    ResearchOutcome actual_outcome;  // set on completion
};

enum class ResearchOutcome : uint8_t {
    success_full,          // full unlock; best case
    success_partial,       // partial progress; can be combined with another run
    unexpected_discovery,  // unintended breakthrough in adjacent area
    failure_dead_end,      // dead end; produces publications increasing domain knowledge
    failure_setback,       // catastrophic; destroys progress, costs researchers
    patent_preempted,      // rival filed a patent on the same node while you were working
};
```

**Researcher Quality Modifier [CORE]** (Pass 5 Session 8):

```cpp
research_progress_per_tick =
    base_progress_rate
    × facility_quality_modifier
    × researcher_quality_modifier(avg_researcher_skill)

researcher_quality_modifier(skill) = 0.5 + 0.5 × (skill / MAX_SKILL)
// Range: [0.5, 1.0]
// skill = NPC education_level (0–10) + experience_ticks / 360
// MAX_SKILL = 20.0 (e.g., 1000 ticks of experience ≈ 2.8 skill points)
```

**Progress accumulation per tick:**
```
progress_per_tick = researchers_assigned
                  × researcher_quality_avg
                  × facility_quality_modifier
                  × domain_knowledge_bonus
                  × funding_adequacy

domain_knowledge_bonus = 1.0 + (domain_knowledge_level × DOMAIN_KNOWLEDGE_BONUS_COEFF)
funding_adequacy = min(1.0, actual_funding / required_funding_per_tick)
```

**Success probability per tick:**
```
base_success_probability = BASE_RESEARCH_SUCCESS_RATE    // 0.75 when difficulty is met
adjusted = base_success_probability
         × facility_success_modifier
         × domain_knowledge_modifier
         × secrecy_penalty (secret projects: -0.10)
```

**Named constants (all in `rnd_config.json`):**
```
BASE_RESEARCH_SUCCESS_RATE       = 0.75
DOMAIN_KNOWLEDGE_BONUS_COEFF     = 0.30
UNEXPECTED_DISCOVERY_PROBABILITY = 0.05
PATENT_PREEMPTION_CHECK_RATE     = 0.02
```

### Productive Failure

On `failure_dead_end`:
- All researchers gain domain experience
- A publication is generated, raising domain knowledge globally
- Progress is lost, but "known dead end" reduces future project cost in the same node

On `failure_setback`:
- Researchers may quit or transfer
- Some progress is destroyed
- If the project was secret, rivals who were watching may gain intelligence

> Test: R&D investments can produce four failure outcomes: (1) Setback: progress reduced by SETBACK_AMOUNT (triggered when random < SETBACK_PROBABILITY per tick). (2) Dead-end: node becomes permanently blocked after cumulative investment exceeds 3× expected_cost without breakthrough (requires pivot to alternative node). (3) Preemption: competitor NPC completes the same node first, granting them the patent. Player must license or find alternative. (4) Partial result: breakthrough occurs but at reduced quality (quality = 0.5 × expected when investment < 0.7 × expected_cost at time of breakthrough).
>
> Seed setup: Player invests in technology_node_X with expected_cost = 1000. (Test A) Set random seed so setback triggers at tick 50 — assert progress decreases. (Test B) Invest 3001 units without breakthrough — assert dead_end state. (Test C) Competitor NPC starts same node 10 ticks earlier with faster progress — assert preemption. (Test D) Breakthrough triggers at 600 investment — assert quality = 0.5 × normal.

### Researcher NPCs

```cpp
struct ResearcherProfile {
    NPCRole            role;           // researcher_scientist, researcher_engineer, etc.
    ResearchDomain     primary_domain;
    float              domain_skill;   // 0.0–1.0; improves with each project
    float              creativity;     // increases unexpected_discovery probability
    float              productivity;   // base progress contribution per tick
    uint32_t           employer_id;
    bool               has_nda;
    float              loyalty;        // low loyalty = IP theft risk
};
```

Researchers move between employers, publish papers, attend conferences, and can be poached. A rival company's lead researcher is a target for headhunting — or corporate espionage. Researchers with low loyalty are insider threats: they may share proprietary research, sell it to criminal buyers, or become whistleblowers.

---

## Part 5 — Patents

### Patent Mechanics

```cpp
struct Patent {
    std::string  patent_key;
    std::string  technology_node_key;
    uint32_t     holder_id;
    uint32_t     filed_tick;
    uint32_t     expires_tick;         // filed_tick + PATENT_DURATION_TICKS
    float        patent_strength;      // 0.0–1.0
    bool         is_contested;
    std::vector<uint32_t> licensees;
};
```

**Named constants (all in `rnd_config.json`):**
```
PATENT_DURATION_TICKS     = 20 × TICKS_PER_YEAR
PATENT_STRENGTH_BASE      = 0.70
PATENT_CHALLENGE_COST     = HIGH
```

**Patent effects:**
- Unlicensed use: `exposure` event, evidence tokens generated, hostile motivated rival
- Licensing: passive income for holder, Obligation structure between licensee and licensor
- Expiry: technology enters public domain; domain knowledge rises sharply

**Patent strategies:**
- **Patent thicket** — adjacent node filings make navigation expensive for rivals
- **Defensive filing** — protect own research without intent to exploit commercially
- **Licensing empire** — R&D business model around licensing not manufacturing
- **Patent acquisition** — buy patents from bankrupt businesses or inventors
- **Design-around** — R&D to achieve same outcome via different technical path

### Patent Theft and Espionage

Tasking an agent with stealing research produces the breakthrough faster but generates exposure risk. If caught: criminal charges (industrial espionage), civil suit, diplomatic consequences (cross-border), permanent hostile relationship with victimized firm.

---

## Part 6 — Technology Diffusion

Technology does not stay proprietary forever. Diffusion mechanisms transfer both access (Stage 1 TechHolding) and a maturation snapshot. See Part 3.5 for maturation transfer rates per mechanism.

1. **Patent expiry** — After 20 in-game years, patented technology enters public domain. New entrants start at `global_avg_maturation × 0.60`.
2. **Academic publication** — University projects produce publications immediately public; each raises domain knowledge. No direct maturation transfer.
3. **Technology licensing** — Voluntary licensed diffusion. Licensor profits; licensee receives maturation snapshot at `licensor_maturation × 0.80`.
4. **Reverse engineering** — Purchase commercialized product, disassemble, research underlying technology. Grants maturation at `implied_maturation × 0.50`. Requires the product to be in Stage 2 (commercialized).
5. **Talent mobility** — When a researcher leaves, they carry tacit knowledge. NDAs slow but do not stop this. Translates to maturation bonus proportional to researcher `domain_skill`.
6. **Regulatory disclosure** — Some regulatory processes (pharmaceutical approval, environmental permits) require technical disclosure. Pushes technology into public domain at reduced maturation transfer.
7. **Industrial accidents** — A catastrophic facility failure may expose what was being produced, giving rivals intelligence about Stage 1 holdings (without transferring maturation).
8. **Corporate espionage** — Grants maturation snapshot at `target_maturation × 0.90`. Highest fidelity transfer; highest risk.

**Diffusion rate by technology type:**

| Technology type | Diffusion speed | Primary mechanism | Maturation gap persistence |
|---|---|---|---|
| Software / platform | Very fast | Reverse engineering, open source | Low — gaps close within 1–2 eras |
| Consumer electronics | Fast | Disassembly, patent expiry | Medium — hardware iteration matters |
| Industrial processes | Medium | Patent expiry, talent mobility | Medium-High — tacit knowledge is sticky |
| Pharmaceutical | Slow | Strict patent enforcement, regulatory moats | High — clinical trial data is non-transferable |
| Semiconductor fabrication | Very slow | Massive capital requirements, export controls | Very high — capex and process secrets compound |
| Criminal synthesis | Fast | Street network sharing, seized lab analysis | Low — synthesis knowledge spreads quickly |
| Space systems [EX] | Very slow | Capital + jurisdiction barriers | Very high — physical infrastructure gap is the moat |
| Fusion technology [EX] | Slow | Capital requirements; rapid scale once ignited | High until commercial, then collapses fast |
| Cognitive technology [EX] | Medium | Regulatory lag; grey market accelerates diffusion | Medium — black market BCI closes gap early |

---

## Part 7 — Criminal R&D

### The Scheduling Race

Designer drugs are legal until they are scheduled. Criminal chemists are in a permanent race against the legislative response cycle.

```
[Criminal lab] researches novel compound in illicit_chemistry domain
                        ↓
[Compound synthesized] → enters market as unscheduled substance
                        ↓
[Regulators detect] → scheduling review initiated (duration proportional to political system speed)
                        ↓
[Scheduling enacted] → compound becomes controlled; existing inventory is now criminal
                        ↓
[Criminal R&D] must already have next compound in pipeline or face supply gap
```

**Key data structures:**
```cpp
struct SchedulingProcess {
    std::string compound_key;
    uint32_t    detection_tick;
    uint32_t    review_duration;
    float       political_delay;
    bool        is_enacted;
};

struct CriminalRnDProject : public ResearchProject {
    std::string  target_compound_key;
    bool         compound_is_legal;
    uint32_t     scheduling_risk_level;
};
```

**Analogue Act evolution by era:**

| Era | Scheduling framework | Effect on designer drug R&D |
|---|---|---|
| Era 1 | Specific molecule bans | Research any novel structure |
| Era 2 | Class-based analogue acts | Must change structural class, not just molecule |
| Era 3 | Pharmacological analogue acts | Effect-based scheduling; much harder to evade |
| Era 4 | AI-assisted scheduling review | Review period shortens dramatically |
| Era 5+ [EX] | Real-time predictive scheduling | Near-instant scheduling from molecular structure alone |
| Era 6+ [EX] | Synthetic biology biosecurity | Criminal biotech faces additional treaty frameworks |

### Criminal Research Infrastructure

```
opsec_profile = {
    power_consumption_signal:  0.5–0.8
    chemical_waste_signal:     0.6–0.9
    foot_traffic_signal:       0.2–0.3
    olfactory_signal:          0.3–0.7
    explosion_risk_per_tick:   0.0005
}
```

In Era 6+ [EX], synthetic biology criminal labs add:
```
opsec_profile_addendum = {
    biological_waste_signal:   0.7–0.9 (characteristic biomass disposal)
    biosafety_inspector_flag:  true    (separate regulatory authority)
    containment_failure_risk:  0.0002  (lower than chemical; contained bioreactors)
}
```

---

## Part 8 — The Climate System

### Design Intent

The climate system is not background flavor. It is a slow-burn economic force that:

1. Creates stranded asset risk for fossil fuel investments
2. Creates first-mover advantage for clean technology
3. Forces regulatory compliance costs onto polluting industries
4. Generates extreme weather events that disrupt supply chains
5. Degrades agricultural yields in affected regions over time
6. Shapes NPC political behavior (climate-aware voters shift political cycles)
7. [EX] In Eras 7–10, geoengineering interventions allow partial climate management — but introduce new geopolitical risks

### Global CO₂ Index

```cpp
struct ClimateState {
    float global_co2_index;          // starts at 1.0 (= 370 ppm)
    float global_temperature_delta;  // degrees above 2000 baseline
    float climate_feedback_rate;     // non-linear feedback multiplier
    float geoengineering_offset;     // [EX] cooling from active interventions; default 0.0
};
```

**CO₂ index update per tick:**
```
co2_emissions_this_tick = sum over all regions:
    (fossil_fuel_consumption × FOSSIL_CO2_COEFFICIENT)
    + (industrial_process_emissions × config.climate.industrial_co2_coefficient)
    - (carbon_capture_installed × config.climate.capture_efficiency)
    - (forest_coverage × config.climate.forest_sequestration_rate)
    - (biological_factory_consumption × config.climate.biofactory_sequestration)  // [EX]

co2_removals_this_tick = carbon_capture + sequestration

co2_removals_this_tick = carbon_capture + sequestration

global_co2_index += (co2_emissions_this_tick - co2_removals_this_tick) × config.climate.co2_persistence

// Non-linear feedback acceleration (Pass 5 Session 8)
climate_stress_delta(t) =
    BASE_WARMING_RATE × cumulative_emissions(t)
    × (1.0 + NONLINEAR_ACCELERATION × max(0, climate_stress(t) - ACCELERATION_THRESHOLD))

// Where: ACCELERATION_THRESHOLD = 1.5 (degrees C above pre-industrial)
//        NONLINEAR_ACCELERATION = 0.5
//        BASE_WARMING_RATE = feedback sensitivity multiplier per unit emissions

global_co2_index *= (1.0 + climate_feedback_rate × config.climate.feedback_sensitivity)

global_temperature_delta = (global_co2_index - 1.0) × config.climate.climate_sensitivity_factor
                         - geoengineering_offset   // [EX] applied after base calculation
```

**Named constants (all in `climate_config.json`):**
```
co2_persistence                 = 0.9998
feedback_sensitivity            = 0.001
climate_sensitivity_factor      = 1.5
forest_sequestration_rate       = 0.02
capture_efficiency              = 0.80
industrial_co2_coefficient      = 0.0005
regional_sensitivity_multiplier = 0.10
farm_stress_sensitivity         = 0.40
drought_yield_penalty           = 0.30
```

### Regional Climate Stress

```cpp
struct RegionClimateState {
    float regional_climate_stress;
    float drought_probability;
    float flood_probability;
    float wildfire_probability;
    float crop_yield_modifier;
    float sea_level_delta;
    float extreme_heat_days;
};
```

**Update formula:**
```
regional_climate_stress += global_temperature_delta × config.climate.regional_sensitivity_multiplier
                         × region.geographic_vulnerability
                         × (1.0 - region.adaptation_investment_level)
```

**Geographic vulnerability by region type:**

| Region Type | Vulnerability | Primary risk |
|---|---|---|
| Arctic / subarctic | Very High | Permafrost release, ice melt |
| Tropical coastal | Very High | Sea level, extreme storms |
| Arid / semi-arid | High | Drought, desertification |
| Temperate agricultural | Medium | Seasonal disruption, flooding |
| Continental industrial | Medium-Low | Heat stress, water scarcity |
| Northern boreal | Low-Medium | Longer growing seasons (initially positive) |

### Climate Effects on the Economy

**Agricultural output:**
```
farm_output_actual = farm_output_base
    × (1.0 - regional_climate_stress × config.climate.farm_stress_sensitivity)
    × (1.0 - drought_active × config.climate.drought_yield_penalty)
    × fertilizer_input_modifier
```

**Extraction costs:**
- Rising sea levels increase cost of coastal extraction
- Extreme heat raises cooling costs and reduces outdoor labor productivity

### Permafrost Thaw Dual-Gate Formula [CORE] [OWNED BY R&D]

**Ownership Statement:** This formula is OWNED by the R&D system. WorldGen seeds the deposit parameters. Climate system updates climate_stress_index. R&D system evaluates accessibility each tick.

**Problem Statement:** Resources locked in permafrost (rare earths, fossil fuels in Arctic) become accessible as temperature rises, BUT only if:
1. The climate has warmed enough to thaw (climate gate)
2. The required extraction technology has been researched (technology gate)

Both gates must pass for accessibility.

**Formula:**

```cpp
bool ResourceDeposit::accessible(const SimCell& province) const {
    // Base accessibility (not climate-gated)
    if (base_accessible) {
        return true;
    }

    // Dual-gate check for climate-gated deposits (permafrost, ice-locked, etc.)
    if (requires_climate_gate) {
        // Gate 1: Climate threshold
        float climate_stress_index = province.climate_profile.regional_climate_stress;
        bool climate_gate_passed = (climate_stress_index >= climate_unlock_threshold);

        // Gate 2: Technology requirement
        bool tech_gate_passed = technology_unlocked(required_tech_node);

        // Both gates must pass
        return climate_gate_passed && tech_gate_passed;
    }

    // Non-climate-gated, non-base deposits (rare; likely not accessible)
    return false;
}
```

**Variable Table:**

| Variable | Type | Range | Notes |
|---|---|---|---|
| `base_accessible` | bool | — | True if deposit is accessible from game start (most deposits) |
| `requires_climate_gate` | bool | — | True if this deposit is locked by climate (permafrost, ice sheets, high Arctic only) |
| `climate_unlock_threshold` | float | 0.0–2.0 | Regional climate stress index at which thaw occurs; tuned per deposit type |
| `regional_climate_stress` | float | 0.0–2.0 | Current accumulated climate stress in the province; updated each tick by Climate system |
| `required_tech_node` | uint32_t | — | Technology node ID that must be researched to extract this deposit (e.g., "arctic_drilling" for oil, "rare_earth_processing" for REE) |
| `technology_unlocked(node_id)` | bool | — | Returns true if player or any NPC has researched this tech; evaluated at tick of deposit access check |

**Climate unlock thresholds by deposit type:**

| Deposit Type | Typical Climate Threshold | Real-World Analog |
|---|---|---|
| Rare earth elements (Arctic) | 1.2–1.5°C above baseline | Greenland REE deposits; accessible as permafrost retreats |
| Petroleum (Arctic shelves) | 0.8–1.2°C | Arctic Ocean oil fields; accessible as ice extent recedes |
| Natural gas (hydrate deposits) | 1.5–2.0°C | Methane hydrate fields; accessible only at severe warming |
| Peat/historical carbon deposits | 0.5–0.8°C | Siberian permafrost peat; releases carbon when thawed |
| Geothermal energy (deep Arctic) | 0.3–0.6°C | Enhanced geothermal systems; permafrost acts as cap rock |

**Data structure for ResourceDeposit:**

```cpp
struct ResourceDeposit {
    // ... existing fields (commodity_id, reserve_size, grade, etc.) ...

    // Climate gating (added)
    bool   requires_climate_gate;      // Set by WorldGen if true
    float  climate_unlock_threshold;   // Set by WorldGen; compared against regional_climate_stress
    uint32_t required_tech_node;       // Tech node ID; must be researched for access

    // Accessor method
    bool is_accessible(const SimCell& province) const {
        if (base_accessible) return true;
        if (!requires_climate_gate) return false;

        float stress = province.climate_profile.regional_climate_stress;
        bool climate_ok = (stress >= climate_unlock_threshold);
        bool tech_ok = world_state->is_tech_unlocked(required_tech_node);
        return climate_ok && tech_ok;
    }
};
```

**Responsibility allocation:**

| System | Responsibility |
|---|---|
| **WorldGen** | Identify deposits in cold/permafrost provinces (latitude > 60°N in Earth analog); set `requires_climate_gate = true`; assign `climate_unlock_threshold` based on deposit geology; assign `required_tech_node` from the technology tree |
| **R&D** | Define the technology nodes referenced in `required_tech_node`; ensure these nodes unlock at appropriate eras (arctic drilling in Era 3–4; rare earth processing in Era 2–3); gate these nodes appropriately |
| **Climate** | Update `province.climate_profile.regional_climate_stress` each tick derived from `global_temperature_delta` |

**Access check timing:**
- Checked every tick when a facility tries to extract from a deposit
- Allows deposits to become accessible *mid-simulation* if both thaw and tech unlock occur
- Allows deposits to become inaccessible again if climate reverses (e.g., geoengineering cools the world) — but not if tech is already researched and active
- No retroactive changes: once a deposit is accessed and extraction begins, climate thaw is "locked in" for that deposit

**Integration with facility recipes:**
When a facility recipe calls for a resource from a deposit:
1. Check `ResourceDeposit::is_accessible()`
2. If false, the recipe is unavailable (greyed out) — player cannot build this facility type
3. If true, facility can operate normally
4. Extraction cost adjusts based on deposit properties (deeper permafrost = more expensive once thawed)

---

**Infrastructure damage:**
Extreme weather events can destroy or damage facilities. Each event writes damage to a random selection of facilities in the affected region. Damage requires repair cost and takes the facility offline for N ticks.

### Regulatory Response to Climate

```cpp
struct ClimateRegulation {
    std::string regulation_key;
    float       co2_threshold;
    float       political_pressure_req;
    RegionScope scope;
    float       implementation_tick_lag;
    float       carbon_tax_rate;
    float       emission_cap;
    float       renewable_mandate;
    std::vector<std::string> banned_goods;
};
```

**Regulatory trajectory (default, unmodified by player):**

| Era | Regulatory measure | Economic effect |
|---|---|---|
| Era 1 | Kyoto Protocol (weak; US non-participant) | Minimal; some regions get carbon reporting requirements |
| Era 2 | Carbon trading markets in some regions | Adds cost to heavy emitters; creates carbon credit market |
| Era 3 | Paris Agreement ratification | Broader carbon targets; renewable mandates in some regions |
| Era 4 | Net-zero commitments + EV mandates | ICE vehicle sales restricted in some regions; carbon tax rises |
| Era 5 | Carbon border adjustments + industrial mandates | Strands high-emission manufacturing; green premium reverses |
| Era 6 [EX] | Mandatory geoengineering governance | Players' geoengineering requires global treaty compliance |
| Era 7 [EX] | Adaptation mandates + tipping point protocols | Forced relocation subsidies; managed retreat from coastal zones |
| Era 8 [EX] | Planetary management treaties | Coordinated climate control; violations trigger sanctions |

### Strategic Implications

**The stranded asset problem:**
A player who maximizes fossil fuel investment in Era 1–2 will have highly profitable operations for 10–15 in-game years. By Era 4, those same assets face carbon taxes, regulatory restrictions, declining demand, and environmental liabilities. The optimal NPV strategy likely involves fossil fuels *early* and pivoting to clean technology *before* the regulatory hammer falls.

**First-mover advantage in clean tech:**
Solar panels are expensive and uncompetitive in 2000. A player who funds solar R&D in Era 1–2 and builds manufacturing capacity early will own a major cost advantage by Era 3 when the market turns competitive.

**Climate as a political tool:**
Regional climate stress raises NPC voter concern, which raises the political pressure required to ignore climate regulation. A player who controls polluting industries in a climate-stressed region will face increasingly hostile local politics.

#### NPC Political Motivation for Climate [CORE]

NPCs adopt climate regulation positions when ALL of the following conditions hold (Pass 5 Session 8):

1. **Regional climate impact exceeds threshold:** `regional_climate_impact > NPC_CLIMATE_AWARENESS_THRESHOLD` (default: 0.3)
2. **Education level meets floor:** `NPC.education_level >= CLIMATE_EDUCATION_FLOOR` (default: 3)
3. **Ideological or economic exposure alignment:**
   - `NPC.ideological_alignment` includes 'environmental' faction OR
   - `NPC.economic_exposure_to_climate > 0.2` (e.g., farmers, coastal workers, renewable energy operators)

**NPC Climate Legislation Proposal:**
Political NPCs (politicians, organizers, lobbyists) may propose climate legislation when:
- `voter_demand_for_climate_action > 0.4` in their constituent base

**Implementation note:** Regional climate impact is computed as `(climate_stress_current - baseline) × region_sensitivity`, where region sensitivity reflects geographic vulnerability (coastal zones, agricultural regions, and Arctic-adjacent areas have higher sensitivity).

> Test: When regional_climate_impact exceeds NPC_CLIMATE_AWARENESS_THRESHOLD (0.3) in a province, politician NPCs with voter_demand_for_climate_action > 0.4 in their constituency propose climate regulation legislation. If legislation passes, affected facilities must comply within REGULATION_COMPLIANCE_WINDOW = 180 ticks or face regulatory_violation evidence generation.
>
> Seed setup: Province with climate_impact = 0.35 (above threshold). Politician NPC with 60% constituency support for climate action. Assert: politician proposes climate regulation within 30 ticks. After legislation passes, non-compliant facilities generate regulatory_violation evidence after 180 ticks. Compliant facilities are unaffected.

**[EX] Geoengineering as power:**
In Eras 6–8, a player who controls or blockades stratospheric aerosol injection capability holds leverage over global climate outcomes. This is both a strategic asset and an enormous political liability — other factions will respond.

---

## Part 9 — R&D Data Structures for TDD Integration

```cpp
struct TechnologyNode {
    std::string   node_key;
    ResearchDomain domain;
    float         difficulty;
    uint8_t       era_available;
    std::vector<std::string> prerequisites;
    std::map<ResearchDomain, float> cross_domain_knowledge_requirements; // domain → min level
    TechNodeOutcome outcome_type;
    std::string   unlocks_good_key;
    std::string   unlocks_facility_key;
    // key_technology_node is NOT a field on TechnologyNode. FacilityRecipe.key_technology_node
    // is a data-file column populated per-recipe (see Commodities & Factories §Recipe File Format).
    // For product_unlock nodes, FacilityRecipe.key_technology_node is set to node_key in the
    // recipe CSV. For process_improvement and tier_advance nodes, it is empty. The field does not
    // live on TechnologyNode — it is a recipe property, not a technology-tree property.
    uint8_t       upgrades_facility_tier;
    bool          patentable;
    float         global_knowledge_contribution;
    std::string   scope_tag;             // "V1", "EX", "CORE"
    std::vector<std::string> stranded_asset_triggers;
    std::vector<std::string> npc_behavior_changes;
};

struct RnDFacility {
    uint32_t        facility_id;
    RnDFacilityType facility_type;
    ResearchDomain  primary_domain;
    float           facility_quality;
    uint32_t        researcher_capacity;
    std::vector<uint32_t> active_project_ids;
    std::vector<uint32_t> active_maturation_project_ids;  // separate from research projects
    std::vector<Patent>   held_patents;
    float           domain_specialization;
    FacilitySignals signals;       // canonical struct: TDD v29 §16.
                                   // Universal — all RnD facility types carry this field.
                                   // Skunkworks and criminal labs have elevated signal values;
                                   // legitimate universities and corporate labs have low-but-nonzero
                                   // power and foot_traffic signals reflecting real research activity.
};

// Per-actor technology portfolio — field on BusinessEntity (TDD)
struct ActorTechnologyState {
    uint32_t  business_id;
    std::map<std::string, TechHolding> holdings;  // node_key → TechHolding (see Part 3.5)

    bool      has_researched(const std::string& node_key) const;
    bool      has_commercialized(const std::string& node_key) const;
    float     maturation_of(const std::string& node_key) const;  // 0.0 if not held
};

// Global state: tracks world-level knowledge and patent arbitration only.
// Does NOT track who has what — that lives in ActorTechnologyState on each BusinessEntity.
struct GlobalTechnologyState {
    // Which nodes have been researched by anyone — for domain knowledge attribution
    // and to signal that a tech category is now possible in the world.
    std::map<std::string, uint32_t>  first_researched_by;      // node_key → holder_id

    // Which nodes have been commercialized by anyone — product is now market-observable
    // and eligible for reverse engineering by other actors.
    std::map<std::string, uint32_t>  first_commercialized_by;  // node_key → holder_id

    std::map<std::string, float>     domain_knowledge_levels;
    std::vector<Patent>              active_patents;
    std::vector<SchedulingProcess>   active_scheduling_reviews;
    SimulationEra                    current_era;
    float                            global_co2_index;
    ClimateState                     climate_state;
};
```

`GlobalTechnologyState` lives inside `WorldState` (TDD Section 10). Updated during tick step 26.

`ActorTechnologyState` lives on `BusinessEntity` (TDD). Updated during tick step 26 alongside GlobalTechnologyState.

**Struct changes vs. v2.0:**
- `TechnologyNode`: `key_technology_node` field removed (v2.2 correction — see I-07). This field does not belong on `TechnologyNode`; it is a per-recipe data property populated in `FacilityRecipe.key_technology_node` via the recipe CSV. See Commodities & Factories §Recipe File Format.
- `RnDFacility` gains `active_maturation_project_ids` (maturation runs alongside research)
- `RnDFacility.opsec: OPSECProfile` replaced by `signals: FacilitySignals` (v2.2 correction — universal signal struct, canonical definition in TDD §16)
- `GlobalTechnologyState.unlocked_nodes: map<string,bool>` replaced by `first_researched_by` and `first_commercialized_by` — per-actor holdings are now in `ActorTechnologyState`
- `ActorTechnologyState` is new; added to `BusinessEntity` in TDD

**Tick step note:** Maturation projects advance each tick inside tick step 26 alongside standard research projects. `maturation_ceiling` is recomputed at era transition for all holdings in all actors.

---

## Part 10 — R&D and the Moddability System

Modders can extend the R&D system through data files:

**Technology node files** (`/data/technology/nodes/*.csv`):
- Add new research nodes with new prerequisites and outcomes
- Add new eras (if modding a future-timeline scenario)
- Modify difficulty, era availability, and knowledge contributions of existing nodes

**Climate regulation files** (`/data/climate/regulations.csv`):
- Modify CO₂ thresholds for regulatory triggers
- Add new regulation types
- Adjust regional vulnerability multipliers

**Research domain files** (`/data/rnd/domains.csv`):
- Add new research domains for mod-specific industries
- Adjust knowledge decay rates

The R&D project logic, patent mechanics, and climate accumulation formulas are engine code. Modders configure parameters; logic is not moddable in V1.

**Historical scenario mods:** A modder could create a "start in 1970" scenario by adjusting era baselines, removing already-invented goods from the starting state, and configuring a different CO₂ starting level. This is fully data-driven.

**Future scenario mods:** A modder could add Era 11 ("Post-Singularity") or extend any domain chain by adding new node CSV entries with `era_available = 11`.

---

## Part 11 — Technology Node Registry

This registry is the machine-readable flat table of all V1 tech nodes. EX nodes are included for engine registration but marked. Scope: Bootstrapper reads this to generate repository interface stubs and scenario test cases.

**Format:** `node_key | domain | era_available | difficulty | outcome_type | patentable | scope | prerequisites (semicolon-delimited) | stranded_assets_triggered`

### Energy Systems Nodes

| node_key | domain | era | difficulty | outcome_type | patentable | scope | prerequisites | strands |
|---|---|---|---|---|---|---|---|---|
| hydraulic_fracturing_v2 | energy_systems | 1 | 1.0 | product_unlock | yes | V1 | — | — |
| directional_drilling_opt | energy_systems | 1 | 0.8 | process_improvement | yes | V1 | hydraulic_fracturing_v2 | — |
| heavy_oil_processing | energy_systems | 2 | 1.2 | process_improvement | yes | V1 | hydraulic_fracturing_v2 | — |
| arctic_offshore_drilling | energy_systems | 3 | 2.0 | facility_unlock | yes | V1 | directional_drilling_opt | — |
| liion_cell_chemistry | energy_systems | 1 | 1.5 | product_unlock | yes | V1 | — | — |
| battery_management_system | energy_systems | 2 | 1.0 | product_unlock | yes | V1 | liion_cell_chemistry | — |
| ev_powertrain_integration | energy_systems | 2 | 2.0 | product_unlock | yes | V1 | battery_management_system | — |
| electric_vehicle | energy_systems | 2 | 3.0 | product_unlock | yes | V1 | ev_powertrain_integration | — |
| ev_charging_station | energy_systems | 3 | 1.5 | facility_unlock | yes | V1 | electric_vehicle | — |
| grid_battery_array | energy_systems | 3 | 4.0 | product_unlock | yes | V1 | ev_charging_station | — |
| vehicle_to_grid | energy_systems | 4 | 1.0 | process_improvement | yes | V1 | grid_battery_array | — |
| solid_state_battery_research | energy_systems | 3 | 3.0 | prerequisite | yes | V1 | liion_cell_chemistry | — |
| solid_state_battery_cell | energy_systems | 4 | 5.0 | product_unlock | yes | V1 | solid_state_battery_research | liion_cell_chemistry |
| pv_cell_efficiency | energy_systems | 1 | 1.0 | prerequisite | no | V1 | — | — |
| solar_panel_cost_competitive | energy_systems | 2 | 2.0 | process_improvement | yes | V1 | pv_cell_efficiency | — |
| solar_farm | energy_systems | 2 | 1.5 | facility_unlock | no | V1 | solar_panel_cost_competitive | — |
| grid_scale_solar | energy_systems | 3 | 2.0 | process_improvement | no | V1 | solar_farm | — |
| perovskite_solar | energy_systems | 4 | 3.0 | process_improvement | yes | V1 | grid_scale_solar | — |
| tandem_solar_cells | energy_systems | 5 | 4.0 | process_improvement | yes | V1 | perovskite_solar | — |
| wind_turbine_efficiency | energy_systems | 1 | 1.0 | prerequisite | no | V1 | — | — |
| offshore_wind_platform | energy_systems | 2 | 2.5 | facility_unlock | yes | V1 | wind_turbine_efficiency | — |
| grid_scale_wind | energy_systems | 3 | 2.0 | process_improvement | no | V1 | offshore_wind_platform | — |
| green_hydrogen_electrolysis | energy_systems | 3 | 2.0 | product_unlock | yes | V1 | grid_scale_solar;grid_scale_wind | — |
| industrial_fuel_cell | energy_systems | 4 | 3.0 | product_unlock | yes | V1 | green_hydrogen_electrolysis | — |
| carbon_capture_industrial | energy_systems | 3 | 2.0 | process_improvement | yes | V1 | — | — |
| smr_design | energy_systems | 2 | 5.0 | facility_unlock | yes | V1 | — | — |
| thorium_fuel_cycle | energy_systems | 3 | 3.5 | process_improvement | yes | V1 | smr_design | — |
| enhanced_oil_recovery_co2 | energy_systems | 3 | 1.5 | process_improvement | yes | V1 | hydraulic_fracturing_v2 | — |
| helium_separation_plant | chemical_synthesis | 3 | 1.5 | product_unlock | yes | V1 | — | — |
| structural_battery_composite | energy_systems | 5 | 4.0 | product_unlock | yes | EX | solid_state_battery_cell | — |
| solar_storage_hybrid | energy_systems | 6 | 2.0 | product_unlock | yes | EX | tandem_solar_cells;grid_battery_array | — |
| floating_offshore_wind | energy_systems | 4 | 4.0 | facility_unlock | yes | EX | offshore_wind_platform | — |
| hydrogen_vehicle | energy_systems | 5 | 3.5 | product_unlock | yes | EX | industrial_fuel_cell | — |
| hydrogen_pipeline_network | energy_systems | 6 | 4.0 | facility_unlock | no | EX | hydrogen_vehicle | — |
| green_ammonia | energy_systems | 7 | 3.0 | product_unlock | yes | EX | hydrogen_pipeline_network | fossil_ammonia |
| dac_prototype | energy_systems | 4 | 4.0 | product_unlock | yes | EX | carbon_capture_industrial | — |
| dac_at_scale | energy_systems | 5 | 6.0 | process_improvement | yes | EX | dac_prototype | — |
| gen4_fast_reactor | nuclear_engineering | 4 | 8.0 | facility_unlock | yes | EX | smr_design | — |
| isotope_separation | nuclear_engineering | 4 | 3.0 | product_unlock | yes | EX | gen4_fast_reactor | — |
| deep_sea_mining | mechanical_engineering | 3 | 2.5 | facility_unlock | yes | EX | — | — |
| fusion_ignition_research | nuclear_engineering | 4 | 25.0 | prerequisite | no | EX | — | — |
| fusion_demonstration | nuclear_engineering | 5 | 15.0 | product_unlock | yes | EX | fusion_ignition_research | — |
| fusion_pilot_plant | nuclear_engineering | 6 | 20.0 | facility_unlock | yes | EX | fusion_demonstration | — |
| commercial_fusion_grid | nuclear_engineering | 7 | 30.0 | process_improvement | no | EX | fusion_pilot_plant | all_fossil_generation;all_nuclear_fission |
| fusion_abundance | nuclear_engineering | 8 | 10.0 | process_improvement | no | EX | commercial_fusion_grid | — |
| spacebased_solar | space_systems | 8 | 30.0 | product_unlock | yes | EX | space_systems.orbital_construction;tandem_solar_cells | — |

### Semiconductor Physics Nodes

| node_key | domain | era | difficulty | outcome_type | patentable | scope | prerequisites |
|---|---|---|---|---|---|---|---|
| process_node_90nm | semiconductor_physics | 1 | 3.0 | tier_advance | yes | V1 | — |
| process_node_65nm | semiconductor_physics | 2 | 4.0 | tier_advance | yes | V1 | process_node_90nm |
| process_node_45nm | semiconductor_physics | 2 | 5.0 | tier_advance | yes | V1 | process_node_65nm |
| process_node_22nm | semiconductor_physics | 3 | 6.0 | tier_advance | yes | V1 | process_node_45nm |
| process_node_7nm | semiconductor_physics | 4 | 8.0 | tier_advance | yes | V1 | process_node_22nm |
| process_node_5nm | semiconductor_physics | 4 | 9.0 | tier_advance | yes | V1 | process_node_7nm |
| process_node_3nm | semiconductor_physics | 5 | 10.0 | tier_advance | yes | V1 | process_node_5nm |
| gate_all_around | semiconductor_physics | 5 | 8.0 | process_improvement | yes | V1 | process_node_3nm |
| process_node_2nm | semiconductor_physics | 5 | 12.0 | tier_advance | yes | V1 | gate_all_around |
| gan_power_electronics | semiconductor_physics | 3 | 2.0 | product_unlock | yes | V1 | — |
| sic_power_electronics | semiconductor_physics | 4 | 2.5 | product_unlock | yes | V1 | gan_power_electronics |
| wide_bandgap_platform | semiconductor_physics | 5 | 4.0 | process_improvement | yes | V1 | sic_power_electronics |
| 3d_chip_stacking | semiconductor_physics | 3 | 3.0 | process_improvement | yes | V1 | — |
| monolithic_3d_ic | semiconductor_physics | 4 | 5.0 | process_improvement | yes | V1 | 3d_chip_stacking |
| photonic_interconnects | semiconductor_physics | 5 | 6.0 | process_improvement | yes | EX | monolithic_3d_ic |
| photonic_processor | semiconductor_physics | 6 | 8.0 | product_unlock | yes | EX | photonic_interconnects |
| neuromorphic_chip | semiconductor_physics | 6 | 7.0 | product_unlock | yes | EX | photonic_processor |
| neuromorphic_general | semiconductor_physics | 7 | 10.0 | product_unlock | yes | EX | neuromorphic_chip |
| nisq_quantum_processor | semiconductor_physics | 4 | 8.0 | product_unlock | yes | EX | — |
| quantum_error_correction | semiconductor_physics | 5 | 15.0 | process_improvement | yes | EX | nisq_quantum_processor |
| fault_tolerant_qc | semiconductor_physics | 6 | 20.0 | product_unlock | yes | EX | quantum_error_correction |
| quantum_supremacy_commercial | semiconductor_physics | 7 | 15.0 | process_improvement | yes | EX | fault_tolerant_qc |
| quantum_internet_node | semiconductor_physics | 8 | 12.0 | product_unlock | yes | EX | quantum_supremacy_commercial |
| molecular_logic_gate | semiconductor_physics | 9 | 25.0 | product_unlock | yes | EX | materials_science.atomically_precise_fabrication |
| molecular_scale_processor | semiconductor_physics | 10 | 30.0 | product_unlock | yes | EX | molecular_logic_gate |

### Materials Science Nodes

| node_key | domain | era | difficulty | outcome_type | patentable | scope | prerequisites |
|---|---|---|---|---|---|---|---|
| carbon_fiber_opt | materials_science | 1 | 1.0 | process_improvement | yes | V1 | — |
| aerospace_carbon_fiber | materials_science | 2 | 2.0 | product_unlock | yes | V1 | carbon_fiber_opt |
| automotive_carbon_fiber | materials_science | 3 | 1.5 | process_improvement | yes | V1 | aerospace_carbon_fiber |
| carbon_fiber_mass_production | materials_science | 4 | 3.0 | product_unlock | no | V1 | automotive_carbon_fiber |
| hsla_steel_opt | materials_science | 1 | 0.8 | process_improvement | no | V1 | — |
| ahss_steel | materials_science | 2 | 1.5 | product_unlock | yes | V1 | hsla_steel_opt |
| high_entropy_alloy | materials_science | 3 | 3.0 | product_unlock | yes | V1 | ahss_steel |
| advanced_polymer | materials_science | 1 | 1.0 | prerequisite | no | V1 | — |
| engineering_thermoplastic | materials_science | 2 | 1.5 | product_unlock | yes | V1 | advanced_polymer |
| graphene_research | materials_science | 3 | 3.0 | prerequisite | yes | V1 | engineering_thermoplastic |
| graphene_composite | materials_science | 4 | 4.0 | product_unlock | yes | V1 | graphene_research |
| aerogel_insulation | materials_science | 3 | 2.0 | product_unlock | yes | V1 | — |
| structural_aerogel | materials_science | 4 | 3.0 | product_unlock | yes | V1 | aerogel_insulation |
| refractory_hea | materials_science | 5 | 4.0 | product_unlock | yes | EX | high_entropy_alloy |
| cnt_fiber_material | materials_science | 5 | 6.0 | product_unlock | yes | EX | graphene_composite |
| cnt_structural_cable | materials_science | 7 | 10.0 | product_unlock | yes | EX | cnt_fiber_material |
| space_elevator_cable | materials_science | 7 | 10.0 | product_unlock | yes | EX | cnt_structural_cable |
| metamaterial_acoustic | materials_science | 4 | 2.0 | product_unlock | yes | EX | — |
| metamaterial_optical | materials_science | 5 | 5.0 | product_unlock | yes | EX | metamaterial_acoustic |
| programmable_metamaterial | materials_science | 6 | 6.0 | product_unlock | yes | EX | metamaterial_optical |
| self_healing_coating | materials_science | 5 | 2.5 | product_unlock | yes | EX | advanced_polymer |
| structural_self_healing | materials_science | 6 | 5.0 | product_unlock | yes | EX | self_healing_coating |
| atomically_precise_fabrication | materials_science | 8 | 25.0 | process_improvement | yes | EX | graphene_composite |
| molecular_assembler | materials_science | 9 | 40.0 | product_unlock | yes | EX | atomically_precise_fabrication |
| generalized_molecular_assembly | materials_science | 10 | 50.0 | process_improvement | no | EX | molecular_assembler |

### Biotechnology Nodes

| node_key | domain | era | difficulty | outcome_type | patentable | scope | prerequisites |
|---|---|---|---|---|---|---|---|
| combinatorial_drug_screening | biotechnology | 1 | 1.0 | process_improvement | no | V1 | — |
| targeted_small_molecule | biotechnology | 1 | 2.0 | product_unlock | yes | V1 | combinatorial_drug_screening |
| biologic_drugs | biotechnology | 2 | 3.0 | product_unlock | yes | V1 | targeted_small_molecule |
| gene_therapy_ex_vivo | biotechnology | 3 | 5.0 | product_unlock | yes | V1 | biologic_drugs |
| mrna_therapeutics | biotechnology | 4 | 6.0 | product_unlock | yes | V1 | gene_therapy_ex_vivo |
| drought_resistant_crops | biotechnology | 2 | 1.5 | process_improvement | yes | V1 | — |
| precision_fermentation | biotechnology | 3 | 2.0 | product_unlock | yes | V1 | — |
| in_vivo_gene_editing | biotechnology | 5 | 7.0 | product_unlock | yes | EX | mrna_therapeutics |
| whole_genome_therapy | biotechnology | 6 | 12.0 | product_unlock | yes | EX | in_vivo_gene_editing |
| polygenic_correction | biotechnology | 6 | 8.0 | product_unlock | yes | EX | whole_genome_therapy |
| senolytic_drug | biotechnology | 5 | 5.0 | product_unlock | yes | EX | targeted_small_molecule |
| telomere_therapy | biotechnology | 6 | 8.0 | product_unlock | yes | EX | senolytic_drug |
| longevity_package | biotechnology | 7 | 15.0 | product_unlock | yes | EX | telomere_therapy |
| radical_longevity | biotechnology | 8 | 25.0 | product_unlock | yes | EX | longevity_package |
| post_biological_substrate | biotechnology | 9 | 50.0 | product_unlock | yes | EX | radical_longevity;cognitive_science.mind_upload |
| lab_grown_meat_prototype | biotechnology | 4 | 4.0 | product_unlock | yes | EX | precision_fermentation |
| lab_grown_meat_parity | biotechnology | 5 | 5.0 | process_improvement | no | EX | lab_grown_meat_prototype |
| synthetic_food_system | biotechnology | 6 | 8.0 | facility_unlock | yes | EX | lab_grown_meat_parity |
| crispr_therapy | synthetic_biology | 4 | 4.0 | product_unlock | yes | EX | in_vivo_gene_editing |
| metabolic_engineering | synthetic_biology | 5 | 5.0 | product_unlock | yes | EX | crispr_therapy |
| programmable_microbiome | synthetic_biology | 6 | 6.0 | product_unlock | yes | EX | metabolic_engineering |
| whole_genome_synthesis | synthetic_biology | 6 | 8.0 | product_unlock | yes | EX | metabolic_engineering |
| contained_gene_drive | synthetic_biology | 7 | 10.0 | product_unlock | yes | EX | whole_genome_synthesis |
| biological_factory | synthetic_biology | 7 | 12.0 | facility_unlock | yes | EX | programmable_microbiome |
| synthetic_ecosystem | synthetic_biology | 8 | 20.0 | product_unlock | yes | EX | biological_factory |
| designer_organism | synthetic_biology | 9 | 30.0 | product_unlock | yes | EX | synthetic_ecosystem |
| post_biological_ecology | synthetic_biology | 10 | 40.0 | process_improvement | no | EX | designer_organism |

### Software and AI Nodes

| node_key | domain | era | difficulty | outcome_type | patentable | scope | prerequisites |
|---|---|---|---|---|---|---|---|
| cdn_infrastructure | software_systems | 1 | 1.0 | product_unlock | yes | V1 | — |
| cloud_compute_platform | software_systems | 2 | 3.0 | product_unlock | yes | V1 | cdn_infrastructure |
| microservices_architecture | software_systems | 3 | 1.5 | process_improvement | no | V1 | cloud_compute_platform |
| edge_compute_network | software_systems | 4 | 3.0 | product_unlock | yes | V1 | microservices_architecture |
| 5g_base_station | software_systems | 4 | 3.5 | product_unlock | yes | V1 | edge_compute_network |
| ml_research | software_systems | 2 | 2.0 | prerequisite | no | V1 | — |
| deep_learning_framework | software_systems | 3 | 3.0 | product_unlock | yes | V1 | ml_research |
| computer_vision | software_systems | 3 | 2.5 | process_improvement | yes | V1 | deep_learning_framework |
| nlp_service | software_systems | 3 | 3.0 | product_unlock | yes | V1 | deep_learning_framework |
| large_language_model | software_systems | 4 | 8.0 | product_unlock | yes | V1 | nlp_service |
| gpu_compute_cluster | software_systems | 4 | 5.0 | product_unlock | yes | V1 | large_language_model |
| social_media_platform | social_media_platforms | 2 | 2.0 | product_unlock | no | V1 | cdn_infrastructure |
| content_algorithm | social_media_platforms | 2 | 1.5 | process_improvement | yes | V1 | social_media_platform |
| targeted_ad_inventory | social_media_platforms | 3 | 2.0 | product_unlock | yes | V1 | content_algorithm |
| influencer_platform | social_media_platforms | 3 | 1.5 | product_unlock | no | V1 | targeted_ad_inventory |
| quantum_resistant_crypto | information_security | 4 | 4.0 | product_unlock | yes | V1 | — |
| autonomous_ai_agent | software_systems | 5 | 8.0 | product_unlock | yes | EX | gpu_compute_cluster |
| agc_precursor | software_systems | 6 | 30.0 | product_unlock | yes | EX | autonomous_ai_agent;semiconductor_physics.neuromorphic_chip |
| economically_autonomous_ai | software_systems | 7 | 20.0 | product_unlock | yes | EX | agc_precursor |
| post_agi_industrial | software_systems | 8 | 15.0 | process_improvement | no | EX | economically_autonomous_ai |
| synthetic_content_feed | social_media_platforms | 4 | 3.0 | product_unlock | yes | EX | content_algorithm;large_language_model |
| immersive_social_platform | social_media_platforms | 5 | 4.0 | product_unlock | yes | EX | synthetic_content_feed |
| persistent_digital_social | social_media_platforms | 6 | 6.0 | product_unlock | yes | EX | immersive_social_platform |
| neural_social_interface | social_media_platforms | 7 | 10.0 | product_unlock | yes | EX | persistent_digital_social;cognitive_science.bidirectional_bci |

### Space Systems Nodes [EX]

| node_key | domain | era | difficulty | outcome_type | patentable | scope | prerequisites |
|---|---|---|---|---|---|---|---|
| reusable_launch_vehicle | space_systems | 3 | 5.0 | product_unlock | yes | EX | — |
| crewed_spacecraft | space_systems | 4 | 6.0 | product_unlock | yes | EX | reusable_launch_vehicle |
| commercial_orbital_station | space_systems | 4 | 8.0 | facility_unlock | yes | EX | crewed_spacecraft |
| lunar_delivery_service | space_systems | 5 | 6.0 | product_unlock | yes | EX | reusable_launch_vehicle |
| lunar_base | space_systems | 5 | 10.0 | facility_unlock | yes | EX | lunar_delivery_service |
| lunar_helium3 | space_systems | 6 | 8.0 | product_unlock | yes | EX | lunar_base |
| lunar_manufacturing | space_systems | 6 | 6.0 | product_unlock | yes | EX | lunar_base |
| mars_base | space_systems | 7 | 15.0 | facility_unlock | yes | EX | lunar_base |
| asteroid_survey | space_systems | 5 | 6.0 | product_unlock | yes | EX | reusable_launch_vehicle |
| asteroid_mining_near | space_systems | 6 | 12.0 | product_unlock | yes | EX | asteroid_survey |
| asteroid_mining_industrial | space_systems | 7 | 15.0 | product_unlock | yes | EX | asteroid_mining_near |
| asteroid_bulk_metal | space_systems | 7 | 15.0 | product_unlock | no | EX | asteroid_mining_industrial |
| outer_belt_mining | space_systems | 8 | 20.0 | product_unlock | yes | EX | asteroid_mining_industrial |
| microgravity_manufacturing | space_systems | 6 | 5.0 | product_unlock | yes | EX | commercial_orbital_station |
| orbital_factory | space_systems | 7 | 10.0 | facility_unlock | yes | EX | microgravity_manufacturing |
| orbital_construction | space_systems | 8 | 8.0 | process_improvement | yes | EX | orbital_factory |
| orbital_megastructure | space_systems | 9 | 40.0 | product_unlock | yes | EX | orbital_construction |
| space_elevator | advanced_transportation | 8 | 40.0 | facility_unlock | no | EX | materials_science.space_elevator_cable;space_systems.orbital_construction |

### Cognitive Science Nodes [EX]

| node_key | domain | era | difficulty | outcome_type | patentable | scope | prerequisites |
|---|---|---|---|---|---|---|---|
| consumer_neurofeedback | cognitive_science | 4 | 2.0 | product_unlock | yes | EX | — |
| medical_bci_motor | cognitive_science | 5 | 6.0 | product_unlock | yes | EX | consumer_neurofeedback |
| high_bandwidth_neural_recording | cognitive_science | 5 | 5.0 | prerequisite | no | EX | medical_bci_motor |
| cognitive_enhancement_bci | cognitive_science | 6 | 8.0 | product_unlock | yes | EX | high_bandwidth_neural_recording |
| memory_augmentation_bci | cognitive_science | 6 | 8.0 | product_unlock | yes | EX | high_bandwidth_neural_recording |
| bidirectional_bci | cognitive_science | 7 | 15.0 | product_unlock | yes | EX | cognitive_enhancement_bci;memory_augmentation_bci |
| neural_mesh_persistent | cognitive_science | 8 | 20.0 | product_unlock | yes | EX | bidirectional_bci |
| mind_upload_prototype | cognitive_science | 9 | 50.0 | product_unlock | yes | EX | neural_mesh_persistent;semiconductor_physics.fault_tolerant_qc |
| substrate_independent | cognitive_science | 10 | 30.0 | product_unlock | no | EX | mind_upload_prototype |

### Geoengineering Nodes [EX]

| node_key | domain | era | difficulty | outcome_type | patentable | scope | prerequisites |
|---|---|---|---|---|---|---|---|
| sai_research | geoengineering | 5 | 5.0 | prerequisite | no | EX | — |
| sai_pilot | geoengineering | 6 | 8.0 | process_improvement | no | EX | sai_research |
| sai_coordinated | geoengineering | 7 | 15.0 | process_improvement | no | EX | sai_pilot |
| ocean_iron_fertilization | geoengineering | 6 | 4.0 | process_improvement | no | EX | — |
| ocean_alkalinity_enhancement | geoengineering | 7 | 5.0 | process_improvement | no | EX | climate_and_environment.enhanced_weathering |
| marine_cloud_brightening | geoengineering | 7 | 6.0 | process_improvement | no | EX | — |
| planetary_albedo_mgmt | geoengineering | 8 | 25.0 | process_improvement | no | EX | space_systems.orbital_megastructure |
| deliberate_climate_control | geoengineering | 9 | 30.0 | process_improvement | no | EX | sai_coordinated;planetary_albedo_mgmt |
| planetary_atmospheric_eng | geoengineering | 10 | 50.0 | process_improvement | no | EX | deliberate_climate_control |

---

## Part 12 — Open Questions

**Resolved since v2.0:**
- ~~WorldGen §8.7 unlock condition "Thorium reactor tech" mismatched era~~ — `thorium_fuel_cycle` moved to Era 3 V1 (`energy_systems` domain, prereq `smr_design`). WorldGen §8.7 and tech tree now agree.
- ~~WorldGen §8.7 lists five unlock conditions with no matching node key~~ — Added: `heavy_oil_processing`, `arctic_offshore_drilling`, `deep_sea_mining`, `helium_separation_plant`, `isotope_separation`.
- ~~Shale oil: Era 1 tech vs Era 2 WorldGen resource gate conflict~~ — Resolved as intentional decoupling. Tech is researchable Era 1; WorldGen deposit `era_available = 2` is a separate physical/regulatory constraint. Both are now annotated.
- ~~GlobalTechnologyState.unlocked_nodes as global boolean~~ — Replaced by `first_researched_by` / `first_commercialized_by`; per-actor holdings in `ActorTechnologyState`.

**Open:**

1. **Climate tipping points.** Should `global_co2_index` have non-linear tipping points where `climate_feedback_rate` jumps? This would model real-world climate science more accurately and create a compelling late-game threat.

2. **Regional climate winners.** Some regions benefit near-term from climate change (northern regions get longer growing seasons, Arctic shipping routes open as ice melts). These effects should be represented.

3. **R&D as a tradeable good.** Should R&D output itself be tradeable? Currently R&D produces TechHoldings and domain knowledge, not a tradeable commodity. Deferred.

4. **Technology export controls [EX].** Era 4+ chip restrictions between nation-analog factions. Requires a national technology export policy system. Flagging for EX scope.

5. **Longevity medicine economic model [EX].** When NPCs live 30+ years longer, labor market, pension system, and political succession models need updating. Significant cross-document change for Era 7+ design phase.

6. **Space as a SimulationRegion [EX].** Asteroid belt and orbital space need new `SimulationRegion` instances. WorldGen v0.16 covers Earth only; space geography is an EX-phase expansion.

7. **Post-scarcity economic modeling [EX].** When fusion and molecular assembly arrive (Era 8–9), the scarcity assumptions underlying the price system change. A new economic paradigm model is needed.

8. **Criminal synthetic biology threat model [EX].** Era 8+ criminal biotech needs a biosecurity regulatory system, containment failure event type, and NPC response behaviors.

9. **AI as economic actor [EX].** When `economically_autonomous_ai` is unlocked (Era 7), the current NPC struct (which assumes biological entities) needs extension to represent AI-held contracts, IP, and transactions.

10. **Interplanetary financial instruments [EX].** Era 7+ multi-region instruments across Earth/lunar/Mars economies break the real-time assumption when light-speed delays become material.

11. **maturation_ceiling data population.** `MATURATION_ERA_CEILING_BASE` and `MATURATION_ENHANCERS` tables (`/data/technology/maturation_ceilings.csv`, `/data/technology/maturation_enhancers.csv`) need values for all V1 product_unlock nodes. Currently only example values exist. This is required before any simulation-accurate testing of technology quality dynamics.

12. ~~**key_technology_node population in FacilityRecipe.**~~ **CLOSED (v2.3):** `key_technology_node` column added to the recipe CSV schema in Commodities & Factories v2.3 §Recipe File Format. Content population (filling values for all technology-intensive recipes) is a data task tracked separately.

13. ~~**Permafrost thaw dual-gate ownership.**~~ **CLOSED (v2.3):** R&D Part 8 now owns the Permafrost Thaw Dual-Gate Formula under Climate Integration section. WorldGen seeds deposit parameters; Climate system updates stress indices; R&D evaluates accessibility each tick.

14. **Planet-age nuclear path branching [EX].** WorldGen §2357 notes that old-planet campaigns (uranium-scarce) need a direct path to thorium that bypasses Gen IV fast reactors. The current chain still requires `smr_design → thorium_fuel_cycle`, which is available at Era 3 — but the Gen IV branch still requires uranium. Needs explicit branch documentation for non-Earth-analog campaigns.

---

---

## Change Log

**v2.2 → v2.3 (Pass 5 Session 4)**
- Added Climate Integration section "Permafrost Thaw Dual-Gate Formula [CORE]" to Part 8 (The Climate System)
- Specifies ownership: R&D system owns the `ResourceDeposit::accessible()` formula; WorldGen seeds parameters; Climate system updates stress index
- Includes dual-gate formula (climate threshold AND technology requirement); variable table; climate unlock thresholds by deposit type
- Specifies data structure additions to ResourceDeposit struct
- Specifies responsibility allocation across WorldGen, R&D, and Climate systems
- Defines access check timing and integration with facility recipes
- Closes Open Question 13 (permafrost thaw dual-gate ownership)
- Updated companion document version references (GDD v1.7, TDD v29, Commodities & Factories v2.3, WorldGen v0.18)

**v2.2 (Pass 3 Session 18 — Cross-Doc Audit Fixes)**

Resolves findings P-01, I-07, and I-01 from the Pass 3 post-Session 17 cross-document consistency audit.

### P-01 — RnDFacility signals migration

- Replaced `OPSECProfile opsec` with `FacilitySignals signals` on `RnDFacility` (Part 9).
- `OPSECProfile` was the pre-Session 13 struct name. TDD v18 §16 defines `FacilitySignals` as the canonical universal signal struct. R&D v2.1 was updated post-Session 17 but had not incorporated the Session 13 rename.
- Signal field is universal — all RnD facility types carry it. Skunkworks and criminal labs carry elevated values; legitimate research facilities carry low-but-nonzero values.

### I-07 — TechnologyNode.key_technology_node removed

- Removed `key_technology_node` field from `TechnologyNode` struct (Part 9).
- The field was redundant: for `product_unlock` nodes it would equal `node_key` (pointing to itself); it was not a relationship to a different node. `FacilityRecipe.key_technology_node` is a data-file column populated per-recipe in the recipe CSV — it is a recipe property, not a technology-tree node property.
- Updated "Struct changes vs. v2.0" summary block to reflect this correction and the signals rename.

### OQ 12 closed

- Open Question 12 (`key_technology_node` population in FacilityRecipe) marked CLOSED. Column added to recipe CSV schema in Commodities & Factories v2.3. Content population (filling values for all tech-intensive recipes) is a separate data task.

### I-01 — Companion document version references updated

- Header and footer updated: TDD v16 → v18, Commodities & Factories v2.1 → v2.3.

---

*EconLife — Research, Development & Technology Progression v2.2*
*250-year tech tree: 10 eras, 19 research domains, ~255 technology nodes*
*V1 scope: Eras 1–5, 12 domains, ~90 nodes | EX scope: Eras 6–10, 7 additional domains, ~165 nodes*
*Technology lifecycle model: per-actor TechHolding, maturation, commercialization as BusinessAction*
*Companion document to TDD v29, GDD v1.7, Commodities & Factories v2.3, WorldGen v0.18*
