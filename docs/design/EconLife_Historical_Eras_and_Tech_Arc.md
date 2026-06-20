# Historical Eras & Tech Arc — Design Proposal

*Grounds the era timeline (eras 1 → modern) in real human history, and sketches the
knowledge/technology arc the world advances through. Companion to the World Spectrum
plan (the knowledge engine) and the Mechanical History Generation plan (era bands).*

## Design principles (what this is and isn't)
- **The world advances on its own.** Era progression is driven by the *population's*
  accumulated knowledge (the knowledge engine: scholars/scribes → knowledge →
  thresholds → era advance). It is **not a tech tree the player researches alone.**
- **The player is optional, not required.** A player who focuses on research can sit
  at the *frontier* (early adopter, inventor, founder of a school) and even pull an
  advance forward — but the world progresses to the next era with or without them.
- **Data-driven.** Eras live in `eras.csv` (already), each with its
  `knowledge_to_advance` threshold; per-era knowledge/goods/recipes/facilities are
  authored data, gated by `era_available`. No hardcoding.
- **Pace accelerates.** Early eras span millennia; later ones, centuries→decades.
  That asymmetry is historical (the quickening of invention), not a bug.

---

## 1. The real historic periods
Standard periodization (Old-World-centric for the canonical arc; dates approximate
and regionally staggered):

| # | Period | Rough span | Defining developments |
|---|---|---|---|
| P0 | **Paleolithic** (Old Stone Age) | ~3 Mya–10,000 BCE | bands, stone tools, fire, language, hunting/foraging (pre-sim / the seed) |
| P1 | **Neolithic** (New Stone Age) | ~10,000–3300 BCE | agriculture, domestication, pottery, permanent villages, megaliths |
| P2 | **Bronze Age** | ~3300–1200 BCE | bronze metallurgy, **writing**, the wheel, plough, irrigation, cities, first states, **mathematics** (Egyptian geometry, Babylonian arithmetic) |
| P3 | **Iron Age** | ~1200–550 BCE | iron smelting, the alphabet, **coinage**, large empires |
| P4 | **Classical Antiquity** | ~550 BCE–500 CE | philosophy & formal science (Greece), **Euclidean math**, Roman engineering (concrete, aqueducts, roads), codified law, astronomy |
| P5 | **Post-Classical / Medieval** | ~500–1450 CE | heavy plough & three-field system, water/wind mills, **universities**, algebra & optics (Islamic Golden Age), gunpowder arrives, mechanical clock, banking origins |
| P6 | **Early Modern** | ~1450–1750 | **printing press**, the Scientific Revolution, global exploration & trade, gunpowder warfare, joint-stock companies, double-entry bookkeeping |
| P7 | **Industrial** | ~1750–1900 | **steam power**, factories, railways, steel, electricity begins, germ theory, mass production |
| P8 | **Modern** | ~1900–2000 | electrification, internal combustion, flight, electronics, **computing**, antibiotics, the present anchor |
| — | (forward) | 2000+ | the existing near-future → space-age arc |

---

## 2. Proposed era mapping
The sim already starts at **the dawn (settlement/subsistence = Neolithic)**, so
Paleolithic (P0) is the pre-game seed, not an era. The existing **modern→future**
eras (the 2000→2250 arc) stay; we ground the **pre-modern** stretch in P1–P8.

**Proposed scheme (8 historical eras, then the existing forward arc):**

| Era | Name | Period | Economic regime |
|---|---|---|---|
| 1 | Neolithic | P1 | subsistence (the dawn — *built*) |
| 2 | Bronze Age | P2 | barter / early redistributive states |
| 3 | Iron Age | P3 | early markets + coinage |
| 4 | Classical | P4 | money, long-distance trade, slavery economies |
| 5 | Medieval | P5 | feudal / guild / manorial |
| 6 | Early Modern | P6 | mercantile / proto-capitalist |
| 7 | Industrial | P7 | industrial capitalism |
| 8 | **Modern (≈2000)** | P8 | market/finance/services — **the current content anchor** |
| 9–17 | (existing) | 2007→2250 | near-future → space age |

