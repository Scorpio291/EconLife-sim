# EconLife — Logistics & Political Scale: from the Ox to the Photon (v01)

Status: **proposed** (2026-06-25). Design principle; no behavioral change.
Generalizes the "tyranny of the ox" (medieval band, §3.5 of
`EconLife_Medieval_Band_Expansion_v01.md`) into the cross-era law that governs how
large a polity can be — bookended by two *physical* limits: the ox-cart (the floor)
and the speed of light (the ceiling).

---

## 0. The one law

**The maximum scale of centralized authority equals the logistics/communication
radius** — how far a centre can project *control, supply, and defence* before the
cost (matter, energy, time, latency) eats the benefit. Cross that radius and
authority *must* devolve to the periphery, which then drifts: economically,
culturally, and eventually genetically.

This is not a political assumption to script — it is a physical constraint to
simulate. Feudal lords, continental empires, the global order, and autonomous star
colonies are all the *same mechanic* at different radii. The radius is set by the
era's transport and communication technology, floored by the ox and capped, finally
and forever, by the photon.

---

## 1. The two physical bookends

### The ox (the floor) — already specified
A draft team eats the grain it hauls, so over land grain has a hard economic radius
of ~tens of km (water extends it). Power therefore fragments to that granularity:
a castle + lord + garrison at each local surplus, fed and defended locally. The
medieval band makes this the feudal generator (§3.5). The control radius here is
*tiny*, so the political map is *maximally fragmented*.

### The photon (the ceiling) — this document
Information and matter cannot exceed `c`. Within a solar system the round-trip
latency is minutes to hours — a centre can still govern (light-lag is annoying, not
fatal). **Between stars it is years to centuries.** You cannot:
- *control* a colony whose reply to an order arrives a decade later;
- *supply* it economically (shipping mass across light-years costs more energy than
  the cargo is worth — the photon-scale tyranny of the ox);
- *defend* it (reinforcements arrive years after the attack is over).

So interstellar authority *must* devolve to each system. Star colonies become
sovereign by physics, trade only the highest-value / lowest-mass / information-like
goods, defend themselves, and — across generations of isolation — **diverge**. That
is era 17 (`divergence`) emergent from `c`, not narrated.

---

## 2. The arc between the bookends: expansion → consolidation → re-fragmentation

Between the ox and the photon, transport/comms technology keeps *expanding* the
radius — so the political scale grows, then collapses again when the photon wall is
hit:

| Era band | Radius-setting tech | Control radius | Political scale (emergent) |
|---|---|---|---|
| Medieval (`feudal`) | ox-cart, horse | ~tens of km (land) | fragmented: manors, lords |
| Classical / Early-modern | Roman roads, money, sail/caravel | regional → oceanic | cities, kingdoms, mercantile reach |
| Industrial | rail, **telegraph**, steamship | continental → global | nation-states, colonial empires (govern India from London) |
| Modern | jet, satellite, **internet** | global, near-instant | globalization; one coordinated market |
| Space (`expansion`, era 16) | fusion/torch ships, in-system comms | solar-system (light-minutes) | a Solar polity — still governable |
| Space (`divergence`, era 17) | **none — the photon is the wall** | one star system | re-fragmentation: sovereign systems, divergence |

The shape is **fragmentation → consolidation → re-fragmentation**. Humanity starts
local (the ox), is knit together as technology expands the radius to planetary
(empire, then globalization), and is torn apart again the moment it leaves the one
place where the radius can span the whole civilization — bounded forever by the only
limit that never yields. The Divergence era is the **medieval mirror**: localism
reborn at cosmic scale, for the same physical reason.

---

## 3. The mechanic (parallel to §3.5, new spatial substrate)

The ox-cart computes deliverable *grain* across `ProvinceLink`s. The interstellar
layer computes deliverable *control / supply / force* across **star links**, with the
same conserved-cost shape but a new primitive: **latency = distance / c**.

- **Control radius.** A polity can enforce policy on a node only while round-trip
  latency stays under an era/tech-set threshold (in-system: hours, fine; interstellar:
  years, impossible). Beyond it the node flips to **autonomous** — it sets its own
  policy, like a province leaving a kingdom's writ.
