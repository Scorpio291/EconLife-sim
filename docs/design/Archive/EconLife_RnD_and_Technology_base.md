# EconLife — Research, Development & Technology Progression
*Companion document to GDD v1.2, Technical Design Document v17, and Commodities & Factories v2.1*
*Version 1.1 — Pass 3 Session 6G: Open questions 1, 2, 3 resolved*

---

## Purpose

This document specifies three deeply connected systems:

1. **The Era System** — the game begins in the year 2000 and time advances, changing what technology exists, what regulations apply, and how the world economy is structured. This is the framework within which everything else operates.

2. **Research & Development** — the mechanism by which technology advances. Without R&D, manufacturing is static. With it, players can be ahead of history or behind it.

3. **The Climate System** — a slow-accumulating global condition driven by industrial activity, modeled from 2000 baselines. It creates long-horizon strategic pressure that reshapes markets, triggers regulations, and strands assets — exactly as it has done in the real world.

These three systems are documented together because they cannot be designed in isolation. The era system defines the timeline. R&D is how players navigate it. Climate is the long-run consequence they cannot entirely escape.

---

## Part 1 — The Era System

### Design Intent

The game starts in **January 2000**. This was chosen deliberately:

- 25+ years of documented economic history serves as the baseline simulation trajectory — the world will unfold roughly as it did if players do nothing extraordinary
- Players have enormous tech progression ahead of them — smartphones, social media, EVs, renewable energy, AI hardware, mRNA medicine
- Climate change is still in its early policy phase — the consequences are coming, the window to influence them is open
- The world is still highly globalized and optimistic — the 2008 financial crisis, COVID, and deglobalization are future shocks players will either navigate or cause

Simulated time passes at a configurable rate. The default: **1 game year = approximately 6 real hours of play** (very rough; tuned during prototype phase). This gives a full 25-year campaign timeline of roughly 150 real hours — a long game, like a Civilization run.

### Eras

The simulation divides history into five eras. Era transitions are not hard cutoffs — they are thresholds on accumulated global conditions that trigger new events, regulations, and technology unlocks. A game where players invest heavily in clean energy and push for global carbon policy might delay Era 4's harsher conditions. A game where players maximize extraction and resist regulation might accelerate them.

```cpp
enum class SimulationEra : uint8_t {
    era_1_turn_of_millennium,  // 2000–2007: Globalization peak, pre-crisis, pre-smartphone
    era_2_disruption,          // 2007–2013: Financial crisis, smartphone revolution, social media
    era_3_acceleration,        // 2013–2019: App economy, early EVs, shale revolution, gig economy
    era_4_fracture,            // 2019–2024: COVID shock, deglobalization, EV mainstream, AI emergence
    era_5_transition,          // 2024+: Energy transition, AI integration, supply chain rebalancing
};
```

Era transitions are triggered by a combination of:
- Simulated calendar year (primary)
- `global_climate_stress` accumulation (can accelerate Era 4/5 thresholds)
- Global economic conditions (a severe simulated financial crisis can trigger Era 2 early)
- Player actions (a player who monopolizes and crashes a sector can accelerate structural change)

### What Changes Between Eras

| Condition | Era 1 | Era 2 | Era 3 | Era 4 | Era 5 |
|---|---|---|---|---|---|
| Mobile technology | Feature phones | First smartphones | App economy | Mobile dominance | Maturation |
| Internet infrastructure | Dial-up / early broadband | Broadband | Cloud computing | Edge / 5G | AI-native |
| EV market | Nonexistent | Early experiments | Early adopters | Early mainstream | Mass market |
| Solar/wind cost | Very expensive | Declining | Competitive in some regions | Cost-competitive | Default option |
| Climate regulation | Kyoto (weak) | Growing awareness | Carbon markets emerging | Paris+targets | Mandates/carbon taxes |
| Global supply chains | Hyperglobalized | Stressed (crisis) | Re-established | Fracturing | Regionalized |
| AI / automation | Early ML research | Data economy | Cloud AI | Generative AI | Industrial AI |
| Drug scheduling | Reactive | Analog act frameworks | Accelerated scheduling | Real-time analogue bans | Predictive scheduling |
| Financial regulation | Pre-crisis light | Crisis response (heavy) | Post-crisis compliance | Fintech disruption | Digital asset frameworks |

