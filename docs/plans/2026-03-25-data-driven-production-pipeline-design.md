# Data-Driven Production Pipeline — Design

**Date:** 2025-03-25
**Status:** Approved
**Approach:** A (data-first, bottom-up)

## Goal

Make the production loop functional by creating the data files (recipes,
facility types) and loaders that feed the already-implemented production
module. All economic content is data-driven — modders edit CSVs, not code.

## Design Principles

1. **Legality is jurisdictional, not intrinsic.** Per the GDD: "A drug lab
   and a pharmaceutical plant are both NPCBusiness records running the same
   recipe system." Legality is determined by `Province.drug_legalization_status`
   and nation law tables, not by goods or facility type metadata. No facility
   type or recipe carries an `illegal` flag.

2. **No criminal-specific facility types.** A `chemical_plant` can run a
   cocaine synthesis recipe or a fertilizer recipe. Detection comes from
   the evidence/investigation modules, not from facility type classification.

3. **Mirror the GoodsCatalog pattern.** RecipeCatalog and FacilityTypeCatalog
   follow the same load-from-directory, immutable-after-init, string-keyed
   design proven by GoodsCatalog.

4. **Config over constexpr.** Production constants move to JSON so modders
   can tune without recompilation.

## Existing State

- **GoodsCatalog:** Working. 252 goods across 5 tier CSVs. Loader at
  `simulation/core/world_gen/goods_catalog.h/.cpp`.
- **Production module:** Fully implemented (`production_module.cpp`, 340 lines).
  RecipeRegistry and FacilityRegistry exist but are empty at runtime — no
  data loaded. Production logic (bottleneck ratio, worker multiplier, tech
  tier bonus, quality ceiling) is correct but produces nothing.
- **WorldGenerator:** Creates NPCBusiness entities with sectors but assigns
  no facilities or recipes.
- **Recipe/facility CSV directories:** Do not exist yet.

## Known Design Doc Inconsistency

The goods CSVs have an `illegal` column (e.g., `coca_leaf,...,true`). This
contradicts the GDD's jurisdiction-based legality model. Cleanup item: rename
to `regulation_class` (values: `unrestricted`, `controlled`, `narcotic`,
`weapon`, `explosive`, `currency`) so nations interpret it through their law
tables rather than treating it as a boolean legality flag. Not blocking for
this work.

The Commodities doc v23 lists `drug_lab` as a criminal facility category.
Per GDD, this should just be `chemical_plant` — same facility, different
recipe, legality determined by jurisdiction. This design follows the GDD.

---

## 1. Recipe CSV Schema

**Directory:** `packages/base_game/recipes/`
**Files:** Split by supply chain stage for maintainability.

**Columns (22):**

```
recipe_key,facility_type_key,display_name,input_1_key,input_1_qty,input_2_key,input_2_qty,input_3_key,input_3_qty,input_4_key,input_4_qty,output_1_key,output_1_qty,output_1_is_byproduct,output_2_key,output_2_qty,output_2_is_byproduct,labor_per_tick,energy_per_tick,min_tech_tier,key_technology_node,era_available
```

- Up to 4 inputs, 2 outputs per row. Empty slots use empty string and 0.
- `output_N_is_byproduct`: 0 or 1.
- `key_technology_node`: empty string for commodity recipes.
- `era_available`: uint8, era when recipe unlocks (1-5 for V1).
- `waste_rate` and `explosion_risk_per_tick` from Commodities doc are dropped
  from the CSV — they are facility-operation-level properties tracked on the
  Facility instance, not recipe-level constants. (A well-maintained chemical
  plant has lower waste than a neglected one running the same recipe.)

**CSV files:**

| File | Content | Approx rows |
|------|---------|-------------|
| `recipes_extraction.csv` | Mining, drilling, quarrying, logging | ~10 |
| `recipes_agriculture.csv` | Crops, livestock, plantations | ~15 |
| `recipes_processing.csv` | Smelting, refining, chemicals, textiles, paper | ~25 |
| `recipes_manufacturing.csv` | Components, sub-assemblies, food processing | ~25 |
| `recipes_assembly.csv` | Vehicles, electronics, machinery, construction | ~15 |
| `recipes_services.csv` | Banking, insurance, legal, logistics, security | ~5 |
| `recipes_pharmaceuticals.csv` | Legal drugs, narcotics, designer drugs | ~10 |

Total: ~105 recipes.

