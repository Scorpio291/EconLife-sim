#include "modules/subsistence/subsistence_module.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/era_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/geography.h"
#include "core/world_state/world_state.h"

namespace econlife {

float SubsistenceModule::surplus_ratio(float output, uint32_t population,
                                       const SubsistenceConfig& cfg) {
    const float need = static_cast<float>(population) * cfg.per_capita_food_per_tick;
    if (need <= 0.0f)
        return 1.0f;  // no mouths to feed -> trivially "fed"
    return output / need;
}


bool SubsistenceModule::regime_active(std::string_view regime) const {
    return regime_in(cfg_.active_regimes, regime);
}

float SubsistenceModule::harvest_failure_factor(float seasonality_dial, DeterministicRNG& rng,
                                                const SubsistenceConfig& cfg) {
    const float s = std::clamp(seasonality_dial, 0.0f, 1.0f);
    if (s <= 0.0f)
        return 1.0f;  // a world with no seasonal swing never has a failed harvest
    const float p = std::clamp(cfg.seasonality_failure_base_rate * s, 0.0f, 1.0f);
    if (rng.next_float() >= p)
        return 1.0f;  // a normal harvest year
    return std::max(0.0f, 1.0f - cfg.seasonality_failure_severity * s);  // a bad harvest year
}

// predator_food_factor and atmosphere_ceiling_factor live inline in the header:
// chronic_ceiling_factors calls both and world-gen calls it, so core must be able
// to link without module object code.

bool SubsistenceModule::regime_manorial(std::string_view regime) const {
    return regime_in(cfg_.manorial_regimes, regime);
}

float SubsistenceModule::proto_share_for(bool is_lord, uint32_t lords_count,
                                         uint32_t residents_count, float total_proto,
                                         bool manorial, const SubsistenceConfig& cfg) {
    if (residents_count == 0 || total_proto <= 0.0f)
        return 0.0f;
    const float n = static_cast<float>(residents_count);
    const float even = total_proto / n;
    if (!manorial || lords_count == 0)
        return even;
    const float tithe = std::clamp(cfg.manorial_tithe_rate, 0.0f, 1.0f);
    const float peasant_base = total_proto * (1.0f - tithe) / n;
    const float lord_bonus = (total_proto * tithe) / static_cast<float>(lords_count);
    return is_lord ? peasant_base + lord_bonus : peasant_base;
}


void SubsistenceModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    // Regime gate: only the pre-market eras run the commons food path. In market
    // eras this module is inert and the field keeps its default ("fed").
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime))
        return;

    const Province& prov = state.provinces[province_idx];
    if (!prov.cohort_stats)
        return;
    const RegionCohortStats& cs = *prov.cohort_stats;
    const uint32_t population = cs.total_population;
    if (population == 0)
        return;

    // Working-age fraction of the population is available to labour on the land.
    const float working_age = cs.working_age_fraction > 0.0f ? cs.working_age_fraction : 0.6f;

    // Natural capital the population can draw food from this tick, INCLUDING the ghost
    // acres coal is standing in for (energy_base). An organic economy is bounded by
    // photosynthesis on finite acres; coal substitutes a stock for that flow, and that
    // is the only term here that can keep rising — hence the only escape from a
    // carrying ceiling that otherwise saturates and fixes the height of every peak.
    const float natural_capital = natural_capital_of(prov, cfg_, cs.soil_health,
                                                     cs.ghost_land_fraction, cs.forest_health);
    // How much ground there is to cover, as opposed to how much it yields. Sets the
    // labour saturation below; fertility and wear belong to the ceiling, not here.
    const float workable_extent = workable_extent_of(prov, cfg_, cs.ghost_land_fraction);

    // Knowledge raises the land's carrying capacity (better technique) — the escape
    // from the Malthusian trap. knowledge_level is accumulated by the knowledge module.
    // Saturating (diminishing returns) so the ceiling tends toward a realistic limit
    // rather than exploding at high knowledge (which would crash the surplus).
    // WHAT THIS PLACE KNOWS (R6), not the world's frontier. A province whose scholars
    // have scattered farms like a province whose scholars have scattered, however much
    // the civilisation next door still knows — which is what makes a regional collapse
    // regional. It relearns by diffusion, not by decree.
    const float K = cs.knowledge_level;
    // KNOWING IS NOT HAVING (R9). Knowledge only raises the harvest as far as the province
    // has built the means to apply it. In 1800 Qing China and Britain did not differ much
    // in what they knew; what differed was the pits, the furnaces, the rails and the
    // drained fields. A place that knows everything and has built nothing farms like the
    // dawn — which is why peoples in the same period advance at such different speeds.
    //
    // This is also the only way the ceiling can fall WITHOUT anybody forgetting: capital
    // wears out and is rebuilt only out of a real surplus, so a population that outruns
    // its capital loses the use of what it still knows.
    // MEASURED AGAINST THE LAND, NOT THE HEADCOUNT. A mill serves a valley; drains, walls,
    // terraces and cleared fields are fixed to the ground and do not become more useful
    // because fewer people are left to use them.
    //
    // Capital per HEAD is a ratio whose denominator can collapse, and it did: a province
    // that lost nine tenths of its people read as ten times better equipped, so the
    // knowing-and-having gate flew open, the carrying ceiling jumped eightfold, the food
    // signal read 8.5, fertility pinned at the biological cap, and the population
    // overshot and crashed again. Measured, that ran a 90% boom-and-bust every seven
    // hundred years and the climb never left the Iron Age. Per unit of workable ground
    // the same die-off leaves the province exactly as equipped as it was, which is what a
    // silent mill actually is.
    const float capital_per_land =
        workable_extent > 0.0f ? std::max(0.0f, cs.productive_capital) / workable_extent : 0.0f;
    const float applied = capital_per_land /
                          (capital_per_land + std::max(1.0f, cfg_.capital_utilisation_halfsat));

    // MACHINES REPLACE HANDS (R11). Capital gated how much of its knowledge a place could
    // use; it did not let capital do the other and more famous thing. An American farmer
    // fed about 3 people in 1800 and 150 by 2000, almost none of it from working harder,
    // and every hand that freed went into a town, a factory, a school or an office.
    //
    // Leverage multiplies the labour one farmer supplies, so the same harvest needs fewer
    // of them. Output is still bounded by the land's ceiling, so this FREES HANDS rather
    // than conjuring food — which is what labour-saving means.
    //
    // Keyed to capital per HEAD rather than per farmer on purpose: per farmer would feed
    // back on itself (fewer farmers -> more capital each -> fewer farmers still), and the
    // stock is a property of the province, not of who happens to be working it.
    // A machine is knowledge AND capital, and it needs BOTH. Gated on the same
    // knowledge saturation the ceiling uses, so a Bronze Age province with granaries,
    // cleared land and good tools gets a strong back and not a tractor.
    //
    // (Measured without the knowledge gate: era 2 — the Bronze Age, year 287 — came out
    // with 75% of its people off the land and 58% of them in towns, because generic
    // capital alone was conferring ninefold labour leverage.)
    const float knows_machines =
        K / (K + std::max(1.0f, cfg_.machine_leverage_knowledge_halfsat));
    const float has_machines =
        capital_per_land / (capital_per_land + std::max(1.0f, cfg_.machine_leverage_halfsat));
    const float machine_leverage =
        1.0f + cfg_.machine_leverage_max * knows_machines * has_machines;
    // What one person's labour is worth on the land, machines included.
    const float working_fraction = working_age * machine_leverage;
    const float knowledge_factor =
        1.0f + cfg_.knowledge_productivity_max * applied * K /
                   (K + std::max(1.0f, cfg_.knowledge_productivity_halfsat));
    // Food techs (plough/irrigation/heavy-plough/watermill) raise the carrying ceiling.
    // Ploughs, irrigation, heavy ploughs, watermills — what THIS province has, resolved
    // by the technology module from what it knows and what it has built. It used to be
    // the era's whole tree switched on by an integer, which is why the era 1 -> 2
    // boundary raised the food surplus from 1.58 to 3.95 in one step.
    const float tech_food_factor = cs.tech_food_mult;
    // Seasonality (relative to Earth) cuts food reliability via lean seasons; gravity
    // does NOT affect the harvest. Earthlike seasonality is neutral.
    const float seasonality_factor = std::clamp(
        1.0f - cfg_.seasonality_food_penalty *
                   (state.hazard_settings.seasonality - earth_hazard().seasonality),
        0.3f, 1.3f);
    // Episodic harvest failure (M6a): on top of the chronic seasonality penalty, a bad
    // harvest year — scaled by the seasonality dial — cuts this year's output. Seeded
    // by YEAR (not tick) so the failure is consistent across a year at any tick
    // resolution, and varies year to year. Deterministic.
    const uint32_t tpy = cfg_.ticks_per_year > 0 ? cfg_.ticks_per_year : 365;
    const uint32_t harvest_year = state.current_tick / tpy;
    DeterministicRNG harvest_rng(state.world_seed ^
                                 (static_cast<uint64_t>(harvest_year) * 0x9E3779B97F4A7C15ull) ^
                                 (static_cast<uint64_t>(prov.id) << 29) ^ 0x4A12E57ull);
    const float harvest_factor =
        harvest_failure_factor(state.hazard_settings.seasonality, harvest_rng, cfg_);

    // Chronic world-hazard food channels (M6a): predators prey on herds (waning as
    // knowledge clears them); a hostile atmosphere caps the ceiling (planetary).
    const float predator_factor =
        predator_food_factor(state.hazard_settings.predators, K, cfg_);
    const float atmosphere_factor =
        atmosphere_ceiling_factor(state.hazard_settings.atmosphere, cfg_);

    // Carrying ceiling: the maximum food this land can yield given technique. Output
    // saturates toward it with labour (diminishing returns). Specialists do not farm,
    // so only the food-producers' labour counts toward output.
    const float base_ceiling = cfg_.ceiling_per_capital_unit * natural_capital * knowledge_factor *
                               seasonality_factor * tech_food_factor * harvest_factor *
                               predator_factor * atmosphere_factor;
    // Half-saturation scales with the EXTENT: enough hands to work all the ground. A
    // flat constant put every province deep in the saturated region where marginal
    // labour is worth nothing, and the food balance then read almost the whole
    // workforce as spare. Scaling it by the YIELD instead cancelled land quality out of
    // per-worker output entirely (see subsistence_output).
    const float half = std::max(1.0f, cfg_.labor_half_saturation_per_extent * workable_extent);
    const float need = static_cast<float>(population) * cfg_.per_capita_food_per_tick;  // per tick
    const float ticks_per_year =
        cfg_.ticks_per_year > 0 ? static_cast<float>(cfg_.ticks_per_year) : 365.0f;

    // The granary demands a permanent production surplus: stored grain spoils, so a
    // society must keep producing extra just to hold its reserves, and more still while
    // building them up. That standing demand (NOT a margin) is what frees a standing
    // specialist class. Work per tick.
    const float target_store = cfg_.granary_reserve_years * need * ticks_per_year;
    const float spoilage = cfg_.granary_spoilage_rate * cs.food_store / ticks_per_year;  // /tick
    const float build = cs.food_store < target_store
                            ? cfg_.granary_build_rate * (target_store - cs.food_store) / ticks_per_year
                            : 0.0f;
    const float granary_demand = spoilage + build;  // extra food/tick the reserves require

    // GROUNDED specialization: how many people must farm to feed everyone AND keep the
    // granary whole. Whoever is left is free to specialize. As knowledge raises the
    // ceiling, fewer farmers are needed, so more are freed — specialization rises with
    // technique, with no heuristic cap doing the work.
    const float desired_output = need + granary_demand;
    const float total_labor = static_cast<float>(population) * working_fraction;
    float labor_needed;
    if (base_ceiling <= 0.0f || desired_output >= base_ceiling) {
        labor_needed = total_labor;  // can't reach the target even with everyone farming
    } else {
        labor_needed = -half * std::log(1.0f - desired_output / base_ceiling);
    }
    const float farmers_needed = labor_needed / std::max(working_fraction, 0.01f);
    float specialists_people = std::max(0.0f, static_cast<float>(population) - farmers_needed);

    // WHAT CAN BE SPARED IS NOT WHAT CAN BE PROVISIONED (R12). Two independent physical
    // limits, and the smaller binds. The harvest says how many hands can leave the land;
    // haulage says how many mouths the surplus can actually REACH once they are off it.
    // A non-farmer has to be fed by somebody else's field, and the grain has to get to
    // him — which is the ox law grain_logistics already computes as `urban_capacity`
    // (net feedable surplus / per-capita food, after the draft teams have eaten).
    //
    // THIS REPLACES A RAIL. It was `clamp(specialists, 0, population * ceiling(regime))`
    // — a per-era constant (0.15 subsistence, 0.18 coinage, 0.22 money, 0.45 industrial)
    // deciding the shape of the economy by fiat. Measured, it BOUND AT EVERY ERA: the
    // supported share sat exactly on 15.0%, 18.0%, 22.0% while the food balance would
    // have freed far more, and the model reached "era 8" with 92% of its people still
    // farming. That is a behaviour-shaping cap on finite values, which the grounding
    // doctrine forbids outright.
    //
    // What the constant stood in for was real — you cannot coordinate a large non-farming
    // population without a way to move food to it — but the real version is a COST that
    // rises with distance and falls with roads and rivers, not a wall that moves when an
    // era ticks over. It is one tick stale (grain_logistics runs after this module), which
    // is nothing on a stock that moves over decades, and it degrades correctly: a province
    // with no haulage and no neighbours is limited to what it can carry itself.
    const float provisionable = std::max(0.0f, cs.urban_capacity);
    if (cs.urban_capacity > 0.0f || cs.net_feedable_surplus > 0.0f)
        specialists_people = std::min(specialists_people, provisionable);

    // AND WHAT CAN BE SPARED IS NOT WHAT CAN BE TAKEN. A non-farmer eats grain somebody
    // else grew and gave up, so somebody had to be able to CLAIM it — to know the crop
    // existed, find it, measure it and enforce a share. That is a third physical limit,
    // and unlike the first two it is not a ceiling on the pool but a share OF it: the
    // stratum is fed out of the part of the surplus that can actually be got at, and the
    // rest simply stays in the households that grew it, eaten by the people who grew it.
    //
    // The model asserted the opposite — that anyone the fields did not need became a
    // specialist — which is the one thing a subsistence economy never did. A Neolithic
    // village that could feed itself with two thirds of its people did not have a third
    // of them making pots; it had underemployed farmers, which is Lewis's unlimited
    // supply of labour and what every development economist since has found in a peasant
    // economy. Measured under that assertion, a dawn world put 45-60% of its people off
    // the land, and it was the deepest reason the model could not produce a Neolithic.
    //
    // This is also the mechanism behind the fact that the surplus theory of state
    // formation gets backwards: societies did not build states because they had a
    // surplus, they could keep a surplus because they built the apparatus to claim one.
    const float reach = claim_reach(population, cs.codified_knowledge, cfg_);
    specialists_people *= reach;
    const float supported_fraction =
        population > 0 ? specialists_people / static_cast<float>(population) : 0.0f;

    // THE STRATUM HAS INERTIA. What the harvest can support is a TARGET, not this
    // year's reality. Scholars, priests, smiths and townsmen persist through lean
    // decades on stores, patronage and tribute, and a scribe does not return to the
    // plough in a season. The stratum therefore moves toward the supported level on a
    // generational timescale, shedding faster than it grows — institutions take longer
    // to build than to lose.
    //
    // This is also what lets a society OVERSHOOT: the superstructure stays on while the
    // land degrades under it, deepening the crisis instead of damping it. Recomputing
    // the stratum from each year's food made collapse instantaneous and total (measured:
    // 17% -> 0% in a single tick) and made elite overproduction impossible to express.
    // The stratum is fed from STORES through bad years, not dismissed on one failed
    // harvest — that is what a granary is for. So the level the society will actually
    // defend is the better of what this year's harvest supports and what its reserves
    // can carry it through, scaled by how full the granary is.
    //
    // Without this the asymmetric rates below (shedding faster than building, which is
    // real) act on a target that fluctuates with every harvest, and the noise ratchets
    // the stratum steadily downward: measured, it fell to 2% where the food balance
    // supported 14-17%, and the knowledge engine collapsed with it.
    // Annual cadence for every STOCK this module evolves: the granary, the soil, the
    // capital and the stratum. A per-tick rate is only correct if every tick runs.
    const bool annual = state.current_tick > 0 && state.current_tick % tpy == 0;

    // THE GRANARY FEEDS THE STRATUM; IT DOES NOT SCALE IT. What stored food buys is the
    // ability to carry people the current harvest cannot: a granary holding N
    // person-years can keep an extra share of the population off the land for a year.
    // So the level a society will defend is what THIS harvest supports PLUS what the
    // stores can carry on top of it.
    //
    // This was `max(supported, held * granary_cover)`, and that multiplication was a
    // slow ratchet nobody could see. `target_store` scales with population, so ANY
    // population growth dilutes the granary and holds cover permanently below 1 — at
    // 0.5%/yr growth, cover settles near 0.98. That made `defended` a couple of percent
    // BELOW `held` every single tick, so the faster shed rate applied forever and the
    // stratum equilibrated far under what the land could support. Measured: 7.9% held
    // against 23.3% supported, after millennia to converge.
    //
    // A GRANARY CARRIES THE STRATUM IT HAS; IT DOES NOT FUND A BIGGER ONE. What the store
    // buys is that one failed harvest does not send the scribes back to the plough — it
    // covers the GAP between what the society already keeps and what this year's fields
    // support, for as long as there is grain to cover it with. It never lifts the level
    // above what is already held, because a reserve is famine insurance and famine is
    // what it is for: drawn down in bad years, refilled in good ones, contributing
    // nothing on average. The society is already charged for keeping it — that is
    // `granary_demand` above, and that standing production cost is the permanent wedge
    // which frees a stratum in the first place.
    //
    // It was `supported + food_store / annual_need`, adding PERSON-YEARS OF STORE to a
    // FRACTION OF PEOPLE. A full granary holds `granary_reserve_years` of food for
    // everybody, so that expression read 2.0 and the target pinned at 1.0 — every
    // society in the model was being told to put its entire population off the land, and
    // the stratum crept upward year after year toward it no matter what else was true.
    // A quantity sitting exactly on its bound, again, and units are how it was found.
    const float store_person_years =
        static_cast<float>(population) * cfg_.per_capita_food_per_tick * ticks_per_year;
    const float store_cover =
        store_person_years > 0.0f ? std::max(0.0f, cs.food_store) / store_person_years : 0.0f;
    const float held = cs.specialist_fraction;
    const float shortfall = std::max(0.0f, held - supported_fraction);
    const float defended_fraction = supported_fraction + std::min(shortfall, store_cover);
    const float rate = defended_fraction < held ? cfg_.specialist_shed_per_year
                                                : cfg_.specialist_growth_per_year;
    // ANNUAL, like the granary, the soil and the capital stock — not a per-tick nibble.
    //
    // It was `rate / ticks_per_year` applied every tick, which is correct only if every
    // tick actually runs. It silently was not: the history harness fast-forwards at one
    // orchestrator step per year, so a per-tick rate advanced 365 times more slowly there
    // than in a real game, and the stratum crawled. Every measurement of it this session
    // was taken under that regime. A per-tick rate under a per-year stride is a silent
    // 365x error and it looks exactly like a slow mechanism.
    const float specialist_fraction =
        annual ? held + (defended_fraction - held) * rate : held;
    specialists_people = static_cast<float>(population) * specialist_fraction;

    // Actual harvest from the farmers who remain on the land. TOWNSFOLK DO NOT FARM:
    // the people who are not in the fields are the union of the institutional stratum
    // (scholars, priests, smiths, lords) and whoever actually lives in the town, and
    // the town is a subset of that stratum whenever the stratum is the larger of the
    // two — hence the max, which is a set union and not a bound on anything.
    //
    // Without this a town was free: migration could pour the whole population into the
    // towns and the harvest would not notice, so urbanisation ran away to 95%+ on a
    // world where nobody was left on the land. Now moving people to town costs exactly
    // what it costs in reality — the hands that would have been reaping.
    const float non_farmers = std::max(specialists_people, cs.urban_population);
    const float farm_labor = (static_cast<float>(population) - non_farmers) * working_fraction;
    const float output = base_ceiling * (1.0f - std::exp(-std::max(0.0f, farm_labor) / half));

    // WHAT THE PROVINCE ACTUALLY EATS (R3D). A province is not limited to its own
    // harvest. Sea and river transport decoupled cities from their hinterland — Egypt
    // shipped on the order of 130,000 tonnes of grain a year to Rome, and moving grain 70
    // miles by road cost more than sailing it 1,400 — so what a place can feed is its own
    // land PLUS what the catchment delivers.
    //
    // grain_logistics allocates every province's exportable surplus across {itself + its
    // reachable neighbours}, conserved, with the draft teams eating the difference; the
    // result is `net_feedable_surplus`. So a province eats what it never put in the
    // haulage pool (min(output, need)) plus whatever the pool sent it. For an isolated
    // province the two are identical and nothing changes.
    //
    // This is the bargain with a bill attached. A province fed from elsewhere prospers
    // beyond its own land right up until the route fails, and then starves in proportion
    // to how far beyond it had grown. The Late Bronze Age collapse is the case: severing
    // Ugarit cut Cyprus off from tin and copper and the whole eastern Mediterranean system
    // came down within a generation. Nothing models the cascade directly — it is what
    // happens when a province whose neighbours fed it loses the neighbours.
    //
    // The flow used here is the REAL conserved one — stored grain diffusing down the
    // scarcity gradient, with the draft teams eating the difference — not the catchment
    // capacity signal, which is a view of the same surplus and would count it twice. It
    // is signed: an exporter loses exactly what it sent, an importer gains exactly what
    // arrived, and a province with no neighbours or no haulage sees zero and is unchanged.
    //
    // One tick behind, because grain_logistics runs after this module and consumes the
    // surplus published here. On a fresh world it is zero, which is the correct reading.
    const float effective_output = std::max(0.0f, output + cs.grain_import_rate);
    const float imported = std::max(0.0f, cs.grain_import_rate);
    const float import_dependence =
        effective_output > 0.0f ? imported / effective_output : 0.0f;

    // Granary: bank the year's net food (after feeding everyone and losing spoilage),
    // or draw it down, once per year. A conserved, capped per-province stock.
    const float net_per_tick = effective_output - need - spoilage;
    float new_store = cs.food_store;
    // Use the sanitized tpy from above: cfg_.ticks_per_year is guarded against 0
    // twice earlier in this function, and dividing by the raw field here was
    // modulo-by-zero UB for exactly the input those guards anticipate.
    // `annual` is hoisted above the stratum inertia, which needs it.
    if (annual)
        new_store = std::clamp(cs.food_store + net_per_tick * ticks_per_year, 0.0f, target_store);

    // The long-run food signal that paces population growth: what the land can yield
    // relative to what a sustainable society must produce — feed everyone AND replace
    // the grain that spoils out of a full reserve. At full upkeep this reads 1.0, so the
    // population settles at its sustainable ceiling. Above it the population can grow;
    // below, it eases off. (Starvation is handled separately, gated on the granary
    // running dry, and that uses the food actually harvested.)
    //
    // MEASURED AT THE LABOUR THE SOCIETY COULD FIELD, NOT THE LABOUR LEFT OVER AFTER THE
    // STRATUM WAS SUBTRACTED. This is the one thing in this function that must not be
    // read off `output`, and the reason is that `labor_needed` above is SOLVED to make
    // `output` equal `need + granary_demand` — which at a full store is `need +
    // full_upkeep`, the denominator here. Dividing one by the other returned 1.0 by
    // construction, at every level of abundance, forever: the demography could never see
    // a rich world, so the population never grew into its land, so labour stayed spare,
    // so the assignment freed still more specialists. Measured under that ring, a dawn
    // world sat at 7,000-13,000 people for six and a half millennia with 47-64% of them
    // off the land — a Neolithic society of scholars — and by the industrial era 97% of
    // people were not farming because nobody had ever needed to.
    //
    // The question fertility answers is whether the land can feed another mouth, not
    // whether this year's labour allocation does. A society that goes hungry puts its
    // scribes back in the fields; the stratum is what it can afford to keep OUT of them,
    // and it is therefore downstream of the food position, not the definition of it. So
    // the signal is the harvest the whole workforce would bring in, plus what the
    // catchment delivers, against sustainable need — and the two claimants on a surplus,
    // mouths and hands, then race for it on their own timescales. Mouths win at the dawn,
    // which is why nothing happens for ten thousand years.
    const float full_upkeep =
        cfg_.granary_spoilage_rate * cfg_.granary_reserve_years * need;  // per tick, at full store
    // Signed, exactly as `effective_output` is: grain that leaves is grain this province
    // cannot feed anyone with, and a place living beyond its own land must read worse
    // when the route reverses. Floored at zero as a quantity of food, not as a rule.
    const float potential_output = std::max(
        0.0f,
        base_ceiling * (1.0f - std::exp(-std::max(0.0f, total_labor) / half)) +
            cs.grain_import_rate);
    const float growth_surplus =
        need > 0.0f ? potential_output / (need + full_upkeep) : 1.0f;

    RegionDelta rd{};
    rd.region_id = prov.region_id;
    rd.subsistence_surplus_replacement = growth_surplus;
    rd.import_dependence_replacement = import_dependence;
    // What the granary can cover, in years of everybody's food. Computed above for the
    // stratum it carries through lean years; published because hunger is what the store
    // CANNOT cover, and the demography needs that rather than a full-or-empty flag.
    rd.granary_cover_years_replacement = store_cover;
    // WHO WORKS THE WATER. The fishery's share of the food base, applied to the hands
    // actually producing food — the people whose living is the fish. seasonal_agriculture
    // turns this into the effort behind the Schaefer harvest, so a fishery is pressed by
    // the population living off it rather than by a constant: empty the province and the
    // stock recovers, crowd it and the stock goes. Fish are a population too.
    const float fishery_share =
        natural_capital > 0.0f
            ? cfg_.weight_fisheries * prov.fisheries.current_stock / natural_capital
            : 0.0f;
    rd.commons_fishers_replacement =
        std::max(0.0f, static_cast<float>(population) - non_farmers) * working_age * fishery_share;
    // Publish the freed stratum itself, not just the food ratio. This is the share of
    // real people the harvest does not need on the land — the pool that scholarship,
    // crafts and trade are drawn from. Consumers (knowledge) must scale with the
    // POPULATION that can be spared, not with a fixed sample of tracked individuals.
    rd.specialist_fraction_replacement = specialist_fraction;
    // Publish what THIS harvest supports as well as what the society is actually
    // holding. The gap between them is elite overproduction (R2D): people raised to
    // expect a place above the plough that the land no longer provides.
    rd.supported_specialist_fraction_replacement = supported_fraction;
    rd.food_store_replacement = new_store;
    // Absolute haulable grain surplus (output beyond bare need) — the grain available
    // to move/feed non-farmers, consumed by grain_logistics (the ox-cart, §3.5).
    rd.grain_surplus_replacement = std::max(0.0f, output - need);

    // PRODUCTIVE CAPITAL — the capacity to use what the society knows. Part of the
    // real food surplus is spent building rather than eating: tools, ploughs,
    // granaries, kilns, cleared and drained land, workshops. It wears out every year,
    // so the stock only grows while the surplus keeps paying for it.
    //
    // This is the natural limiter on advancement. Knowledge is information and can
    // spike; capital is matter and labour, accumulates only as fast as a real surplus
    // allows, and decays. A society can know far more than it can build — which is why
    // era advancement gates on BOTH (see knowledge_module).
    //
    // Annual cadence, same gate as the granary banking above.
    if (annual) {
        const float surplus_food = std::max(0.0f, output - need) * ticks_per_year;
        // WHY ANYONE BOTHERS BUILDING (R4B). A society commits its surplus to building
        // only in proportion to what it expects to still own when the building pays back
        // — thirty-odd years out, for a mill or a cleared field. A province where nobody
        // trusts anybody, or where the polity is coming apart, or where armies are
        // taking things, does not accumulate however large its harvest is. That is the
        // difference between a civilisation that builds and one that merely survives.
        const float hazard =
            expropriation_hazard(prov.community.institutional_trust, cs.political_stress,
                                 cs.war_death_fraction, cfg_);
        const float investment = surplus_food * effective_investment_share(hazard, cfg_);
        const float wear = cs.productive_capital * cfg_.capital_depreciation_per_year;
        rd.productive_capital_delta = investment - wear;

        // THE LAND WEARS OUT. What the land renews indefinitely rises with technique,
        // but only as its SQUARE ROOT: better ploughs and irrigation raise the harvest
        // far faster than they replace nutrients, and that gap is soil mining. A
        // society that grows into its land and keeps pressing strips fertility; one
        // working at or under what the land renews lets it rebuild, slowly.
        //
        // This is the only channel that can lower the carrying ceiling, and so the only
        // way a society can overshoot and fall rather than climb forever.
        const float chronic = chronic_ceiling_factors(K, tech_food_factor, state.hazard_settings,
                                                      cfg_) *
                              harvest_factor;
        // THE BOSERUP ESCAPE. What the land bears indefinitely is a SHARE of what it can
        // yield, and that share RISES WITH TECHNIQUE: crop rotation, fallowing, manuring,
        // legumes and terracing are exactly the knowledge that keeps fields alive. A
        // society with no technique mines its land at ~2.9x renewal working flat out; one
        // that has learned to farm sustainably approaches balance.
        //
        // Without this the trap is inescapable and the measurement showed it: with a
        // FIXED sustainable share, any population large enough to work the land fully
        // mines it forever regardless of how advanced it becomes, so earthlike soil
        // collapsed 1.0 -> 0.17 within 500 years and the society oscillated at bare
        // subsistence for 14,000 years, reaching 396 of the 3,830 knowledge it needed.
        // Pressure on the land is what drives the intensification that relieves it —
        // and the knowledge engine already turns scarcity into innovation.
        const float technique_share =
            cfg_.sustainable_yield_per_capital +
            (cfg_.sustainable_yield_technique_max - cfg_.sustainable_yield_per_capital) * K /
                (K + std::max(1.0f, cfg_.sustainable_yield_technique_halfsat));
        // Expressed against the land's maximum yield (ceiling_per_capital_unit x natural
        // capital x technique), not raw natural capital — the two differ by three orders
        // of magnitude, and comparing the harvest to the latter made even a handful of
        // people read as mining the land.
        //
        // RENEWAL IS ABSOLUTE, NOT PROPORTIONAL. What returns fertility to a field is
        // weathering of the parent rock, rainfall, and biological nitrogen fixation —
        // all properties of the PLACE, not of how depleted the topsoil currently is. So
        // the sustainable harvest is measured against the land's PRISTINE capacity
        // (soil_health = 1), not its worn-out present state.
        //
        // Measured, this is the difference between a world and a wasteland. Scaling the
        // sustainable yield by current soil health made the pressure ratio
        // scale-invariant in soil — it cancelled out of both sides — so the land had no
        // restoring force at all: earthlike settled at 10-20% of pristine fertility and
        // stayed there for 14,000 years, pinning the population near 10,000 and the
        // knowledge rate near 0.05/yr. With renewal absolute, worn land out-renews what
        // a shrunken population can take from it, and the soil climbs back.
        // The ghost acres belong in this base too. Work done by coal is work the soil
        // did not have to do, so a society running its economy on a burning stock is
        // pressing proportionally less on its fields — which is exactly why industrial
        // agriculture stopped being limited by soil exhaustion. Both sides of the ratio
        // then carry the same ghost term, so as coal comes to dominate the pressure on
        // the land tends to balance rather than to ruin.
        const float pristine_capital = natural_capital_of(
            prov, cfg_, /*soil_health=*/1.0f, cs.ghost_land_fraction, /*forest_health=*/1.0f);
        const float sustainable_output =
            technique_share * cfg_.ceiling_per_capital_unit * pristine_capital * chronic;
        if (sustainable_output > 0.0f) {
            const float pressure_ratio = output / sustainable_output;
            if (pressure_ratio > 1.0f) {
                // Mining the land: lose a share of what fertility remains.
                rd.soil_health_delta =
                    -cfg_.soil_degradation_per_year * (pressure_ratio - 1.0f) * cs.soil_health;
            } else {
                // Working within its renewal: nutrients return — but only up to the
                // ground that is still there. Fertility is held IN the soil, so a
                // province that has lost its profile recovers to what it has left, not to
                // what it once had. Without this ceiling, erosion below would be undone
                // in a generation by a nutrient cycle, and desertification would be a dip
                // rather than the thing that outlives the civilisation that caused it.
                rd.soil_health_delta =
                    cfg_.soil_recovery_per_year * std::max(0.0f, cs.topsoil - cs.soil_health);
            }

            // DESERTIFICATION. The ground itself leaves when it is worked past what it
            // renews AND the wild cover that would have held it is gone. Both drivers,
            // multiplied, because that is what the record shows: ploughing within the
            // land's renewal keeps a crop on it, and intact woods and grass hold soil
            // through a bad year even where farming is hard. It took the sod-busting AND
            // the drought to make a Dust Bowl; the Mediterranean hills needed the
            // deforestation AND the goats.
            //
            // Unlike the nutrient cycle above this is a loss of the profile, so it is
            // measured against a formation rate that is geological — a centimetre every
            // few centuries — and it is therefore, on any human timescale, permanent.
            const float over_pressure = std::max(0.0f, pressure_ratio - 1.0f);
            const float cover_lost = 1.0f - std::clamp(cs.forest_health, 0.0f, 1.0f);
            const float erosion =
                cfg_.soil_erosion_per_year * over_pressure * (1.0f + cover_lost) * cs.topsoil;
            const float formation = cfg_.topsoil_formation_per_year * (1.0f - cs.topsoil);
            const float net = formation - erosion;
            if (net != 0.0f)
                rd.topsoil_delta = net;
        }

        // AND THE WILD STOCK ANSWERS BACK. The same law, applied to the food nobody
        // planted. A share of every harvest is TAKEN rather than grown — the forage and
        // the game the woods carry — and the woods renew only a fraction of their
        // standing biomass each year. Take more than that and there is less next year:
        // fewer deer, fewer nut trees, thinner cover.
        //
        // This is the second thing that can lower a carrying ceiling, and unlike soil it
        // is what limits a society BEFORE it farms — which is why hunter-gatherers ran at
        // a hundredth of agricultural densities and why the first thing a growing
        // population does is stop foraging and start planting. The forest was a constant
        // that fed people for free forever: measured, coverage sat at exactly 0.2822 for
        // four thousand years while the population living off it doubled.
        //
        // The renewal is ABSOLUTE, measured against the PRISTINE woods, for the same
        // reason the soil's is: what regrows a forest is sunlight and rain on a given
        // area, not how much of it is left. Scaling renewal by the present stock would
        // cancel it out of both sides and leave the woods with no restoring force at all.
        const float wild_take = output * wild_share_of(prov, cfg_, cs.forest_health,
                                                       natural_capital) * ticks_per_year;
        const float pristine_wild =
            cfg_.weight_forest_forage * prov.geography.forest_coverage +
            cfg_.weight_fisheries * prov.fisheries.current_stock;
        const float wild_renewal = cfg_.forage_sustainable_share * cfg_.ceiling_per_capital_unit *
                                   pristine_wild * chronic * ticks_per_year;
        if (wild_renewal > 0.0f) {
            const float forage_pressure = wild_take / wild_renewal;
            if (forage_pressure > 1.0f) {
                // Eating the herd: the standing stock falls by a share of what is left.
                rd.forest_health_delta =
                    -cfg_.forage_depletion_per_year * (forage_pressure - 1.0f) * cs.forest_health;
            } else {
                // Taking less than the woods make: they close back over, slowly.
                rd.forest_health_delta =
                    cfg_.forage_recovery_per_year * (1.0f - cs.forest_health);
            }
        }
    }
    province_delta.region_deltas.push_back(rd);

    if (province_idx >= state.npc_indices_by_home_province.size())
        return;
    const auto& residents = state.npc_indices_by_home_province[province_idx];
    if (residents.empty())
        return;

    // Livelihoods: assign each resident an occupation. Everyone is a food producer
    // (Layer 1) by default; the food-balance share above is freed into Layer-2
    // specialists (knowledge-keepers first). Self-employed livelihoods, NOT firms.
    const auto layer1 = state.occupation_catalog.in_layer(1);
    // Layer-2 specialists available at this era: knowledge-keepers unlock over time
    // (elder at the dawn -> scribe once writing exists -> scholar with formal
    // scholarship), so the knowledge trickle starts tiny and accelerates.
    const auto layer2 = state.occupation_catalog.in_layer_for_era(2, state.technology.current_era);
    const uint32_t specialists = std::min(
        static_cast<uint32_t>(static_cast<float>(residents.size()) * specialist_fraction),
        static_cast<uint32_t>(residents.size()));

    // Proto-capital: food beyond need is stored (grain/herds/tools), controlled by
    // the resident heads/founders — the origin of capital. In the egalitarian commons
    // it splits evenly; under MANORIALISM (feudal+ regimes) a tithe concentrates it
    // toward a lord stratum (the lord/peasant divide). It is the wealth that later
    // funds the first firms (genesis is founder-capital-gated).
    const float surplus_food = output - need;
    const float total_proto = (cfg_.proto_capital_rate > 0.0f && surplus_food > 0.0f)
                                  ? cfg_.proto_capital_rate * surplus_food
                                  : 0.0f;
    const bool manorial = regime_manorial(era->economic_regime);

    // The lord stratum is EMERGENT: the wealthiest resident heads (capital rank,
    // ties broken by id for determinism). Wealth collects the tithe; the tithe
    // compounds the wealth — an aristocracy that entrenches, and can be displaced.
    const uint32_t lords = manorial ? lord_count(static_cast<uint32_t>(residents.size()), cfg_)
                                    : 0u;
    std::vector<uint32_t> lord_indices;  // resident-vector positions of the lords
    if (lords > 0) {
        std::vector<uint32_t> order(residents.size());
        for (uint32_t i = 0; i < residents.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
            const uint32_t ix = residents[x], iy = residents[y];
            const float cx = ix < state.significant_npcs.size() ? state.significant_npcs[ix].capital
                                                                : -1.0f;
            const float cy = iy < state.significant_npcs.size() ? state.significant_npcs[iy].capital
                                                                : -1.0f;
            if (cx != cy)
                return cx > cy;  // richest first
            const uint32_t idx_x = ix < state.significant_npcs.size() ? state.significant_npcs[ix].id
                                                                      : ix;
            const uint32_t idx_y = iy < state.significant_npcs.size() ? state.significant_npcs[iy].id
                                                                      : iy;
            return idx_x < idx_y;  // deterministic tie-break
        });
        lord_indices.assign(order.begin(), order.begin() + lords);
        std::sort(lord_indices.begin(), lord_indices.end());
    }
    auto is_lord_at = [&](uint32_t pos) {
        return std::binary_search(lord_indices.begin(), lord_indices.end(), pos);
    };

    for (uint32_t i = 0; i < residents.size(); ++i) {
        const uint32_t idx = residents[i];
        if (idx >= state.significant_npcs.size())
            continue;
        const NPC& npc = state.significant_npcs[idx];

        // Choose this resident's livelihood. The food balance already decided HOW MANY
        // can be freed from farming; here we just spread them across the era's available
        // Layer-2 roles, knowledge-keepers first. Everyone else farms (Layer 1).
        uint16_t occ = npc.occupation;
        const OccupationDefinition* chosen = nullptr;
        if (i < specialists && !layer2.empty())
            chosen = layer2[i % layer2.size()];
        if (chosen == nullptr && !layer1.empty())
            chosen = layer1[i % layer1.size()];
        if (chosen != nullptr)
            occ = chosen->index;

        const bool occupation_changed = (occ != npc.occupation);
        const float proto_share =
            proto_share_for(is_lord_at(i), lords, static_cast<uint32_t>(residents.size()),
                            total_proto, manorial, cfg_);
        if (!occupation_changed && proto_share <= 0.0f)
            continue;  // nothing to write for this resident

        NPCDelta nd{};
        nd.npc_id = npc.id;
        if (occupation_changed)
            nd.new_occupation = occ;
        if (proto_share > 0.0f)
            nd.capital_delta = proto_share;
        province_delta.npc_deltas.push_back(nd);
    }
}

void SubsistenceModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel: the food economy itself runs in execute_province().
    //
    // The one thing that must happen globally is the REGIME-EXIT RESET. This
    // module is the sole writer of cohort_stats->subsistence_surplus_ratio, and it
    // goes inert the moment the era leaves the commons regimes — so without a
    // reset the last pre-market value (a famine year's 0.7, say) persisted forever
    // and kept scaling births in every later era, because population_aging
    // consumes the field unconditionally. The publisher owns the invariant, so
    // publish a one-time return to the neutral 1.0 ("fed") on the way out. Same
    // dirty-flag pattern warfare uses for war_death_fraction.
    const uint32_t n = static_cast<uint32_t>(state.provinces.size());
    if (n == 0)
        return;
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    const bool active = era != nullptr && regime_active(era->economic_regime);
    if (active) {
        commons_state_dirty_ = true;
        return;
    }
    if (!commons_state_dirty_)
        return;
    for (uint32_t i = 0; i < n; ++i) {
        RegionDelta rd{};
        rd.region_id = state.provinces[i].region_id;
        rd.subsistence_surplus_replacement = 1.0f;  // neutral: "fed"
        rd.grain_surplus_replacement = 0.0f;        // no commons surplus to haul
        rd.specialist_fraction_replacement = 0.0f;  // the commons stratum is gone
        rd.supported_specialist_fraction_replacement = 0.0f;
        delta.region_deltas.push_back(rd);
    }
    commons_state_dirty_ = false;
}

}  // namespace econlife