### The Living World

Era transitions are not instant. They propagate through the simulation as:

- **New NPC behaviors** — NPC businesses adopt new strategies; some obsolete themselves, new entrants appear
- **Regulatory changes** — new laws governing emissions, financial instruments, data, narcotics
- **Market structure shifts** — demand for fossil fuels peaks; renewables become competitive; EV sub-assembly markets appear
- **Stranded assets** — businesses built on Era 1 assumptions face declining revenue in Era 4
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
- **Failures** — partial results or dead ends; not wasted (see "Productive Failure" below)

**R&D is probabilistic, not deterministic.** Every research project has a probability distribution over outcomes. The same project, run twice, may succeed spectacularly once and produce a partial result the other time. This is by design: it models how actual research works, it creates variance that makes R&D a strategic bet rather than a purchase, and it creates interesting moments when a rival NPC stumbles into a breakthrough before you.

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
                           // Player uses this to hide R&D spend from competitors
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
};
```

**Domain knowledge accumulation:**
```
domain_knowledge_level += publications_this_tick × PUBLICATION_KNOWLEDGE_INCREMENT
domain_knowledge_level += technology_transfer_events_this_tick × TRANSFER_INCREMENT
domain_knowledge_level decays at KNOWLEDGE_DECAY_RATE per tick (obsolescence)
```

A project in a high-knowledge domain has a higher base success probability. Early in the game, most domains are at low-to-medium levels (matching real-world 2000 baselines). Players who fund academic research help raise domain knowledge for everyone — including competitors.

---

## Part 3 — Technology Tree

### Design Philosophy

The technology tree is not a static branching diagram. It is a set of **technology nodes** that become *researchable* when:
1. The era conditions are met (calendar year + global conditions)
2. Prerequisite nodes are unlocked
3. A facility with sufficient domain expertise exists
4. A research project targeting that node is funded and succeeds

Technology nodes are defined in data files (moddable). Each node specifies:
- **Era available** (earliest possible — can't research smartphones in 2000)
- **Prerequisites** (what must already be unlocked)
- **Domain** (which research domain the project falls in)
- **Difficulty** (effort points required)
- **Outcome type** (product unlock, tech tier advance, process improvement, etc.)
- **Patentable** (yes/no)

### Era 1 Starting State (Year 2000)

What exists at game start — no research required:

**Available Tech Tiers by Category:**
- Basic extraction (mining, farming, logging): Tier 2 (mature)
- Steel and metals processing: Tier 2 (mature industry)
- Petroleum refining: Tier 2 (mature)
- Chemical processing: Tier 2 (mature)
- Basic manufacturing (vehicles, appliances): Tier 2
- Advanced manufacturing (precision machinery): Tier 3 (frontier)
- Semiconductor fabrication: Tier 3 (frontier, expensive)
- Consumer electronics: Tier 2–3

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

**NOT available at Year 2000 (must be researched):**
- Smartphones and app platforms
- Lithium-ion batteries at EV scale
- Electric vehicles
- OLED displays
- Competitive solar panels
- Large-scale wind generation
- mRNA pharmaceuticals
- Social media platforms as goods
- AI hardware accelerators
- Designer drugs (era 1 analogue frameworks allow research)
- Advanced synthetic opioids

---

### Technology Tree — Selected Chains

*(Full tree is data-file defined; these illustrate the depth and connection logic)*

**Chain: Smartphones**
```
[Era 1] Mobile chipsets (Tier 3 semiconductors)
    → [Era 2] ARM processor architecture license
        → [Era 2] Mobile operating system
            → [Era 2] Smartphone (product unlock: mobile_phone_smartphone)
                → [Era 2] App platform (product unlock: app_platform)
                    → [Era 3] App economy infrastructure
```

**Chain: Electric Vehicles**
```
[Era 1] Lithium-ion cell chemistry (R&D in energy_systems)
    → [Era 1] Li-ion battery cell (product unlock: battery_cell_liion, higher energy density)
        → [Era 2] Battery management system
            → [Era 2] EV powertrain (product unlock: ev_drivetrain)
                → [Era 2] Electric vehicle (product unlock: electric_vehicle, proper)
                    → [Era 3] Fast charging infrastructure
                        → [Era 3] Grid-scale battery storage
