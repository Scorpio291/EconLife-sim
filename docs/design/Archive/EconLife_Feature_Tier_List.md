# EconLife — Feature Tier List
*Companion document to GDD v0.9*
*Living document — updated in sync with GDD*
Cross-document fixes applied: C1

---

## How to Read This Document

Every system in the GDD is assigned to one of three tiers:

**V1 — Ships with the base game.** Non-negotiable for the core loop to function or for the game to be coherent. Cutting any V1 item requires a design review, not just a production decision.

**EX — Post-launch expansion.** Deepens the game but is not required for v1 to stand on its own. Should be designed with v1 in mind so expansion integration is clean rather than retrofitted.

**CUT — Not pursued.** Either scope-prohibitive, technically premature, or a distraction from what makes the game distinctive. Can be revisited at any point but is not on any roadmap.

Each tier entry includes a **rationale** and, where relevant, **dependencies** — systems that must exist before this one can be built.

---

## Simulation Foundation

| System | Tier | Rationale |
|---|---|---|
| Daily simulation tick | V1 | Everything runs on it |
| NPC memory log | V1 | Core of emergent narrative |
| NPC motivation model | V1 | Core of emergent narrative |
| NPC relationship scores (directional) | V1 | Required for trust, obligation, fear mechanics |
| NPC knowledge map | V1 | Required for whistleblowing, evidence, journalism |
| NPC risk tolerance | V1 | Determines whether NPCs act on what they know |
| NPC behavior generation engine | V1 | The thing that makes NPCs agents not state machines |
| NPC delay principle / consequence queue | V1 | Without this, consequences feel arbitrary |
| Significant NPC promotion from background population | V1 | Needed for union organizers, witnesses, investigators to emerge |
| Background population cohort simulation | V1 | Required for labor markets, voting, consumer demand |
| Procedural world generation | V1 | Without it you have one world |
| Configurable world parameters | EX | V1 ships with one well-tuned default world; full parameter control is an expansion feature |
| Multi-nation world generation | EX | V1 is one nation; multi-nation is expansion scope |

---

## World Structure

| System | Tier | Rationale |
|---|---|---|
| Region attributes (demographics, resources, infrastructure) | V1 | Foundation of every regional simulation |
| Dynamic regional conditions (stability, inequality, crime rate) | V1 | Required for World Response System |
| One nation, 3–5 regions | V1 | Defined scope for V1 |
| Multiple nations | EX | Depends on: V1 nation working well |
| Dynamic diplomatic relationships between nations | EX | Depends on: multiple nations |
| War as simulation failure mode | EX | Depends on: multiple nations; too much scope for V1 |
| Near-future setting (2040s–2060s) | V1 | Baked into world generation parameters |
| Climate-redistributed agricultural productivity | V1 | Affects resource distribution; core to setting |
| Automation-displaced labor baseline | V1 | Affects labor market and inequality baseline |
| Pervasive surveillance infrastructure (nation-variable) | V1 | Affects criminal OPSEC systems |
| CBDC / crypto landscape | V1 | Affects money laundering mechanics |

---

## Player Character

| System | Tier | Rationale |
|---|---|---|
| Character creation (background, traits, starting region) | V1 | Entry point |
| Character stats (Health, Age, Wealth, Reputation, Exposure, Obligation, Skills) | V1 | Core tracking |
| Skill leveling (by doing) and skill rust (by neglect) | V1 | Simple but important for character identity |
| Health degradation model | V1 | Feeds lifespan and aging systems |
| Aging and lifespan | V1 | Required for legacy and succession to matter |
| Health as situational engagement modifier | V1 | Replaces energy token system |
| Terminal illness phase | V1 | Required for meaningful succession preparation |
| Sudden death from violence | V1 | Required for meaningful personal security system |
| Personal life (residence, relationships, children) | V1 | Required for heir system and late-game trust relationships |
| Scene cards for personal life events | V1 | Depends on: scene card system |
| Romantic relationship as significant NPC | V1 | Requires full NPC model |
| Children aging and mentorship | V1 | Required for heir system |
| Early-life relationship NPCs | V1 | Important for obligation-free trust; can be thin in V1 |
| Substance use / addiction risk for player character | EX | Depends on: addiction system; V1 tracks health effects only |

