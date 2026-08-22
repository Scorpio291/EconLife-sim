#include "population_aging_module.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

float PopulationAgingModule::compute_income_convergence(float current_income, float target_wage,
                                                        float rate) {
    return current_income + rate * (target_wage - current_income);
}

float PopulationAgingModule::compute_employment_convergence(float current_rate, float target_rate,
                                                            float rate) {
    return std::clamp(current_rate + rate * (target_rate - current_rate), 0.0f, 1.0f);
}

float PopulationAgingModule::compute_education_drift(float current_level, float province_level,
                                                     float max_drift) {
    float diff = province_level - current_level;
    float drift = std::clamp(diff, -max_drift, max_drift);
    return std::clamp(current_level + drift, 0.0f, 1.0f);
}

float PopulationAgingModule::compute_gini_coefficient(const std::vector<float>& sorted_incomes) {
    if (sorted_incomes.empty())
        return 0.0f;

    float n = static_cast<float>(sorted_incomes.size());
    float total_income = 0.0f;
    for (float inc : sorted_incomes)
        total_income += inc;

    if (total_income <= 0.0f)
        return 0.0f;

    float weighted_sum = 0.0f;
    for (size_t i = 0; i < sorted_incomes.size(); ++i) {
        float rank =
            2.0f * static_cast<float>(i) - static_cast<float>(sorted_incomes.size()) + 1.0f;
        weighted_sum += rank * sorted_incomes[i];
    }

    float gini = weighted_sum / (n * total_income);
    return std::clamp(gini, 0.0f, 1.0f);
}

bool PopulationAgingModule::is_monthly_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_MONTH) == 0;
}

bool PopulationAgingModule::is_annual_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_YEAR) == 0;
}

PopulationAgingModule::PlagueYear PopulationAgingModule::plague_year(
    float disease_dial, float urban_fraction, float susceptible, DeterministicRNG& rng,
    const PopulationAgingConfig& cfg) {
    PlagueYear out;
    out.susceptible_after = std::clamp(susceptible, 0.0f, 1.0f);
    const float d = std::clamp(disease_dial, 0.0f, 1.0f);

    // Population turnover refills the susceptible pool: the people born since the last
    // wave have never met the disease. At a pre-modern life expectancy of ~35 years that
    // is roughly a thirtieth of the population a year, which is what sets the recurrence
    // interval — long enough for a new generation, short enough that plague returned to
    // England six times in the fifty years after 1348.
    out.susceptible_after +=
        (1.0f - out.susceptible_after) * std::max(0.0f, cfg.epidemic_susceptible_recovery_per_year);
    out.susceptible_after = std::clamp(out.susceptible_after, 0.0f, 1.0f);
    if (d <= 0.0f)
        return out;

    const float density = std::clamp(urban_fraction, 0.0f, 1.0f);
    // Outbreak hazard rate rises with the world's disease load AND crowding (towns
    // are disease vectors — so disease brakes urbanization). The annual probability
    // is the Poisson arrival 1 - exp(-rate): physically in [0,1) with no cap.
    const float rate = cfg.epidemic_base_rate * d * (1.0f + cfg.epidemic_density_weight * density);
    const float p = 1.0f - std::exp(-std::max(0.0f, rate));
    if (rng.next_float() >= p)
        return out;  // no outbreak this year
    out.outbreak = true;

    // A wave reaches a share of the people who have never had it, and kills in
    // proportion to how many that is. The FIRST wave into a wholly susceptible
    // population is catastrophic; the same wave twenty years later, when most of the
    // survivors carry resistance and only the young are new, is a bad year. Nothing
    // states the declining lethality — it is the stock being drawn down.
    const float reached = std::max(0.0f, cfg.epidemic_attack_rate) * out.susceptible_after;
    out.mortality_factor = 1.0f + cfg.epidemic_severity * d * (1.0f + density) * reached;
    out.susceptible_after = std::clamp(out.susceptible_after - reached, 0.0f, 1.0f);
    return out;
}

float PopulationAgingModule::epidemic_mortality_factor(float disease_dial, float urban_fraction,
                                                       DeterministicRNG& rng,
                                                       const PopulationAgingConfig& cfg) {
    // A wholly susceptible population — the severity of a first wave.
    return plague_year(disease_dial, urban_fraction, 1.0f, rng, cfg).mortality_factor;
}

float PopulationAgingModule::disaster_mortality_factor(float geology_dial, DeterministicRNG& rng,
                                                       const PopulationAgingConfig& cfg) {
    const float g = std::clamp(geology_dial, 0.0f, 1.0f);
    if (g <= 0.0f)
        return 1.0f;
    // Poisson arrival: probability = 1 - exp(-rate); physical, uncapped.
    const float p = 1.0f - std::exp(-std::max(0.0f, cfg.geology_disaster_base_rate * g));
    if (rng.next_float() >= p)
        return 1.0f;  // no disaster this year
    return 1.0f + cfg.geology_disaster_severity * g;  // quake/storm/wildfire mortality spike
}

float PopulationAgingModule::annual_probability_from_rate(float annual_rate) {
    // Poisson first-arrival: with a hazard rate of `annual_rate` deaths per person-year,
    // the chance of dying at least once during the year is 1 - exp(-rate). Physically
    // self-bounding in [0, 1) — the reason no cap is needed on the rate.
    if (std::isnan(annual_rate))
        return 0.0f;  // crash sentinel only: a NaN rate must not annihilate a cohort
    if (annual_rate <= 0.0f)
        return 0.0f;
    return 1.0f - std::exp(-annual_rate);  // +inf -> exactly 1.0
}