```

**Chain: Renewable Energy**
```
[Era 1] Photovoltaic cell efficiency (research)
    → [Era 2] Cost-competitive solar panel (process improvement: 40% cost reduction)
        → [Era 2] Solar farm (new facility type)
            → [Era 3] Grid-scale solar
                → [Era 4] Solar-battery integrated storage

[Era 1] Wind turbine efficiency
    → [Era 2] Offshore wind platform
        → [Era 3] Grid-scale wind
```

**Chain: Semiconductors (historical)**
```
[Era 1] 130nm process node (Tier 3, available at start)
    → [Era 1] 90nm process node (research, ~2003)
        → [Era 2] 65nm process node (~2006)
            → [Era 2] 45nm process node (~2008)
                → [Era 3] 22nm process node (~2012)
                    → [Era 4] 7nm process node (~2018)
                        → [Era 4] 5nm process node (~2020)
                            → [Era 5] 3nm process node (~2022)
```
Each node: reduces energy consumption, increases output quality ceiling, reduces per-unit cost. Requires massive capital expenditure at each step. This is why semiconductors are a natural monopoly business — the capex per node generation makes entry prohibitive for all but the largest players.

**Chain: Advanced Pharmaceuticals**
```
[Era 1] Combinatorial drug screening (research)
    → [Era 1] Targeted small molecule drugs (product unlock: pharmaceutical_rx_targeted)
        → [Era 2] Biologic drugs (protein-based therapeutics)
            → [Era 3] Gene therapy
                → [Era 4] mRNA therapeutics (product unlock: mrna_pharmaceutical)
```

**Chain: Criminal R&D — Designer Drugs**
```
[Era 1] Novel synthetic cannabinoid (product unlock: synthetic_cannabinoid)
    → Research: scheduling status unknown → legal until scheduled
        → [Clock runs]: legislative response schedules the compound
            → [Race]: research next compound before scheduling takes effect
```
See Section 7 (Criminal R&D) for full scheduling race mechanics.

**Chain: Social Media and Platforms**
```
[Era 1] Early internet forums and communities (available at start)
    → [Era 2] Social network platform (product unlock: social_media_platform)
        → [Era 2] Content algorithm (process improvement: user retention +X%)
            → [Era 3] Advertising targeting (product unlock: targeted_advertising_inventory)
                → [Era 3] Influencer economy
                    → [Era 4] AI-generated content
```

**Chain: AI and Automation**
```
[Era 2] Machine learning (academic research domain)
    → [Era 3] Computer vision (process improvement: quality control automation)
        → [Era 3] Natural language processing
            → [Era 4] Large language models (product unlock: AI services)
                → [Era 4] AI hardware accelerator (product unlock: gpu_compute_cluster)
                    → [Era 5] AI-native industrial automation
```

---

## Part 4 — Project Mechanics

### The Research Project Lifecycle

```cpp
struct ResearchProject {
    std::string project_key;
    uint32_t    facility_id;         // which R&D facility is running this
    ResearchDomain domain;
    std::string target_node_key;     // which technology tree node
    float       difficulty;          // effort points required to complete
    float       progress;            // accumulated effort points so far
    uint32_t    researchers_assigned;
    float       funding_per_tick;    // budget allocation
    float       success_probability; // recalculated each tick
    uint32_t    started_tick;
    bool        is_secret;           // patent intent; secret projects cannot use published literature freely

    ResearchOutcome expected_outcome;
    ResearchOutcome actual_outcome;  // set on completion
};

enum class ResearchOutcome : uint8_t {
    success_full,          // full unlock; best case
    success_partial,       // partial progress; can be combined with another run
    unexpected_discovery,  // unintended breakthrough in adjacent area
    failure_dead_end,      // dead end; but produces publications increasing domain knowledge
    failure_setback,       // catastrophic; destroys progress, costs researchers
    patent_preempted,      // rival filed a patent on the same node while you were working
};
```

**Progress accumulation per tick:**
```
progress_per_tick = researchers_assigned
                  × researcher_quality_avg
                  × facility_quality_modifier
                  × domain_knowledge_bonus        // higher domain knowledge = faster research
                  × funding_adequacy              // underfunded projects slow down