**Note on pharmaceuticals:** These are unified with narcotics. The recipe
`opioid_synthesis` produces `synthetic_opioid`. Whether that's a legal
pharmaceutical or an illegal narcotic depends on the province's law table.
Same recipe, same facility type, different legal treatment.

## 2. Facility Types CSV Schema

**File:** `packages/base_game/facility_types/facility_types.csv`

**Columns (11):**

```
facility_type_key,display_name,category,base_construction_cost,base_operating_cost,max_workers,signal_weight_noise,signal_weight_waste,signal_weight_traffic,signal_weight_pollution,signal_weight_odor
```

- `category`: `extraction`, `agriculture`, `processing`, `manufacturing`,
  `services`, `logistics`, `research`. No `criminal` category.
- Signal weights (0.0–1.0) feed the facility_signals module for evidence
  generation. A chemical plant has high waste/pollution signals regardless
  of whether it's making fertilizer or cocaine.

**Facility types (~17):**

| Key | Category | Runs recipes for |
|-----|----------|-----------------|
| `mine` | extraction | iron_ore, coal, copper_ore, bauxite |
| `oil_well` | extraction | crude_oil, natural_gas |
| `quarry` | extraction | raw_stone, limestone |
| `logging_camp` | extraction | raw_timber |
| `farm` | agriculture | wheat, corn, rice, soybeans, vegetables, fruit, cattle, poultry, cotton |
| `plantation` | agriculture | coffee, tobacco, sugar_cane, coca_leaf, poppy, cannabis_raw, hemp |
| `sawmill` | processing | lumber, plywood |
| `smelter` | processing | pig_iron, steel, copper_ingot, aluminum_ingot |
| `refinery` | processing | refined_petroleum, diesel, kerosene, jet_fuel, lubricants, plastics |
| `chemical_plant` | processing | sulfuric_acid, ammonia, fertilizer, pharmaceuticals, cocaine, meth, synthetic_opioid, designer_drug |
| `paper_mill` | processing | paper, cardboard |
| `textile_mill` | manufacturing | thread, fabric, clothing |
| `food_processing_plant` | manufacturing | flour, bread, canned_food, frozen_meals, cooking_oil, sugar, processed_meat |
| `electronics_factory` | manufacturing | silicon_wafers, circuit_boards, microprocessors, consumer_electronics, batteries |
| `vehicle_plant` | manufacturing | engine_blocks, automobiles, trucks |
| `machinery_factory` | manufacturing | industrial_machinery, agricultural_equipment |
| `workshop` | manufacturing | furniture, leather_goods, counterfeit_currency, pirated_goods, illegal_weapons |

`workshop` is the general-purpose small-scale facility. It can make furniture
or counterfeit currency — same type, different recipe, different legality.

## 3. Loader Architecture

### FacilityType struct (new, added to `production_types.h`)

```cpp
struct FacilityType {
    std::string key;
    std::string display_name;
    std::string category;
    float base_construction_cost;
    float base_operating_cost;
    uint32_t max_workers;
    float signal_weight_noise;
    float signal_weight_waste;
    float signal_weight_traffic;
    float signal_weight_pollution;
    float signal_weight_odor;
};
```

### RecipeCatalog (`simulation/core/data/recipe_catalog.h/.cpp`)

Mirrors GoodsCatalog:

```cpp
class RecipeCatalog {
public:
    bool load_from_directory(const std::string& recipes_dir);
    bool load_csv(const std::string& filepath);

    const Recipe* find(const std::string& recipe_key) const;
    std::vector<const Recipe*> recipes_for_facility_type(const std::string& facility_type_key) const;
    std::vector<const Recipe*> recipes_by_output(const std::string& good_id) const;
    std::vector<const Recipe*> recipes_available_at(uint8_t era) const;
    const std::vector<Recipe>& all() const;

private:
    std::vector<Recipe> recipes_;
    std::unordered_map<std::string, size_t> key_index_;
    std::unordered_map<std::string, std::vector<size_t>> facility_type_index_;
    std::unordered_map<std::string, std::vector<size_t>> output_index_;
};
```

### FacilityTypeCatalog (`simulation/core/data/facility_type_catalog.h/.cpp`)