float PopulationAgingModule::hazard_rate_multiplier(float world_hazard, float hardiness,
                                                    float hardiness_floor) {
    // std::max is the divide-by-zero sentinel, NOT an outcome bound: the ratio itself is
    // deliberately uncapped (see annual_probability_from_rate for the physical bound).
    return world_hazard / std::max(hardiness, hardiness_floor);
}

float PopulationAgingModule::radiation_fertility_factor(float radiation_dial,
                                                        const PopulationAgingConfig& cfg) {
    const float r = std::clamp(radiation_dial, 0.0f, 1.0f);
    return std::max(0.0f, 1.0f - cfg.radiation_fertility_penalty * r);
}

float PopulationAgingModule::compute_natural_death_probability(float age, float lifespan,
                                                               float base_prob) {
    if (age < lifespan)
        return 0.0f;
    float over = 1.0f + (age - lifespan) * 0.05f;  // +5% of base per year past lifespan
    return std::clamp(base_prob * over, 0.0f, 1.0f);
}

float PopulationAgingModule::compute_mean_income(
    const std::map<DemographicGroup, PopulationCohort>& cohorts) {
    double weighted = 0.0;
    uint64_t total = 0;
    for (const auto& [group, c] : cohorts) {
        (void)group;
        weighted += static_cast<double>(c.size) * static_cast<double>(c.median_income);
        total += c.size;
    }
    if (total == 0)
        return 0.0f;
    return static_cast<float>(weighted / static_cast<double>(total));
}

