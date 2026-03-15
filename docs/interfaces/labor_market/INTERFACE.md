# Module: labor_market

## Purpose
Matches unemployed NPCs to job postings through three hiring channels (public board, professional network, personal referral), processes hiring and firing decisions, adjusts regional wages based on labor supply/demand imbalance, and tracks employer reputation from worker memory logs. Province-parallel -- each province's labor market is independent.

## Inputs (from WorldState)
- `provinces[].regional_wage_by_skill` — per-SkillDomain wage rate (per-tick currency), updated monthly
- `provinces[].cohort_stats.cohorts[]` — PopulationCohort records: skill_supply per domain, size, employment_rate, demographic_group
- `provinces[].facilities[]` — facility labor_requirement and primary SkillDomain from FacilityRecipe
- `businesses[]` — NPCBusiness records: province_id, owner_id, criminal_sector, cash, employer_history
- `significant_npcs[]` — named NPCs: province_id, skill levels, motivation weights, employer_id, employer_history, travel_status, relationship graph (trust values)
- `job_postings[]` — active JobPosting records: id, owner_id, business_id, province_id, required_domain, min_skill_level, offered_wage, channel, posted_tick, expires_tick, applicant_ids, filled
- `current_tick` — current simulation tick
- `config.labor` — wage_adjustment_rate (0.03), wage_floor (0.01), wage_ceiling_multiplier (5.0), pool_size_public (12), pool_size_professional (5), pool_size_referral (3), reputation_threshold (0.3), reputation_pool_penalty_scale (8.0), salary_premium_per_reputation_point (0.5), voluntary_departure_threshold (0.35), departure_base_rate (0.08), reputation_default (0.5), deferred_salary_max_ticks (30)
- `deferred_work_queue` — WorkType::npc_hire_decision and WorkType::wage_market_update items with due_tick <= current_tick

## Outputs (to DeltaBuffer)
- `NPCDelta.employer_business_id` (replacement): set on hire, cleared on fire/departure
- `NPCDelta.capital_delta` (additive): wage payments from business to employed NPC each tick
- `NPCDelta` memory entries: employment_positive (on hire, emotional_weight +0.1 to +0.5 scaled by overpay), employment_negative (on fire, weight -0.2 to -0.7 by reason; on rejection, weight -0.1)
- `NPCDelta` memory entry: witnessed_wage_theft (emotional_valence -0.3 to -0.7) when deferred_salary_liability exceeds deferred_salary_max_ticks
- `RegionDelta.regional_wage_by_skill` (replacement): updated monthly per province per SkillDomain
- `MarketDelta` (indirect): employer_reputation derived from worker memory logs affects future hiring pool quality
- JobPosting.filled set to true when hire completes; remaining applicants receive rejection memory
- DeferredWorkQueue entries for recurring monthly wage_market_update and npc_hire_decision batches

## Preconditions
- Production module has completed: facility labor_requirement values are finalized for this tick.
- Province cohort_stats are populated with current skill_supply and employment_rate.
- regional_wage_by_skill is initialized (at world gen from province median_income with domain multipliers).
- NPC relationship graphs are populated (required for personal_referral channel: trust > 0.4 filter).
- criminal_sector flag is authoritative on each NPCBusiness (professional_network channel is unavailable to criminal businesses).

## Postconditions
- All expired job postings (expires_tick <= current_tick) are closed; unfilled postings generate no further applicants.
- All NPC hire decisions from DeferredWorkQueue for this tick are processed: best applicant (highest skill_level / salary_expectation ratio, subject to min_skill_level) is hired if offered_wage >= salary_expectation.
- Hired NPCs have employer_id set and gain employment_positive memory entry.
- Fired NPCs have employer_id cleared, gain employment_negative memory entry, and retain all prior memory (including witnessed_illegal_activity).
- Monthly wage_market_update (when due): regional_wage_by_skill updated for all SkillDomains in each province, clamped to [wage_floor, wage_ceiling_multiplier * median_income].
- Employer reputation is consistent with current worker memory logs (not a stored field; derived on demand).

## Invariants
- Hiring applies identically to player and NPC businesses; no separate hiring path for the player.
- Applicant pool is generated once at JobPosting creation and attached to applicant_ids; it does not regenerate each tick.
- professional_network channel is unavailable to businesses with criminal_sector == true.
- personal_referral pool draws only from NPCs with trust > 0.4 in the hiring actor's relationship graph.
- salary_expectation = regional_wage * (1.0 + npc.motivation.weights[money] * 0.3) for standard applicants.
- Low-reputation employers (reputation < 0.3) face reduced pool size AND salary premium on applicants: salary_expectation *= 1.0 + (threshold - reputation) * salary_premium_per_reputation_point.
- Employer reputation range is [0.0, 1.0]; unknown employers default to 0.5.
- Wages are clamped: wage_floor <= regional_wage_by_skill[domain] <= wage_ceiling_multiplier * province.median_income.
- Firing does not clear NPC knowledge (witnessed_illegal_activity, witnessed_safety_violation memories persist).
- Background NPC stubs (from public_board when named NPC supply is insufficient) have skill_level drawn from N(cohort.skill_supply[domain], 0.15) and salary_expectation at exactly regional_wage.