```cpp
class FacilityTypeCatalog {
public:
    bool load_from_csv(const std::string& filepath);

    const FacilityType* find(const std::string& key) const;
    std::vector<const FacilityType*> by_category(const std::string& category) const;
    const std::vector<FacilityType>& all() const;

private:
    std::vector<FacilityType> types_;
    std::unordered_map<std::string, size_t> key_index_;
};
```

### Recipe struct changes

The existing `Recipe` in `production_types.h` needs these additions:
- `std::string facility_type_key` — which facility type runs this recipe
- `uint8_t era_available` — era gating
- `RecipeOutput.is_byproduct` (bool) — already partially there as `quality_base`,
  needs the byproduct flag added

The existing `RecipeInput.quantity_per_tick` and `RecipeOutput.quantity_per_tick`
map directly to the CSV `input_N_qty` / `output_N_qty` columns.

## 4. WorldGenerator Wiring

After loading catalogs and creating businesses, WorldGenerator assigns facilities:

1. Each province has resource tags (from province template data) that determine
   which extraction/agriculture facility types are viable.
2. For each business, based on its `sector`:
   - Pick 1–3 facility types appropriate for the sector
   - For each facility, pick a recipe from `RecipeCatalog::recipes_for_facility_type()`
   - Create `Facility` instance with initial values
   - Register in `FacilityRegistry`
3. Set initial input good inventories (enough for ~5 ticks of production).

Province resource profiles loaded from:
`packages/base_game/provinces/province_templates.csv`

```
province_template_key,display_name,resource_tags,primary_sectors
coastal_industrial,Coastal Industrial,"iron_ore;coal;fish",manufacturing;processing
agricultural_heartland,Agricultural Heartland,"wheat;corn;cattle",agriculture;food_processing
```

## 5. Production Config Externalization

**File:** `packages/base_game/config/production_config.json`

```json
{
    "tech_tier_output_bonus_per_tier": 0.08,
    "tech_tier_cost_reduction_per_tier": 0.05,
    "tech_quality_ceiling_base": 0.5,
    "tech_quality_ceiling_step": 0.1,
    "worker_productivity_diminishing_factor": 0.85,
    "minimum_input_fraction_to_produce": 0.1
}
```

`ProductionConstants` becomes a runtime-loaded struct. Production module reads
it at init from the config path.

## 6. Test Strategy

1. **Unit tests:** RecipeCatalog load/find/index, FacilityTypeCatalog load/find,
   malformed CSV handling, missing fields, duplicate keys.
2. **Integration test:** Load all CSVs → WorldGenerator creates world with
   facilities → run 10 ticks → verify production output > 0 for at least
   one good.
3. **Cross-validation:** Every recipe input/output good_key exists in GoodsCatalog.
   Every recipe facility_type_key exists in FacilityTypeCatalog. Checked at
   load time with warnings.
4. **Determinism:** Same seed → same facility assignment → same production
   after 100 ticks.

## 7. Files to Create/Modify

### New files:
- `packages/base_game/recipes/recipes_extraction.csv`
- `packages/base_game/recipes/recipes_agriculture.csv`
- `packages/base_game/recipes/recipes_processing.csv`
- `packages/base_game/recipes/recipes_manufacturing.csv`
- `packages/base_game/recipes/recipes_assembly.csv`
- `packages/base_game/recipes/recipes_services.csv`
- `packages/base_game/recipes/recipes_pharmaceuticals.csv`
- `packages/base_game/facility_types/facility_types.csv`
- `simulation/core/data/recipe_catalog.h`
- `simulation/core/data/recipe_catalog.cpp`
- `simulation/core/data/facility_type_catalog.h`
- `simulation/core/data/facility_type_catalog.cpp`
- `simulation/tests/unit/recipe_catalog_test.cpp`
- `simulation/tests/unit/facility_type_catalog_test.cpp`

### Modified files:
- `simulation/modules/production/production_types.h` — add FacilityType struct,
  extend Recipe with facility_type_key and era_available
- `simulation/core/world_gen/world_generator.cpp` — facility assignment
- `simulation/core/world_gen/world_generator.h` — accept catalog references
- `packages/base_game/config/production_config.json` — new config file
- `simulation/modules/production/production_module.cpp` — load config from JSON
- CMakeLists.txt files — add new sources

## 8. Out of Scope

- R&D module / tech tree — separate session per plan
- `illegal` column cleanup in goods CSVs — tracked as tech debt
- Province template CSV — minimal version for now, full implementation
  when regional_conditions module is wired
- LOD overlay logic — per plan, Session 6