namespace {

bool is_retiree_group(DemographicGroup g) {
    return g == DemographicGroup::retiree_urban || g == DemographicGroup::retiree_rural;
}

bool is_youth_group(DemographicGroup g) {
    return g == DemographicGroup::youth_urban || g == DemographicGroup::youth_rural;
}

bool is_urban_group(DemographicGroup g) {
    return g == DemographicGroup::youth_urban || g == DemographicGroup::working_urban_low ||
           g == DemographicGroup::working_urban_mid || g == DemographicGroup::working_urban_high ||
           g == DemographicGroup::retiree_urban;
}

// Land <-> town, matched by income tier so a migrant arrives as the same sort of
// person they left as. Retirees are absent on purpose: it is the young who walk to
// town for work, and the old who stay where they are.
constexpr std::pair<DemographicGroup, DemographicGroup> kMigrationPairs[] = {
    {DemographicGroup::youth_rural, DemographicGroup::youth_urban},
    {DemographicGroup::working_rural_low, DemographicGroup::working_urban_low},
    {DemographicGroup::working_rural_mid, DemographicGroup::working_urban_mid},
    {DemographicGroup::working_rural_high, DemographicGroup::working_urban_high},
};
constexpr size_t kMigrationPairCount = 4;

// MIGRATION — people walk toward bread. `town_the_land_can_hold` is the size the
// countryside can both spare and feed (the caller takes the binding one of those two
// limits); the gap between it and who actually lives in the town closes at `rate` per
// year, in whichever direction it points. A town with grain coming in fills up; a town
// whose catchment has failed empties back onto the land, which is what a famine did to
// cities.
//
// Conserved head for head: every person added to an urban cohort is removed from the
// matching rural one and vice versa. Nobody is created or destroyed here — the town's
// size is this flow meeting the urban graveyard's mortality, and that balance is what
// holds pre-industrial urbanisation to a few percent (measured 3-5%) with nothing
// anywhere saying so.
void migrate_land_and_town(std::map<DemographicGroup, PopulationCohort>& cohorts,
                           float town_the_land_can_hold, float rate) {
    if (rate <= 0.0f)
        return;
    // Read without inserting: a province that has no such cohort has nobody in it, and
    // conjuring an empty one would quietly change the province's income distribution.
    auto held = [&cohorts](DemographicGroup g) -> uint64_t {
        auto it = cohorts.find(g);
        return it == cohorts.end() ? 0u : it->second.size;
    };

    uint64_t town = held(DemographicGroup::retiree_urban);
    for (const auto& [land, city] : kMigrationPairs) {
        (void)land;
        town += held(city);
    }
    const double gap = static_cast<double>(town_the_land_can_hold) - static_cast<double>(town);
    const int64_t signed_movers = std::llround(gap * static_cast<double>(rate));
    if (signed_movers == 0)
        return;
    const bool to_town = signed_movers > 0;
    uint64_t remaining = static_cast<uint64_t>(to_town ? signed_movers : -signed_movers);

    uint64_t available[kMigrationPairCount] = {};
    uint64_t pool = 0;
    for (size_t i = 0; i < kMigrationPairCount; ++i) {
        available[i] = held(to_town ? kMigrationPairs[i].first : kMigrationPairs[i].second);
        pool += available[i];
    }
    if (pool == 0)
        return;
    if (remaining > pool)
        remaining = pool;  // you cannot move people who are not there

    // Proportional to where the movers actually live, then the rounding remainder is
    // handed out in canonical group order to whoever still has people to send — so the
    // headcount that moves is exact even when some cohorts are empty.
    uint64_t take[kMigrationPairCount] = {};
    uint64_t allocated = 0;
    for (size_t i = 0; i < kMigrationPairCount; ++i) {
        take[i] = remaining * available[i] / pool;
        allocated += take[i];
    }
    for (size_t i = 0; i < kMigrationPairCount && allocated < remaining; ++i) {
        const uint64_t room = available[i] - take[i];
        const uint64_t extra = std::min(room, remaining - allocated);
        take[i] += extra;
        allocated += extra;
    }

    for (size_t i = 0; i < kMigrationPairCount; ++i) {
        if (take[i] == 0)
            continue;
        const auto& [land, city] = kMigrationPairs[i];
        cohorts[to_town ? land : city].size -= static_cast<uint32_t>(take[i]);
        auto& dst = cohorts[to_town ? city : land];
        dst.group = to_town ? city : land;  // a cohort created by arrivals knows what it is
        dst.size += static_cast<uint32_t>(take[i]);
    }
}

// COHORTS AGE. Each year a share of the young reach working age and a share of the
// workers retire — 1/18 and 1/47, the lengths of a childhood and a working life.
//
// This was missing entirely, and it was survivable only while the young died at the same
// rate as everyone else. The moment child mortality was represented (R4A), the youth
// cohort became a trap: every birth landed in a bucket with five times the mortality and
// never left it, so the whole birth stream was consumed and the climb stopped. The age
// structure has to be real.
//
// Conserved head for head: nobody is created or destroyed by having a birthday. Retiring
// happens before graduating so this year's new workers do not retire in the same year,
// and graduates are distributed across the income tiers in proportion to the tiers that
// already exist — a child inherits its parents' station.
void age_cohorts(std::map<DemographicGroup, PopulationCohort>& cohorts,
                 const PopulationAgingConfig& cfg) {
    struct Ladder {
        DemographicGroup youth;
        DemographicGroup working[3];
        DemographicGroup retiree;
    };
    static constexpr Ladder kLadders[] = {
        {DemographicGroup::youth_urban,
         {DemographicGroup::working_urban_low, DemographicGroup::working_urban_mid,
          DemographicGroup::working_urban_high},
         DemographicGroup::retiree_urban},
        {DemographicGroup::youth_rural,
         {DemographicGroup::working_rural_low, DemographicGroup::working_rural_mid,
          DemographicGroup::working_rural_high},
         DemographicGroup::retiree_rural},
    };
    const double youth_years = std::max(1.0, static_cast<double>(cfg.youth_years));
    const double working_years = std::max(1.0, static_cast<double>(cfg.working_years));

    auto held = [&cohorts](DemographicGroup g) -> uint64_t {
        auto it = cohorts.find(g);
        return it == cohorts.end() ? 0u : it->second.size;
    };

    for (const auto& L : kLadders) {
        // Retire first: this year's graduates are not also this year's retirees.
        uint64_t retiring = 0;
        for (const auto& w : L.working) {
            const uint64_t size = held(w);
            if (size == 0)
                continue;
            const auto leaving = static_cast<uint64_t>(static_cast<double>(size) / working_years);
            if (leaving == 0)
                continue;
            cohorts[w].size -= static_cast<uint32_t>(leaving);
            retiring += leaving;
        }
        if (retiring > 0) {
            auto& r = cohorts[L.retiree];
            r.group = L.retiree;
            r.size += static_cast<uint32_t>(retiring);
        }

        // Then graduate, distributed across the tiers that already exist.
        const uint64_t young = held(L.youth);
        if (young == 0)
            continue;
        const auto graduating = static_cast<uint64_t>(static_cast<double>(young) / youth_years);
        if (graduating == 0)
            continue;
        cohorts[L.youth].size -= static_cast<uint32_t>(graduating);

        uint64_t tier_total = 0;
        for (const auto& w : L.working)
            tier_total += held(w);
        uint64_t placed = 0;
        for (size_t i = 0; i < 3; ++i) {
            const uint64_t take =
                (i + 1 == 3) ? (graduating > placed ? graduating - placed : 0u)
                             : (tier_total > 0 ? graduating * held(L.working[i]) / tier_total : 0u);
            if (take == 0)
                continue;
            auto& w = cohorts[L.working[i]];
            w.group = L.working[i];
            w.size += static_cast<uint32_t>(take);
            placed += take;
        }
    }
}

// REFUGEES ARRIVE AND LEAVE (R5). structural_demography decides who flees where — it is
// the only module that can, since flight crosses province borders — and publishes a
// signed headcount per province, conserved across the world. This applies it to the
// actual people.
//
// Refugees are drawn from and delivered to every cohort in proportion to its size: a
// famine does not select by station. Deterministic integer arithmetic, canonical group
// order, and a province can never lose more people than it has.
void apply_refugee_flow(std::map<DemographicGroup, PopulationCohort>& cohorts, float flow) {
    if (!(std::fabs(flow) >= 1.0f))
        return;
    uint64_t total = 0;
    for (const auto& [g, c] : cohorts) {
        (void)g;
        total += c.size;
    }
    if (total == 0)
        return;

    const bool arriving = flow > 0.0f;
    auto moving = static_cast<uint64_t>(std::llround(std::fabs(static_cast<double>(flow))));
    if (!arriving && moving > total)
        moving = total;  // you cannot send away more people than live here

    uint64_t placed = 0;
    size_t remaining_cohorts = cohorts.size();
    for (auto& [g, c] : cohorts) {
        (void)g;
        --remaining_cohorts;
        // The last cohort takes the rounding remainder so the headcount is exact.
        uint64_t share = remaining_cohorts == 0 ? (moving > placed ? moving - placed : 0u)
                                                : moving * c.size / total;
        if (!arriving && share > c.size)
            share = c.size;
        if (share == 0)
            continue;
        if (arriving)
            c.size += static_cast<uint32_t>(share);
        else
            c.size -= static_cast<uint32_t>(share);
        placed += share;
    }
}

// Annual births (added to youth cohorts) and per-cohort deaths, applied in
// place to a working copy of the cohort map. Deterministic: integer rounding,
// canonical group order.
// birth_surplus drives fertility (long-run food security: can the land sustainably
// support more people, reserves included?). famine_surplus drives starvation mortality
// (immediate availability, buffered by the granary). They differ in the commons: a
// society at its sustainable ceiling has birth_surplus ~1 (stops growing) but, with full
// granaries, famine_surplus ~1 (nobody starves). Both are 1.0 in market eras (neutral).
// hazard_rate_mult scales the mortality RATE (it is not a probability): see
// PopulationAgingModule::hazard_rate_multiplier.
void process_births_deaths(std::map<DemographicGroup, PopulationCohort>& cohorts, float stability,
                           float sick_rate, float addiction_rate, float birth_surplus,
                           float famine_surplus, float hazard_rate_mult, float fertility_mult,
                           float war_death_fraction, float faction_death_fraction,
                           float urban_crowding_rate, const PopulationAgingConfig& cfg) {
    uint64_t total = 0;
    for (const auto& [g, c] : cohorts) {
        (void)g;
        total += c.size;
    }

    // Food coupling (the Malthusian loop). All factors are NEUTRAL at surplus == 1.0,
    // so market eras (where surplus is always 1.0) are unchanged. A surplus lifts
    // births toward a cap AND relieves mortality (well-fed survival); a deficit
    // (surplus < 1) raises mortality.
    // THE WAGE VALVE. Fertility and mortality both answer to how well fed people are,
    // as power laws in w = consumption/subsistence, so the growth rate is the point
    // where they cross rather than a number imposed anywhere. A society exactly at
    // subsistence (w = 1) has both factors at 1 and stops growing on its own; one with
    // slack grows; one pressing on its land shrinks. This is what pinned real
    // pre-industrial growth near 0.04%/yr while population tripled.
    const float b_surplus = std::clamp(birth_surplus, 0.0f, 10.0f);
    const float birth_food_factor =
        std::clamp(std::pow(std::max(b_surplus, 1e-3f), cfg.wage_fertility_elasticity), 0.0f,
                   cfg.food_surplus_birth_cap);
    const float f_surplus = std::clamp(famine_surplus, 0.0f, 10.0f);
    float famine_mortality_factor;
    if (f_surplus < 1.0f) {
        // Hunger kills long before outright famine: mortality rises as a power law in
        // the shortfall, on top of the acute famine term below.
        famine_mortality_factor =
            std::pow(std::max(f_surplus, 1e-3f), -cfg.wage_mortality_elasticity) +
            cfg.food_deficit_mortality_strength * (1.0f - f_surplus) - 1.0f;
        famine_mortality_factor = std::max(1.0f, famine_mortality_factor);
    } else {
        famine_mortality_factor = std::max(
            cfg.food_mortality_floor, 1.0f - cfg.food_surplus_mortality_relief * (f_surplus - 1.0f));
    }

    // Births: survival scales with stability and healthcare (proxied by the
    // inverse of sick_rate, since HealthcareProfile is not on WorldState).
    float healthcare_proxy = std::clamp(1.0f - sick_rate, 0.0f, 1.0f);
    // THE QUANTITY-QUALITY TRANSITION (R4A). Families target surviving children, not
    // births, so the birth rate answers to how many of them live. It is neutral at the
    // pre-modern norm and falls as medicine takes hold — which is what breaks the
    // Malthusian feedback, and the reason the modern world stopped breeding into every
    // gain it made instead of staying at subsistence forever.
    //
    // Composed the same way the death loop below composes the youth rate, so families are
    // reading the mortality their children actually face.
    const float env_for_survival = (1.0f + (1.0f - std::clamp(stability, 0.0f, 1.0f))) *
                                   (1.0f + std::clamp(addiction_rate, 0.0f, 1.0f)) *
                                   famine_mortality_factor * hazard_rate_mult;
    const float youth_rate =
        cfg.base_annual_death_rate * env_for_survival * cfg.youth_mortality_multiplier;
    const float survival_factor = PopulationAgingModule::desired_births_factor(
        PopulationAgingModule::child_survival(youth_rate, cfg), cfg);
    float desired_rate = cfg.base_annual_birth_rate * std::clamp(stability, 0.0f, 1.0f) *
                         healthcare_proxy * birth_food_factor * survival_factor *
                         std::clamp(fertility_mult, 0.0f, 1.0f);  // radiation depresses fertility
    // Wanting more surviving children does not make a woman able to bear more. The
    // desired rate approaches what a population can physically produce — the highest
    // crude birth rates ever recorded sit near 55 per 1000 — and never reaches it, by
    // the same 1 - exp form mortality uses. At ordinary rates this is indistinguishable
    // from the desire; it bends only where biology actually bends.
    const float max_rate = std::max(1e-4f, cfg.max_biological_birth_rate);
    float birth_rate = max_rate * (1.0f - std::exp(-std::max(0.0f, desired_rate) / max_rate));
    auto births = static_cast<uint32_t>(
        std::llround(static_cast<double>(total) * static_cast<double>(birth_rate)));
    // Children are born where their parents live. This used to be a flat half-and-half
    // split, which silently drove ANY society toward a 50% urban composition no matter
    // what its land could feed — the town's share was decided by the split, not by
    // anything happening in the world. Splitting by where the working-age population
    // actually is makes the urban share a consequence of migration and mortality
    // instead. With no working-age population anywhere, births fall to the land: the
    // countryside is where people are when there is no town.
    uint64_t urban_parents = 0, rural_parents = 0;
    for (const auto& [g, c] : cohorts) {
        if (g == DemographicGroup::working_urban_low || g == DemographicGroup::working_urban_mid ||
            g == DemographicGroup::working_urban_high)
            urban_parents += c.size;
        else if (g == DemographicGroup::working_rural_low ||
                 g == DemographicGroup::working_rural_mid ||
                 g == DemographicGroup::working_rural_high)
            rural_parents += c.size;
    }
    const uint64_t parents = urban_parents + rural_parents;
    const auto urban_births =
        parents > 0 ? static_cast<uint32_t>(static_cast<uint64_t>(births) * urban_parents / parents)
                    : 0u;
    cohorts[DemographicGroup::youth_urban].size += urban_births;
    cohorts[DemographicGroup::youth_rural].size += births - urban_births;

    // Deaths. Mortality is composed as an annual HAZARD RATE (expected deaths per
    // person-year) — instability, addiction, famine and the world's hazards each scale
    // the rate — and the annual death PROBABILITY is the Poisson first-arrival
    // 1 - exp(-rate). That conversion is the bound: mortality approaches 100% and can
    // never exceed it, so nothing in the chain needs a cap.
    float mortality_rate_env = (1.0f + (1.0f - std::clamp(stability, 0.0f, 1.0f))) *
                               (1.0f + std::clamp(addiction_rate, 0.0f, 1.0f)) *
                               famine_mortality_factor *
                               hazard_rate_mult;  // per-setting world hazards (1.0 = earthlike)
    // War casualties are an INDEPENDENT competing risk, published by warfare in real
    // units (battle dead / population; G2). Compose SURVIVALS instead of adding
    // probabilities: S = (1 - p_env) * (1 - p_war). This is exactly the addition of the
    // two hazard RATES — 1 - (1-p_env)(1-p_war) == 1 - exp(-(rate_env + rate_war)) with
    // rate_war = -ln(1 - p_war) — so nothing is double-counted, and the publisher's
    // contract stays exact at both ends: with no environmental deaths a cohort loses
    // precisely the published fraction, and a published 1.0 still annihilates it. (The
    // old form added the two probabilities, which could exceed 1.0 and leaned on the
    // cohort-size floor below to stay physical; at Earth-normal rates the two agree to
    // within p_env * p_war, i.e. under 1% of the war term.)
    const float p_war = std::clamp(war_death_fraction, 0.0f, 1.0f);  // domain sentinel: it is
                                                                     // published as a fraction;
                                                                     // warfare owns its size
    // Factional conflict (R2D) is a THIRD independent competing risk, composed the same
    // way. Kept separate from war on purpose: one is a polity fighting a neighbour, the
    // other is a polity coming apart from the inside, and a society can be doing both.
    const float p_faction = std::clamp(faction_death_fraction, 0.0f, 1.0f);
    for (auto& [g, c] : cohorts) {
        if (c.size == 0)
            continue;
        float death_rate = cfg.base_annual_death_rate * mortality_rate_env;
        if (is_retiree_group(g))
            death_rate *= cfg.retiree_mortality_multiplier;  // frailer bodies: a higher rate
        // CHILDREN DIED. Roughly half of those born did not reach fifteen, across
        // societies as different as classical Rome, Tokugawa Japan and Stuart England.
        // Without this the young die at the same rate as the middle-aged and there is
        // nothing for medicine to fix — so no demographic transition is available.
        if (is_youth_group(g))
            death_rate *= cfg.youth_mortality_multiplier;
        // THE URBAN GRAVEYARD. Living in a town carries its own hazard, arriving as an
        // ADDITIVE rate rather than a multiplier because it is a distinct cause of
        // death — the crowd's endemic disease — not an amplification of the rest. It
        // does not touch the countryside, and it is what makes a pre-modern town a net
        // consumer of people that survives only on migrants.
        if (is_urban_group(g))
            death_rate += urban_crowding_rate;
        const float p_env = PopulationAgingModule::annual_probability_from_rate(death_rate);
        const float p_death = 1.0f - (1.0f - p_env) * (1.0f - p_war) * (1.0f - p_faction);
        auto deaths = static_cast<uint32_t>(
            std::llround(static_cast<double>(c.size) * static_cast<double>(p_death)));
        // A cohort cannot lose more people than it has (the remaining physical bound).
        c.size = (deaths >= c.size) ? 0u : (c.size - deaths);
    }
}

}  // namespace