domain_knowledge_bonus = 1.0 + (domain_knowledge_level × DOMAIN_KNOWLEDGE_BONUS_COEFF)
funding_adequacy = min(1.0, actual_funding / required_funding_per_tick)
```

**Success probability per tick:**
```
base_success_probability = BASE_RESEARCH_SUCCESS_RATE    // 0.75 when difficulty is met
adjusted = base_success_probability
         × facility_success_modifier
         × domain_knowledge_modifier
         × secrecy_penalty (secret projects: -0.10, cannot access published literature freely)
```

**Named constants:**
```
BASE_RESEARCH_SUCCESS_RATE       = 0.75
DOMAIN_KNOWLEDGE_BONUS_COEFF     = 0.30   // 30% faster in highly-developed domain
UNEXPECTED_DISCOVERY_PROBABILITY = 0.05   // 5% chance per completion of adjacent find
PATENT_PREEMPTION_CHECK_RATE     = 0.02   // 2% chance per tick rival files same node
```

### Productive Failure

A failed project is not a total loss. On `failure_dead_end`:
- All researchers gain domain experience (their individual skill increases)
- A publication is generated, raising domain knowledge globally
- Progress is lost, but a "known dead end" record means the domain knowledge covers that approach — future projects in the same domain benefit

On `failure_setback`:
- Researchers may quit or transfer (NPC memory event: "witnessed research failure")
- Some progress is destroyed (cannot be recovered)
- If the project was secret, rivals who were watching may gain intelligence about what was being researched

### Researcher NPCs

Researchers are NPCs with specialized skills. Key attributes:

```cpp
struct ResearcherProfile {
    NPCRole            role;           // researcher_scientist, researcher_engineer, etc.
    ResearchDomain     primary_domain;
    float              domain_skill;   // 0.0–1.0; improves with each project
    float              creativity;     // increases unexpected_discovery probability
    float              productivity;   // base progress contribution per tick
    uint32_t           employer_id;    // current facility
    bool               has_nda;        // non-disclosure agreement in place
    float              loyalty;        // low loyalty = IP theft risk
};
```

Researchers are NPCs who move between employers, publish papers, attend conferences, and can be poached. A rival company's lead researcher is a target for headhunting — or corporate espionage. Researchers with low loyalty are potential insider threats: they may share proprietary research with former employers, sell it to criminal buyers (for illicit chemistry research), or become whistleblowers if they're researching something they consider harmful.

---

## Part 5 — Patents

### Patent Mechanics

When a project completes with `success_full` and the project was marked `is_secret`, the player (or NPC) can file a patent.

```cpp
struct Patent {
    std::string  patent_key;
    std::string  technology_node_key;   // what is patented
    uint32_t     holder_id;            // business that holds the patent
    uint32_t     filed_tick;
    uint32_t     expires_tick;         // filed_tick + PATENT_DURATION_TICKS
    float        patent_strength;      // 0.0–1.0; how defensible the patent is
    bool         is_contested;         // someone is challenging it
    std::vector<uint32_t> licensees;   // businesses currently paying for a license
};
```

**Named constants:**
```
PATENT_DURATION_TICKS     = 20 × TICKS_PER_YEAR    // 20-year patent term (standard)
PATENT_STRENGTH_BASE      = 0.70                    // base defensibility
PATENT_CHALLENGE_COST     = HIGH                    // challenging a patent requires legal resources
```

**Patent effects:**
- Unlicensed use of a patented technology is an `exposure` event — generates evidence tokens, creates a hostile motivated rival (the patent holder)
- Licensing generates passive income for the holder and creates an Obligation structure between licensee and licensor
- Patent expiry converts the technology to public domain — all can use without license, domain knowledge level increases significantly

**Patent strategies:**
- **Patent thicket**: File patents on many adjacent nodes simultaneously, making it expensive for rivals to navigate around you
- **Defensive filing**: Patent your own research primarily to protect against being sued, not to exploit commercially
- **Licensing empire**: Build an R&D business model around licensing rather than manufacturing
- **Patent acquisition**: Buy patents from bankrupt businesses or independent inventors — a financial vehicle for market control
- **Design-around**: Invest R&D in achieving the same outcome via a different technical path (avoids the patent, adds difficulty and cost)

### Patent Theft and Espionage

A player or NPC can task an agent with stealing research. This produces the patentable breakthrough faster — but generates significant exposure risk. If caught:
- Criminal charges (industrial espionage)
- Civil suit from victim (large financial obligation)
- Diplomatic/trade consequences if the theft crosses national boundaries
- Permanent hostile relationship with the victimized firm and their allies

---

## Part 6 — Technology Diffusion

Technology does not stay proprietary forever. It diffuses through several mechanisms:

**1. Patent expiry** — After 20 in-game years, all patented technology enters the public domain. Domain knowledge rises sharply.

**2. Academic publication** — University research projects produce publications. These are immediately public. Each publication raises domain knowledge level, accelerating everyone's research in that domain.

**3. Technology licensing** — Voluntary licensed diffusion. The licensor profits; the licensee gains access faster than independent R&D would provide.

**4. Reverse engineering** — An NPC or player purchases a finished product, disassembles it, and researches the underlying technology. Slower than licensing, faster than original research, legally grey in many jurisdictions.

**5. Talent mobility** — When a researcher leaves a company, they carry tacit knowledge with them. NDAs slow but do not stop this. After NDA expiry, researchers can apply their skills freely.

**6. Regulatory disclosure** — Some regulatory processes (pharmaceutical approval, environmental permits) require technical disclosure that effectively patents the approach publicly.

**7. Industrial accidents** — A catastrophic facility failure (explosion, chemical spill, criminal lab incident) may expose what the facility was producing, giving rivals intelligence about what was being developed.

**Diffusion rate by technology type:**

| Technology type | Diffusion speed | Primary mechanism |
|---|---|---|
| Software / platform | Very fast | Reverse engineering, open source |
| Consumer electronics | Fast | Disassembly, patent expiry |
| Industrial processes | Medium | Patent expiry, talent mobility |
| Pharmaceutical | Slow | Strict patent enforcement, regulatory moats |
| Semiconductor fabrication | Very slow | Massive capital requirements, export controls |
| Criminal synthesis | Fast | Street network sharing, seized lab analysis |

---

## Part 7 — Criminal R&D

### The Scheduling Race

Designer drugs are legal until they are scheduled. Criminal chemists are in a permanent race against the legislative response cycle.

```
[Criminal lab] researches novel compound in illicit_chemistry domain
                        ↓