---

## Career Paths

| System | Tier | Rationale |
|---|---|---|
| Employee path | V1 | Starting point for most players; scene cards for work events |
| Entrepreneur / Business Owner path | V1 | Core loop |
| Investor path (stock market, real estate, bonds) | V1 | Required for late-game wealth management |
| Hostile takeover mechanics | EX | Depends on: investor path; adds depth post-launch |
| Short-selling rivals | EX | Depends on: investor path |
| Politician path (city council → head of state) | V1 | Required for political endgame |
| Appointed political roles | EX | Depends on: politician path; can be unlocked post-launch |
| Union Leader path | EX | Compelling but not required for V1 core loop; depends on: labor system |
| Infiltrator / Investigator path | EX | Depends on: criminal economy; high complexity, expansion scope |
| Criminal Operator path (drug supply chain, money laundering) | V1 | Part of V1 criminal economy |
| Workforce Management (Section 6.8) | V1 | Required for operations and whistleblower systems |

---

## Facility Design

| System | Tier | Rationale |
|---|---|---|
| Top-down abstract facility planning | V1 | Core design system |
| Factory design and bottleneck simulation | V1 | Flagship facility type |
| Office design | V1 | Required for business and political operations |
| Restaurant design | V1 | Good early-game entry point |
| Farm design | V1 | Required for agricultural supply chain |
| Extraction facilities (mine, oil well) | V1 | Required for raw resource layer |
| Processing facilities (smelter, refinery) | V1 | Required for supply chain stack |
| Criminal facilities (lab, distribution point) | V1 | Required for criminal economy |
| Retail stores | V1 | Simple; needed for consumer goods chain |
| Power plants | EX | **V1 decision: energy is modeled as a regional utility cost paid per-tick by facilities, not as a buildable facility.** Crude oil, natural gas, and coal are V1 extractable resources (part of the geological resource layer) but their processing into electricity is abstracted. A refinery has an energy cost; the region has an energy price; the player pays it. Building power plants that affect regional energy price is expansion scope. |
| Geological resource layer | V1 | Full list as in GDD Section 8.3. **Energy resources (crude oil, natural gas, thermal coal, uranium) are included as extractable and tradeable goods. They are NOT processed into electricity in V1 — they are exported or sold to regional utilities at market price.** |
| Biological resource layer | V1 | Full list |
| Agricultural processing | V1 | Required for food supply chain |
| Metallurgy and petroleum refining | V1 | Required for industrial supply chain |
| Chemical processing | V1 | Required for pharmaceutical / drug precursor overlap |
| Heavy manufacturing | V1 | Required for industrial economy |
| Consumer goods manufacturing | V1 | Required for consumer demand simulation |
| Pharmaceutical manufacturing | V1 | Required for drug precursor overlap |
| Distribution layer (trucking, rail, shipping, warehousing) | V1 | Required for supply chain to function |
| Pipeline infrastructure | EX | Extreme capital cost, niche use case; expansion |
| Air freight | EX | High-value perishables; expansion |
| NPC businesses as active simulation participants | V1 | Required for competition to mean anything |
| NPC business behavioral profiles | V1 | Depends on: NPC businesses |
| NPC business bankruptcy and consolidation | V1 | Depends on: NPC businesses |
| Stock market (company share prices driven by real NPC business performance) | V1 | Required for investor path; prices are derived from NPC business revenue/profit each tick |
| Real estate market (dynamic regional pricing) | V1 | Required for investor path and residence system |
| Commodities trading | V1 | Required for investor path; falls out of spot price model |
| Government bonds | V1 | Required for investor path and governing budget system |
| NPC scheduling and calendar availability | V1 | Required for scheduling friction to be real; NPCs have their own calendar that the scheduling system queries |
| Informal / black market economy layer | V1 | Required for criminal economy to have a price signal |
| Import/export price ceiling and floor | V1 | Required for economy model to be correct |
| Deposit depletion over time | V1 | Resource scarcity is a long-game pressure |
| Environmental consequences of extraction | V1 | Required for community response and regulation systems |
| Renewable energy components as manufactured goods | EX | Interesting but not V1 critical |
| Defense and aerospace manufacturing | CUT | Scope-prohibitive; no design payoff in V1 |