void PopulationAgingModule::execute_province(uint32_t province_idx, const WorldState& state,
                                             DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    const auto& province = state.provinces[province_idx];

    // --- Significant-NPC aging (annual) ----------------------------------------
    // Advance age one year and roll natural death for NPCs past their lifespan.
    // Retirement role transitions are NOT modelled: there is no `retired`
    // NPCRole/status in V1 (documented gap; see population_aging/INTERFACE.md).
    if (is_annual_tick(state.current_tick)) {
        DeterministicRNG tick_rng(state.world_seed ^ static_cast<uint64_t>(state.current_tick));
        for (const auto& npc : state.significant_npcs) {
            if (npc.status != NPCStatus::active || npc.current_province_id != province.id)
                continue;
            NPCDelta nd;
            nd.npc_id = npc.id;
            nd.age_delta = 1.0f;
            float death_p = compute_natural_death_probability(
                npc.age_years, cfg_.natural_lifespan_years, cfg_.natural_death_annual_prob);
            if (death_p > 0.0f) {
                DeterministicRNG npc_rng = tick_rng.fork(npc.id);
                if (npc_rng.next_float() < death_p)
                    nd.new_status = NPCStatus::dead;
            }
            province_delta.npc_deltas.push_back(nd);
        }
    }

    // --- Background-population cohort lifecycle ---------------------------------
    // Monthly: income + employment convergence. Annual: education drift +
    // births/deaths. Aggregates (total_population, mean_income, gini) recomputed
    // after any change. Skipped entirely for unseeded (empty) or zero-population
    // provinces.
    const bool monthly = is_monthly_tick(state.current_tick);
    const bool annual = is_annual_tick(state.current_tick);
    if (province.cohort_stats && !province.cohort_stats->cohorts.empty() && (monthly || annual)) {
        const RegionCohortStats& cs = *province.cohort_stats;
        uint64_t pop = 0;
        for (const auto& [g, c] : cs.cohorts) {
            (void)g;
            pop += c.size;
        }
        if (pop > 0) {
            std::map<DemographicGroup, PopulationCohort> next = cs.cohorts;
            float new_hardiness = cs.hardiness;  // preserved unless the annual drift updates it
            // The plague stock: drawn down by a wave, refilled by turnover. Declared out
            // here so it can be published with the cohorts it acted on; only touched in a
            // year the annual block runs, so a monthly tick never resets it.
            float plague_susceptible_next = cs.plague_susceptible_fraction;
            bool plague_published = false;

            if (monthly) {
                for (auto& [g, c] : next) {
                    (void)g;
                    if (c.size == 0)
                        continue;
                    c.median_income = compute_income_convergence(
                        c.median_income, cs.regional_wage_anchor, cfg_.cohort_income_update_rate);
                    c.employment_rate =
                        compute_employment_convergence(c.employment_rate, cs.formal_employment_rate,
                                                       cfg_.cohort_employment_update_rate);
                }
            }
            if (annual) {
                for (auto& [g, c] : next) {
                    (void)g;
                    if (c.size == 0)
                        continue;
                    c.education_level = compute_education_drift(
                        c.education_level, province.demographics.education_level,
                        cfg_.max_education_drift_per_year);
                }
                const EraDefinition* era =
                    state.era_catalog.by_index(state.technology.current_era);
                // Pre-market (commons) demographics span the whole agrarian arc — through
                // the money/coinage eras and the feudal/mercantile/industrial regimes —
                // until the modern market economy takes over.
                const bool commons =
                    era && (era->economic_regime == "subsistence" ||
                            era->economic_regime == "barter" || era->economic_regime == "coinage" ||
                            era->economic_regime == "money" || era->economic_regime == "feudal" ||
                            era->economic_regime == "mercantile" ||
                            era->economic_regime == "industrial");

                // Pre-market demographics are FOOD-driven, not politics-driven: a dawn
                // society has no modern institutions, and the political stability score is
                // a proxy built for one — measured, it reads 0.11-0.22 on a perfectly
                // healthy Neolithic world, which would crush it. So the political channel
                // is NEUTRAL in the commons and food, disease, predators and the weather
                // are left to do the whole job, which is what actually moved a dawn
                // population.
                //
                // THIS WAS A RAIL AND IT DECIDED THE ECONOMY. It used to substitute a
                // constant, `commons_stability_floor` = 0.76, whose own comment said what
                // it was for: "set a little below the carrying ceiling so a fed population
                // stabilises JUST below the maximum the land can feed... Higher => less
                // surplus." Stability multiplies births and divides into mortality, so
                // that single number fixed the surplus at which births meet deaths — 1.45,
                // measured — and below the knee of the production curve the sparable share
                // is exactly 1 - 1/S, so it fixed the size of the non-farming class too.
                // The population could not press on its land because a constant would not
                // let it. What the number stood in for was real (the dawn was lethal), and
                // the real version is mortality in real units — see
                // base_annual_death_rate, which had been carrying a MODERN crude death
                // rate while medicine multiplied it further down.
                const float eff_stability = commons ? 1.0f : province.conditions.stability_score;

                // The world's hazard pressure (disease/predators/radiation/atmosphere/
                // geology/gravity-falls), Earth-normalized.
                const float world_hazard = hazard_mortality_from_settings(state.hazard_settings);
                // Generational hardiness: the mortality RATE scales with how far the
                // population's adaptation falls short of what the world demands. A people
                // matched to their world (hardiness == world_hazard) is neutral; a soft
                // people on a hard world (hardiness << world_hazard) is culled until they
                // adapt; an over-hardened people survive better. So harshness is relative
                // to the adapted population.
                //
                // --- Calibration: Earth-normal preserved, the transient un-softened -----
                // The rate coefficient is base_annual_death_rate itself — 0.008 deaths per
                // person-year, a real unit (Earth's crude death rate is ~0.0076/yr) — and
                // the maladaptation term is dimensionless and exactly 1.0 for an adapted
                // people (hazard_mortality_from_settings is Earth-normalized), so no new
                // coefficient is needed and Earth-normal mortality is preserved by
                // construction. Earth-normal, stability 0.9, unaddicted, fed:
                //     rate = 0.008 * (1 + 0.1) * 1.0 * 1.0 = 0.0088 per person-year
                //     was (rate used directly AS a probability): p = 0.008800
                //     now  p = 1 - exp(-0.0088)                  = 0.0087614  (-0.45% rel.)
                //     retirees (x4): rate 0.0352 -> was 0.035200, now 0.034587 (-1.74% rel.)
                // i.e. Earth-normal cohort deaths fall by under 2%; on a 100k cohort that
                // is 876 deaths/yr instead of 880.
                // Where the retired [0.15, 3.0] band actually bound — the multi-decade
                // maladaptation transient that IS the deathworld-colonisation arc — the
                // change is deliberate and large. Garden-bred hardiness 0.19 on a
                // deathworld (world_hazard 1.076) gives a ratio of 5.66, formerly pinned
                // to 3.0 (a 1.9x softening of the culling):
                //     rate = 0.008 * 1.1 * 5.66 = 0.0498 -> p = 4.86%/yr (was 2.64%)
                // and the ceiling is now physical: as the ratio grows p -> 100%, never
                // past it, and it is reached by dying, not by hitting a number.
                float hazard_rate_mult =
                    hazard_rate_multiplier(world_hazard, cs.hardiness, cfg_.hardiness_floor);
                // Medicine (germ theory, sanitation, …) from the tech tree cuts the
                // mortality RATE — halving the rate halves expected deaths. Applied to the
                // rate, before the rate->probability conversion, so (unlike under the old
                // band, which medicine was applied AFTER and could therefore push the
                // result below the supposed minimum anyway) there is no ordering subtlety.
                hazard_rate_mult *=
                    state.tech_effects_for_era(state.technology.current_era).mortality_mult;

                // Disease epidemics (M6a): an episodic mortality spike in the pre-market
                // (commons) arc, scaled by the world's disease dial and urban crowding —
                // the plague dips real population history shows. Medicine releases
                // disease-as-population-check in the modern era (commons == false there).
                if (commons && annual) {
                    const float urban_frac =
                        cs.total_population > 0
                            ? cs.urban_population / static_cast<float>(cs.total_population)
                            : 0.0f;
                    DeterministicRNG epi_rng(state.world_seed ^
                                             (static_cast<uint64_t>(state.current_tick) *
                                              0x9E3779B97F4A7C15ull) ^
                                             (static_cast<uint64_t>(province.id) << 17) ^
                                             0xED1DEC1Cull);
                    // PLAGUE COMES BACK. The wave's severity scales with how many people
                    // have never met the disease, and that stock is drawn down here and
                    // refilled by turnover — so the recurrence interval and the declining
                    // lethality emerge rather than being written anywhere. England kept
                    // losing people until 1450, a century after the Black Death, because
                    // plague returned six times in the first fifty years.
                    const PlagueYear plague =
                        plague_year(state.hazard_settings.disease, urban_frac,
                                    cs.plague_susceptible_fraction, epi_rng, cfg_);
                    hazard_rate_mult *= plague.mortality_factor;
                    plague_susceptible_next = plague.susceptible_after;
                    plague_published = true;
                    // Geology disasters (quakes/storms/wildfires) — a separate episodic
                    // spike scaled by the geology dial (not density). Independent RNG.
                    DeterministicRNG geo_rng(state.world_seed ^
                                             (static_cast<uint64_t>(state.current_tick) *
                                              0xC2B2AE3D27D4EB4Full) ^
                                             (static_cast<uint64_t>(province.id) << 23) ^
                                             0x6E01060715ull);
                    hazard_rate_mult *=
                        disaster_mortality_factor(state.hazard_settings.geology, geo_rng, cfg_);
                }
                // War casualties (G2): warfare publishes an EXTRA annual death
                // fraction (real units: battle dead / population, Lanchester
                // attrition). Owned by the publisher (reset on regime exit; 0 at
                // peace), applied unconditionally here. Passed as an additive rate,
                // not a multiplier — war kills a fraction of people, it does not
                // scale background mortality.
                const float war_deaths = annual ? cs.war_death_fraction : 0.0f;
                // Factional conflict from structural stress (R2D). Published by
                // structural_demography in this same annual tick (it declares
                // runs_before population_aging), so publication and consumption never
                // straddle a tick. 0 in a society that is not coming apart.
                const float faction_deaths = annual ? cs.faction_death_fraction : 0.0f;

                // Fertility tracks the long-run food signal the subsistence module writes
                // (output vs need + reserve upkeep): population grows only when the land
                // can sustainably support more, and stops at the ceiling — leaving the
                // upkeep surplus that funds specialists. Starvation, separately, fires
                // only once the granary itself is exhausted (a real reserve buffer), so a
                // population at its ceiling holds steady instead of either starving or
                // breeding into collapse.
                // TRIED AND REVERTED (2026-07-30): making fertility answer to
                // `wage_reference` — what people EXPECT, formed over years — rather than
                // to this year's harvest. Family formation genuinely does lag conditions,
                // and the lag is what produces the overshoot a secular cycle is made of.
                //
                // It bought almost nothing (peak PSI 1.59 -> 1.85, no behavioural change,
                // still no fragmentation) and cost a fragile cross-module dependency:
                // fertility would be silently pinned at the reference default wherever
                // structural_demography had not run, which is exactly the failure mode
                // the grain catchment signal had. Not worth it for the measured gain.
                const float birth_surplus = cs.subsistence_surplus_ratio;
                const float famine_surplus =
                    (commons && cs.food_store <= 0.0f) ? cs.subsistence_surplus_ratio : 1.0f;
                // Radiation chronically depresses fertility (planetary — applies in all
                // eras, never released, unlike the conquerable disease/geology shocks).
                const float fertility_mult =
                    radiation_fertility_factor(state.hazard_settings.radiation, cfg_);
                // The urban graveyard: crowding is what kills, so the penalty scales with
                // how big the town actually is — a hamlet is barely worse than the
                // countryside, a city of 100k carries nearly the whole burden. Saturating
                // in town size, so the hazard approaches the full rate and never exceeds
                // it. Sanitation and germ theory are exactly what closed the grave
                // historically, so the same tech mortality multiplier that ends the
                // plagues releases this too — which is why urbanisation could only break
                // past its pre-modern tenth once medicine arrived.
                const float crowding_rate = urban_crowding_rate(
                    cs.urban_population,
                    state.tech_effects_for_era(state.technology.current_era).mortality_mult, cfg_);

                process_births_deaths(next, eff_stability, cs.sick_rate, cs.addiction_rate,
                                      birth_surplus, famine_surplus, hazard_rate_mult,
                                      fertility_mult, war_deaths, faction_deaths, crowding_rate,
                                      cfg_);

                // Then everyone has a birthday. Without this the young never grow up and
                // nobody replaces the workers who die.
                age_cohorts(next, cfg_);

                // And those whose land has failed walk somewhere else. Conserved across
                // the world by the publisher: what leaves one province arrives at
                // another. This is how a collapse crosses a border — the arrivals are
                // more mouths on land that was already only just feeding itself.
                apply_refugee_flow(next, cs.refugee_flow);

                // Then people move. A town is people the countryside must both SPARE and
                // FEED, and those are two separate physical limits: the harvest says how
                // many hands can leave the fields (the non-farming stratum subsistence
                // publishes), and haulage says how many can be fed once concentrated in
                // one place (urban_capacity — the oxen eat what they carry). Whichever
                // binds first is the town the land can hold.
                //
                // The gap between that and who lives there closes a little each year, in
                // whichever direction it points. Conserved head for head — the town grows
                // by emptying the countryside and empties back onto it when either limit
                // fails. Commons only: in a market economy people move for wages and
                // work, which this law does not model.
                if (commons) {
                    const float stratum_town =
                        static_cast<float>(cs.total_population) * cs.specialist_fraction;
                    migrate_land_and_town(next, std::min(cs.urban_capacity, stratum_town),
                                          cfg_.urban_migration_rate_per_year);
                }

                // Hardiness drifts toward the world's hazard level over generations
                // (adaptation under sustained pressure; softening under ease).
                new_hardiness = cs.hardiness + (world_hazard - cs.hardiness) * cfg_.hardiness_drift_rate;
            }

            // Recompute aggregates over the canonical (sorted) group order.
            CohortStatsDelta cd;
            cd.region_id = province_idx;
            uint32_t new_total = 0;
            std::vector<float> incomes;
            incomes.reserve(next.size());
            for (const auto& [g, c] : next) {
                (void)g;
                new_total += c.size;
                incomes.push_back(c.median_income);
            }
            std::sort(incomes.begin(), incomes.end());
            cd.total_population = new_total;
            cd.mean_income = compute_mean_income(next);
            cd.gini_coefficient = compute_gini_coefficient(incomes);
            cd.hardiness = new_hardiness;
            cd.cohorts = std::move(next);
            province_delta.cohort_stats_deltas.push_back(std::move(cd));

            // The plague stock goes out with the cohorts it acted on: drawn down by this
            // year's wave, refilled by turnover. Only published in a year the annual
            // block actually ran, so a monthly tick never resets it.
            if (plague_published) {
                RegionDelta pd{};
                pd.region_id = province_idx;
                pd.plague_susceptible_replacement = plague_susceptible_next;
                province_delta.region_deltas.push_back(pd);
            }
        }
    }

    // --- Province-level demographic stress proxies (legacy monthly signal) -----
    if (!is_monthly_tick(state.current_tick))
        return;

    const auto& demographics = province.demographics;

    RegionDelta rdelta;
    rdelta.region_id = province_idx;

    // --- Stability contribution from demographic changes ---
    // High dependency ratio (old/young relative to working-age) degrades stability
    // by increasing fiscal strain on the province.
    // Proxy: if income_low_fraction dominates, stability faces downward pressure.
    {
        // Compute a simple demographic stress index: high share of low-income
        // cohorts combined with low education drives instability.
        float demographic_stress =
            demographics.income_low_fraction * (1.0f - demographics.education_level);
        constexpr float DEMOGRAPHIC_STABILITY_WEIGHT = -0.002f;
        rdelta.stability_delta = DEMOGRAPHIC_STABILITY_WEIGHT * demographic_stress;
    }

    // --- Inequality contribution from income distribution ---
    // Monthly re-evaluation: the Gini proxy (high minus low income fraction)
    // nudges the province inequality index toward the current demographic signal.
    {
        float income_spread = demographics.income_high_fraction - demographics.income_low_fraction;
        float target_inequality = std::clamp(income_spread, 0.0f, 1.0f);
        constexpr float MONTHLY_INEQUALITY_CONVERGENCE = 0.005f;
        rdelta.inequality_delta = MONTHLY_INEQUALITY_CONVERGENCE *
                                  (target_inequality - province.conditions.inequality_index);
    }

    // --- Grievance ---
    // Owned by community_response. This module previously injected
    // `+0.003 × income_low_fraction` EVERY tick — a constant, never-decaying pump
    // off a static demographic, disconnected from current conditions, which (with
    // regional_conditions' writer) pinned grievance at the ceiling regardless of
    // economic reality. Economic deprivation is now grounded in
    // community_response's material grievance term (unemployment + inequality).
    // Removed; grievance has a single owner.

    // Only push the delta if at least one field was set.
    province_delta.region_deltas.push_back(rdelta);
}

void PopulationAgingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