[Compound synthesized] → enters market as unscheduled substance
                        ↓
[Regulators detect] → scheduling review initiated (takes time proportional to political system speed)
                        ↓
[Scheduling enacted] → compound becomes controlled; existing inventory is now criminal
                        ↓
[Criminal R&D] must already have next compound in pipeline or face supply gap
```

**Key data structures:**
```cpp
struct SchedulingProcess {
    std::string compound_key;
    uint32_t    detection_tick;     // when regulators first noted the compound
    uint32_t    review_duration;    // ticks until scheduled (varies by political system)
    float       political_delay;    // lobbying and regulatory capture can extend this
    bool        is_enacted;
};

struct CriminalRnDProject : public ResearchProject {
    std::string  target_compound_key;
    bool         compound_is_legal;     // at time of research
    uint32_t     scheduling_risk_level; // how closely regulators are watching this domain
};
```

**Strategic options:**
- **Stay ahead of scheduling**: Maintain a pipeline of 2–3 compounds in various research stages; when one is scheduled, pivot to the next
- **Delay scheduling**: Use political influence to slow the regulatory process (expensive and generates exposure)
- **Market the grey period**: Flood the market during the window between discovery and scheduling to maximize legal-window revenue
- **Transition to pharmaceutical**: A criminal chemist operation with sufficiently good research may be able to patent a compound as a legitimate pharmaceutical — crossing back into the legal economy with the same synthesis knowledge

**Analytical analogue acts** (Era 2+): As governments get smarter, scheduling frameworks expand to cover entire structural classes of compounds, not just specific molecules. This dramatically narrows the designer drug R&D space and increases the domain expertise required.

### Criminal Research Infrastructure

Criminal R&D is hidden within legitimate facilities or run in fully clandestine labs. The OPSEC profile of a criminal research lab:

```
opsec_profile = {
    power_consumption_signal:  0.5–0.8   (high; synthesis equipment is power-hungry)
    chemical_waste_signal:     0.6–0.9   (very high; synthesis produces characteristic waste)
    foot_traffic_signal:       0.2–0.3   (low; researchers are few and arrive irregularly)
    olfactory_signal:          0.3–0.7   (varies by compound; some synthesis is odorless)
    explosion_risk_per_tick:   0.0005    (lower than a meth lab; more controlled)
}
```

Embedding criminal R&D inside a legitimate pharmaceutical company (the `skunkworks` facility type with illicit_chemistry domain) dramatically reduces OPSEC signals but creates a new risk: a legitimate researcher discovers the illicit work and becomes a whistleblower.

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

The modeling does not need to be climatologically precise — it needs to be *economically plausible* and *strategically consequential*. Players should feel the pressure build over decades, not arrive as a sudden event.

---

### Global CO₂ Index

The primary climate variable is `global_co2_index` — a simplified proxy for atmospheric carbon concentration, starting at the historical 2000 baseline.

```cpp
struct ClimateState {
    float global_co2_index;          // starts at 1.0 (= 370 ppm); grows over time
    float global_temperature_delta;  // degrees above 2000 baseline; derived from CO₂ index
    float climate_feedback_rate;     // non-linear feedback multiplier; rises with temp
};
```

**CO₂ index update per tick:**
```
co2_emissions_this_tick = sum over all regions:
    (fossil_fuel_consumption × FOSSIL_CO2_COEFFICIENT)
    + (industrial_process_emissions × config.climate.industrial_co2_coefficient)
    - (carbon_capture_installed × config.climate.capture_efficiency)
    - (forest_coverage × config.climate.forest_sequestration_rate)