---

## Knowledge Economy

| System | Tier | Rationale |
|---|---|---|
| R&D system (corporate labs, project uncertainty, breakthroughs) | V1 | Technology advancement is how late entrants compete |
| Patents and licensing | V1 | Depends on: R&D |
| Technology level tiers (equipment upgrades) | V1 | Depends on: R&D |
| Criminal R&D (designer drug scheduling race) | V1 | Depends on: R&D and drug economy |
| Commercial bank (deposits, loans, payments) | V1 | Required for credit and economic pressure |
| Investment bank | EX | Depends on: commercial bank; adds depth |
| Hedge fund / asset manager | EX | Depends on: stock market |
| Insurance company | EX | Interesting mechanic but not V1 critical |
| Microfinance / community development bank | EX | Influence-building path; expansion depth |
| "Too big to fail" leverage mechanic | EX | Depends on: bank scale; great late-game mechanic, expansion |
| Corporate law firm | EX | Intelligence asset and offensive tool; expansion |
| Litigation firm | EX | Depends on: legal services |
| Regulatory and lobbying firm | EX | Depends on: political system; expansion |
| Criminal defense firm | V1 | Required for legal process to have a defense layer |
| International law firm | EX | Depends on: multi-nation expansion |
| Vocational school | EX | Depends on: labor market depth; expansion |
| Secondary school | EX | Regional development; expansion |
| University | EX | Long-term influence building; expansion |
| Online learning platform | CUT | Too adjacent to real-world product; scope not justified in game |
| Education as non-monetary influence | EX | Depends on: education system |

---

## Power, Exposure & Consequence

| System | Tier | Rationale |
|---|---|---|
| Evidence token system | V1 | Core of the consequence architecture |
| Evidence types (financial, testimonial, documentary, physical) | V1 | Required for different investigative methodologies to matter |
| Player evidence awareness gap | V1 | The primary late-game tension; must ship |
| Evidence neutralization options | V1 | Required for the player to have agency over exposure |
| Mutual assured destruction leverage | V1 | The most interesting evidence state; must ship |
| Journalist NPC investigators | V1 | Required for exposure to become public crisis |
| Regulator NPC investigators | V1 | Required for institutional consequences |
| Whistleblower emergence from workforce | V1 | Depends on: workforce management |
| Law enforcement special units | V1 | Required for criminal economy to have teeth |
| International agencies (cross-border criminal) | EX | Depends on: multi-nation; expansion |
| Obligation network | V1 | Core mechanic |
| Obligation escalation | V1 | The ratchet effect; must ship |
| Consequence thresholds (organic NPC behavior triggers) | V1 | Required for scale to feel meaningful |
| Legal process (investigation → charges → trial → prison) | V1 | Required for criminal path to be complete |
| Prison as constrained operational state | V1 | Depends on: legal process |
| Prison exit options (appeal, pardon, escape) | EX | Full suite is expansion; V1 ships serve sentence + appeal |
| Personal security system | EX | High threat level is late game; expansion polish |

---

## People in the Grey (Section 11)

| System | Tier | Rationale |
|---|---|---|
| Grey-area contacts via normal social mechanics | V1 | This is just the NPC social web working correctly; no special system |
| Capability escalation through relationship depth | V1 | Same — falls out of NPC relationship model |
| Street reputation (separate from public reputation) | V1 | Required for grey-area contacts to find appropriate players |

---

## Criminal Economy

