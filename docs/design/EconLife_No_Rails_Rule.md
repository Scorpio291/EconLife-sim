# EconLife — The No-Rails Rule

**Ratified 2026-07-30**, after the third rail in this codebase was found the same
way: not by review, but by printing a limit next to the value it limited and seeing
the value sitting exactly on it.

The root `CLAUDE.md` states the principle. This states how to CATCH a violation,
because every rail here was written in good faith, looked like modelling, and hid a
real defect underneath it for months.

---

### The test: name the thing that stops it

For any limit, answer in one sentence: **what physically stops this from going
higher?** If the answer names something in the world — the land is finite, the
granary holds what was stored, a cohort cannot lose more people than it has, the
oxen eat what they carry, you cannot record what nobody knows — it is a
mechanism, and the limit is a consequence of it.

If the answer is "the constant", it is a rail. Four phrasings that always mean
the answer is "the constant":

- *"That's roughly what it should be."* — a fitted outcome, not a cause.
- *"Otherwise it explodes."* — the explosion is the defect; the cap is a gag.
- *"It rises with the era / tier / level."* — a schedule of outcomes with the
  mechanism left out. This is the most dangerous form because it looks like
  modelling, and it was the form of every rail found here.
- *"It's an absolute number."* — **if a limit does not scale with the thing it
  bounds, it is a rail in another dress.** `labor_half_saturation` was a flat 1,500
  workers while provinces held 15,000 to 3,000,000 people, so every province sat
  deep in the saturated region where marginal labour is worth nothing. Ask what the
  quantity is proportional to (here: the land) and write that.

### What is allowed

- **Domain bounds on a quantity's own definition.** A fraction lies in [0,1]; a
  probability cannot exceed 1; a stock cannot go negative. Bounding a share of
  people at "all of them" is arithmetic, not a rail.
- **Crash sentinels for non-finite values.** `std::max(x, 1e-3f)` guarding a
  divide. Say so in the comment, so nobody later reads it as a design bound.
- **Set unions and independent limits.** `max(stratum, town)` because the town is
  a subset of the stratum; `min(what can be spared, what can be provisioned)`
  because both are real and the smaller binds. Two mechanisms, not a cap.
- **Pure pacing dials with no mechanical consequence.** The era knowledge
  thresholds are the only ones here: they label a trajectory, they do not shape
  it. Anything that feeds back into behaviour is not in this class.

### Prefer these forms

| instead of | write | because |
|---|---|---|
| `min(x, cap)` | `x / (1 + x/scale)` or `1 - exp(-x/scale)` | approaches a bound, never reaches it, and the bound is a property of the form |
| a probability constant | `1 - exp(-rate)` | Poisson first-arrival — physically in [0,1) with no cap needed |
| a per-era schedule | the quantity the era is a proxy for | eras are labels; knowledge, capital and haulage are things |
| a fixed share | a stock with an inflow and an outflow | stocks can fall, which is where falls come from |

### The five ways a rail hides, all found here

1. **It binds silently.** The per-regime specialist ceiling sat exactly on 15.0%,
   18.0%, 22.0% at every era for the whole climb. Nothing failed; the economy
   simply never industrialised. **Print the limit next to the value it limits.**
   A quantity that sits exactly on its bound is a rail doing the deciding.
2. **It compensates for a defect elsewhere.** That ceiling was holding down a
   non-farming share inflated by an equilibrium surplus of 1.5, which mechanically
   implies at least 33% of people are spare. Removing the rail did not break the
   model — it revealed the surplus. **Expect removing a rail to expose something.
   That is the point, and the exposure is progress, not regression.**
3. **It does not scale — or scales against the wrong thing.** A constant
   calibrated for one magnitude and applied at another decides the outcome by
   accident. **Ask what every constant is proportional to**, and write the
   proportionality rather than a number that happens to be right once.

   Getting the proportionality *approximately* right is not enough, because the
   wrong denominator can cancel a real effect out completely. Making the labour
   half-saturation proportional to natural capital fixed the scale and broke the
   physics: the carrying ceiling and the half-saturation then both carried soil
   fertility, so their ratio — the marginal product of a pair of hands, which is
   what a thinly settled population actually lives on — was *identical on a river
   valley and on scrubland*. Land quality had been silently divided out. Hands are
   spent by the acre and harvest is taken by the quality, so extent is the
   denominator and yield is the numerator. **Check the ratio, not just the units:
   ask what survives when you divide the numerator by the denominator, and whether
   the thing that cancelled was supposed to matter.**
4. **It pins the signal it answers to.** The specialist assignment solved for the
   farm labour that makes output equal `need + granary upkeep`, and the
   population-growth signal was then measured as `output / (need + upkeep)` — so it
   read ~1.0 by construction, at every level of abundance, forever. The demography
   could never see a rich world, so the population never grew into its land, so
   labour stayed spare, so the assignment freed more specialists. A closed ring with
   no external quantity in it. **For every signal, ask which module last wrote the
   numerator and which wrote the denominator. If it is the same one, the signal is
   reporting its own decision back to itself.**
5. **It is arithmetically unreachable.** Secession required a member holding 1/N
   to out-muscle `0.8 x cohesion x (N-1)/N`, impossible for N >= 3 at any cohesion.
   It read as "rare"; it was "never". **Work the arithmetic of every threshold at
   the extremes before believing a mechanism is merely inactive.**

### Before adding any constant

1. State its **real-world unit and source** in the comment: tonnes, acres, years,
   deaths per person-year, a dated figure from the record. "Tuned so the curve
   comes out" is the confession that it is a rail.
2. If it is a **unit bridge** between model-internal scales, say so explicitly
   (`tonnes_per_deposit_unit`, `capital_utilisation_halfsat`) — those are honest,
   but they must be labelled or the next reader will treat them as physical.
3. Write the **test that would fail if it became load-bearing**: assert the
   mechanism's shape (monotone, saturating, zero at zero, unreachable-at-extremes),
   not the number.

### And the one that is not about rails at all

**A per-tick rate is only correct if every tick runs.** The stratum inertia was
`rate / ticks_per_year` applied per tick; the history harness fast-forwards at one
step per year, so it advanced 365x too slowly there — and every measurement of it
taken this session was taken under that regime. It looked exactly like a slow
mechanism. **Evolve stocks on a stated cadence (annual, like the granary, the soil
and the capital), or scale by real elapsed time. Never assume the caller's stride.**