## Failure Modes
- No candidates available for a job posting channel + province combination: applicant pool is empty; posting remains unfilled until expiry.
- Business cash insufficient for offered_wage: wage is deferred; deferred_salary_liability accumulates. Sustained deferral (> deferred_salary_max_ticks) generates witnessed_wage_theft memory entry.
- All applicants have salary_expectation > offered_wage: no hire occurs; posting remains unfilled.
- NPC is in_transit (travel_status != resident): NPC cannot be hired or start work until arrival; application remains but hire is deferred.

## Performance Contract
- Tick step 4 budget: province-parallel, fully independent per province.
- Monthly wage_market_update: O(num_skill_domains * num_provinces) -- trivially cheap (~6 provinces * ~15 domains).
- Applicant pool generation: O(pool_size_target) per posting creation, not per tick.
- Full labor step at 2,000 NPCs, 6 provinces: < 20ms target.

## Dependencies
- runs_after: ["production"]
- runs_before: ["price_engine"]

## Test Scenarios
- `test_public_board_generates_full_pool`: Create a public_board JobPosting in a province with sufficient named NPCs and background population. Verify applicant_ids contains exactly pool_size_public (12) entries.
- `test_professional_network_excludes_criminal`: Create a professional_network JobPosting for a business with criminal_sector == true. Verify the posting is rejected or the channel is unavailable.
- `test_personal_referral_trust_filter`: Create a personal_referral JobPosting. Seed the hiring actor's relationship graph with 5 NPCs, 3 with trust > 0.4 and 2 with trust <= 0.4. Verify only the 3 high-trust NPCs appear in the applicant pool.
- `test_npc_hire_best_ratio`: Create a JobPosting with 3 applicants: A (skill 0.8, salary 1.0), B (skill 0.6, salary 0.5), C (skill 0.3, salary 0.2). Verify NPC hiring selects B (highest skill/salary ratio of 1.2) subject to min_skill_level.
- `test_hire_fails_below_salary_expectation`: Create a JobPosting with offered_wage = 0.5 and a single applicant with salary_expectation = 0.8. Verify no hire occurs and the posting remains unfilled.
- `test_wage_adjustment_labor_shortage`: Set up a province where demand for Engineering exceeds supply by 2:1. Run one monthly wage_market_update. Verify regional_wage_by_skill[Engineering] increased by approximately wage_adjustment_rate * (2.0 - 1.0) = 3%.
- `test_wage_adjustment_labor_surplus`: Set up a province where supply for Trade exceeds demand by 2:1. Run one monthly wage_market_update. Verify regional_wage_by_skill[Trade] decreased, but remains above wage_floor.
- `test_wage_ceiling_clamp`: Set median_income such that wage_ceiling_multiplier * median_income = 10.0. Push Engineering wage to 12.0 via extreme demand. Verify wage is clamped to 10.0.
- `test_low_reputation_reduces_pool`: Set employer reputation to 0.1 (below threshold 0.3). Create a public_board posting. Verify applicant pool is smaller than pool_size_public by (0.3 - 0.1) * reputation_pool_penalty_scale = 1.6, rounded to 10 applicants.
- `test_low_reputation_salary_premium`: Set employer reputation to 0.2. Verify applicants' salary_expectation is multiplied by 1.0 + (0.3 - 0.2) * 0.5 = 1.05 (5% premium).
- `test_firing_retains_memory`: Hire an NPC who witnesses illegal activity. Fire the NPC. Verify NPC's memory still contains witnessed_illegal_activity entries after firing.
- `test_deferred_salary_generates_wage_theft`: Set business cash to 0 so salary cannot be paid for 31 consecutive ticks (> deferred_salary_max_ticks). Verify the employed NPC gains a witnessed_wage_theft memory entry with emotional_valence between -0.3 and -0.7.
- `test_voluntary_departure_below_threshold`: Set worker_satisfaction to 0.2 (below voluntary_departure_threshold 0.35). Run monthly evaluation. Verify departure_probability = departure_base_rate * (1.0 - 0.2) * npc.motivation.weights[career] > 0 and NPC may depart.
- `test_expired_posting_closes`: Create a JobPosting with expires_tick = current_tick + 5. Advance 6 ticks without hiring. Verify posting is closed and no further applicants are generated.
- `test_hire_sets_employment_positive_memory`: Hire an NPC with offered_wage = 1.5 * salary_expectation. Verify the NPC gains an employment_positive memory with emotional_weight = (1.5 - 1.0) * 0.5 = 0.25.
- `test_deterministic_across_core_counts`: Run 50 ticks of labor market processing on 1 core and 6 cores with identical seeds. Verify bit-identical regional_wage_by_skill and employer_id assignments.