| System | Tier | Rationale |
|---|---|---|
| Drug economy (production → wholesale → retail) | V1 | Core V1 criminal path |
| Cannabis, cocaine, meth, opioids, synthetics | V1 | Full drug type list ships in V1 |
| Designer drugs / scheduling race | V1 | Depends on: criminal R&D |
| Drug law enforcement response (patrol → task force) | V1 | Required for criminal path to have escalating consequences |
| Informant problem | V1 | Required for criminal network to feel fragile |
| Human trafficking | EX | Mechanically complete in GDD; deliberately excluded from V1 for scope and focus; first major expansion |
| Weapons trafficking | EX | Expansion |
| Protection rackets | V1 | Accessible early criminal revenue; low system complexity |
| Counterfeiting (currency, pharmaceutical, document) | EX | Expansion; document fraud needed for alternative identities — can be a simple unlock in V1. <!-- C1 fix --> Alternative identity creation requires basic document fraud capability. For V1, document fraud is implemented as a minimal unlock — a scene card interaction with a contact who has forger access, producing an AlternativeIdentity record. The full counterfeiting economy (currency and pharmaceutical counterfeiting) is EX. |
| Cybercrime | EX | Requires separate system design; expansion |
| Money laundering (all methods) | V1 | Required for criminal economy to function |
| Financial Intelligence Unit (FIU pattern analysis) | V1 | Depends on: money laundering |
| Operational security (OPSEC) architecture | V1 | Required for criminal path to have meaningful security decisions |
| Corruption of law enforcement / judiciary | V1 | Required for criminal protection to work |
| Alternative identities | V1 | Required for OPSEC and infiltration paths |
| Pre-existing NPC criminal organizations | V1 | Required for the criminal world to exist before the player arrives |
| Criminal organization structure (gang → cartel) | V1 | Required for player to understand what they're building |
| Criminal territorial conflict escalation | V1 | Required for rival organizations to be threats |
| Infiltration path (cover identity, cooperation dilemma) | EX | Depends on: full criminal economy; high complexity; expansion |
| Playing both sides mechanic | EX | Depends on: infiltration path |
| Addiction as regional consequence | V1 | Required for criminal dominance to have systemic effects |
| Systemic consequences of criminal dominance | V1 | Required for criminal path to feel real |

---

## Influence Architecture

| System | Tier | Rationale |
|---|---|---|
| Influence as relationship network (not stat) | V1 | Core design |
| Trust-based influence | V1 | Slowest, most important |
| Obligation-based influence | V1 | The Obligation network |
| Fear-based influence (leverage) | V1 | Required for shadow power to work |
| Movement-based influence | EX | Union and political movement paths are expansion; can be in V1 as a thin system |
| Influence rebuilding after loss | V1 | Required for the floor principle to be felt |
| Influential non-billionaire path | V1 | A design principle, not a system; falls out of the influence model working correctly |

---

## World Response System

| System | Tier | Rationale |
|---|---|---|
| Community response escalation (7 stages) | V1 | Core consequence system |
| Community intervention points | V1 | Required for player agency against community opposition |
| Trust economy (separate from reputation) | V1 | Required for NPC loyalty model to matter |
| Opposition formation | V1 | Required for late game to generate real antagonists |
| Personal security (protective detail, secure locations) | EX | High-threat scenarios are late game; can ship with basic implementation in V1 |

---

## Political & Governance System

| System | Tier | Rationale |
|---|---|---|
| Campaign mechanics (money, time, coalition) | V1 | Required for politician path |
| Voter coalition by demographic | V1 | Required for campaigns to be strategic |
| Election resolution (probabilistic) | V1 | Required for politician path |
| Legislative cycle (proposal → committee → vote) | V1 | Required for governing to mean something |
| Policy consequence engine | V1 | Required for legislation to matter |
| Governing budget and fiscal constraints | V1 | Required for head of state to have real tradeoffs |
| Political crises | V1 | Generated by simulation; required for governing to be challenging |
| Corruption dimension of politics | V1 | Required for obligation network and politics to be connected |
| Clean governance path | V1 | Design principle; falls out of system if consequence model is correct |
| International diplomacy | EX | Depends on: multi-nation |
| War as failure mode | EX | Depends on: multi-nation |
| Appointed political roles | EX | Expansion depth |
| Intelligence oversight as law category | EX | Too complex for V1; expansion |

---

## Social Systems

| System | Tier | Rationale |
|---|---|---|
| Background population cohort simulation | V1 | Required for labor markets, voting, consumer demand |
| Domain-specific reputation (Business, Political, Social, Street) | V1 | Required for different paths to feel different |
| Media system (independent outlets, editorial agendas) | V1 | Required for exposure to become public crisis |
| Story propagation mechanics | V1 | Required for media to work correctly |
| Story planting by player | V1 | Depends on: media system |
| Social media dynamics | EX | Depends on: media system; adds depth but not V1 critical |
| Media outlet ownership | EX | Depends on: media system; expansion |
| Social stability model | V1 | Required for regional conditions to respond correctly |
| Inequality → instability feedback | V1 | Required for criminal dominance consequences |