co2_removals_this_tick = carbon_capture + sequestration

global_co2_index += (co2_emissions_this_tick - co2_removals_this_tick) × config.climate.co2_persistence
global_co2_index *= (1.0 + climate_feedback_rate × config.climate.feedback_sensitivity)

global_temperature_delta = (global_co2_index - 1.0) × config.climate.climate_sensitivity_factor
```

**Named constants (all to `climate_config.json`):**
```
co2_persistence                 = 0.9998     // CO₂ stays in atmosphere for centuries
feedback_sensitivity            = 0.001      // small but compounding feedback loop
climate_sensitivity_factor      = 1.5        // degrees per CO₂ index unit (simplified)
forest_sequestration_rate       = 0.02       // per unit of forest coverage per tick
capture_efficiency              = 0.80       // fraction of rated capture capacity actually removed
industrial_co2_coefficient      = 0.0005     // CO₂ index units per unit of industrial emission per tick
regional_sensitivity_multiplier = 0.10       // scales global temperature delta into regional climate stress
farm_stress_sensitivity         = 0.40       // yield reduction per unit of regional climate stress
drought_yield_penalty           = 0.30       // additional yield reduction per tick of active drought
```

**Moddability:** All constants are loaded from `climate_config.json` at startup. The climate accumulation formulas are engine code; the calibration constants are data. Modders can adjust climate speed, severity, agricultural impact, and carbon cycle behavior without recompilation. The `co2_persistence` value is the primary lever for controlling how fast climate change unfolds across the 25-year simulation arc.

---

### Regional Climate Stress

The global temperature delta propagates into **regional climate stress** — how much the climate has changed from historical norms in each specific region.

```cpp
struct RegionClimateState {
    float regional_climate_stress;    // 0.0–1.0; local deviation from 2000 baseline
    float drought_probability;        // per-tick chance of drought event
    float flood_probability;          // per-tick chance of flood/storm event
    float wildfire_probability;       // per-tick chance of wildfire disruption
    float crop_yield_modifier;        // multiplier on farm output (< 1.0 as stress rises)
    float sea_level_delta;            // affects coastal extraction and infrastructure
    float extreme_heat_days;          // workdays lost to dangerous heat (labor productivity)
};
```

**Regional climate stress update:**
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

---

### Climate Effects on the Economy

**Agricultural output:**
```
farm_output_actual = farm_output_base
    × (1.0 - regional_climate_stress × config.climate.farm_stress_sensitivity)
    × (1.0 - drought_active × config.climate.drought_yield_penalty)
    × fertilizer_input_modifier