- **Supply.** Interstellar cargo delivered = `payload − energy_cost(distance)`; for
  bulk goods the cost exceeds the cargo well before the nearest star, so only
  high-value/low-mass/information crosses. Most systems are self-sufficient or
  isolated (the photon-scale ox-cart limit).
- **Defence.** Force projection has the same latency gate: you defend what you can
  reach in time. Sovereign systems garrison themselves (the castle, at a star).
- **Divergence.** Isolation over generations drifts each system's economy, tech,
  institutions, and (with the existing generational-adaptation/`hardiness` model)
  biology — emergent speciation of polities. This is the era's payoff.
- **Genesis.** A sovereign **system-polity** forms at each colonized star once it
  falls outside the control radius — exactly as a castle forms at each grain locus
  outside the haulage radius. Same generator, new scale.

Conserved (energy/mass costs are real and deducted; no FTL exceptions),
deterministic, and — like the ox — it only ever *constrains*: it never grants reach,
it removes it past the limit.

---

## 4. Scope & sequencing

- **Out of V1.** Eras 16–17 are `v1_in_scope = 0`; this needs an interstellar spatial
  layer (star systems, light-year distances, latency) that does not exist yet. This
  doc captures the principle now, while the ox-cart insight that generalizes to it is
  fresh, so the space-age band is designed *before* it is built — and so the engine's
  spatial abstractions (`ProvinceLink` → a general "link with a transit cost and a
  latency") are shaped with both bookends in mind.
- **Reuse, don't reinvent.** The medieval ox-cart layer (`grain_logistics`, M2 of the
  medieval band) and the interstellar layer are the *same* computation over different
  links and costs; build the medieval one as the general case (link → deliverable
  fraction + latency) so the space age extends rather than duplicates it.
- **Latency is the new axis.** The ox-cart cares about *mass cost over distance*; the
  photon adds *time cost over distance* (latency), which is what actually breaks
  central authority interstellar. The general link model should carry both a
  throughput cost and a latency, so feudal fragmentation (mass-bound) and interstellar
  fragmentation (latency-bound) are two settings of one mechanic.
- **The ceiling is "permanent under *known* physics" — an explicit sci-fi extension
  point.** `c` is the hard limit *today*; speculative tech (FTL, wormholes/jump gates,
  relativistic or entangled comms, generation-ship logistics) is intended later and
  would *relax* the latency/throughput cost on specific links — i.e., a high-tech link
  buys back radius, re-enabling consolidation across stars. So the limit is modelled as
  a **configurable frontier per link**, not a global constant: baseline links obey the
  photon, sci-fi links lower its bite. The mechanic (radius → political scale) is
  unchanged; sci-fi science just edits the cost, exactly as roads once edited the ox's.

## 5. Open decisions

- **L1:** the control-radius threshold — a single latency cap that flips a node to
  autonomous, or a graded authority that decays with latency (partial control in the
  light-hours band, none in the light-years band)? Graded is richer and matches the
  empire→globalization→fragmentation gradient.
- **L2:** does the in-system space era (`expansion`, era 16) stay centralized (a Solar
  polity) under light-minute latency, making `divergence` (era 17) the first era the
  wall actually bites? (Recommended — it makes the re-fragmentation a *threshold event*
  the player can see coming.)
- **L3:** how far to take divergence — economic/institutional only, or also the
  generational biological drift the `hardiness`/adaptation model already supports
  (speciation of isolated colonies)? The latter is the dramatic, on-brand outcome.

---

## 6. One-paragraph summary

One law sets political scale across all of history: a centre can rule only as far as
it can project control, supply, and defence before the cost eats the benefit — the
logistics/communication radius. It is floored by the ox (grain eaten in transit →
feudal fragmentation) and capped, permanently, by the photon (years of interstellar
latency → sovereign, diverging star systems). Between them, transport and comms tech
keep widening the radius — roads, sail, rail, telegraph, the internet — so humanity
consolidates from manors to empires to one global market; then it leaves Earth, hits
the one limit that never yields, and re-fragments. Feudalism and interstellar
Divergence are the same mechanic at the two ends of the only ruler the universe
enforces: the speed of light.