This is the main decision to confirm: **8 historical eras** (vs the current coarse 4
— subsistence/agrarian/preindustrial/industrial). It moves the modern anchor from
era 5 → era 8 (a data re-base: shift existing content `era_available` +3, widen
`knowledge_to_advance`, add the new era rows). Alternatives if 8 feels too many:
- **6 eras**: fold Bronze+Iron+Classical → one "Ancient" era, Medieval, Early
  Modern, Industrial, Modern (modern → era 6).
- **Keep 4**, just rename to historical labels (least change, least fidelity).

Recommendation: **8** — each transition (bronze, iron, classical synthesis, the
medieval mill/university, printing, steam) is a genuinely distinct economic/knowledge
regime, and the granularity is what makes "watch a society move through history"
legible.

---

## 3. The knowledge / tech arc (what the world moves through)
Per era, the **knowledge** the population accumulates and the **capabilities** it
unlocks — the content the knowledge engine advances through. (Goods/recipes/
facilities are authored per era via `era_available`; this is the staged content
effort. Listed as the arc, not a player tree.)

- **1 Neolithic** — domestication, crop rotation basics, pottery, weaving, the bow.
  *Unlocks:* settled farming, storage (granary), basic crafts. (Mostly built.)
- **2 Bronze Age** — **writing & record-keeping**, **arithmetic/geometry**, bronze
  smithing, the wheel & cart, irrigation, sailing. *Unlocks:* metal tools, the
  scribe/temple bureaucracy, surplus accounting, the first cities.
- **3 Iron Age** — iron smelting, the alphabet, **coinage**, masonry. *Unlocks:*
  cheap durable tools/weapons, money-mediated trade, larger polities.
- **4 Classical** — **formal mathematics & logic**, mechanics, concrete & the arch,
  aqueducts, codified law, astronomy/calendar. *Unlocks:* engineering works, roads &
  long-distance trade, philosophy/medicine as professions.
- **5 Medieval** — heavy plough, the **horse collar**, water/wind mills, **algebra**,
  optics, the mechanical clock, the **university**, double-entry's ancestors,
  gunpowder. *Unlocks:* mechanical power, institutional knowledge, guild manufacture,
  banking.
- **6 Early Modern** — the **printing press** (knowledge compounding), the
  **scientific method**, navigation/cartography, ballistics, joint-stock finance.
  *Unlocks:* mass literacy, global trade & chartered companies, the science feedback
  loop (knowledge production accelerates sharply here).
- **7 Industrial** — the **steam engine**, factory system, railways, the telegraph,
  steel, **germ theory**, chemistry. *Unlocks:* mechanized mass production, fossil
  energy, public health, the wage-labour economy.
- **8 Modern** — electrification, internal combustion, **electronics & computing**,
  antibiotics, mass media. *Unlocks:* the existing V1 modern economy (the anchor).

A key emergent property to preserve: the **knowledge production rate rises by era**
(more scholars supportable, printing/universities multiply output), so advancement
accelerates — the millennia of the Bronze Age give way to the decades of the modern
arc, *because* knowledge compounds. The knowledge engine already has the lever
(scholar output × era multiplier); the per-era thresholds (`knowledge_to_advance`)
encode the rising bar.

---

## 4. Implementation staging (once the scheme is confirmed)
1. **Re-base `eras.csv`** to the 8 historical eras + the forward arc; shift existing
   content `era_available` to the new modern index; set per-era `knowledge_to_advance`
   and `economic_regime`. (Mechanical, data-driven; mirrors the prior +4 re-base.)
2. **Per-era knowledge milestones** — encode the defining advances (writing,
   coinage, printing, steam…) as the knowledge content the engine moves through, and
   the regime each era switches on (barter → coinage → money → mercantile → …).
3. **Per-era goods/recipes/facilities** — author outward from the dawn (the big,
   ongoing content effort; each era ships playable on its own).

## 5. Decision to confirm
- **Era count/scheme**: 8 historical eras (recommended), 6, or rename-only 4?
- Anything to add/cut from the period list or per-era arc before I re-base the data
  and start authoring the tech arc?