```

Severe climate stress doesn't just reduce yield — it makes yield *unpredictable*. The variance of farm output increases with regional_climate_stress, which matters for supply chains that depend on consistent agricultural input. Crop insurance becomes valuable. Diversifying crop type and sourcing region becomes strategically important.

**Extraction costs:**
- Rising sea levels increase cost of coastal extraction (offshore oil, port-dependent operations)
- Permafrost thaw opens new Arctic resources while making existing northern infrastructure unstable
- Extreme heat raises cooling costs and reduces outdoor labor productivity

**Infrastructure damage:**
Extreme weather events (drought, flood, wildfire, storm surge) can destroy or damage facilities. Each event writes damage to a random selection of facilities in the affected region. Damage requires repair cost and takes the facility offline for N ticks.

**Energy demand:**
Rising temperatures increase cooling demand (air conditioning) and decrease heating demand. In hot regions, this raises peak electricity load, straining energy infrastructure and raising energy prices in summer months.

---

### Regulatory Response to Climate

As `global_co2_index` crosses threshold values, regulatory responses trigger. These are not random events — they follow a model of growing political pressure that scales with both objective climate damage and NPC voter response.

```cpp
struct ClimateRegulation {
    std::string regulation_key;
    float       co2_threshold;            // global_co2_index level that makes this possible
    float       political_pressure_req;   // how much NPC political mobilization is needed
    RegionScope scope;                    // global / national / regional
    float       implementation_tick_lag;  // political process takes time even after threshold
    float       carbon_tax_rate;          // per unit of CO₂ emitted
    float       emission_cap;             // max allowed emissions per region per tick
    float       renewable_mandate;        // fraction of energy that must be renewable
    std::vector<std::string> banned_goods; // goods that become regulated/banned
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

Players can **accelerate** regulatory response by funding climate NGOs, lobbying for carbon policy, and building political capital with climate-aligned politicians. They can **delay** it by funding climate denial, lobbying against regulation, and capturing regulatory agencies. Both are visible to NPC journalists and NGO investigators — both generate exposure risk.

---

### Strategic Implications

**The stranded asset problem:**
A player who maximizes fossil fuel investment in Era 1–2 will have highly profitable operations for 10–15 in-game years. By Era 4, those same assets face: carbon taxes raising operating costs, regulatory restrictions on sales, declining demand as EVs and renewables mature, and environmental liabilities from accumulated regional damage. The optimal strategy from a pure NPV standpoint likely involves fossil fuels *early* and pivoting to clean technology *before* the regulatory hammer falls — exactly the dilemma real oil majors faced.

**First-mover advantage in clean tech:**
Solar panels are expensive and uncompetitive in 2000. A player who funds solar R&D in Era 1–2 and builds manufacturing capacity early will own a major cost advantage by Era 3 when the market turns competitive. They will also have accumulated brand equity and patent portfolios. This mirrors the story of companies like First Solar, BYD, and CATL — who committed early and dominated when the market arrived.

**Climate as a political tool:**
Regional climate stress raises NPC voter concern about the environment, which raises the political pressure required to ignore climate regulation. A player who controls polluting industries in a climate-stressed region will face increasingly hostile local politics — rising community response levels, more aggressive regulatory enforcement, and NPC politicians who see attacking the player as a winning campaign issue.

---

## Part 9 — R&D Data Structures for TDD Integration

These structures supplement the Technical Design Document Section 20 (R&D Data Structures) (to be added in Pass 3).

```cpp
struct TechnologyNode {
    std::string   node_key;
    ResearchDomain domain;
    float         difficulty;
    uint8_t       era_available;      // minimum era for this to be researchable
    std::vector<std::string> prerequisites;
    TechNodeOutcome outcome_type;
    std::string   unlocks_good_key;   // if product unlock
    uint8_t       upgrades_facility_tier; // if tech tier advance
    bool          patentable;
    float         global_knowledge_contribution; // what publishing results gives to domain
};

struct RnDFacility {
    uint32_t        facility_id;
    RnDFacilityType facility_type;
    ResearchDomain  primary_domain;
    float           facility_quality;       // 0.0–1.0
    uint32_t        researcher_capacity;    // max NPCs that can work here
    std::vector<uint32_t> active_project_ids;
    std::vector<Patent>   held_patents;
    float           domain_specialization;  // bonus for projects in primary_domain
    OPSECProfile    opsec;                  // nonzero only for criminal_lab and skunkworks
};

struct GlobalTechnologyState {
    std::map<std::string, bool>  unlocked_nodes;          // node_key → unlocked by anyone
    std::map<std::string, float> domain_knowledge_levels; // domain → 0.0–1.0
    std::vector<Patent>          active_patents;
    std::vector<SchedulingProcess> active_scheduling_reviews;
    SimulationEra                current_era;
    float                        global_co2_index;
    ClimateState                 climate_state;
};
```

**GlobalTechnologyState lives inside WorldState** (Section 10 of Technical Design). It is updated during tick step 26 (after all production, trade, and NPC decisions have been processed for the tick).

---

## Part 10 — R&D and the Moddability System

Modders can extend the R&D system through data files:

**Technology node files** (`/data/technology/nodes/*.csv`):
- Add new research nodes with new prerequisites and outcomes
- Add new eras (if modding a future-timeline scenario)
- Modify difficulty, era availability, and knowledge contributions of existing nodes

**Climate regulation files** (`/data/climate/regulations.csv`):
- Modify CO₂ thresholds for regulatory triggers
- Add new regulation types (e.g., plastic bans, deforestation restrictions)
- Adjust regional vulnerability multipliers

**Research domain files** (`/data/rnd/domains.csv`):
- Add new research domains for mod-specific industries
- Adjust knowledge decay rates

The R&D project logic, patent mechanics, and climate accumulation formulas are engine code. Modders configure parameters; the logic is not moddable in V1.

**Historical scenario mods**: A modder could create a "start in 1970" scenario by adjusting era baselines, removing already-invented goods from the starting state, and configuring a different CO₂ starting level. The system is designed to support this without engine changes — it's all data-driven.

---

## Part 11 — Open Questions

1. ~~**Tick resolution for climate.**~~ **RESOLVED (Pass 3 Session 6G):** Per-tick accumulation with small constants. Year-end UI summary event (a climate report visible to the player) is a UX decision — the math runs per-tick regardless. The tick constants keep the change slow enough to feel like strategic pressure.

2. ~~**NPC R&D investment logic.**~~ **RESOLVED (Pass 3 Session 4C, TDD v5 Section 5):** Full NPC business strategic decision matrix specified by behavioral profile. `cost_cutter` profile spends minimally on R&D; `fast_expander` allocates above baseline; `defensive_incumbent` prefers regulatory lobbying over technology investment and will not voluntarily upgrade facilities when lobbying is viable. Decision rates and thresholds are in `economy.json`.

3. ~~**Technology equilibrium / NPC upgrade timing.**~~ **RESOLVED (Pass 3 Session 4C):** `defensive_incumbent` profile never upgrades voluntarily — lobbying to suppress competitive tech is preferred. All other profiles upgrade when cash reserves exceed the upgrade cost threshold (configurable per profile in `economy.json`). This creates realistic market windows where a tech-advanced player can undercut NPCs on a capital planning lag.

4. **Climate tipping points.** Should the climate system have non-linear tipping points (e.g., at `global_co2_index` = 1.8, the feedback rate jumps, accelerating further accumulation)? This would model real-world climate science more accurately and create a compelling late-game threat. Adds complexity but may be worth it for Era 4+ gameplay.

5. **Regional climate winners.** Some regions benefit in the near-term from climate change (northern regions get longer growing seasons, new shipping routes open as Arctic ice melts). These effects should be represented — climate change is not uniformly bad in the short run, which is part of why it's politically contested.

6. **R&D as a good.** Should R&D output itself be a tradeable good? Currently R&D produces technology nodes (unlocks, tier advances) not a tradeable commodity. An alternative: R&D labs produce a `research_output` good with a quality rating, which is then "consumed" by a manufacturing upgrade process. This is more modular but adds complexity.

7. **Intellectual property theft and geopolitics.** In Era 4+, technology export controls (e.g., US chip restrictions on China-analog factions) could be a major mechanic. This requires a national technology export policy system that doesn't currently exist in the design. Flagging for potential EX scope addition.

---

*EconLife — Research, Development & Technology Progression v1.1 — Pass 3 Session 6G complete*
*R&D system, technology tree, climate model — V1 scope with EX markers where indicated*
