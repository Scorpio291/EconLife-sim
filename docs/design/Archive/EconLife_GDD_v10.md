# EconLife — Game Design Document
*Version 1.0 — Living Document*
*Rating: M (Mature 17+)*
*Cross-document fixes applied: C2, C3, C4, C5, C6*
*Scenario tags added: 22 (see Scenario Tag Index)*

---

> **Document Philosophy**
> This is a living design document. Every system described here is a starting point, not a contract. As development progresses, systems will be revised, merged, cut, or replaced based on what the simulation actually produces. The goal is not to build exactly what is written here — it is to build something that achieves the design intent described in Section 1. When a system fails to serve that intent, the system changes, not the intent.

---

## Table of Contents

1. Vision & Design Philosophy
2. World Structure
3. NPC Architecture — Memory, Motivation & Agency
4. Player Identity & Character
5. The Three Challenges of Play
6. Career Paths & Roles
7. Facility Design System
8. The Full Economy — Supply Chain Stack
9. Knowledge Economy
10. Power, Exposure & Consequence Systems
11. People in the Grey
12. The Criminal Economy
13. Influence Architecture
14. The World Response System
15. Political & Governance System
16. Social Systems
17. Random Events as Perturbations
18. Power Endgames
19. Progression & Legacy
20. UI/UX Philosophy
21. Technical Architecture
22. Development Philosophy
23. Production Scope & Companion Documents

---

## 1. Vision & Design Philosophy

### The Premise

EconLife is an open-ended life and economy simulator where the player takes any role in a living, persistent world — from a minimum-wage worker to a factory owner, politician, crime boss, or someone pulling strings from the shadows. There are no scripted story events. There is no authored narrative. The world runs on rules, and the story emerges from the collision of those rules with the player's choices and the independent behavior of everyone else.

Everything the player can imagine is available. They can build legitimate empires or criminal ones. Run drugs, traffic people, corrupt governments, go to war with rival cartels, launder money through shell companies, infiltrate criminal networks to destroy them, or spend a career as a union organizer who never breaks a law. They can be all of these things simultaneously. The world responds to everything they do. Nothing is consequence-free.

### The Core Insight

Every tycoon game eventually goes hollow because it treats money as the permanent challenge. Money is only a challenge when you don't have enough. Once you do, the game has already ended — it just hasn't admitted it yet.

EconLife solves this by understanding that **wealth changes the nature of the challenge, it doesn't end it.** The game tends to present three distinct challenge types across a playthrough:

**Survival challenges** arise from scarcity. Can you make rent? Can you keep the business alive? Can you avoid getting arrested before your operation is big enough to protect itself?

**Dominance challenges** arise from strategy. Can you outcompete, scale, and consolidate? Can you secure supply chains, build the right relationships, and outlast rivals who are doing the same?

**Preservation challenges** arise from power. Can you protect what you've built from the people who want to dismantle it? Power, unlike money, is never secure.

These challenge types are not phases that a player progresses through in sequence. They emerge organically from the simulation as the player's situation changes. A player can face all three simultaneously across different parts of their operation. The restaurant they're barely keeping alive is a survival challenge while the trucking company they've scaled to regional dominance is a preservation challenge. No announcement is ever made. No threshold is ever crossed that the game acknowledges. The challenge simply changes because the simulation is responding to what exists.

### The Emergent Narrative Principle

EconLife generates no story content. It generates story *conditions* — and the story emerges from how those conditions interact.