---

## Random Events

| System | Tier | Rationale |
|---|---|---|
| Random events as perturbations (all four types) | V1 | Core design principle; must ship |
| Genuine randomness (not narrative-timed) | V1 | Required for the perturbation model to be philosophically correct |

---

## Power Endgames

| System | Tier | Rationale |
|---|---|---|
| Legitimate sovereign path | V1 | End state of politician path |
| Covert sovereign path | V1 | End state of influence/obligation path |
| Mixed power (realistic endgame) | V1 | The most common player position; must work |
| Criminal sovereign | EX | Depends on: full criminal economy including trafficking; expansion |
| Becoming indispensable mechanic | V1 | Falls out of scale consequences working correctly |

---

## Progression & Legacy

| System | Tier | Rationale |
|---|---|---|
| No forced end state | V1 | Design principle |
| Organic milestone tracking (silent) | V1 | Character timeline; low cost, high value |
| Death → what you set up is what happens | V1 | Core design |
| Corporate succession | V1 | Required for legacy to mean anything |
| Will and estate execution | V1 | Depends on: legal system |
| Heir system (mentored vs. unmentored) | V1 | Required for personal life investment to pay off |
| Playing as heir (new character, inherited world) | V1 | Core of generational play |
| Start fresh in same world | V1 | Required |
| Start fresh in new world | V1 | Required |
| Corporate culture legacy | V1 | Required for companies to outlive founders meaningfully |
| Political legacy (laws persist) | V1 | Required for political path to have long-term meaning |
| Criminal legacy (markets persist) | V1 | Required for criminal path to feel consequential |
| Philanthropic legacy | EX | Depends on: education and infrastructure systems; expansion |
| Full generational play (multiple heir generations) | EX | Depends on: V1 heir system working well; expansion depth |

---

## UI/UX

| System | Tier | Rationale |
|---|---|---|
| Abstract top-down management interface | V1 | Core interface decision |
| Scene cards for NPC interactions | V1 | Visual texture without 3D world |
| Atmospheric scene settings (boardroom, parking garage, etc.) | V1 | Depends on: scene cards; part of information delivery |
| Calendar interface | V1 | Primary self-management tool |
| Map layer (contact-network-filtered) | V1 | Information asymmetry made visible |
| Operational layer (dashboards) | V1 | How delegated functions are monitored |
| Communications layer (incoming stream) | V1 | Messages, news, contacts |
| Facility top-down planning tool | V1 | Required for facility design |
| Visual feedback through dashboard states | V1 | Problems visible before numerical |
| Contextual system introduction (first-time overlays) | V1 | Required for complex systems to be legible |
| Reference log on device | V1 | Depends on: contextual introduction |
| Continuous autosave, no reload | V1 | Non-negotiable |
| Fast-forward (up to 30x) | V1 | Required given 1 min = 1 day timescale |
| Fast-forward interruption for mandatory events | V1 | Required for fast-forward not to break the game |

---

## Cut List

Systems not pursued in any tier. Documented here so the decision is explicit and doesn't need to be re-litigated.

| System | Reason |
|---|---|
| Third-person 3D world | Incompatible with 1-min tick timescale; replaced by scene cards |
| Energy / action token budget | Mobile-game feel; replaced by time-as-constraint |
| Morality score | Violates core design principle |
| Tutorial | Replaced by contextual introduction system |
| Online multiplayer | Scope-prohibitive; persistent world simulation does not fit multiplayer architecture |
| Online learning platform | Too adjacent to real-world product; no game design payoff |
| Defense and aerospace manufacturing | No design payoff; prohibitive content scope |
| War as V1 feature | Failure mode, not feature; requires multi-nation which is expansion |
| Brain-computer interfaces / AGI / fusion | Science-fictional discontinuities explicitly excluded from setting |
| Scripted story events | Violates emergent narrative principle; never permitted |

---

*EconLife Feature Tier List — Companion to GDD v0.9 — Living Document*