This means: no scripted events (random perturbations are genuinely random and indifferent to narrative timing); no authored consequences (when the warehouse burns down, it burns down because of a specific traceable chain of causes rooted in prior decisions — the game doesn't explain this, the player reconstructs it); no moral commentary (the game doesn't reward clean play or punish dirty play ideologically — it models consequences realistically; corruption works until it doesn't; clean play is harder but more stable).

The writers' job on this project is not to write story events. It is to write the **behavioral logic** of NPCs, institutions, and systems — and then let that logic run. Every hour spent writing a story event is an hour that should have been spent deepening a system that generates ten thousand unscripted story events.

### Design Pillars

**Systems are the story** — narrative emerges from simulation, never from scripts.

**Causality over content** — a consequence that traces back to the player's decisions is worth more than any authored event.

**The world is indifferent** — it runs whether the player acts or not, and it pursues its own logic regardless of player convenience.

**Wealth is a liability as much as an asset** — the bigger you get, the more people want to regulate, exploit, expose, or dismantle you.

**Influence is not money** — the ability to make things happen comes from relationships, trust, fear, and movement; wealth amplifies influence but does not create it.

**Not punishment — consequence** — the game does not judge the player. It simulates a world full of agents who observe what happens and act accordingly. The player who builds a drug empire doesn't receive a morality penalty; they receive law enforcement attention, rival criminal organizations, victim NPCs with agency and memory, and people who will eventually turn on them if they're not strong enough to stay on top.

**Information is earned, not granted** — the player knows what they've done and what their contacts tell them. They do not have a god's-eye view of the world. The gap between what they know and what is actually happening is the primary source of tension in the late game.

**Readable complexity** — deep systems revealed gradually through play, never dumped through tutorial.

---

## 2. World Structure

### The Setting

EconLife is set in the 2040s–2060s, referred to in-game simply as the present. The world is recognizably derived from the real world but with the following conditions baked in:

**Climate shift has redistributed agricultural productivity.** Some historically productive regions have become severely water-stressed, reducing output and creating scarcity-driven tension. Other regions have gained productivity from warming and are now significant agricultural producers. These shifts are reflected in which regions have which resources — they are not a political system, they are a resource distribution system with climate as one input.

**Automation has displaced a large portion of routine labor.** The labor market has bifurcated: high-skill professional work remains human-dominated and well-compensated; routine physical and cognitive work has collapsed in employment volume. The resulting displaced population is politically radicalized, economically vulnerable, and available for both legitimate low-wage employment and criminal recruitment. High inequality is the baseline condition in most nations, not an outlier.

**Surveillance infrastructure is pervasive in most nations.** Networked cameras, communications monitoring, and financial transaction tracking make physical operational security harder and more consequential. Nations vary dramatically in surveillance intensity — authoritarian states have more pervasive but more corruptible systems; high-rule-of-law states have stronger legal protections but more technically capable investigators.

**Cryptocurrency is normalized and regulated in some nations, restricted in others, banned in a few.** Central bank digital currencies coexist with private crypto, providing states with transaction visibility that conventional cash didn't allow. Cash is a premium privacy asset in high-surveillance jurisdictions.

**No artificial general intelligence, no brain-computer interfaces, no space economy, no fusion power.** The near-future is defined by extrapolation of existing trends, not science-fictional discontinuities.

### The Map

The world is composed of **regions** (equivalent to counties or municipalities), each belonging to one of several **nations**. Regions have simulated attributes: population and demographics (age distribution, education levels, income brackets, political lean), natural resources (ore deposits, arable land, oil reserves, forests, fisheries), infrastructure rating (roads, power grid, broadband, water supply), local real estate market with dynamic pricing, and crime rate, inequality index, and social stability score.

Nations have independent governments, currencies, tax codes, trade relationships, central banks, and diplomatic postures toward other nations. A nation's internal conditions — stability, inequality, debt level, corruption index — evolve over time based on the aggregate behavior of all actors within it, including the player.

### Time Scale

One real-time minute equals one in-game day at normal speed. Fast-forward is available up to 30x. A full in-game year takes roughly six real-time hours at normal speed. Consequence delays are measured in in-game months and years — some decisions won't surface their full consequences for a decade of game time.

### The World Runs Without You

If the player does nothing: markets fluctuate, elections happen, businesses open and close, inequality shifts, politicians rise and fall, crimes are committed, investigations are opened. The world is not waiting for the player. The player is one actor in a system that has its own momentum.

A player cannot operate in a region they have no presence in. No contacts means no information, no access, no ability to conduct business. Expanding into a new region requires establishing a foothold first — a contact, a local hire, a physical presence — before infrastructure investment makes any sense. This applies to legitimate expansion and criminal expansion alike.

---

## 3. NPC Architecture — Memory, Motivation & Agency

This section is foundational. The quality of emergent narrative depends entirely on NPCs that behave as motivated agents with memory, not state machines with flags.

### The Difference

A **stateful NPC** knows they are currently hostile toward the player. A **motivated NPC with memory** knows *why* they're hostile, *when it started*, what they've done about it since, and what they're planning next.

Every significant NPC in the game — executives, politicians, journalists, union leaders, regulators, fixers, lawyers, criminals, trafficking victims — operates on the second model.

### NPC Core Attributes

Each significant NPC has a **Role** (what they do in the world), a **Motivation** (what they want — career advancement, money, ideology, revenge, stability, power, survival), a **Memory Log** (a timestamped record of interactions with the player and world events that affected them), a **Relationship Score** with the player and with other NPCs (directional — A's view of B ≠ B's view of A), a **Knowledge Map** (what they know, when they learned it, and how they could use it), a **Risk Tolerance** (how likely they are to act on what they know), and **Resources** (what tools they have available — funding, contacts, platform, legal authority, physical capacity).

### Behavior Generation

NPCs make decisions by weighing their motivations against their available resources and their assessment of risk. They do not roll random event dice. They follow the logic of their situation.

A journalist with a motivation of career advancement, moderate risk tolerance, and knowledge of a financial irregularity will pursue the story if the career upside outweighs the legal and personal risk of publishing. If the player buys the outlet she works for, her calculation changes — but she doesn't disappear. She takes her notes somewhere else, and now she's specifically hostile rather than merely investigative.

> [SCENARIO: scenario_journalist_pursues_story_on_career_motivation]
> Test: A journalist NPC with career_advancement motivation pursues a financial irregularity story when career upside exceeds legal and personal risk in their expected_value calculation.
> Seed setup: Significant NPC with role=journalist, motivation=career_advancement, risk_tolerance=0.5; player generates a financial evidence token visible to that NPC; NPC's career_upside weight > legal_risk weight.
> Run length: 30 ticks.
> Assertion: Journalist NPC has initiated an investigation action (investigator meter filling) against the player within the run window.

> [SCENARIO: scenario_journalist_leaves_hostile_after_outlet_purchase]
> Test: When the player acquires the media outlet employing an investigating journalist, the journalist's relationship_score with the player shifts to hostile and they initiate investigation from a new outlet.
> Seed setup: Significant NPC journalist actively investigating player; player acquires journalist's employer outlet (owner changes to player).
> Run length: 15 ticks.
> Assertion: Journalist NPC relationship_score with player is negative (hostile); journalist has a new employer (different outlet); journalist's investigator meter continues filling.

A regulator with an ideology of institutional integrity, low risk tolerance, and a supervisor who answers to a politician the player controls will file a preliminary inquiry but move slowly and cautiously. Change the supervisor, and the regulator's risk calculation changes.

> [SCENARIO: scenario_regulator_slows_when_supervisor_controlled]
> Test: A regulator NPC files an inquiry but advances it at reduced speed when their supervising politician is under player Obligation control; speed increases when that relationship is severed.
> Seed setup: Regulator NPC (role=regulator, ideology=institutional_integrity, risk_tolerance=0.2); player has Obligation node over the regulator's supervising politician; player has generated regulatory-triggering evidence.
> Run length: 60 ticks.
> Assertion: Regulator's investigator meter fill_rate is below baseline; after supervisor obligation is removed (tick 30), fill_rate increases to baseline within 10 ticks.

A trafficking victim with a high survival motivation, a memory of who brought them here, and a knowledge of the venue's layout will look for escape opportunities. Their risk tolerance for escape attempts increases as their hope of other rescue decreases.

### The Delay Principle

NPC actions take time. The fired security guard doesn't retaliate next week. He spends two months unemployed, reconnects with old contacts, and acts four months later when the opportunity is right. The wage cut that demoralized a workforce doesn't produce a strike next quarter — it produces a union organizer quietly working the floor six months from now.

> [SCENARIO: scenario_wage_cut_produces_union_organizer]
> Test: A significant wage cut event produces a union organizer NPC (promoted from workforce) who begins organizing activity no sooner than 90 ticks (approx. 3 months) after the cut and no later than 180 ticks (6 months).
> Seed setup: Player owns a facility with 50+ workers; player reduces wages by ≥20% below regional market rate in a single action; no pre-existing union organizer NPC in the workforce.
> Run length: 200 ticks.
> Assertion: A Named or Significant NPC with role=union_organizer has been promoted from the player's workforce; NPC's first organizing action occurs between tick 90 and tick 180. A witness who was too frightened to come forward in year one may come forward in year four when their circumstances change.

> [SCENARIO: scenario_witness_delayed_cooperation_on_circumstance_change]
> Test: A witness NPC whose risk_tolerance was below action threshold at time of witnessing an event crosses the threshold and contacts investigators when their personal circumstances improve sufficiently.
> Seed setup: NPC with witnessed_illegal_activity memory entry; risk_tolerance initially < 0.3; NPC's circumstances change (e.g., new employment, relocation) such that risk_tolerance rises above 0.5 by tick 1000 (approx. year 3).
> Run length: 1460 ticks (4 in-game years).
> Assertion: Witness NPC has taken a whistleblower or investigator-contact action after tick 730 (year 2) but not before tick 365 (year 1).

This delay is not artificial pacing. It is the realistic timescale of human decision-making and institutional response. The past is always present.

### How NPCs Find Each Other

NPCs connect through the same social mechanics the player uses. A fixer hears about a business owner who handled something quietly. A corrupt official has a lawyer friend who mentioned an interesting client. A criminal organization looking for someone with logistics experience asks around through people they trust. The world is a web of overlapping social networks — professional, familial, criminal, civic — and information and introductions travel through that web at the pace of real social life.

The player is part of this web. People approach them because someone mentioned them. They get access to new people because someone they know makes an introduction. This applies regardless of what kind of person is doing the approaching or being approached. A legitimate business owner may be approached by someone who offers discreet accounting services. A career criminal may get a call from a city official who heard through a mutual contact that they're a person of discretion. These are not special events from a special system. They are social mechanics working as described.

### NPC Population Scale

**Significant NPCs** (500–1,000 per region) receive a full memory and motivation model — named, persistent, individually simulated. This is the target validated by the simulation prototype; see Technical Design Section 4. <!-- C5 fix: cross-reference audit performed. This GDD contains two references to Technical Design sections: this reference to Section 4 (NPC Architecture — valid and exists) and the worker satisfaction reference in Section 6.8 (fixed by C3, now also points to Section 4). No additional broken cross-references found. -->

**Named background NPCs** (2,000–5,000 per region) are simulated with a simplified model: a motivation score and a single relationship to the player if they have interacted, but no full memory log. They are the pool from which significant NPCs are promoted.

**Background population** (millions) are simulated as statistical cohorts whose collective behavior drives labor markets, consumer demand, voting patterns, and social stability. Individual members can be promoted to Named or Significant NPC status when promotion conditions are met.

**Background NPC promotion conditions** — A background cohort member is promoted to Named NPC status when any of the following occur: they are hired by the player; they are a direct witness to a player action (present at the location where an evidence token is generated); they file a formal complaint against a player-owned operation; they are identified as a candidate to run against a player-affiliated politician; or they are a family member of a trafficking victim (expansion only). A Named NPC is promoted to Significant NPC status when any of the following occur: they accumulate three or more memory entries with emotional\_weight > 0.5; they are named in an active evidence token; they become an obligation creditor or debtor of the player; they are hired into a management role; or they are identified by the investigator engine as an active investigator. Promotion is irreversible — a promoted NPC retains their full model for the remainder of the simulation.

---

## 4. Player Identity & Character

### Character Creation

At game start the player creates a character with a **Background** (Born Poor / Working Class / Middle Class / Wealthy — determines starting cash, education level, social connections, and initial credit score), **Traits** (choose 3 from: Charismatic, Analytical, Ruthless, Cautious, Creative, Physically Robust, Politically Astute, Street Smart — each gives small but meaningful bonuses to relevant systems), and a **Starting Region** (affects available jobs, property prices, dominant industries, and local political environment).

### Character Stats

| Stat | Function |
|---|---|
| **Health** | Current physical condition. Affects the character's capacity for demanding engagements. Degrades from violence, neglect, and substance use. Managed through healthcare and lifestyle choices. |
| **Age** | Characters age at one in-game year per six real-time hours at normal speed. Affects health degradation rate, some skill effectiveness, and NPC social perceptions. |
| **Reputation (Public)** | How the general population, media, and institutions perceive the player. Domain-specific: Business / Political / Social. |
| **Reputation (Street)** | How people who operate in grey and black areas perceive the player — built and damaged through entirely different actions than public reputation. |
| **Wealth** | Liquid cash + net assets. Critical in early play, increasingly a tool rather than a goal as operations scale. |
| **Exposure** | Accumulated body of evidence of legally or socially damaging actions. Lives in the world as specific tokens. Doesn't hurt the player until someone finds and acts on it. |
| **Obligation** | Favors owed to people who helped the player. Each one is a future cost of unpredictable size. |
| **Skills** | Per-domain, leveled by doing and rusted through neglect. |

**Influence** is not a stat. It is a relationship network tracked separately — see Section 13. What the player's dashboard shows is a summary readout: counts of active relationships by type (trusted relationships, obligations held, leverage held over others, movement followers) and an overall network health indicator. Tapping into the full network visualization is available from this readout.

### Skills

Business, Finance, Engineering, Politics, Management, Trade, Intelligence, Persuasion, Criminal Operations, Undercover/Infiltration, and Specialty skills (culinary, chemistry, coding, agriculture, construction, and others).

### Time as the Real Constraint

There is no action budget. No daily token limit. No "you've used your actions for today." The player can engage with as many things as the simulation is generating and their calendar allows. What limits them is real scarcity: time slots in the day and access to NPCs who have their own schedules.

**Every engagement takes simulated time.** A meeting is scheduled for a specific date. A negotiation spans two in-game days. A court appearance occupies a week. A political rally is blocked in the calendar. A criminal operation requires preparation days before it executes. While those days are committed, the simulation keeps running and everything else develops without the player's attention.

The player's calendar is their primary self-management interface. It shows what's committed, what's incoming, what decisions are pending with a deadline, and what they've been neglecting long enough that the lack of attention is itself becoming a problem. The calendar fills from two directions: things the player chose to engage with, and things the world sent them that they accepted.

**Scheduling has friction.** When the player initiates contact with an NPC, a time gets agreed — not immediate access. A politician mid-campaign may not be available for three in-game days. A lawyer in trial is unreachable for a week. A cautious criminal contact insists on an in-person meeting and needs 48 hours' notice. The gap between initiating contact and having the meeting is real time during which the situation may change.

Incoming demands work the same way in reverse. A manager escalates a problem that needs a decision by Thursday or they'll act on their own initiative. An NPC requests a meeting — the player can accept, decline, or delay, knowing the simulation continues either way. A legal summons arrives with a mandatory appearance date. These are calendar items the player must engage with or consciously let pass.

**The natural pressure.** As operations grow, the simulation generates more events, decisions, and demands than any one person can personally attend to. The player doesn't run out of action points — they run out of hours in the day because the world is producing more than one person can handle. This is how real organizations work. It's the mechanism that makes delegation a genuine necessity rather than an optional optimization.

**Focus is an advantage.** A player who deliberately limits scope — fewer operations, fewer relationships, genuine attention to each — gets compounding advantages the spread-thin player doesn't. NPCs who receive regular substantive engagement develop deeper relationships. Investigations personally monitored get caught earlier. Operations the player actively oversees run closer to their potential. The simulation rewards presence because the player is actually there for things that matter — not through a stat bonus, but because attention produces better outcomes than inattention.

Early on, when operations are small, the player can be personally involved in most things and the calendar is manageable. As scale grows, the gap between everything happening and everything the player can attend to widens. They start making real choices about what to let go of. That transition isn't announced or gated. It emerges because the calendar is filling up.

**Health and exhaustion as a situational modifier.** The character's physical state affects the quality of demanding engagements, not the quantity permitted. A healthy, rested character handles a high-stakes negotiation at full effectiveness. A character who is ill, injured, or has been running at full capacity for too many consecutive days without rest is operating at reduced capacity — they may read a situation less clearly, handle a difficult conversation worse, or miss a signal they'd normally catch. They aren't blocked from engaging. They're just not at their best, which is realistic and legible without being punitive.

Exhaustion accumulates when the player fills every calendar slot for extended periods without recovery time. It resolves when they don't. The character's physical state is visible on their profile as a status indicator — not a resource bar.

### Health, Aging, and Lifespan

A character's natural lifespan starts at a base of 70–80 in-game years with randomized variation, modified by background and traits. Health degrades from violence and injury (immediate and significant), lifestyle neglect (slow and accumulated — consistently filling the calendar without recovery accumulates stress damage over years), substance use (short-term performance benefits at long-term health cost; harder substances carry addiction risk that applies the same addiction mechanics as the NPC system), and medical neglect.

Healthcare access — through owned facilities, private care, or the regional health system — treats damage and slows degradation. A player who invests in their own health can extend their lifespan by a meaningful margin. A player who neglects it can lose years.

Physical decline with age affects the character's capacity for demanding engagements and makes delegation increasingly important. Cognitive skills (Business, Finance, Politics, Intelligence) remain effective until very late. Physical skills begin declining from the character's in-game fifties. Older characters command different social responses — more authority in some contexts, less dynamism in others.

When health drops below a critical threshold, the character enters a visible terminal phase. The player has time to manage succession and settle affairs. Sudden death from violence or catastrophic health failure arrives without warning. There is no special mechanism for either — what happens is entirely determined by what the player set up while alive.

### Personal Life

The player character has a personal life that runs in parallel to their professional one. It is never mandatory, but it is always consequential when engaged. Personal life events surface as scene cards — a dinner at home, a difficult conversation with a partner, a child's milestone, a call from an old friend — that arrive at intervals determined by the state of those relationships and how much the player has been investing in them.

**Residence** can be upgraded throughout the game. Where the player lives affects health recovery rate and physical security. A well-known luxury address is exposed — it's publicly associated with the player and harder to secure. A low-profile address is safer but sends its own signals to visiting contacts.

**Romantic relationships** are optional. A partner is a significant NPC with full memory and motivation — someone who observes the player's behavior over time through their interactions and the things that reach them, develops genuine views, and will act on those views. A partner may become the most loyal relationship in the player's life, or may create exposure risk if they learn things they weren't meant to know, or may reach a point where they choose to act on what they know. Relationships develop through the scene cards the player engages with honestly. They decay when the player declines them, delays them, or engages with them dishonestly.

**Children** are optional. Children age through childhood and adolescence, with personality and skills shaped partly by how much the player engaged with the personal life scene cards that involved them. The relationship quality at time of succession determines heir capability and loyalty. A neglected child who rarely appeared in the player's calendar may inherit great wealth and be entirely unprepared to manage it, or carry a specific resentment that the player never saw coming because they were never present to see it develop.

Children who grew up in a household where criminal operations were visible have that in their memory log. The player does not control what they do with it.

**Early relationships** — friends, mentors, rivals from the player's origin — persist through the character's life as significant NPCs. They knew the player before the money or the power, and that history is in their memory. These relationships carry a kind of trust that cannot be manufactured through favors, and they are often the only relationships that survived the player's rise without Obligation attached to them.

Neglecting personal life produces real consequences: social isolation compounds health degradation over years, no functional heir creates a succession crisis at death, and a player surrounded entirely by transactional relationships has no one in their network who isn't there for what the network provides.

---

## 5. The Three Challenges of Play

This section describes patterns that tend to emerge during a playthrough. These are a vocabulary for developers and designers to think with — not states the game tracks, not thresholds that fire, not anything the player ever sees named. The simulation produces these challenge patterns through its own logic as the player's situation changes.

### Survival Challenges

Survival challenges arise from scarcity. The player has limited resources, limited connections, limited options. Every decision has material stakes. Missing rent is a real consequence. Getting fired sets back months. Getting arrested before the operation is established is potentially ruinous.

**Legitimate path:** Financial scarcity drives every decision. Income must be secured before expansion. The world does not accommodate the player — landlords charge market rates, employers have requirements, banks check credit.

**Criminal path:** Enforcement scarcity and territorial pressure drive decisions. The challenge is establishing a revenue stream without being immediately arrested or killed by an organization that already owns the territory. Cash matters but safety comes first.

**Political path:** Social and credibility scarcity dominate. The player has no base, no coalition, no name recognition. Every endorsement requires something given. Every vote won is a favor incurred.

Survival challenges recede when the player establishes stable footing in whatever domain they're operating in — not because the game changes, but because the simulation is no longer generating the conditions that created them.

### Dominance Challenges

Dominance challenges arise from strategy. The player has resources; the question is how to deploy them to build something durable. Competitors exist and react. Markets move. Political conditions change. Supply chains have friction. Rival organizations probe for weakness.

These challenges often begin before survival challenges have fully resolved — a small business is fighting for survival while simultaneously making strategic decisions about which market to expand into. The overlap is by design.

The first Obligations accumulate during this period: the bank that gave the favorable loan, the official who fast-tracked the permit, the partner who introduced the key contact, the criminal organization that agreed to share territory. Every advantage creates a future cost.

### Preservation Challenges

Preservation challenges arise from power. The player's wealth and position have crossed thresholds that attract sustained opposition — investigators, rivals, regulators, political enemies, criminal organizations that see them as a target. The tools available to solve these problems generate new exposure. The obligations accumulated during growth are calling in their debts.

The most important characteristic of preservation challenges: **the player's size itself is the source of the problem.** A billionaire playing clean is under more investigative pressure than a small business owner playing dirty, because the institutions that regulate them have more reason and more resources. Being powerful generates opposition permanently. This never stops.

Preservation challenges do not replace dominance or survival challenges — they layer on top. A player managing a major investigation while their core business faces a new competitor while their criminal operation faces a new enforcement unit is facing all three simultaneously. That is the late game.

---

## 6. Career Paths & Roles

These are not locked choices. The player can hold multiple roles simultaneously, pivot at any time, and may eventually occupy roles in both legitimate and grey economies concurrently. Transitioning costs time, sometimes money, sometimes reputation.

### 6.1 Employee

Works for NPC or player-owned companies. Apply for jobs based on skills and reputation, schedule work shifts that consume calendar time and return wages, earn promotions through performance and relationships, and build knowledge of an industry from the inside — knowledge that is valuable if the player later owns a business in that sector. Work days surface as brief scene cards for significant events: a conversation with a manager, a colleague who mentions something interesting, a task that builds a relevant skill.

### 6.2 Entrepreneur / Business Owner

Start, acquire, or inherit a business. Legal structures range from sole proprietorship (minimal overhead, full personal liability) through partnership to LLC/corporation (legal separation, share issuance, shareholder scrutiny). Sectors: Manufacturing, Food & Beverage, Retail, Services, Real Estate, Agriculture, Energy, Technology, Finance, Transport & Logistics, Media, Security, Research, and Criminal operations (see Section 12).

### 6.3 Investor

Capital allocation as primary activity. Stock market with prices driven by real company performance, real estate with dynamic markets, government bonds, venture capital, commodity trading. In periods of high power: distressed asset acquisition, hostile takeovers, short-selling rivals into bankruptcy.

### 6.4 Politician

The path that shapes the rules everyone else plays by. Career ladder: City Council → Mayor → Regional Governor → Head of State. Alternatively: appointed roles (cabinet minister, central bank governor, regulatory agency head).

Campaigns require money and relationships — specifically, the political relationships from the player's existing network. A player who has spent years building trusted relationships with union leaders, community organizations, civic figures, and other politicians has a base they can activate. A player who enters politics with no relationships but significant money can purchase early access — but money-bought political support is obligation-based, less durable, and more expensive to maintain over time. Voter coalition management is a direct function of community-level trust and grievance (Section 14.2).

Governing requires drafting legislation, building coalitions, navigating opposition, managing scandals, and balancing budgets. Political office creates Influence and creates vulnerability simultaneously. The player as politician will be approached with bribes and favors — accepting builds Obligation and Exposure, refusing creates enemies among those who expected compliance.

### 6.5 Criminal Operator

An underground economic layer running parallel to and interacting with the legitimate one. Drug production and distribution, human trafficking, weapons dealing, counterfeiting, cybercrime, protection rackets, money laundering. Higher risk/reward ratio with asymmetric exposure. The criminal path is not separate from the business path — it is an alternative, and eventually complementary, set of tools. Full system detail in Section 12.

### 6.6 Union Leader

Organize workers, negotiate collective agreements, call strikes, run for political office. A legitimate way to gain Influence from the bottom of the economic hierarchy upward. A union leader who organizes 40,000 workers can shut down every factory in a region simultaneously. That is Influence that wealth cannot simply purchase.

*[Expansion scope — see Feature Tier List. Section included for design completeness; mechanics are thin in V1.]*

### 6.7 Infiltrator / Undercover Investigator

*[Expansion scope — see Feature Tier List. Described here for design completeness; not built in V1.]*

Build a cover identity to enter and destroy criminal enterprises from the inside. Gather evidence, cultivate cooperative witnesses, and engineer takedowns of organizations the player has spent years embedding in. The most complex and morally ambiguous path in the game — it forces the player to participate in the thing they're trying to destroy in order to destroy it. The cover identity system (Section 12.6) provides the primary tool; full mechanics in Section 12.9.

### 6.8 Workforce Management

Hiring, managing, and losing workers is one of the most consequential ongoing activities in the game — both for operational output and for exposure management.

**Hiring:** The player posts a role (job title, requirements, compensation) through available channels: public job boards (maximum applicant pool, minimum control over who applies), professional networks (narrower pool, higher average quality, requires existing relationships in that professional community), and personal referrals (smallest pool, highest trust, the player's existing contacts recommend someone they know). Each channel produces a pool of NPC applicants with their own skills, motivations, salary expectations, and backgrounds. The player interviews candidates — a lightweight interaction that surfaces relevant NPC attributes — and makes a hire.

Word travels. If the player pays well and treats workers fairly, their employer reputation improves and future applicant quality rises. If workers leave unhappy or under suspicious circumstances, hiring becomes harder and the quality of applicants willing to join them declines.

**Day-to-day management:** Workers have satisfaction (a float 0.0–1.0), which is affected by: pay relative to market (the player can see the regional wage market and set compensation accordingly), working conditions (determined by facility design and management quality), how the player treats them in direct interactions, job security signals (is the business visibly healthy?), and what they witness at work. Satisfaction affects productivity, loyalty, and retention. <!-- C3 fix --> Full satisfaction model in Technical Design Section 4 (NPC Architecture) — satisfaction is derived from NPC memory entries about employment situation, not tracked as a separate field. Satisfaction is a property of the worker NPC's motivation model — it is not a separate tracking system; it is derived each tick from the NPC's memory entries about their employment situation.

> [SCENARIO: scenario_low_satisfaction_produces_npc_adverse_action]
> Test: Worker NPCs with satisfaction below 0.35 are statistically more likely to voluntarily leave employment, take whistleblower action, or begin union organizing than workers above 0.6 satisfaction over the same period.
> Seed setup: Two matched cohorts of 20 worker NPCs each with identical role, skill, and witnessed_illegal_activity memory entries; cohort A satisfaction = 0.25; cohort B satisfaction = 0.75. No other variables differ.
> Run length: 180 ticks.
> Assertion: Cohort A has a higher rate of adverse actions (voluntary quit, whistleblower contact, union organizing initiation) than cohort B; difference must be statistically significant (p < 0.05 across 10 simulation runs with different seeds).

**Whistleblower risk:** A worker NPC becomes whistleblower-eligible when all three conditions hold: satisfaction < 0.35 AND the worker's memory log contains at least one entry of type `witnessed_illegal_activity` with emotional\_weight > 0.6 AND the worker's risk\_tolerance > 0.4. The whistleblower-eligible state is checked by the investigator engine each tick and may trigger a whistleblowing action based on the worker's motivation model. This is the threshold referenced in Section 22's contextual introduction examples.

> [SCENARIO: scenario_whistleblower_emerges_on_eligible_conditions]
> Test: A worker NPC with all three whistleblower-eligible conditions satisfied (satisfaction < 0.35, witnessed_illegal_activity entry with emotional_weight > 0.6, risk_tolerance > 0.4) eventually contacts investigators; a worker missing any one condition does not.
> Seed setup: Four worker NPCs — one satisfying all three conditions; three each missing exactly one condition. All have identical employer situation and no external pressure.
> Run length: 120 ticks.
> Assertion: Only the NPC satisfying all three conditions triggers a whistleblowing action within the run window. NPCs missing any single condition do not trigger whistleblowing.

**Firing:** Firing a worker removes them from the operation and frees their budget. What it doesn't do is remove their memory. Every fired worker carries everything they saw into their post-employment life. A worker fired after witnessing financial irregularities, seeing something they shouldn't have, or being treated badly is a potential future problem. The gap between when they're fired and when that problem surfaces is governed by the Delay Principle and the consequence queue — it may be months or years, and it depends on their motivation, their risk tolerance, their new circumstances, and whether anyone else gives them a reason or opportunity to act.

> [SCENARIO: scenario_fired_worker_with_knowledge_delayed_consequence]
> Test: A worker fired while carrying a witnessed_illegal_activity memory entry eventually takes an adverse action against the player, with the action occurring in the consequence queue at a delay of at least 90 ticks after termination.
> Seed setup: Worker NPC with witnessed_illegal_activity memory entry (emotional_weight > 0.6), risk_tolerance = 0.5, motivation includes financial grievance; player fires worker at tick 0.
> Run length: 400 ticks.
> Assertion: Worker NPC generates a consequence queue entry (type in {whistleblower_contacts_authority, npc_action, media_story_breaks}) with scheduled_tick >= 90; that consequence fires within the run window.

**Promotion:** Workers promoted into management gain access to more of the operation and more of its decisions. Promoting a loyal, skilled worker into a management position is both operationally valuable and an exposure-management tool — competent, satisfied managers who trust the player are the strongest buffer against internal whistleblowing. Promoting the wrong person is the opposite.

**The knowledge problem:** What each significant worker has seen and heard on the job is stored in their NPC memory log — this is not a separate tracking system. A worker who was present when a sensitive financial conversation happened has a memory entry for it. A factory worker who noticed the chemical storage room doesn't match any legitimate product line has a memory entry for that observation. Workers don't always act on this knowledge immediately — risk tolerance and motivation determine if and when they do — but it's in their memory log permanently. Managing what workers are exposed to is a design choice during facility layout (Section 7) and an ongoing management consideration.

> [SCENARIO: scenario_worker_observes_sensitive_event_creates_memory]
> Test: A worker NPC present at a location where a sensitive financial event is generated receives a corresponding memory log entry with the correct event type and emotional_weight.
> Seed setup: Worker NPC assigned to a room adjacent to where an illegal financial transaction scene card fires; transaction generates a financial evidence token; worker's location matches event location at the tick of occurrence.
> Run length: 2 ticks.
> Assertion: Worker NPC's memory_log contains an entry with type=witnessed_illegal_activity referencing the financial transaction event, with emotional_weight > 0.0, created at the correct tick.

---

## 7. Facility Design System

When the player owns a business, they design and build facilities. Building layout has real mechanical consequences — this is not cosmetic.

### 7.1 Building Fundamentals

Every building occupies a purchased or leased plot with size, zoning type, and location quality attributes. Facilities are designed using a top-down planning view — a room and module system accessed through the facility's entry in the operational dashboard. Place rooms, connect them, furnish them, and the simulation evaluates the resulting design for efficiency, worker satisfaction, and operational output. Facility design is an abstract planning exercise; the player is arranging a floorplan and its consequences, not physically walking the space.

### 7.2 Factory Design

Defined by a production line — a chain of machines and workers converting inputs to outputs. Key components: Intake Bay, Raw Storage, Processing Machines, Assembly Stations, Quality Control, Packaging, Finished Goods Storage, Dispatch Bay, Break Room/Canteen, Utility Room.

The slowest stage determines total throughput. Players must identify and resolve bottlenecks through redesign, automation upgrades, or workforce adjustments. Production flow is visually represented — goods move, bottlenecks pile up, problems are visible before data panels confirm them.

### 7.3 Office Design

Room adjacency affects productivity. A noisy social space next to a development floor reduces focus. Natural light, temperature, furniture quality, and space per person all contribute to an Office Quality Score driving employee productivity and retention. Layout also determines which employees are present for sensitive conversations, which staff have system access, and who accumulates whistleblower-risk knowledge — connecting directly to the Exposure system.

### 7.4 Restaurant Design

Front of house (customer experience) and back of house (production efficiency) must both be optimized. Menu design: each dish has a recipe, margin, and reputation effect. Foot traffic is driven by location, marketing, and review scores generated by actual NPC customer simulation.

### 7.5 Farm Design

Field tiles assigned to crops or livestock. Soil health tracked over seasons. Irrigation, fertilizer, and crop rotation affect yield. Weather events create risk mitigable through insurance.

### 7.6 Extraction Facilities

Open-pit mine, underground mine, oil and gas wellfield, logging operation — each with its own layout logic, input requirements, environmental impact tracking, and visual bottleneck representation. Deposit quality and depth affect extraction cost and output ratio.

### 7.7 Processing Facilities

Smelters, refineries, chemical plants, agricultural processors — production line logic applies with input specifications, conversion ratios, energy consumption, and byproduct generation.

### 7.8 Criminal Facilities

Drug processing labs, smuggling waypoints, storage and distribution points, locations used for trafficking operations. These use the same design system with additional mechanics: detection risk from power consumption patterns, chemical waste output, observable foot traffic, and olfactory signatures. Facility design directly affects operational security — a poorly designed lab generates evidence. A well-designed one minimizes it. See Section 12.

### 7.9 Other Facility Types

Retail stores, power plants, logistics hubs, apartment buildings, media studios, research labs, bank branches, law firm offices, schools, universities — each with sector-specific design mechanics following the same principle: layout choices have real operational consequences.

---

## 8. The Full Economy — Supply Chain Stack

### 8.1 Philosophy

The economy is structured as a vertical stack. At the bottom: raw resources extracted from specific physical locations. At the top: finished goods and services that reach consumers. Every layer adds value, requires labor and capital, and creates both opportunity and dependency.

The player can enter this stack at any point. Every link in the chain is a market. Every market has prices set by supply and demand. Nobody controls prices — they emerge from aggregate behavior. A drought killing cotton harvests raises textile prices a year later. A new copper deposit collapsing global copper prices makes everyone who invested in copper infrastructure poorer. The world doesn't wait for the player to cause these things.

### 8.2 The Economy Model

Each traded good has a **regional spot price** that adjusts each tick toward a calculated equilibrium at a configurable adjustment rate. Financial assets adjust quickly; housing adjusts slowly; commodities at medium speed. This stickiness is the source of arbitrage opportunities and meaningful price signals.

**Equilibrium price calculation:** supply is the sum of all in-region production output from player and NPC facilities. Demand has two components: derived demand (industrial consumption of inputs by local facilities) and consumer demand (NPC purchase decisions, updated each tick with a one-period lag — see Section 21). Import availability creates a price ceiling; export demand creates a floor.

**Player price influence:** a player supplying less than 20% of regional supply is a price-taker. Between 20–50%, their output decisions have minor price impact. Above 50%, they are a significant price-mover. Above 70%, their production decisions effectively set the regional price floor. This is the mechanic behind antitrust regulatory attention and the vertical integration incentive simultaneously.

<!-- C4 fix -->
**Market influence thresholds (named constants for Technical Design):**
```
PRICE_TAKER_THRESHOLD = 0.20
MINOR_PRICE_IMPACT_THRESHOLD = 0.50
DOMINANT_PRICE_MOVER_THRESHOLD = 0.70
```
When player supply share exceeds DOMINANT_PRICE_MOVER_THRESHOLD, the player's production target becomes an input floor to the equilibrium price function.

### 8.3 Raw Resource Layer

Raw resources are extracted from specific locations and cannot be relocated. Their physical attributes determine the economics of extraction.

**Geological:** Iron ore, copper, bauxite, tin, zinc, lead, nickel, cobalt, lithium, rare earth elements, gold, silver, platinum group metals, gemstones, coal, crude oil, natural gas, uranium, limestone, silica sand, potash, phosphate, sulfur.

**Biological:** Wheat, corn, soybeans, cotton, rubber, sugar cane, tobacco, coffee, cacao, cattle, pigs, poultry, sheep, fish (farmed and wild-caught), softwood and hardwood timber, pulpwood.

**Energy:** Crude oil and natural gas, thermal and coking coal, uranium. Renewables require manufactured components (solar panels, wind turbines) that are themselves products of the supply chain — connecting energy production to manufacturing.

Every deposit has Quantity (total extractable volume), Quality (grade affects processing efficiency), Depth (extraction cost), Accessibility (infrastructure required), and Depletion rate.

**Environmental consequences of extraction** accumulate over time. Above threshold levels: groundwater contamination affects local agriculture, air quality affects health metrics, soil degradation affects adjacent productivity. Environmental impact generates community opposition, regulatory exposure, and political pressure. This is a consequence system, not a morality system.

### 8.4 Processing Layer

Metallurgy (iron ore and coking coal to steel; copper to wire rod; aluminum smelting — electricity-intensive, location driven by power cost); petroleum refining (crude oil fractional distillation producing multiple simultaneous product streams — the refinery doesn't choose what to produce); chemical processing (petrochemical feedstocks to plastics, synthetic rubber, fertilizers — the same precursor chemical supply chains that feed drug synthesis, creating an overlap investigated from both directions); agricultural processing (grain milling, oilseed crushing, meat processing, dairy); timber processing (sawmill, pulp mill, engineered wood).

Bottlenecks in processing ripple up and down the chain.

### 8.5 Manufacturing Layer

**Heavy industry:** Steel fabrication, machinery, vehicles, construction materials, defense and aerospace equipment.

**Consumer goods:** Electronics (rare earth elements, copper, silicon, and plastics converging into devices), textiles and apparel (highly labor-intensive — location follows cheap labor), packaged foods, pharmaceuticals (chemical feedstocks plus R&D to active ingredients to formulated drugs), industrial and consumer chemicals.

**Components and intermediates:** Integrated circuits, bearings, fasteners — manufactured goods that are themselves inputs to other manufacturing. Controlling component manufacturing creates supplier power across multiple downstream industries.

**Vertical integration:** A player who owns extraction, processing, and manufacturing captures margin at every stage. They also own and must manage multiple complex operations, depend on their internal supply chain integrity, and have massive capital tied up with limited flexibility. Vertical integration is powerful and fragile simultaneously. That tension is a feature.

### 8.6 Distribution Layer

Trucking (flexible, driver labor market a major variable), rail freight (high volume, low cost per tonne-kilometer, inflexible routing — owning rail creates chokepoint leverage), shipping (bulk carriers, container vessels, tankers), pipeline (extremely low operating cost, enormous capital cost, permanent fixed route), warehousing (buffers supply chain variability), air freight (high cost, high speed — perishables and high-value goods).

Owning the port that handles a region's imports and exports gives toll-booth pricing power. Logistics monopolies generate political scrutiny and regulatory exposure faster than almost any other business type.

Legitimate logistics operations are the primary vehicle for large-scale smuggling. The player who owns container shipping, trucking networks, or warehouse facilities has infrastructure that can move both legitimate and illicit goods through the same channels. Section 12 explores this.

### 8.7 NPC Businesses as Active Participants

NPC businesses follow simplified versions of the same supply chain rules as the player — full simulation of production output and input consumption, labor demand and hiring, revenue and cost modeling, investment decisions, expansion behavior, and competitive response to market conditions. They do not use tile-level facility design but their economics are real.

**Behavioral profiles** determine competitive response: **Cost-cutters** respond to pressure by reducing labor and input quality — survive on margin compression, degrade product and worker satisfaction over time, vulnerable to quality competitors. **Quality players** compete on reputation and premium pricing — survive in high-income segments, vulnerable to economic downturns that squeeze premium willingness. **Fast expanders** grow aggressively into any available opportunity — overextend regularly, vulnerable to credit tightening and supply disruption. **Defensive incumbents** maintain existing position with minimal investment — hard to dislodge through established relationships and political connections, vulnerable to technological disruption.

<!-- C6 fix --> Decision logic for each behavioral profile is specified in the Technical Design. The GDD description defines the personality; the Technical Design defines the decision matrix.

NPC businesses make strategic decisions quarterly. Between decisions, they follow their behavioral profile within current constraints. This produces predictable but not scripted competitors whose patterns the player can learn and exploit.

NPC businesses that reach cash depletion enter bankruptcy. They are acquirable by the player or by rival NPC businesses — consolidation happens between NPC actors independently of the player, reshaping competitive landscapes continuously.

> [SCENARIO: scenario_npc_business_bankruptcy_on_cash_depletion]
> Test: An NPC business whose cash reserves reach zero enters bankruptcy state and becomes acquirable.
> Seed setup: NPC business with cash = 10 (near depletion), monthly costs = 15, no revenue sources active; business behavioral_profile = cost_cutter (minimum mitigation).
> Run length: 10 ticks.
> Assertion: NPC business status transitions to BANKRUPT within the run window; business appears in the acquirable assets list for the player and/or rival NPC businesses.

### 8.8 The Informal Economy

A parallel economic layer runs beneath the formal one. Black market prices respond to formal market conditions — smuggling is more profitable when tariffs are high, underground labor is more available when formal unemployment is high. The informal economy is a permanent layer of the same simulation, intersecting with the formal economy at every level.

---

## 9. Knowledge Economy

### 9.1 Research & Development

R&D is the mechanism by which technology advances. Without it, manufacturing is static.

**Facility types:** Corporate R&D labs, independent research institutions, and university research departments (combining teaching with research output — see Section 9.4).

**Research mechanics:** Projects take time proportional to ambition and team quality. Each project has a field, a target capability, a timeline of months to years, uncertainty (research can fail, produce partial results, or produce unexpected findings), and an output (a technology advancement, a patent, a publication, or a failure).

**Patents** grant exclusive commercial rights for a defined period. Competitors must license (creating Obligation), design around (costing time and R&D), or steal the technology (corporate espionage — generating Exposure and a hostile, motivated rival).

**Technology levels** propagate through the supply chain. A manufacturing plant running Tier 3 equipment is less efficient and competitive than one running Tier 5. Technology advancement is how players who start late can disrupt incumbents locked into aging infrastructure.

**Criminal R&D:** The same system applies to drug development. Illicit chemists developing novel synthetics are racing patent timelines against the legislative scheduling process — each new compound is legal until scheduled, and staying ahead requires in-house chemistry capability.

### 9.2 Banking & Financial Services

**Commercial bank:** Takes deposits, makes loans, processes payments. The player-owned bank controls who gets credit — a direct weapon against competitors and a tool for funding the player's own operations.

**Investment bank:** Facilitates large transactions. A player-owned investment bank advising companies the player wants to acquire generates a continuous stream of inside intelligence — technically illegal, practically unavoidable, not always traceable.

**Hedge fund / asset manager:** Most powerful instrument for stock market manipulation — and the most scrutinized by regulators.

**Insurance company:** Selectively denying coverage to competitors raises their risk exposure and operating costs.

**Microfinance / community development bank:** Lower margins, significant community Influence generation. The player who genuinely extends credit to people who had none builds durable grassroots loyalty that cannot be purchased directly.

**Structural power through scale:** The bank large enough that its failure would collapse regional credit markets has captured the government through scale rather than bribery. The government cannot afford to let it fail, which means it cannot meaningfully threaten it. This is not illegal. It is leverage at its most extreme.

**Player-owned banks and money laundering:** A player-owned bank is the most powerful laundering tool because the player controls the reporting mechanism. Bank regulators are specifically designed to detect this — it is the most scrutinized structure in the financial system. Full laundering mechanics in Section 12.5.

### 9.3 Legal Services

**Corporate law firm:** Every major business deal goes through a corporate lawyer. A player who owns a top firm sees every deal their clients are working on — a continuous intelligence stream.

**Litigation firm:** Can be used offensively — filing suits against competitors costs them time, money, and distraction even if suits ultimately fail.

**Regulatory and lobbying firm:** Navigates and influences regulatory processes. Drafts comments on proposed regulations, places former clients in regulatory positions. The regulatory firm is a licensed corruption interface — technically legal, structurally corrupt.

**Criminal defense firm:** When Exposure converts to active charges, criminal defense is the first line of resistance. Building or retaining a top criminal defense firm before it's needed is one of the highest-value preparations the player can make.

**International law and trade firm:** Cross-border transactions, trade dispute resolution, sanctions navigation. Essential for any multi-national operation.

**Law firms as intelligence assets:** A top firm representing the dominant bank, the regional government, the largest manufacturer, and the main media company has embedded visibility into every powerful institution in the region.

### 9.4 Education

*[Expansion scope — see Feature Tier List. Education facilities (vocational schools, secondary schools, universities) are not built in V1. V1 models education level as a regional attribute that shifts slowly over time based on economic conditions, not as a player-buildable facility. The Bootstrapper should not generate facility interface specs for this section.]*

**Vocational school:** Produces trade-skilled workers fastest (1–2 in-game years). Directly addresses common labor market bottlenecks.

**Secondary school:** Raises the general education level of the population. In regions with low secondary completion rates, building schools is the highest-leverage long-term investment available — and the highest non-monetary Influence investment.

**University:** Produces professionals over 4–6 in-game year programs. Generates academic research. Creates an institutional anchor that attracts other businesses. Educated populations develop distinct political cultures more resistant to populist capture.

**Specialized research university:** Produces highest-skill graduates and significant research output. Extremely expensive. Extremely high Influence for the founding donor.

**Online learning platform:** Scales education beyond physical infrastructure constraints. A dominant platform holds effective monopoly on skills certification with pricing power over the entire labor market.

**Education as non-monetary Influence:** A player who builds genuine educational access builds trust that cannot be purchased. The community knows who helped them. That trust translates into workers who don't whistle-blow, voters who support their allies, officials who answer their calls.

---

## 10. Power, Exposure & Consequence Systems

This section describes the architecture that sustains late-game challenge. These systems run from the beginning of the game but become dominant as the player's position grows.

### 10.1 The Three Late-Game Dynamics

**Influence** — the ability to make things happen through relationships and leverage. Not a number — a network of relationships. Unlike money, Influence cannot be simply spent — it must be maintained through ongoing reciprocal relationships and can evaporate with a scandal, a change in government, or a betrayal. Full architecture in Section 13.

**Exposure** — the accumulated body of evidence that could damage the player if surfaced. Created by every legally or socially questionable action, living in the world as specific evidence tokens attached to specific people, locations, or records. Exposure doesn't hurt the player directly — only when someone finds it and acts on it.

**Obligation** — favors owed to people who helped the player reach their current position. Obligations don't expire. They accumulate interest in the form of escalating demands. The bigger the help received, the more dangerous the obligation that comes with it.

These three interact constantly. Influence lets the player suppress Exposure. Gaining Influence usually increases Obligation. Calling in Obligations reduces them but generates new Exposure. The player is always managing a triangle of risk.

### 10.2 The Evidence Trail

Every legally or morally questionable action generates evidence tokens:

**Financial** — wire transfers, shell company records, unusual cash flows. **Testimonial** — witnesses who saw or heard something relevant. **Documentary** — contracts, emails, memos that shouldn't exist. **Physical** — meetings that were observed, locations that were visited.

Evidence lives in the world attached to specific NPCs or locations. It persists until actively neutralized.

**What the player knows and doesn't know:** The player is aware of evidence they directly generated — they know what they did. They are not automatically aware of secondary evidence generated by NPCs who witnessed, documented, or reported on their actions. A bookkeeper who noticed financial irregularities and mentioned it to a colleague has created a testimonial evidence token the player doesn't know exists unless they're actively monitoring that bookkeeper. The evidence map shows the player's *working knowledge* of their exposure — not an omniscient registry. The gap between what is on their map and what investigators actually hold is the primary source of late-game surprise.

Awareness of unknown evidence comes through: intelligence investment (monitoring people who might have evidence), NPC loyalty (a trusted contact warns them that questions are being asked), and observation of investigation behavior (when investigators act on evidence, the player can sometimes infer what they have from which records they're requesting).

**Neutralization options:** Buy it (pay the holder — creates Obligation and leaves a financial trail); destroy it (direct action — risky, leaves its own evidence of destruction); discredit it (undermine the holder's credibility — costs Influence); bury it (distraction through friendly media — temporary, evidence remains); leverage it (the person holding evidence against the player has their own Exposure — establish mutual assured destruction, where neither party can move). The last option creates webs of mutual vulnerability that characterize real elite corruption. The most powerful position is not clean hands — it is having enough dirt on enough people that everyone is too exposed to move.

### 10.3 The Investigator Engine

**Journalists** are individual significant NPCs with investigation specializations, funding sources, risk tolerances, and career motivations. A financial journalist doesn't pursue labor violations. A labor reporter doesn't follow money. Investigation meters fill over time when the player is a high-profile target in their beat. Critically: fired journalists don't disappear — they take their notes somewhere else and are now specifically hostile.

> [SCENARIO: scenario_fired_journalist_becomes_hostile_investigator]
> Test: A journalist NPC fired from their employer retains all investigation data, transfers their active investigator meter to a new outlet, and has their relationship_score with the player set to hostile (negative) within the same tick.
> Seed setup: Journalist NPC with investigator_meter.current_level = 0.4 (actively building case); player fires journalist NPC from their employed outlet.
> Run length: 5 ticks.
> Assertion: Journalist NPC employer has changed; investigator_meter.current_level is preserved (±0.05); NPC relationship_score with player is negative; NPC investigation continues filling at new outlet.

**Regulators** operate on bureaucratic timelines — slower than journalists, more dangerous in outcome. Respond to: political pressure from player allies, revolving-door hiring, legal delay tactics, and agency capture through political Influence.

**Whistleblowers** emerge from inside the player's own organization. Any employee whose satisfaction drops below threshold and who has witnessed legally significant activity becomes a risk. The game tracks which employees know what. Managing employee satisfaction is not just about productivity — it is about controlling the information environment inside your own operation.

**Law enforcement special units** (anti-trafficking, anti-narcotics, organized crime task forces) operate with specific methodologies for each criminal sector. They build victim cooperation over long periods, work with NGO networks for intelligence, cooperate internationally, and pursue financial flows with asset forfeiture authority. They are harder to corrupt than general law enforcement — their mandate and training make them more resistant.

**International agencies** respond to cross-border criminal activity. They have no stake in local political corruption and cannot be neutralized through normal domestic channels. Their involvement changes the character of any investigation.

### 10.4 The Obligation Network

Every favor received creates a node in a network the player can visualize — showing who they owe, what the favor was, when it was received, and how long the other party has been waiting.

**Obligation escalation:** What people ask for in return escalates unpredictably. A politician accepts a campaign contribution, then asks for a hire, then for influence over a regulatory decision, then for something that crosses into criminal exposure. Each step seems incremental. The cumulative commitment is not.

**Resolution options:** Pay it (do what's asked, accept the Exposure); renegotiate (offer something different — requires Influence, may fail); eliminate the relationship (destroy the person professionally or personally — expensive, creates new evidence, generates new enemies); preemptive exposure (surface them before they surface you — damages the player but removes the threat).

The Obligation network creates a ratchet effect. The further the player goes, the harder it becomes to stop.

### 10.5 Consequence Thresholds

Certain player states change how other NPCs and institutions weigh the player in their own decision-making — not as a scripted trigger, but as the natural consequence of being noticed. Specific conditions and their realistic outcomes:

Reaching significant net worth → a financial journalist who was already covering wealth stories adds the player to their watchlist and begins building a profile. Controlling 40%+ of a regional market → the competition regulator, whose job is to monitor market concentration, opens a preliminary inquiry. Becoming the largest regional employer → union organizers, who were already working the region, identify the player's workforce as their highest-value target. Winning a national election → foreign intelligence services begin standard monitoring of a new head of state. Owning a media outlet → political opponents begin investigating the ownership for conflicts of interest. Entering the drug distribution market at scale → narcotics investigators, already working the region, elevate the player to a priority target. Operating a trafficking network → international anti-trafficking agencies begin flagging anomalies in movement and financial patterns. Destabilizing a foreign political situation → international bodies and foreign intelligence services add the player to their active monitoring lists.

> [SCENARIO: scenario_journalist_watchlist_on_wealth_threshold]
> Test: When the player's net worth crosses the "significant net worth" threshold, a financial journalist NPC already active in the region adds the player to their watchlist and begins building an investigator profile.
> Seed setup: Region contains a Significant NPC journalist with beat=financial, investigator_meter not targeting player; player net worth starts below threshold; player wealth increases across the threshold within the run.
> Run length: 10 ticks after threshold crossed.
> Assertion: Journalist NPC's investigator_meter has a new entry targeting the player; meter status = building.

> [SCENARIO: scenario_antitrust_inquiry_on_market_concentration]
> Test: When the player controls ≥40% of regional supply for any single good, the competition regulator NPC opens a preliminary inquiry.
> Seed setup: Region has a Significant NPC with role=regulator, specialization=antitrust; player market share for good X is 38%; player increases production to bring share to 42%.
> Run length: 5 ticks after threshold crossed.
> Assertion: Regulator NPC has an active InvestigatorMeter targeting the player with status=building; a consequence queue entry of type investigation_opens is present.

> [SCENARIO: scenario_union_targets_largest_employer]
> Test: When the player becomes the largest employer in a region, existing union organizer NPCs add the player's workforce as their highest-priority target.
> Seed setup: Region has at least one Significant NPC with role=union_organizer actively working the region; player becomes the largest employer (surpassing previous largest by at least 1 worker).
> Run length: 10 ticks.
> Assertion: Union organizer NPC's primary target_id is updated to reference the player's workforce; organizer NPC's behavior action queue prioritizes player-workforce organizing actions.

> [SCENARIO: scenario_expansion_trafficking_international_attention]
> Test: Operating a trafficking network at threshold scale causes international anti-trafficking agency NPCs to flag financial and movement anomalies against the player.
> Seed setup: Player operates a trafficking network (Section 12.3) with volume above detection threshold; international anti-trafficking agency NPCs exist in world state.
> Run length: 30 ticks.
> Assertion: International agency InvestigatorMeter targeting player has status=building; at least one financial evidence token and one movement pattern token are associated with the investigation.
> Scope: EXPANSION — Bootstrapper skips this tag for V1.

None of these are switches being thrown. They are existing institutions doing their jobs when the player's actions cross into the range those institutions monitor. The player can anticipate them because the thresholds are logical. They cannot be avoided entirely — being powerful means being watched.

### 10.6 The Legal Process and Prison

Legal consequences operate as a multi-stage process with player agency at each stage. There is no binary caught/not caught.

**Investigation:** Law enforcement has opened a case. Evidence is being gathered. The player may or may not know this is happening — awareness depends on whether they have contacts inside the investigating agency, whether a loyal employee has noticed unusual inquiries, or whether the investigation's behavior (records requests, surveillance activity, interviews with their associates) is observable enough to infer. A player with deep law enforcement relationships may learn of an investigation on day one. A player with no such contacts may not discover it until the arrest. This is where most defensive action takes place for players who know — and where the most dangerous cases are built for players who don't.

**Arrest (minor charges):** The player character is detained temporarily. Bail is available. Operations continue under NPC management during detention. The arrest is public — it generates Exposure that triggers NPC relationship recalibrations.

**Charges filed (felony level):** A formal accusation with specific charges. The player's legal defense activates. Discovery proceedings mean both sides see each other's evidence — the player learns what investigators actually have. Plea negotiation and case dismissal based on procedural challenges are both available.

**Trial:** Outcome is probabilistic based on evidence quality, witness reliability (affected by the player's prior management of witnesses and their satisfaction), and judicial factors (affected by the player's political Influence and applicable corruption). A well-prepared defense improves outcomes significantly.

**Prison:** Prison is not game-over. It is a heavily constrained operational state. The player character is physically confined. External operations continue under NPC management at significantly reduced efficiency. Obligations continue accumulating. Rivals probe. The player can interact with other prisoners (some high-value criminal NPC contacts serve time in the same facility), manage health, and conduct limited external communications through the prison's systems.

Prison exit options: serving the full sentence (the conviction record remains and affects future outcomes, but the specific exposure from the convicted offense is cleared); successful appeal (early release, possible exoneration); pardon (requires substantial political Influence with someone positioned to grant it — creates significant Obligation); escape (a risky operation requiring external contacts and planning, generating a permanent additional charge and dramatically elevated enforcement priority); and prison death (rival criminal organizations sometimes see imprisonment as the ideal moment to eliminate a threat).

---

## 11. People in the Grey

### The Landscape

People who operate in grey and black areas exist throughout the normal social world. They are not a separate layer, not a special network, not something the player unlocks. They are people — with motivations, memories, relationships, and limits — who happen to deal in things that don't appear on formal invoices.

A fixer who quietly resolves problems for a fee. A customs official who can be reached through a particular lawyer. A corporate intelligence contractor who does background checks that go further than background checks. A money manager who asks fewer questions than others. A logistics operator who doesn't always know exactly what's in some of their containers. These people exist in the same social world as everyone else, and they find each other — and the player — the same way everyone finds everyone: through reputation, through mutual contacts, through circumstance.

### How Contact Works

A legitimate business owner facing a problem they don't want to handle through official channels mentions it to their lawyer. The lawyer happens to know someone. That someone calls. The player didn't search for them — they arrived because of who the player is and what they've been doing.

A successful criminal operation generates a reputation in the communities where it operates. Someone who knows someone hears about it. An approach is made, carefully, through an intermediary. What's offered might be useful or might not be. The player decides.

Even a player who has never committed a crime may receive approaches from people operating in grey areas — because grey-area operators look for anyone with resources, discretion, and a problem that doesn't have a clean solution. Whether the player engages is entirely their choice.

This is the mechanism by which capability escalation works: not by unlocking a hidden network, but by the normal social dynamics of people finding people who can do what they need done. Each relationship deepens through use and trust. Each deepened relationship changes what the other person is willing to discuss.

### Capability Escalation Through Relationship

The escalation from "person who handles discreet inquiries" to "person who can make things permanently disappear" happens through relationship depth, not through a progress bar. The security director who has been doing quiet background work for two years is a different conversation partner than the security director met yesterday. What they're willing to offer, what they expect in return, and what leverage they accumulate over the player all change as the relationship deepens.

The player does not pursue these escalations. They emerge from the logic of the relationships involved. The security director who knows the player ordered evidence destruction has knowledge that changes the terms of their relationship — they now have leverage, and they know it. What they do with that leverage follows their motivation model. Some become more valuable partners. Some become threats. The player made the relationship what it is through what they asked for, and they live with what it became.

No action in this space carries a label. Nobody offers "assassination services." What exists is a relationship with a person who has certain capabilities, and within that relationship, at a certain depth, certain conversations become possible. The player can decline. Declining changes the relationship in its own way.

### What Others Know About the Player

The player's street reputation — how people who operate in these areas perceive them — is tracked separately from their public reputation. It reflects what these people believe about the player's discretion, their willingness to be involved in certain things, their reliability, and their danger level. This reputation travels through the same social channels that everything else travels through: word of mouth, inference from observed actions, and the testimony of people who've dealt with the player before.

High street reputation means more approaches, better terms, and more capable contacts reaching out. Low street reputation means fewer approaches and worse terms. Damaged street reputation — through unreliability, betrayal, or being perceived as too visible — means approaches dry up and some people who previously cooperated begin to distance themselves.

The same social mechanics that bring grey-area contacts to the player are how criminal operations find their suppliers, their workforce, their distribution channels, and their protection. Section 12 describes those operations in full; Section 11 describes the social substrate through which every one of them is initiated and maintained.

---

## 12. The Criminal Economy

### 12.1 Design Principle

The game does not judge criminal activity — it simulates it. There is no morality score. A player can build a global drug cartel, traffic human beings, corrupt entire governments, and die of old age in a mansion. What the game guarantees is not punishment. It guarantees consequence. Every criminal operation generates systemic pressure that grows over time: law enforcement attention scaling with operation size, rival criminal organizations that want territory, victim NPCs with memory and agency, and international agencies that respond to cross-border activity regardless of local political protection.

Criminal operations are entered, staffed, supplied, and protected through the same social mechanics that govern all relationships in the game (Section 11). No criminal operation begins by clicking a menu option — it begins when the player has the right relationships, the right contacts, and the right reputation to make it possible. A drug distribution network requires someone willing to supply, someone willing to move product, and someone willing to look the other way. Each of those is a relationship, found and cultivated through normal social channels.

### 12.2 The Drug Economy

The drug trade follows the same supply chain logic as the legitimate economy. Each stage has different risk profiles, different margins, and different operator types.

**Production:** Cannabis — cultivatable in multiple climates, legal in some nations; indoor cultivation detection risk comes from power consumption and ventilation requirements; where legal, the player competes with criminal suppliers who undercut on price by avoiding taxes and regulatory costs. Cocaine — coca leaf cultivation, multiple chemical processing stages, each generating traceable waste; controlled precursor chemicals must be diverted from legitimate pharmaceutical and chemical supply chains. Heroin and opioids — poppy cultivation or synthetic pharmaceutical precursor paths; the synthetic path crosses directly into legitimate pharmaceutical industry corruption. Methamphetamine and synthetic stimulants — fully chemical synthesis; highly portable production; very toxic process with physically detectable consequences including lab explosion risk. Designer drugs — manufactured to remain technically legal by staying ahead of scheduling; requires in-house chemistry R&D.

**Processing:** Labs require concealment, controlled equipment and chemical inputs, skilled labor who are also witnesses, and waste management creating both toxic and evidential trails. Facility design directly affects detection risk.

**Wholesale distribution:** Moving product from production to regional markets is the highest-risk segment for seizure. Legitimate logistics infrastructure is the primary vehicle. Route security requires corrupted officials at chokepoints (customs, border control, port authority) — each corruption is an Obligation node with escalating demands.

**Retail distribution:** Street-level dealer networks (maximum coverage, maximum law enforcement contact), delivery service models (low profile, dependent on communications security), venue-based distribution (concentration risk, laundering opportunity), and online markets (no physical exposure, cyber crime unit attention at delivery).

**Law enforcement response:** Street dealing generates patrol attention. Mid-level distribution generates narcotics unit attention. Large-scale supply chains generate multi-agency task force attention and international cooperation. Investigative techniques include surveillance, controlled buys, informant networks built from arrested operatives, financial investigation, wiretap, asset forfeiture, and international coordination.

**The informant problem:** Every person in the criminal network is a potential informant the moment they are arrested. Cooperation value to law enforcement is an extremely powerful incentive. The player manages this through high loyalty payments (making cooperation financially irrational), mutual incrimination structures, compartmentalization (ensuring operatives don't know enough to be useful even if they cooperate), or violence to deter cooperation (which escalates law enforcement response dramatically and generates new Exposure).

### 12.3 Human Trafficking

*[Expansion scope — see Feature Tier List. This section is included in the GDD for complete design documentation. No implementation work happens for V1. The Bootstrapper and any V1 interface generation should skip this section entirely.]*

**Design note:** This system is included because it is a defining feature of real criminal economies and the game simulates the full criminal world without sanitizing it. It is the system with the most severe mechanical consequences — no other criminal activity generates the same breadth and intensity of response. The game represents trafficking at the operational level: logistics, economics, law enforcement interaction. No explicit content depicting exploitation.

**Labor trafficking:** Coerced or deceptive recruitment of workers for labor exploitation — debt bondage, document confiscation, physical confinement. These workers generate legitimate-appearing economic output at near-zero cost.

**Sex trafficking:** Coerced, deceptive, or forced commercial sexual exploitation. The game models the criminal infrastructure: recruitment, transportation, coercion, and venue operations.

**Smuggling (consenting):** Moving people across borders they cannot cross legally. Not inherently exploitative, though it easily becomes so. The distinction from trafficking is whether movement is consenting and whether the person is free on arrival.

**Victims as NPCs:** Victims are significant NPCs with memory, motivation, and social connections. They have families who notice they're missing. They have communities they came from. They have a level of trust in institutions that determines whether they will cooperate with law enforcement. They have survival instincts and agency — they look for escape opportunities. Every victim the player's operation holds is a security risk.

**Why this is the hardest criminal path:**

Victim agency means every victim is actively trying to escape, contact authorities, or communicate with family. Operations that fail to contain this generate cooperative witnesses.

Family and community response: Missing persons generate reports. A trafficking operation that takes fifty people has generated fifty families with fifty reasons to pursue investigation — persistent, motivated, and not corruptible through normal channels.

NGO and international agency attention: These organizations have no stake in local political corruption and cannot be neutralized by tools that manage domestic law enforcement.

Cross-political consensus: Human trafficking generates genuine cross-political consensus in opposition. A politician who can plausibly connect a rival to a trafficking operation has a weapon that transcends normal political calculation.

Internal betrayal rate: Operatives who participated in trafficking have witnessed crimes with severe sentencing. Their cooperation value to law enforcement is extremely high.

### 12.4 Other Criminal Operations

**Weapons trafficking:** Procurement from legal or grey-market channels (corrupt military, defense contractor theft) and diversion into criminal markets or conflict zones. Weapons sold appear in the world — used in crimes, in conflicts, in assassinations. The game tracks downstream effects and so do investigators. Arms embargo violations generate intelligence service attention that does not respond to normal criminal management tools.

**Protection rackets:** Systematic extortion of legitimate businesses. Generates significant community grievance. Business owners paying are hostile but dependent — potential witnesses who are also afraid, and fear is often stronger than grievance until law enforcement can credibly protect them.

**Counterfeiting:** Currency counterfeiting generates central bank-level response. Pharmaceutical counterfeiting is the most severe — counterfeit drugs kill people, and deaths are traceable to the product. Document fraud is essential infrastructure for trafficking operations and for the player's own operational security.

**Cybercrime:** Financial fraud (highly scalable, geographically dispersed, difficult to attribute); ransomware and extortion (attacking hospitals kills patients — generates national security responses); corporate espionage (hacking competitor systems for R&D data, trade secrets, strategic plans).

### 12.5 Money Laundering

Criminal revenue is cash that cannot be explained. Laundering converts it into explainable wealth. Every method generates its own evidence trail.

**Structuring (smurfing):** Depositing cash in amounts below reporting thresholds across many accounts. Generates patterns that financial intelligence units are trained to detect.

**Shell company chains:** Cash moves through a series of companies across multiple jurisdictions. Each company is a node that can be traced and requires a person to front it — each person is a witness.

**Real estate:** Purchase property with cash through intermediaries, sell it at inflated price to another entity the player controls, receive clean funds as apparent sale proceeds. Laundering pressure elevates property prices in affected markets — affecting legitimate buyers and generating political consequences.

**Trade-based laundering:** Over- or under-invoicing legitimate trade transactions to move value across borders. Requires an import/export operation and a cooperative counterpart.

**Cryptocurrency:** Pseudonymity (not full anonymity). Blockchain transactions are traceable by sufficiently resourced investigators — requires mixing services, privacy coins, and layered transaction structures with their own traceability.

**Cash business commingling:** Mixing criminal proceeds with legitimate revenue in naturally high-cash-volume businesses. Classic and effective. The business's employees and suppliers are potential witnesses to the discrepancy between apparent volume and actual customer traffic.

**The Financial Intelligence Unit:** The banking system includes an FIU processing suspicious activity reports and running pattern analysis. Owning a bank is the most powerful laundering tool and the most scrutinized structure simultaneously.

### 12.6 Law Evasion Architecture

**Operational Security:** Compartmentalization (no single operative knows the full picture); communications discipline (nothing incriminating in writing, coded language, physical meetings for sensitive discussions); physical security (surveillance detection routes, dead drops); front organizations (all activity attributed to entities with plausible alternative explanations); non-traceable personnel (workers recruited through multiple intermediaries).

**Corruption:** Maintaining relationships with law enforcement (tip-offs before raids, evidence suppression), prosecutors (cases that don't proceed, evidence that gets lost), judiciary (favorable rulings in corruptible jurisdictions), political oversight (legislative direction away from player's operations). Each corrupted official is a liability: they know what was arranged, and they will cooperate with investigators if a better deal is available.

**Legal defense:** Jurisdictional complexity (coordination barriers across jurisdictions); evidence challenges (attacking legality of collection); witness management (legitimate relocation through to threats and intimidation); parallel proceedings; asset protection (trusts, offshore entities, nominees creating barriers to forfeiture).

**Alternative identities:** The player can establish and maintain alternative identities with clean documentation. All actions taken under an alternative identity attach to that identity's evidence record, not the real identity's. When an alternative identity is burned, its evidence record enters a dormant state — the tokens exist but are only actionable if someone connects the burned identity to the real player. This connection can be made by surviving network members, by forensic investigation, or by rivals who've done their homework. Every burned identity is a permanent latent liability.

The same identity mechanics used here for law evasion are used in the infiltration path (Section 12.9), where a cover identity is built for the inverse purpose — entering a criminal organization to destroy it rather than to protect oneself from prosecution. The architecture is the same; the intent is opposite.

### 12.7 Criminal Organization Structure

**Street gang:** Controls territory measured in city blocks. Revenue from protection rackets, retail drug sales, theft. High violence, low sophistication, highly visible. The most accessible entry point and the most dangerous place to stay long-term.

**Organized crime family / syndicate:** Mid-scale with functional specialization, hierarchical structure, and significant legitimate business integration for money laundering. Internal hierarchy creates both efficiency and betrayal risk — every layer is a potential informant.

**Cartel:** Industrial-scale enterprise organized around a specific commodity. Operates across multiple nations. Private military capacity. Corrupts governments at national level. In territories it controls, effectively governs — providing employment, security, dispute resolution, and social services where the legitimate state has failed. Population that depends on a cartel for employment and security will not cooperate with law enforcement because the cartel is the only functioning institution in their world.

**Transnational criminal network:** Loosely affiliated specialists collaborating on specific operations without permanent organizational structure. No fixed hierarchy to dismantle. Extremely difficult to prosecute and to infiltrate.

### 12.8 Pre-Existing Criminal Organizations

The world generates NPC criminal organizations at world creation based on the corruption and criminal activity baseline parameters. These organizations own territory, employ NPCs, operate their own criminal supply chains, corrupt their own officials, and pursue their own strategic goals. They use the same motivation and memory architecture as every other significant NPC category.

A player entering a region where an organization already operates faces real choices: cooperate (pay tribute or form partnership), compete (entering their market generates response), infiltrate (see Section 12.9), or coexist at smaller scale below their notice threshold. There is no neutral. Presence in their territory is observed and responded to.

NPC criminal organizations also compete with each other, form and break alliances, and shift in power — generating a dynamic criminal landscape that the player must navigate even when they didn't cause the changes.

### 12.9 The Infiltration Path

A player can enter criminal enterprises with the goal of destroying them from within. The cover identity mechanics used here are the same alternative identity system described in Section 12.6, applied to a different purpose. The architecture — parallel identity records, dormant exposure on burn, latent liability from surviving witnesses — is identical. What differs is the intent and the context: here the cover identity is built to penetrate an organization rather than to evade prosecution, and the people who might eventually connect the identities include both criminal survivors and the very law enforcement the player was ostensibly assisting.

**Building a criminal cover identity:** The player maintains two parallel identity tracks — their real identity and a cover identity building a criminal career. Cover identity management requires actually committing crimes as the cover identity — criminal networks verify members through demonstrated participation, and the people harmed are real NPCs. Trust takes time. There is no shortcut.

**The cooperation dilemma:** A player running an infiltration operation controls when to trigger the takedown. Waiting longer builds stronger cases and reaches higher-level targets. But waiting longer means crimes continue against real NPCs — crimes the player could prevent by ending the operation earlier. The game tracks this. NPCs harmed during the infiltration period remember the player was there and didn't intervene. Some will cooperate with the ultimate prosecution. Some won't. The calculus of evidence sufficiency versus ongoing harm is the moral core of the infiltration path. The game does not resolve it.

**Burning the network:** The player must simultaneously secure admissible evidence, extract cooperative witnesses, prevent evidence destruction before law enforcement arrives, and protect their own identity — because burned operatives name the person who set them up, and that person becomes the highest-priority target in the organization's remaining capacity.

**Playing both sides:** Simultaneously running a criminal operation and cooperating with law enforcement against competing organizations. Never allowing either side to discover the other relationship. The criminal network's discovery response is physical and immediate. Law enforcement's discovery response is legal — prosecution using everything the player provided, plus the criminal activity observed during the cooperation period.

### 12.10 Criminal Territorial Conflict

Criminal territorial conflict follows a natural escalation model: economic competition → intelligence gathering and harassment → targeted violence against assets and property → violence against personnel → open territorial warfare → resolution through one party's elimination, absorption, or negotiated partition.

Each stage has mechanical costs and Exposure implications. Criminal warfare is one of the few situations where law enforcement attention increases regardless of the player's corruption coverage — because open conflict generates evidence that doesn't need a complainant. Bodies and burned buildings are self-reporting. Criminal warfare is therefore strategically dangerous even for heavily protected players: it activates investigation capacity that was previously dormant.

> [SCENARIO: scenario_criminal_warfare_activates_dormant_investigation]
> Test: When the player initiates or participates in criminal territorial conflict that generates physical evidence (violence events), law enforcement investigator meters activate even when the player has corruption coverage that suppresses other investigation types.
> Seed setup: Player has active corruption nodes covering regional law enforcement (suppressing standard narcotics investigation); player initiates criminal warfare escalation generating at least 3 violence evidence tokens in 10 ticks.
> Run length: 20 ticks.
> Assertion: At least one law enforcement InvestigatorMeter targeting the player has status=building despite active corruption coverage; meter's fill source references the violence evidence tokens, not the suppressed investigation type.

The player can initiate, escalate, de-escalate, or end conflict at each stage. They can offer negotiation, seek mediation through mutual contacts, use political influence to direct law enforcement against rivals while maintaining their own protection, or commit to total war.

### 12.11 Addiction as a Regional Consequence

Drug use by NPCs produces addiction according to a regional vulnerability model. Populations with high unemployment, low social cohesion, and poor institutional trust have higher addiction conversion rates.

**Addiction progression states:** Casual use → Regular use → Dependency → Active addiction → Recovery or terminal decline.

**Behavioral effects by state:** Casual use produces minimal behavioral change and slightly increased drug market demand. Regular use produces reduced work performance and increased drug spending. Dependency produces significant work incapacity, increased secondary crime motivation (stealing to fund addiction), and relationship strain. Active addiction produces substantial work incapacity, active criminal behavior to fund use, serious health decline, and family and community impacts visible at district level. Terminal decline produces health failure or institutional intervention.

**Recovery:** Possible at all stages before terminal. Requires recovery infrastructure — medical treatment, support programs, stable housing, employment — all of which the player can invest in or neglect. A player who invests in addiction treatment in a region they've flooded with drugs creates an interesting situation: they are simultaneously the source of the problem and the provider of the solution, building community Influence while maintaining revenue from addiction. The world will eventually notice this contradiction.

> [SCENARIO: scenario_treatment_investment_generates_influence_despite_source]
> Test: A player who operates a drug distribution network AND funds addiction treatment infrastructure in the same region generates positive community trust/influence gain from the treatment investment, even while the drug operation generates grievance.
> Seed setup: Player operates drug distribution at moderate scale in region; player also funds an addiction treatment facility (active and serving recovering NPCs); community grievance is elevated from drug activity.
> Run length: 90 ticks.
> Assertion: Region's community trust score for the player has increased from the treatment investment (treatment_influence_delta > 0); this gain is tracked separately from the grievance generated by drug operations; net effect may be positive or negative but both components must exist with correct sign.

**Regional consequences of high addiction:** Labor force participation declines, healthcare costs rise, secondary crime increases, social stability falls, inequality rises, political pressure for drug policy change intensifies. A region with 15%+ active addiction rates in the working population experiences visible economic degradation that feeds into every other simulation system.

> [SCENARIO: scenario_addiction_rate_produces_economic_degradation]
> Test: When regional active addiction rate reaches 15%+ of the working population, measurable economic degradation occurs across at least three simulation metrics: labor force participation, secondary crime rate, and social stability score.
> Seed setup: Region with addiction_rate = 0.05 at start; drug distribution network operating for sufficient scale to raise addiction rate to 0.15+ within the run; no other major negative economic events.
> Run length: 365 ticks (1 in-game year after threshold crossed).
> Assertion: Region.labor_force_participation has decreased by ≥5% from baseline; Region.secondary_crime_rate has increased; Region.social_stability_score has decreased; all three changes are statistically attributable to the addiction rate change.

### 12.12 Systemic Consequences of Criminal Dominance

Criminal dominance degrades every simulation parameter in the affected region: property values fall, legitimate businesses face protection costs or refuse to operate entirely, public services deteriorate as tax revenue falls, educational outcomes decline, health outcomes decline, social trust collapses.

> [SCENARIO: scenario_criminal_dominance_degrades_regional_economy]
> Test: A region where a criminal organization (player or NPC) reaches dominant control (criminal_dominance_index ≥ 0.7) shows measurable degradation in property values and legitimate business operating costs within a defined period.
> Seed setup: Region with criminal_dominance_index = 0.3; criminal organization grows to dominance_index ≥ 0.7 by tick 180; no other major economic shocks.
> Run length: 90 ticks after dominance threshold crossed.
> Assertion: Region.avg_property_value has decreased by ≥10% from pre-dominance baseline; at least 2 NPC businesses report elevated protection_cost in their expense ledger; at least 1 NPC business exits the region or enters bankruptcy citing protection costs.

Criminal dominance also creates a political actor. Criminal organizations can mobilize communities through employment, dependency, and fear; fund campaigns through laundering channels; threaten candidates; provide intelligence on political opponents. In regions where they are the dominant employer and security provider, they hold the local government hostage — the government cannot act against them without destabilizing the region's economy and security simultaneously. This is an accurate model of how criminal dominance interacts with governance.

---

## 13. Influence Architecture

### 13.1 The Fundamental Principle

Influence is the ability to make things happen through relationships and leverage. It is not money. Money can purchase shortcuts to Influence and amplify the effect of existing Influence. But Influence itself comes from other people's willingness to act on the player's behalf because they trust them, fear them, are obligated to them, or believe in what they represent.

The game does not track a single Influence number. It tracks a network of relationships, each with a type, an intensity, a direction, and a history. When the player attempts to use Influence to achieve something, the game evaluates: do they have the right kind of relationship, with the right person, strong enough to support this request?

### 13.2 The Four Types of Influence

**Earned Influence (Trust-Based):** Built through consistent, genuine action over time. Slowest to accumulate. Most durable. Cannot be purchased directly. Cannot be destroyed by a single scandal because it's distributed across hundreds of individual relationships, each with its own history. Sources: being a good employer, genuine community investment, honest dealing, personal integrity maintained over years, building institutions that outlast the player's direct involvement.

**Obligated Influence (Debt-Based):** Built through favors extended — the Obligation Network from Section 10.4. Can be built with or without money. Sources: helping people before they had power, backing candidates before they won, providing loans when banks wouldn't, offering jobs when needed.

**Fear-Based Influence (Leverage):** Knowing something damaging. Being capable of something harmful. Can be built without money — through witnessing damaging events, being present when powerful people act badly, accumulating evidence through proximity. The person who knows where the bodies are buried doesn't need to be wealthy.

**Ideological Influence (Movement-Based):** Leading something people believe in — a political movement, a union, an environmental campaign, a cultural institution. The leader of a mass movement has Influence that money cannot replicate because the Influence is distributed across every member of the movement. High potential scale, slow to build, fragile to personal scandal.

### 13.3 Influence and Money

Money can create Obligations faster, purchase access, and amplify reach. It cannot substitute for the right type of relationship in the right context.

A union leader who organizes 40,000 workers can shut down every factory in a region simultaneously. That is more Influence over industrial output than a factory owner who employs 500 people. The union leader doesn't need to be wealthy.

A university rector who has trained every doctor, lawyer, engineer, and civil servant in a region over 30 years has the most extensive professional network in that region. Former students answer their calls. They are not wealthy. They are deeply Influential.

**The Influential Non-Billionaire is a full play path** — less liquid, less flexible, more durable, more legitimate, and more resistant to the pressures that destroy the billionaire, because their Influence doesn't depend on maintaining a financial position and you cannot expose dirt on someone who hasn't done anything dirty.

### 13.4 Rebuilding Influence After Loss

**Trust-based Influence** is the hardest to rebuild. An NPC who reduced their trust based on betrayal has a memory of the specific event. Rebuild requires time (memory fades in intensity over years, not months), demonstrated behavioral change over multiple interactions, and often a specific restorative action. Some trust relationships are permanently partially damaged — an NPC who was seriously harmed may never restore to pre-harm trust regardless of subsequent behavior. This permanent partial loss is by design.

**Obligation-based Influence** rebuilds fastest — new obligations can be created through new favors. The limitation: obligation networks where trust has also been damaged are less stable; obligated NPCs who also distrust the player will defect when given adequate incentive.

**Fear-based Influence** rebuilds by re-demonstrating capability. If the player's capacity to threaten was diminished, fear-based relationships evaporate quickly — NPCs stop being afraid of someone they no longer believe can harm them.

**Movement-based Influence** is the most fragile. A disgraced movement leader may never recover with that constituency. Rebuilding a different movement with a different identity or constituency from scratch is available; rebuilding the same movement after a scandal is nearly impossible.

**The floor principle:** No Influence type fully rebuilds to its pre-loss level by default. A player who lost 70% of their trust-based Influence through a major scandal and rebuilds over years may recover 50–60% of the original level — the remaining gap is the permanent record of the event. The more catastrophic the original loss, the higher the permanent floor of irreparability.

---

## 14. The World Response System

### 14.1 The Core Principle

The world doesn't judge the player. It responds to the player.

The player who exploits and harms doesn't receive a morality penalty. They receive: workers who are dissatisfied, leave, or organize; communities that are hostile and support anyone who opposes them; regulators under pressure from constituents to act; journalists with motivated sources; politicians who campaign against them and win votes doing it; competitors who find allies in their enemies; partners who distance themselves before consequences arrive.

The player who is a genuine positive force doesn't receive a virtue bonus. They receive: workers who are loyal, productive, and don't whistle-blow; communities that actively support their interests; regulators with no motivated complainants; journalists with no hostile sources; politicians who benefit from association with them; partners who actively refer business their way.

None of this is scripted. Every response comes from an NPC or institution following its own logic in response to observable conditions.

### 14.2 The Community Response Model

Communities have Cohesion (how strongly members identify with each other and act collectively), Grievance Level (accumulated sense of being wronged by specific actors), Trust in Institutions (how much members believe the formal system will address their concerns), and Access to Resources (what they can actually do about their grievances).

**Community response escalation through seven stages:** informal complaint (grumbling, reduced cooperation); organized complaint (petitions, formal regulatory complaints); political mobilization (supporting candidates who oppose the player); economic resistance (boycotts, refusing business, supporting competitors); direct action (strikes, protests, demonstrations); institutional action (successful election of hostile politicians, regulatory investigations supported by community testimony); sustained opposition (permanent organized structure whose purpose is constraining the player).

**Intervention points:** Address the grievance (genuinely fix the problem — grievance drops, community may become positive); deflect (buys time, grievance resumes); suppress (works temporarily, increases grievance significantly, if it becomes public produces backlash at scale); co-opt the leadership (reduces independent response capacity, risk: visible co-option produces betrayal, and betrayal is the fastest path to maximum grievance); ignore it (grievance accumulates until the player no longer controls the trajectory).

### 14.3 The Trust Economy

Trust is not reputation. Reputation is what people say publicly. Trust is what people actually believe about the player's intentions and behavior based on their direct experience.

High trust means people give the player the benefit of the doubt when accusations surface, warn them when they hear something concerning, stretch for them when needed. Low trust means people believe accusations immediately, share information about the player with anyone interested, and leave at the first better option.

Trust is built slowly through consistency and destroyed quickly through single betrayals. The NPC memory system is, ultimately, a trust ledger.

### 14.4 Opposition Formation

The world generates organized opposition proportional to the gap between the player's power and the grievances they've created. Opposition formation requires three things to converge: shared grievance (enough people affected by the same actions), leadership (a significant NPC with sufficient motivation, skills, and risk tolerance to organize), and resources (access to funding, platform, legal access, or political opportunity that makes organized action viable).

> [SCENARIO: scenario_opposition_forms_on_convergence]
> Test: An opposition organization is created only when all three required elements (grievance, leadership, resources) are simultaneously present; removing any single element prevents formation.
> Seed setup: Region with elevated community grievance (≥0.6); a Significant NPC with role=community_leader, motivation=ideological, risk_tolerance > 0.5 (leadership); access to funding source (resources). Then run three control conditions each missing one element.
> Run length: 60 ticks.
> Assertion: Full condition: opposition organization object created within run window. Each control condition (missing grievance / missing qualifying leader / missing resources): no opposition organization created within the same window.

Opposition, once formed, has its own goals, internal dynamics, allies, and enemies. It is not simply "the player's enemy" — it is an organization pursuing a specific agenda, and that agenda may create opportunities as well as threats.

### 14.5 Personal Security <!-- C2 fix -->

[EXPANSION — Personal security system is post-launch scope. V1 models physical threat as a consequence queue entry only. Protective detail management and secure location systems are not V1 features.]

When the player accumulates enough enemies with enough capability, their physical person becomes a real target. This is not a discrete system — it is the logical consequence of the NPC motivation and memory model applied to people who have the resources and motive to act on their grievances through physical means.

**Protective detail:** NPC bodyguards with loyalty, skill, and awareness stats. Skill determines effectiveness at detecting and responding to threats. Loyalty determines whether they can be flipped by rivals — an insufficiently paid or resentful bodyguard is a liability, not protection. Awareness determines whether they detect surveillance and threat precursor behavior.

**Secure locations:** Residences and operational sites rated for physical security. A luxury penthouse is visible, accessible, and publicly known. A purpose-built secure residence reduces assassination risk. Safe houses unknown to rivals and law enforcement serve as emergency retreat options.

**Counter-surveillance:** Active monitoring for threats against the player's person. This is an intelligence operation — cultivating sources in rival organizations, monitoring communications channels used by known enemies, maintaining awareness of anomalous behavior near the player's locations.

**Threat events:** When threat level is high enough, specific events enter the consequence queue: surveillance detection (the player notices they're being watched — an opportunity to identify who and neutralize the threat before it escalates), intimidation (warning signals that can be ignored, addressed, or escalated in response), and assassination attempts (only when threat level is critical and personal security is inadequate — outcome probability affected by protective detail quality and location security).

---

## 15. Political & Governance System

### The Political Career

Political ambition begins the same way it begins in the real world: at a level where your name and relationships are enough to get started. A local city council seat requires neighborhood connections, modest campaign resources, and a coherent local pitch. A head of state requires years of prior office, a national coalition, a credible mandate, and resources on a different scale entirely.

The career ladder is City Council → Mayor → Regional Governor → Parliament / National Legislature → Cabinet Minister → Head of State. Alternatively: appointed positions (central bank governor, regulatory agency head, ministry leadership) which don't require electoral success but require someone already in power to appoint you — meaning they require political relationships with whoever controls the appointment.

The player does not have to follow the ladder in order, but skipping rungs has costs: no base of tested relationships, no track record for voters to evaluate, no experience with the mechanics of governing at lower levels. A billionaire who buys their way into a national election run without prior political experience is running against politicians who spent a decade building coalitions the billionaire doesn't have.

### Campaigns

A campaign is a resource deployment problem with a relationship component. Resources required: money (advertising, staff, travel, events, research, crisis management), time (campaigns consume the player's calendar — most significant decisions and appearances require direct engagement, crowding out other commitments), and relationships (the political relationships in the player's network that translate into endorsements, coalition commitments, and ground-level support).

**Voter coalition management:** The player's approval is tracked by demographic group, not as a single number. Each group has issues they care about, candidates they distrust, and a baseline disposition toward the player based on prior interactions. Building a winning coalition requires identifying a path to enough groups with enough combined weight, making commitments to them (tracked as obligations), and managing the contradictions between what different groups want.

**Campaign mechanics by phase:**
- *Announcement and positioning:* Define the platform — a set of stated commitments on key issues. Platform positions are tracked. Voters remember what was promised. Governing later requires either delivering, managing the disappointment of not delivering, or explaining why circumstances changed.
- *Coalition building:* Activate political relationships — request endorsements, negotiate with community leaders, secure institutional support from unions, business associations, civil society organizations. Each endorsement typically comes with an expectation. Some are implicit (a union that endorsed you expects labor-friendly policy). Some are explicit (a business association that funded your campaign expects specific regulatory outcomes). These are Obligation nodes.
- *Opposition research and response:* Opponents have their own Exposure records. The player can commission research (through contacts or through their own intelligence capacity) to find damaging information. Opponents are doing the same. Scandal management — both deploying attacks and responding to them — is an active campaign mechanic.
- *Closing:* Final days of the campaign are about turnout in favorable demographics and suppression of unfavorable ones (legal suppression through resource allocation, or through methods that carry their own Exposure).

**Election resolution:** Elections are resolved probabilistically based on coalition strength (sum of each demographic group's support weighted by their turnout propensity), campaign resource deployment, and random variance from events that occurred during the campaign period. The outcome is not predetermined and cannot be mechanically guaranteed — which is what makes pre-election relationship building matter. A player who spent five years building genuine community trust before announcing has a different foundation than one who bought their coalition two months before polling day.

### Governing

Winning the election is the beginning of a different set of problems.

**The legislative cycle:** Laws originate as proposals — by the player if they hold executive or legislative office, by allied NPC legislators, by opponents, or as regulatory responses to external events. A proposal must pass through committee (where it can be amended, buried, or expedited depending on who chairs the committee and their relationship with the player), floor debate (where it attracts public attention and NPC legislator positions become visible), and a vote (which resolves based on each NPC legislator's motivations, constituency pressures, and the offers the player has made through the Obligation network).

Building a legislative majority requires understanding what each NPC legislator needs. Some want money. Some want policy concessions. Some want jobs for their constituents. Some are genuinely ideological and will vote on principle regardless of what the player offers — these are the ones to identify early, because they cannot be bought and trying to buy them converts them into hostile witnesses.

**Law categories:** Tax code, labor law, environmental regulation, trade policy, zoning, antitrust, subsidies, criminal law, drug scheduling and enforcement policy, intelligence oversight, constitutional changes (require higher thresholds and longer timelines).

**Policy consequences:** Every law runs through a modeled consequence engine. Effects cascade: a corporate tax cut means more business investment, less public revenue, possible deficit, bond market reaction, credit rating movement. A minimum wage increase means higher worker spending, higher business costs, possible layoffs in margin-sensitive industries, consumer demand shift. Drug legalization means reduced criminal market margins, new legitimate tax revenue, law enforcement resource reallocation, changed social consumption patterns. Antitrust enforcement means market competition increases, incumbent profits fall, political donations to the player's opponents increase.

Second-order effects emerge from NPC behavior responding to new conditions — these are not scripted, they are the simulation running under new rules. A player who legalizes a drug doesn't receive a notification that the criminal market shrank by a modeled percentage — they watch, over months of game time, as their own criminal distribution network's margins compress, as rival criminal organizations shift their commodity mix, as the black market price falls below what the tax-regulated legal market can match.

> [SCENARIO: scenario_legalization_compresses_criminal_market]
> Test: After a drug legalization law passes, the criminal market for that drug shows measurable margin compression over subsequent game months, driven by legal market price competition.
> Seed setup: Player (or NPC politician) passes drug legalization legislation for substance X; active criminal distribution network for substance X exists in the region; legal retail market for X opens at regulated pricing.
> Run length: 180 ticks (6 in-game months).
> Assertion: Criminal market profit margin for substance X decreases by ≥20% from pre-legalization baseline within the run window; at least one NPC criminal operator shifts commodity mix away from substance X; black market price for X converges toward (within 15% of) legal market price.

**Governing budget and constraints:** As a governing politician the player manages a budget that is partly inherited from their predecessor and partly shaped by their own policy decisions. Revenue comes from the tax structure they enact. Expenditure goes to services, public sector salaries, debt servicing, and whatever new programs they've committed to. Deficit spending is possible but creates bond market pressure and eventually credit rating downgrades that constrain future options. Surplus creates political controversy from groups who believe the money should be spent differently.

**Approval management:** Approval is granular by demographic. Every policy decision moves some groups up and some groups down. There is no path through office that pleases everyone — the player is always managing a coalition in which some members are disappointed. Managing disappointment at acceptable levels while delivering enough to keep the coalition together is the core skill of governing.

**Political crises:** Scandals, disasters, economic shocks, and foreign policy failures are generated by the simulation regardless of the player's preferences. Crisis response is calendar-consuming — it forces reactive engagements that crowd out planned activity, and the player who is already overcommitted when a crisis hits will handle it worse than one who had slack in their schedule. A natural disaster that kills hundreds demands a visible, competent response. A financial scandal requires either credible distance from the person responsible or a convincing explanation. A foreign policy crisis may require military or diplomatic commitments that create obligations far beyond the immediate event.

### The Corruption Dimension of Politics

Political office creates Influence and creates vulnerability simultaneously. The player as politician will be approached with bribes and favors — accepting builds Obligation and Exposure, refusing creates enemies among those who expected compliance. The player as business owner or criminal will try to buy politicians. The same system governs both sides of this relationship.

A politician who has never accepted anything improper has a kind of invulnerability that the player who has accepted even one favor does not: there is nothing to threaten them with. The tradeoff is that they've also refused the resources, the expedited decisions, and the access that corruption provides. Pure clean governance is a harder, slower path through politics. It is available, and it arrives at a more stable late-game position — but the player must be willing to lose things in the short term that bought politicians would have kept.

> [SCENARIO: scenario_clean_politician_immune_to_obligation_leverage]
> Test: A politician NPC with zero obligation_balance and no Exposure records cannot be threatened into compliance by the player; a politician NPC with ≥1 obligation node can be.
> Seed setup: Two politician NPCs — NPC A with obligation_balance = 0 and no evidence tokens; NPC B with one active obligation node linking them to the player. Player attempts leverage action (threat of Exposure) against both.
> Run length: 5 ticks.
> Assertion: NPC A's leverage action fails (no compliance behavior change); NPC B's leverage action succeeds (compliance behavior change occurs); NPC A's relationship score with player decreases (becoming hostile to threatening approach).

### International Relations

Nations have diplomatic relationships shaped by trade, history, and the behavior of actors within them. Trade agreements, defense pacts, and sanctions are negotiable through diplomatic processes — formal state-to-state interactions the player can participate in if they hold office, or influence through their business and relationship networks if they don't. War is a risk triggered by resource scarcity, territorial disputes, or political miscalculation — when it arrives, it devastates economies, disrupts supply chains, creates refugee pressure, and reshapes the competitive landscape. It is not a feature. It is a failure mode.

---

## 16. Social Systems

### The NPC Population

Background population NPCs are simulated as demographic cohorts whose collective behavior drives labor markets, consumer demand, voting, and social stability. Their behavior responds to economic conditions: rising inequality increases crime and populist political sentiment; prosperity produces social conservatism; mass unemployment creates instability; drug addiction concentrations affect labor productivity and create healthcare demand.

### Reputation

Reputation is domain-specific and separately tracked: Public Business Reputation (affects credit, partners, recruitment, customer trust), Public Political Reputation (affects voter approval, media treatment, regulatory posture), Public Social Reputation (affects personal relationships, community standing), Street Reputation (how people operating in grey and black areas perceive the player — built and damaged through entirely different actions). Reputation damage is asymmetric — easier to lose than regain. A scandal from three years ago is still findable and still relevant.

### Media System

Newspapers, television, digital platforms, and social media networks are independent actors with their own editorial agendas, funding structures, and relationships. They can be owned by the player — which creates both a tool and a conflict of interest generating its own regulatory exposure. A captured outlet is both useful and evidence of capture.

**Media as the Exposure activation mechanism:** Exposure tokens live in the world inertly until someone finds them and does something with them. The media system is the primary mechanism by which discovered Exposure converts into public crisis. A journalist who receives a document, a whistleblower tip, or a source's testimony decides — based on their own motivation model — whether to publish, how prominently, and when. A financial journalist who has been building a profile on the player for two years and receives a single damaging document publishes it quickly. A journalist at an outlet the player owns faces calculation about the professional and personal cost of publishing something their employer doesn't want published.

**How stories reach journalists:** Evidence tokens can move through the NPC social network by various paths. A disgruntled ex-employee tells a friend who knows a journalist. A rival compiles damaging information and routes it to an investigative reporter through an intermediary. A whistleblower contacts a journalist directly. A document is leaked from inside a regulatory agency. The player is rarely the cause of any single act of journalism — the causes are the real events that produced the evidence, and the social network that moved it toward someone with a platform and a motivation to publish.

**Planting stories:** The player can route information to journalists for their own purposes — damaging information about a rival, a narrative that deflects attention from their own Exposure, a story that builds their public image. This requires: a journalist who covers the relevant beat, a relationship with that journalist or an intermediary who has one, and information that the journalist finds credible and newsworthy enough to print. Feeding a journalist false information creates its own risk — if the false story is disproved, the journalist's reputation is damaged, they become hostile and motivated, and the player has created an enemy with a professional grievance and a platform.

**Social media dynamics:** Social media platforms behave differently from traditional media. Stories spread faster and with less editorial filter. A narrative that reaches enough individual NPC users can generate political pressure faster than any traditional outlet. But social media also amplifies false information at the same speed as true information, produces backlash against perceived attacks, and can be gamed by coordinated activity that the game models as a player capability (operating multiple accounts through intermediaries, funding amplification networks). Social media manipulation is an Exposure-generating activity if traced.

**Outlet ownership:** The player can acquire media outlets. An owned outlet will not publish stories damaging to the player by default — editors employed by the player have motivation to protect their employer. But journalists at the outlet still have their own motivation models. A journalist at an outlet who witnesses editorial suppression of an important story has a whistleblowing incentive that ownership doesn't eliminate — it just changes the outlet they'll take the story to when they leave. Owning an outlet gives the player influence over what it publishes; it doesn't give them control over every journalist who works there or what those journalists do after they leave.

### Social Stability

Inequality beyond a threshold increases regional crime, reduces investment attractiveness, creates political instability, and produces conditions for populist political movements. The player's actions — wage decisions, tax avoidance, layoffs, automation — aggregate into regional inequality metrics. Criminal dominance degrades all social stability metrics (Section 12.12). Drug market concentration creates addiction patterns visible in labor force participation rates and public health spending.

---

## 17. Random Events as Perturbations

Random events are not story events. They are disturbances to a system that was already under tension. Their function is to reveal the current state of the player's position by stressing it.

Types: Natural (earthquakes, floods, droughts, pandemics, fires); Accidents (industrial incidents, transport failures, data breaches, lab explosions); Human (deaths of key NPCs, unexpected political outcomes, competitor breakthroughs, defection of key criminal network members); Economic (market shocks, commodity price spikes, currency crises, sudden drug market collapse due to substitute product emergence).

Design principles: genuinely random in timing; indiscriminate, affecting everyone in their radius; their consequences interact with the player's existing position in ways that feel personal; they create decision points, not story beats.

The same earthquake that destroys a port is a disaster if that port handles 40% of the player's imports. It is an opportunity if the player owns a competing port. The same event produces different stories depending on position. That is emergence.

---

## 18. Power Endgames

### 18.1 The Spectrum

The endgame of EconLife is not a single destination. It is a spectrum from pure covert power to pure legitimate power, with every combination available and interesting.

```
COVERT ←————————————————————————————→ LEGITIMATE
Criminal Sovereign    Mixed Power    Elected Sovereign
```

Both ends of the spectrum are powerful. Both have characteristic vulnerabilities. Most players in the late game occupy a position somewhere in the middle — legitimate enough to enjoy institutional protection, covert enough to exercise capabilities that legitimacy doesn't allow.

### 18.2 The Legitimate Sovereign Path

Maximum legitimate power means controlling a nation through formal, legally sanctioned means. Reached through decades of political career building, coalition management, and genuine public trust construction. What it enables: the full power of a state — tax policy, regulatory architecture, law enforcement priority, military capacity, diplomatic relationships, treaty-making. The legitimate sovereign can legalize things previously criminal (eliminating certain Exposure overnight) and restructure the regulatory environment for a generation.

Its vulnerabilities: elections, coalitions, the press, international relationships. Maximum power with maximum accountability.

### 18.3 The Covert Sovereign Path

Maximum covert power means controlling a nation without holding any formal position. The politicians answer to this person. The regulators do what they're told. They exist, legally, as a private citizen. Their actual power is exercised through the Obligation network, through relationships with people who operate in grey and black areas, and through the capture of legitimate institutions by people they control.

Reached through accumulated Obligation networks across every major institution in the nation. Every significant politician owes them something. Every major regulator was placed by them or has been captured. The media landscape is controlled or intimidated. Law enforcement leadership is purchased or leveraged.

What it enables: everything a legitimate sovereign can do, plus things they cannot. Direct state power without accountability.

Its vulnerabilities: every node in the network is a liability. A new government can displace all placed officials. A foreign intelligence service that has mapped the network can threaten to surface it. A single cooperative witness can unravel decades of construction. Covert power rests on a foundation of secrets, and secrets are the most fragile of foundations.

### 18.4 The Criminal Sovereign

A variation on the covert sovereign built specifically from criminal infrastructure. Controls territory, employs populations, provides governance functions in areas the legitimate state has abandoned. Has political protection through dependency and fear rather than through legitimate democratic relationships.

The most powerful and most precarious position in the game. The entire structure is built on violence and secret knowledge — and violence can be matched, and secrets can be surfaced.

### 18.5 Mixed Power: The Realistic Endgame

Most players occupy the middle: legitimate enough to enjoy institutional protection and public credibility, covert enough to exercise capabilities that legitimacy doesn't allow. The skill is keeping the two worlds separated while using each to protect the other. The risk is always that they collide.

### 18.6 Becoming Indispensable

The ultimate late-game achievement is not maximum power — it is becoming structurally indispensable. The player whose bank funds the government's deficit, whose factories employ 20% of the regional workforce, whose university trains the nation's professionals, whose logistics network moves 40% of the nation's goods — that player can behave in ways that would destroy anyone less indispensable, because the cost of removing them has become too high for any single actor to bear.

The world will still try. Being powerful generates opposition permanently. That's the game.

---

## 19. Progression & Legacy

EconLife has no forced end state. Progression is the accumulation of decisions and their consequences, not a level meter.

### Organic Milestones

The game tracks significant firsts and crosses silently — first profitable business, first hostile takeover, first election won, first law passed, first cover-up executed, first cartel established, first network dismantled — not as achievements but as historical record. Accessible in a character timeline. Visible to future characters in generational play.

### Death

What happens when the player character dies is entirely determined by what they set up while alive.

A player who wrote a will through their lawyer, established corporate succession documents, built a genuine relationship with an heir and mentored them into capability, and structured their criminal operation with a clear second-in-command who has the loyalty and knowledge to take over — that player's death is clean. The instructions execute. The structures activate. The heir is positioned.

A player who did none of this: probate, legal dispute, NPC lawyers billing the estate, operations fracturing under whoever grabs what they can, rivals moving into the vacuum, creditors calling in obligations. The consequence queue keeps processing — deaths the player's character triggered before dying still land on schedule. The heir, if one exists, inherits a crisis rather than an empire.

Sudden death from violence or health failure gives no preparation time — whatever the player built or neglected is what exists. A visible terminal phase from illness gives time to manage succession, settle affairs, and shape the legacy being left behind. This final phase, for a player who engages with it, is its own kind of gameplay.

When a character dies, the player can: play as their heir (inheriting assets, reputation, obligations, enemies, and the world as their predecessor shaped it), start a new character in the same persistent world, or start fresh in a different nation.

**The heir:** If the player built a genuine relationship with a child or designated successor during their character's life, that person exists as a fully modeled NPC at the time of death — with skills developed during the mentorship period, a personality shaped by their upbringing and experiences, relationships inherited from proximity to the player, and their own view of what they've inherited. Playing as the heir starts with that NPC's attributes, relationships, and knowledge as the new character. The heir knows what they know — which may be more or less than the player assumed.

If no heir was established, the player creates a new character who enters the world as a beneficiary of the estate — they inherit documented assets and formal business structures but arrive without the relationships, street knowledge, or shadow connections the previous character built. They are starting with resources but not with the network that made those resources defensible. The world around them remembers the previous character and will relate to the heir in light of that memory — some as potential allies, some as creditors, some as threats who saw the succession as an opportunity.

The world remembers. The laws still govern. The enemies still operate. The world is not reset — it is continued.

### The Legacy System

What persists after the player's character is gone:

**Corporate legacy:** Companies outlive their founders. A company's culture is the aggregate of how the player managed it — how they treated workers over time (tracked in aggregate satisfaction and turnover history), what ethical standards they held (whether they cut corners, falsified records, mistreated suppliers), how they responded to crises (did they cover up problems or address them), and who they promoted into leadership (the management layer inherits the player's behavioral patterns or corrects them, depending on who those people are and what they believe). A company the player ran with integrity and populated with capable, ethical management will continue to behave that way after the founder dies — because the people running it are products of that culture and have their own motivations aligned with it. A company built on exploitation and staffed with compliant people who were rewarded for compliance will continue behaving exploitatively — and the legal, reputational, and community consequences of that behavior will continue landing on whoever inherited it.

**Political legacy:** Laws passed remain in force. The player's legislative record shapes the world future characters inhabit. Legalization or criminalization decisions persist.

**Criminal legacy:** The drug markets established, the trafficking networks built or destroyed, the communities degraded or improved — all persist as conditions future characters inherit. A world where a cartel ran unchecked for twenty years looks different from one where the player infiltrated and dismantled three major trafficking networks.

**Philanthropic legacy:** Schools, hospitals, infrastructure funded by the player shape regional demographics and social conditions over decades.

---

## 20. UI/UX Philosophy

### The Player's View

EconLife is not a third-person game. The player manages from an abstract top-down interface — maps, dashboards, calendars, device screens. There is no character walking between locations.

The world's visual texture is delivered through **scene cards**: framed illustrations or rendered environments that appear when an engagement is initiated or arrives. A sit-down meeting with a politician renders as a conference room — two figures across a table, natural light through venetian blinds, a formal framing. A meeting with a criminal contact in a parking garage looks like that: concrete, low light, the suggestion of surveillance cameras that may or may not be active. A call from a lawyer mid-crisis is a phone screen with their name and photo, the audio of a voice delivering news the player didn't want. A family dinner at home is framed differently again.

These scenes are not navigated or physically inhabited. They appear, they present the interaction — dialogue choices, decisions, information received — and they resolve. The atmospheric framing does real work: the same information delivered in a marble-floored boardroom and in the back of a moving car communicates different things about the relationship and the stakes. Designers should treat scene settings as part of the information delivered to the player, not as cosmetic backgrounds.

**What the top-level view shows:** The map layer — regions, the player's facilities and operations, visible regional conditions (stability, visible infrastructure, markers for active events). The dashboard layer — operational summaries from delegated functions, financial positions, influence network health, exposure indicators, obligation network. The device layer — messages, news feeds, intelligence reports, the incoming stream from contacts. These layers are switchable and can be displayed simultaneously at different levels of detail.

The player never has a god-mode view of the world. The map shows what their contact network covers. Regions with no contacts show nothing. Regions with limited contacts show partial pictures. The strategic picture on screen at any given moment is a direct representation of the quality and breadth of the player's relationships.

### Information Through Devices and Contacts

Wider information about the world — what's happening in regions the player isn't present in, what competitors are doing, what investigations are building — comes through their device and their contact network, never through a map the game provides by default.

The device receives: news (from media outlets filtered by their editorial agendas), messages and calls from contacts (NPCs who choose to share what they know, at their own level of accuracy and with their own motivations for sharing), financial and operational data from owned businesses (what managers choose to report, which is not always everything), market data from financial platforms the player subscribes to, and intelligence reports from people they pay to gather information.

A player with many contacts in a region receives a rich picture. A player with none receives a blank. A player whose contacts are low-quality or low-loyalty receives distorted information. The map layer reflects this: regions populate with data as the player's network there develops, and go dark when it doesn't.

**Information is never complete or automatically accurate.** Contacts share what they know and what they decide to share. Managers report what they think the player wants to hear alongside what's genuinely important. News outlets reflect their editorial positions and funding relationships. The gap between what the player's information network shows them and what is actually happening is permanent and irreducible — and it is the primary source of late-game surprise. Investing in better contacts, more reliable sources, and actively cultivated intelligence relationships is one of the highest-value ongoing activities in the game.

### The Interface Layers

At any point the player engages with one or more of three interface layers, switchable and composable:

**Map layer** — the world as the player's network covers it. Regions the player has contacts in show regional conditions, facility markers, and active event indicators. Regions without coverage show nothing. This is the physical and political world rendered as intelligence, not as a camera feed.

**Operational layer** — dashboards for each business, operation, or political position the player holds. Production data, financial reporting, staff status, delegation chains, escalation queues. What managers are reporting. What they're not reporting, inferred from gaps. This is where the player monitors delegated functions and sets escalation thresholds.

**Communications layer** — the incoming stream. Messages, calls, news feeds, intelligence reports, calendar invitations. Everything the world is sending the player. This is where demands arrive, where relationships are maintained at distance, and where the gap between what the player knows and what is actually happening is most directly felt.

### Visual Feedback Principle

Problems should be visible before they're numerical. The simulation should be readable through observation before the player drills into data panels — and scene cards, map indicators, and dashboard states should communicate condition before raw metrics do.

**Facility dashboards:** A bottlenecked production stage shows input queues building visually while output queues stay empty. A machine approaching failure shows a warning indicator before the maintenance alert fires. An office with falling satisfaction shows lower productivity figures and more escalations before the satisfaction metric is explicitly flagged.

**Scene cards:** The quality of an NPC meeting is visible in how they present. An NPC who is comfortable and cooperative speaks directly, makes eye contact in their portrait, offers information freely. An NPC who is under pressure, afraid, or hostile appears differently — the framing tighter, the setting less controlled, their dialogue choices shorter and more guarded. Designers should treat the scene card presentation of an NPC as a readable signal, not just an aesthetic backdrop.

**Map layer indicators:** Regional deterioration is visible on the map before the statistics update — facility markers dim, activity indicators slow, the density of event markers in an area tells a story. A region the player has invested in looks different from one they've neglected or extracted from.

**The design principle across all of these:** the player should be able to make a reasonable assessment of the state of any part of their operation from a quick look before they read a single number. Numbers confirm and quantify what the visual state already suggested.

### The Calendar Interface

The player's calendar is their primary self-management tool. It shows committed engagements (meetings scheduled, events locked in, operations in progress), incoming demands (escalations from managers, requested meetings, legal dates, deadlines), pending decisions (things that need a response before a stated time or a default outcome fires), and visible gaps (days with nothing committed that can be filled or left deliberately open for recovery).

The calendar doesn't enforce limits. The player can pack it or leave it sparse. What packing it produces is a character who is never in the right headspace, whose relationships are maintained at a surface level, and who finds out about developing problems after they've already become crises. What leaving it too sparse produces is operations drifting under delegation, relationships cooling from neglect, and a player who is comfortably reactive rather than strategically ahead.

Delegation is managed from the same interface. Assigning a function to a manager removes its routine demands from the calendar and replaces them with a periodic check-in and an escalation path for problems the manager can't handle. The player sets the escalation threshold — what decisions the manager handles autonomously and what they surface — and lives with the consequences of where they drew that line.

---

## 21. Technical Architecture

### Simulation Tick

The world runs on a daily tick (one per real-time minute at normal speed). Each tick processes in order:

1. Update all production outputs; consume inputs; record bottlenecks
2. Update supply chain propagation — shortages and surpluses at one layer affect prices and availability at adjacent layers
3. Update R&D project progress; check for completions, breakthroughs, and failures
4. Update labor supply and demand; adjust wages; flag dissatisfaction changes
5. Update market prices via supply-demand equilibrium — supply from step 1 output, demand from previous tick's demand buffer (one-tick lag is intentional — it creates the window between conditions changing and prices reflecting those changes that skilled players can exploit)
6. Distribute wages, profits, dividends; apply taxes; update credit scores
7. Run NPC behavior engine — each significant NPC evaluates their situation and may act based on motivation, memory, and available resources
8. Run NPC business decision engine — quarterly decisions activate on schedule; between decisions, NPC businesses follow behavioral profiles within current constraints
9. Run community cohesion and grievance updates — aggregate individual NPC experiences into community-level response metrics
10. Check opposition formation conditions — if grievance, leadership, and resources converge, trigger opposition organization process
11. Update evidence trail — new evidence tokens created by actions in previous ticks
12. Update criminal operation detection risk — OPSEC scores, surveillance coverage, informant network status
13. Run investigator engine — journalists, regulators, whistleblowers, law enforcement units, and international agencies advance investigation meters
14. Update legal process state — active cases advance; trial outcomes resolve; prison terms tick; parole and appeal processes advance
15. Update obligation network — aging obligations increase pressure; due obligations trigger requests
16. Check consequence thresholds — new opposition categories activate when applicable conditions are met
17. Update NPC spending decisions; write to demand buffer for next tick's price calculations
18. Age population; process births, deaths, education completions, career changes, addiction progression and recovery
19. Run political cycle — elections due? Coalition shifts? Scandal breaks? Drug policy votes?
20. Update regional conditions — infrastructure decay, inequality index, stability score, criminal dominance index, addiction rate
21. Update trust balances for all significant NPC relationships based on recent player actions
22. Update Influence network weights — trust, obligation, and fear relationships decay or strengthen based on recent interactions
23. Update street reputation based on observed player behavior in grey and black areas
24. Update character health and age; check health thresholds; queue health-related consequence events
25. Process random event probability rolls — truly random, not narrative-timed
26. Update device notifications — news, messages, financial alerts, intelligence reports delivered to player's device based on their contact network
27. Update world map state; propagate cross-regional effects

### Modularity Principle

Each system in the tick is a module. Modules communicate through standardized data interfaces. A new system can be added by inserting a new module into the tick sequence without redesigning existing modules.

### Fast-Forward and Time Compression

Fast-forward is available up to 30x. At 30x, an in-game year passes in 12 real minutes. This has implications for the consequence queue: a player who fast-forwards through a decade of in-game time may experience multiple queued consequence events firing in rapid succession — the fired employee retaliates, an investigation converts to charges, an obligation calls in, and a rival makes their move in the space of a few real minutes.

Fast-forward is not artificially capped, but the player is expected to understand the tradeoff: time compression means less time to respond to developing situations. A player who fast-forwards through a building investigation without monitoring their device for signals may arrive at arrest having missed every indicator that could have prompted action. The game does not protect the player from the consequences of not paying attention.

Fast-forward is automatically interrupted when: a situation requires the player's direct decision (a manager has escalated a problem above their delegation threshold), a crisis event fires that requires player response, or a legal situation advances to a stage that changes the player's operational status (charges filed, trial date set, verdict reached). These interruptions are not intrusions — they are the game ensuring the player cannot sleep through a burning building.

### Persistence and Saving

EconLife uses continuous autosave. There is no manual save and no reload. The world state is committed to disk continuously. A player who makes a decision cannot undo it by quitting and reloading.

This is the architectural expression of the causality-over-content design principle. The consequence queue is only meaningful if its contents cannot be deleted. An Obligation network is only a real constraint if it cannot be restarted. The world only feels alive if it holds onto what happened.

The practical implication: players should understand before their first major irreversible action that the game does not have a safety net. There is no "undo criminal operation." There is no "reload before the investigation opened." What happens, happened. The world responds. This is the intended design, not an oversight.

### Consequence Delay Buffer

Delayed consequences are stored in a consequence queue — a timestamped list of outcomes scheduled to emerge in the future. When the fired security guard acts in four months, his action was queued when he was fired. The queue is not visible to the player. It is the game's memory. The queue continues processing regardless of player character status — death does not clear the queue.

### Procedural World Generation

Worlds generated with configurable parameters: number of nations and regions, starting inequality distribution, resource richness and distribution, political system type (democracy, autocracy, federation, failed state), economic development stage (developing, industrial, post-industrial, near-future), corruption baseline (clean institutions vs. already-captured), criminal activity baseline (how established existing criminal organizations are before the player enters the world), and climate stress distribution (which regions have lost or gained agricultural productivity).

---

## 22. Development Philosophy — What This Document Is

### Introducing Systems to New Players

"Readable complexity — deep systems revealed gradually through play, never dumped through tutorial" is a design pillar, not an excuse for no guidance. The game has enough interlocking systems that a new player dropped into a running simulation with no orientation will be confused and will leave. The solution is not a tutorial. The solution is contextual reveal: the system is introduced at the exact moment it becomes personally relevant to the player's current situation.

**The mechanism:** Every system in the game has a first-time activation condition — the first time the player triggers it, or the first time it triggers against them. At that moment and only that moment, a brief, non-interrupting overlay appears describing what just happened and what the player's available options are. This overlay is shown once, can be dismissed immediately, and is accessible again through a reference log on the player's device.

Examples: The first time the player receives an obligation from a favor, a short note appears explaining what the obligation node they just acquired represents and where they can view the full network. The first time a worker's satisfaction drops below the whistleblower-risk threshold, a brief notification on their device indicates that someone in the relevant facility seems uneasy — the player can investigate or ignore it. The first time an investigation opens against them, their device receives a message from a contact (if they have one) or shows an observable signal (if they don't). The first time they encounter a grey-area contact, no special explanation appears — the person behaves like a person, and the player figures out what they're offering.

Some systems are intentionally opaque on first encounter. The obligation escalation path — where a small favor becomes an escalating commitment — should not come with a warning. The warning would destroy the experience of discovering what obligations become. The design principle is: reveal systems that the player needs to navigate deliberately, and let systems that are meant to sneak up on them do exactly that. Every system here is a hypothesis about what will produce the desired player experience. Some hypotheses will be wrong. The test is always: does this system generate interesting emergent stories, or does it generate noise? If noise, revise. If the system works but the numbers are off, tune. If the system fundamentally doesn't produce what it should, replace it without sentiment.

The core intent is non-negotiable: a living world where the story emerges from the simulation, where wealth changes the nature of the challenge rather than ending it, where Influence is earned as much as purchased, where the player's information about the world is always partial and earned through relationships, and where the world responds to everything they do with the logic of a real system, not the judgment of an author.

---

## 23. Production Scope & Companion Documents

### What This Document Is Not

This GDD describes the complete intended design of EconLife — the full vision. It does not describe what ships in v1. Building the full document in one production cycle is not viable. The design must be tiered, the first release scoped to a vertical slice that proves the core simulation works, and the technical architecture validated before content production scales up.

### The V1 Vertical Slice

V1 EconLife is a contained, deep version of the core loop set in one nation with three to five regions. It includes legitimate business and political career paths at full depth, a criminal economy limited to the drug supply chain and money laundering, NPC simulation at meaningful but not maximal complexity, and the full consequence, exposure, and obligation architecture. It does not include human trafficking, weapons trafficking, generational play, multi-nation operations, or the full endgame power spectrum. Those are post-launch expansions built on a proven simulation foundation.

The vertical slice is still an enormously ambitious game. The goal of scoping it is not to make it smaller — it is to make it shippable.

### What Must Be Answered Before Production Scales

The existential technical risk is NPC simulation performance. Before the content team expands, a simulation prototype must demonstrate that thousands of individually modeled agents with memory, motivation, and relationship networks can run at target performance on one-minute ticks. If the answer requires architectural simplification, the design adapts. This question gets answered in month three, not year four.

See companion documents for complete scope triage and technical architecture specification.

### Companion Documents

**EconLife — Feature Tier List** — every system in this GDD assigned to V1 Ship, Expansion, or Cut, with rationale and dependencies.

**EconLife — Technical Design Document** — simulation architecture specification, data structures, performance targets, prototype scope, and known technical risks.

Both companion documents are living documents maintained in sync with this GDD.

### Design Principles That Must Be Protected

These are under pressure during every production. They do not bend.

**Systems over content.** When the team is behind schedule, the proposal will be to add a scripted event instead of deepening a system. That proposal loses. Every time.

**No morality score.** The consequence-not-punishment framing is a design decision filter. When someone asks "should we add a morality meter," the document already answered.

**Device-mediated information.** The player only sees what their contact network gives them. This is both philosophically correct and a practical scope constraint — you don't simulate or render what isn't connected.

**No reload.** Continuous autosave without reload is the architectural expression of causality. Without it the consequence queue is meaningless and the obligation network has no teeth.

---

## Revision Log

- v0.1 — Initial concept draft
- v0.2 — Competitor research incorporated; identified industry-wide gap in late-game design
- v0.3 — Emergent narrative philosophy established; three-challenge structure defined; Power/Exposure/Obligation system designed; NPC memory/motivation architecture specified; random events reframed as perturbations; simulation tick established
- v0.4 — Full supply chain economy specified; knowledge economy added; Influence redefined as relationship network; World Response System designed; power endgame spectrum defined
- v0.5 — Mature rating applied; full criminal economy integrated; infiltration mechanics added; simulation tick expanded
- v0.6 — Shadow Network retired; phases reframed as design vocabulary; device-mediated information model established; death handling simplified; 20+ system solutions integrated; tick expanded to 27 steps
- v0.7 — Six consistency corrections; workforce management added (Section 6.8); political system expanded (Section 15); media system designed (Section 16); corporate culture legacy and heir succession specified; visual feedback examples; save system and fast-forward documented; contextual introduction mechanism specified
- v0.8 — Third-person 3D removed; abstract top-down management with scene cards adopted; energy/token budget removed; time-as-constraint model established; calendar interface specified; all physical presence language cleaned throughout
- v0.9 — Production scope section added (Section 23); V1 vertical slice defined; companion documents commissioned (Feature Tier List, Technical Design Document); design principles requiring production protection enumerated; revision log consolidated
- v1.0 — Cross-document fixes C2–C6 integrated; 22 scenario tags added for Bootstrapper test generation; companion Technical Design updated to v2

---

*EconLife GDD — v1.0 — Living Document*

---

## Scenario Tag Index — Bootstrapper Reference

*This section lists all 22 [SCENARIO] tags added in the annotation pass. Each entry gives the scenario name, its section location, the first five words of the tagged sentence (for text search), and the V1/Expansion scope. The Bootstrapper should convert every tag into a scenario test stub; tags marked EXPANSION should be skipped for V1.*

---

| # | Scenario Name | Section | First 5 Words of Tagged Sentence | Scope |
|---|---|---|---|---|
| 1 | `scenario_journalist_pursues_story_on_career_motivation` | §3 Behavior Generation | "A journalist with a motivation" | V1 |
| 2 | `scenario_journalist_leaves_hostile_after_outlet_purchase` | §3 Behavior Generation | "A journalist with a motivation" (second tag, same paragraph) | V1 |
| 3 | `scenario_regulator_slows_when_supervisor_controlled` | §3 Behavior Generation | "A regulator with an ideology" | V1 |
| 4 | `scenario_wage_cut_produces_union_organizer` | §3 The Delay Principle | "The wage cut that demoralized" | V1 |
| 5 | `scenario_journalist_watchlist_on_wealth_threshold` | §10.5 Consequence Thresholds | "Reaching significant net worth →" | V1 |
| 6 | `scenario_antitrust_inquiry_on_market_concentration` | §10.5 Consequence Thresholds | "Reaching significant net worth →" (same paragraph) | V1 |
| 7 | `scenario_union_targets_largest_employer` | §10.5 Consequence Thresholds | "Reaching significant net worth →" (same paragraph) | V1 |
| 8 | `scenario_fired_journalist_becomes_hostile_investigator` | §10.3 The Investigator Engine | "Journalists are individual significant NPCs" | V1 |
| 9 | `scenario_witness_delayed_cooperation_on_circumstance_change` | §3 The Delay Principle | "A witness who was too" | V1 |
| 10 | `scenario_criminal_warfare_activates_dormant_investigation` | §12.10 Criminal Territorial Conflict | "Each stage has mechanical costs" | V1 |
| 11 | `scenario_worker_observes_sensitive_event_creates_memory` | §6.8 Workforce Management | "What each significant worker has" | V1 |
| 12 | `scenario_npc_business_bankruptcy_on_cash_depletion` | §8.7 NPC Businesses as Active Participants | "NPC businesses that reach cash" | V1 |
| 13 | `scenario_whistleblower_emerges_on_eligible_conditions` | §6.8 Workforce Management | "A worker NPC becomes whistleblower-eligible" | V1 |
| 14 | `scenario_addiction_rate_produces_economic_degradation` | §12.11 Addiction as a Regional Consequence | "Regional consequences of high addiction" | V1 |
| 15 | `scenario_fired_worker_with_knowledge_delayed_consequence` | §6.8 Workforce Management | "Firing a worker removes them" | V1 |
| 16 | `scenario_opposition_forms_on_convergence` | §14.4 Opposition Formation | "The world generates organized opposition" | V1 |
| 17 | `scenario_criminal_dominance_degrades_regional_economy` | §12.12 Systemic Consequences of Criminal Dominance | "Criminal dominance degrades every simulation" | V1 |
| 18 | `scenario_expansion_trafficking_international_attention` | §10.5 Consequence Thresholds | "Reaching significant net worth →" (same paragraph) | **EXPANSION — Bootstrapper skips for V1** |
| 19 | `scenario_clean_politician_immune_to_obligation_leverage` | §15 The Corruption Dimension of Politics | "A politician who has never" | V1 |
| 20 | `scenario_legalization_compresses_criminal_market` | §15 Governing / Policy Consequences | "A player who legalizes a" | V1 |
| 21 | `scenario_treatment_investment_generates_influence_despite_source` | §12.11 Addiction as a Regional Consequence | "Recovery: Possible at all stages" | V1 |
| 22 | `scenario_low_satisfaction_produces_npc_adverse_action` | §6.8 Workforce Management | "Workers have satisfaction (a float" | V1 |

---

**Notes for the Bootstrapper:**

- Tags 1 and 2 are placed after the same paragraph in §3 (the journalist example spans both scenarios). The tag for scenario 1 (`scenario_journalist_pursues_story_on_career_motivation`) covers the first sentence; tag 2 (`scenario_journalist_leaves_hostile_after_outlet_purchase`) covers the "if the player buys the outlet" conclusion.
- Tags 5, 6, 7, and 18 are all placed after the single §10.5 consequence threshold paragraph, which lists all four triggering conditions in sequence. Each tag is a distinct test for a distinct threshold.
- Tag 18 (`scenario_expansion_trafficking_international_attention`) is derived from content that is Expansion scope (§12.3). The tag is present in the GDD for documentation completeness; the Bootstrapper must not generate a V1 test stub for it.
- The section numbers above use the GDD's numbering. All 22 tags are text-searchable by their `[SCENARIO: scenario_name]` header.
