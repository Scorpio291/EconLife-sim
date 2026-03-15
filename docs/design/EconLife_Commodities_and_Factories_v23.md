# EconLife — Commodities, Resources & Factory Specification
*Companion document to GDD v1.7, Technical Design Document v29, R&D & Technology v2.2*
*Version 2.3 — Pass 3 Session 18: Recipe CSV schema updated (key_technology_node column added, signal columns moved to facility_types.csv); OPSECProfile removed; FacilityRecipe updated with FacilitySignals signals and explosion_risk_per_tick; drug lab recipe example updated to new signal naming*

*Pass 5 Session 3 (COM-B1): facility_types.csv format specification completed with signal weight columns and canonical examples; signal architecture explanation added; drug lab examples reconciled to canonical field naming*

*Pass 5 Session 7 (FT-I2, COM-P1 through COM-SC3, COM-S1 through COM-S3): Added coca_leaf and poppy as Tier 0 biological resources [V1]; standardized drug lab signal notation to numeric format; added facility types master list; completed quality weights documentation; added labor unit specifications; energy self-sourcing clarification; recipe tech tier ceiling documentation; modding API field documentation; criminal maturation mechanics; technology node registry; circular recipe dependency detection; mod load order conflict resolution; seasonal vs. continuous farm production resolved to SEASONAL WITH CLIMATE*

*Pass 5 Session 9 (Scenario Tests) applied: 3 Commodities scenario stubs added (COM-S1 Circular Recipe Dependency Detection, COM-S2 Mod Load Order Conflicts, COM-S3 Seasonal Farm Production Validation)*

---

## Purpose

This document defines:

1. **The data-driven architecture** — how goods, recipes, and facility types are defined in external data files rather than code, enabling end-user modding without recompilation.
2. **The complete goods list** — all tradeable goods in the base game, organized into five supply chain tiers with full sub-assembly depth.
3. **Factory recipes** — the black-box transformation model: goods in, goods out. The facility design system adds efficiency modifiers on top; the recipe is the permanent economic truth underneath.
4. **Modding specification** — what modders can add, override, and remove, and how the engine loads it.

---

## Part 1 — Data-Driven Architecture

### Why Data-Driven

The original specification assigned stable integer IDs to goods (iron ore = 1, copper ore = 2, etc.) and referenced them in C++ enums. This approach has a fatal flaw: modders cannot add a new good without modifying the enum and recompiling the game. It also makes mod compatibility fragile — two mods that each add a good will collide on IDs.

The solution is to make goods, recipes, and facility types fully data-driven. The engine defines *behavior* — how the market clears, how recipes consume inputs and produce outputs, how factories are ticked. The *content* — what goods exist, what recipes transform them, what facilities can be built — lives in data files that anyone can edit or extend.

This is how Capitalism Lab's `Manufacturing.DBF` works at its core: the game engine doesn't know what a "car" is. It knows how to run a manufacturing simulation. The data file tells it that "Car" consumes "Car Body × 1 + Engine × 1 + Wheels × 4" and produces "Car × 1." Modders have added hundreds of new products this way without touching engine code.

EconLife follows the same principle, but with a more expressive format.

---

### Good Keys vs Integer IDs

Internally, the engine maps goods to `uint32_t` IDs at load time for performance. But goods are *authored* and *referenced* by **string keys** — human-readable identifiers like `"iron_ore"`, `"steel"`, `"car_engine"`.

```
iron_ore       → loaded → uint32 ID 1   (assigned by load order, not hardcoded)
copper_ore     → loaded → uint32 ID 2
steel          → loaded → uint32 ID 47
car_engine     → loaded → uint32 ID 203
```

Modders reference goods by key in their data files. The engine resolves keys to IDs at load time. Two mods can both add goods — they get different IDs assigned at load time, and there is no collision. Recipe files reference goods by key, so recipes work regardless of what ID a good is assigned.

The goods list in this document shows keys. The integer IDs shown are *base game load-order* values for reference; they are not guaranteed across mod configurations.

---

### File Structure

```
/data/
  goods/
    geological.csv          ← raw geological resources
    biological.csv          ← raw biological resources
    processed_metals.csv    ← tier 1: metal products
    processed_petroleum.csv ← tier 1: petroleum products
    processed_chemicals.csv ← tier 1: chemical products
    processed_food.csv      ← tier 1: food processing outputs
    processed_timber.csv    ← tier 1: timber products
    subassemblies.csv       ← tier 2: components and sub-assemblies
    finished_goods.csv      ← tier 3: end products
    criminal.csv            ← tier 4: criminal goods
    financial.csv           ← tier 5: financial instruments
  recipes/
    extraction.csv          ← extraction facility recipes
    processing.csv          ← processing facility recipes
    manufacturing.csv       ← manufacturing facility recipes
    assembly.csv            ← final assembly recipes
    criminal.csv            ← criminal facility recipes
  facilities/
    facility_types.csv      ← facility type definitions
  mods/
    [mod_name]/
      goods/                ← mod-provided goods files
      recipes/              ← mod-provided recipe files
      facilities/           ← mod-provided facility types
      mod.json              ← mod manifest
```

---

### Goods File Format

Each row in a goods CSV defines one good. Required fields:

```
key,display_name,tier,category,base_price,adjustment_rate,unit,physical_size,is_perishable,notes
```

Example rows:
```csv
iron_ore,Iron Ore,0,geological,12.00,0.04,tonne,bulk,false,Primary steel feedstock
steel,Steel,1,metals,180.00,0.06,tonne,bulk,false,Foundation of heavy industry
car_engine,Car Engine,2,mechanical,2400.00,0.05,unit,medium,false,Assembled engine unit
passenger_car,Passenger Car,3,vehicles,28000.00,0.03,unit,large,false,Consumer vehicle
```

Fields:
- **key** — unique string identifier; snake_case; must be globally unique across base game and all loaded mods
- **tier** — 0 (raw) / 1 (processed) / 2 (sub-assembly) / 3 (finished) / 4 (criminal) / 5 (financial)
- **base_price** — reference price at world generation; regional prices diverge from this
- **adjustment_rate** — how fast spot price tracks equilibrium per tick (see adjustment rate table)
- **unit** — tonne / unit / litre / MWh / etc; display only
- **physical_size** — bulk / medium / large / liquid / gas; affects transport cost and facility storage
- **is_perishable** — if true, goods in transit degrade over ticks (food, live animals)

---

### Recipe File Format

Each row in a recipe CSV defines one transformation. A facility runs one recipe.

```
recipe_key,facility_type_key,display_name,
input_1_key,input_1_qty,input_2_key,input_2_qty,input_3_key,input_3_qty,
output_1_key,output_1_qty,output_1_is_byproduct,output_2_key,output_2_qty,output_2_is_byproduct,
labor,energy,min_tech_tier,tech_bonus_per_tier,key_technology_node,
waste_rate,explosion_risk_per_tick
```

**`key_technology_node`** — the R&D node key (string) whose `maturation_level` caps output quality for this recipe. Empty string (`""`) for commodity and mature-process recipes (steel, cement, basic chemicals) where no maturation ceiling applies. Example: `"electric_vehicle"` for EV assembly recipes; `""` for iron ore extraction. Validated against the loaded technology node registry at data load time; unknown non-empty keys cause a load error.

**`explosion_risk_per_tick`** — probability of a catastrophic facility incident each tick (0.0–1.0). Read by the random events system (tick step 19) independently of the signal struct. Not part of `FacilitySignals`. Criminal facilities (drug labs, illegal chemical plants) carry elevated values; standard industrial facilities carry low but nonzero values reflecting real industrial accident rates.

**Signal fields removed from CSV schema:** `opsec_power_signal`, `opsec_chem_signal`, and `opsec_odor_signal` have been removed from the recipe CSV. Signal component values are facility-type properties, not recipe properties — they belong in `facility_types.csv` as per-facility-type signal weight columns (see Facility Type File Format below and TDD v23 §16 `FacilityTypeSignalWeights`). Removing them from recipe rows eliminates the duplication that existed in the prior schema version.

---

### Facility Type File Format

```
facility_type_key,display_name,category,base_construction_cost,base_operating_cost,max_workers,power_draw_signal,chemical_waste_signal,foot_traffic_signal,noise_signal,financial_anomaly_signal
```

**Column definitions:**

- **`facility_type_key`** — Unique identifier (string). Used in recipe rows to specify which facility type can run the recipe.
- **`display_name`** — Human-readable name (string).
- **`category`** — Facility category (string). Examples: `agricultural`, `mining`, `chemical_processing`, `manufacturing`, `criminal`, `commercial`. Used for sorting and filtering.
- **`base_construction_cost`** — Initial build cost (float). Normalized unit; multiplied by map terrain and location modifiers at build time.
- **`base_operating_cost`** — Per-tick operating cost (float). Labor, maintenance, rent, taxes.
- **`max_workers`** — Maximum facility staff (integer). Hard cap on labor input; facility cannot exceed this regardless of recipe labor demand.
- **`power_draw_signal`, `chemical_waste_signal`, `foot_traffic_signal`, `noise_signal`, `financial_anomaly_signal`** — Signal weight components (floats, 0.0–1.0). Per-facility-type constants that define how observable each facility type is for each signal dimension. A `drug_lab` has fixed signal characteristics regardless of which drug recipe it runs. A `retail_store` has low power draw and high foot traffic. Canonical struct: TDD v23 §16 `FacilityTypeSignalWeights`. See **Signal Architecture** section below.

---

**Example facility types:**

```csv
drug_lab,Drug Manufacturing Lab,criminal,50000,2500,5,0.3,0.9,0.1,0.8,0.2
factory,General Factory,manufacturing,150000,5000,50,0.8,0.5,0.3,0.6,0.1
retail_store,Retail Store,commercial,30000,1000,20,0.4,0.1,0.95,0.2,0.05
office,Office Building,commercial,80000,2000,100,0.5,0.0,0.4,0.1,0.3
```

---

A facility type defines *what recipes can run in it*, not the recipe itself. A generic `chemical_plant` facility type might allow any recipe in the `chemical_processing` category. A specialized `petroleum_refinery` facility type might only allow `crude_oil_distillation`.

Modders can add new facility types that accept their new recipes without touching existing types. When adding a new facility type, modders must specify all signal weight columns; unknown signal column names cause a load error.

---

### Signal Architecture

**Signal weights are defined per facility TYPE, not per recipe.** A `drug_lab` has fixed signal characteristics regardless of which drug recipe it runs. This separation eliminates the duplication that existed in prior schema versions where signal values appeared in both recipe rows and facility definitions.

At runtime (tick step 13):
1. The base signal composite is computed from the facility type's signal weights.
2. The recipe adds additional signal burden (if any) via its `FacilitySignals signals` field.
3. Scrutiny mitigation is applied (guards, shell companies, etc.).
4. Net signal is computed and used by investigator systems (journalists, regulators, law enforcement).

The `FacilityRecipe.signals` field and the `FacilityTypeSignalWeights` field are complementary: facility type provides baseline, recipe provides additional burden. This allows:
- A large, noisy factory has high base signals but can run a quiet recipe.
- A hidden lab has low base signals (small, remote) but running a drug recipe adds chemical_waste_signal weight.
- Criminal facilities (drug_lab, illegal_chemical_plant) carry elevated signal weights across multiple dimensions, reflecting their inherent visibility risk.

---

## Part 2 — Goods Master List

Five tiers. Each tier builds on the previous. The goal: a player can enter any tier of any industry and find meaningful choices, meaningful competition, and meaningful vertical integration depth.

---

### Tier 0 — Raw Resources

Extracted from specific deposits on the map. Cannot be manufactured. Supply is geographically fixed and depletes over time. Every deposit has: Quantity, Quality (0.0–1.0, affects processing efficiency), Depth (affects extraction cost), Accessibility, and Depletion rate.

#### Geological

| Key | Display Name | Notes |
|---|---|---|
| `iron_ore` | Iron Ore | Primary steel feedstock; abundant globally |
| `copper_ore` | Copper Ore | Electrical, construction, component feedstock |
| `bauxite` | Bauxite | Aluminum feedstock; smelting is electricity-intensive |
| `tin_ore` | Tin Ore | Solder, plating, alloy feedstock |
| `zinc_ore` | Zinc Ore | Galvanizing, brass, rubber accelerant feedstock |
| `lead_ore` | Lead Ore | Battery, radiation shielding feedstock |
| `nickel_ore` | Nickel Ore | Steel alloy, battery cathode feedstock |
| `cobalt_ore` | Cobalt Ore | Battery, superalloy feedstock; rare, high-value |
| `lithium_ore` | Lithium Ore | Battery feedstock; demand tracks tech tier |
| `rare_earth_ore` | Rare Earth Ore | Electronics, magnets; geographic concentration creates leverage |
| `manganese_ore` | Manganese Ore | Steel alloy, battery feedstock |
| `chromite_ore` | Chromite Ore | Stainless steel, chrome plating feedstock |
| `titanium_ore` | Titanium Ore | Aerospace, medical, high-performance alloy feedstock |
| `gold_ore` | Gold Ore | Financial asset, electronics, jewelry feedstock |
| `silver_ore` | Silver Ore | Electronics, photovoltaic, jewelry feedstock |
| `platinum_ore` | Platinum Group Ore | Catalysts, electronics; extreme value density |
| `gemstones` | Gemstones | High-value commodity; money laundering vehicle |
| `thermal_coal` | Thermal Coal | Energy resource; sold to regional utilities in V1 |
| `coking_coal` | Coking Coal | Steel production feedstock; distinct market from thermal coal |
| `crude_oil` | Crude Oil | Primary petroleum feedstock; multiple simultaneous output streams |
| `natural_gas` | Natural Gas | Energy resource; petrochemical feedstock; byproduct of oil extraction |
| `uranium_ore` | Uranium Ore | Energy resource; sold to utilities in V1 |
| `limestone` | Limestone | Cement, glass, chemical feedstock |
| `silica_sand` | Silica Sand | Glass, silicon, semiconductor feedstock |
| `potash` | Potash | Fertilizer feedstock |
| `phosphate_rock` | Phosphate Rock | Fertilizer feedstock |
| `sulfur` | Sulfur | Sulfuric acid feedstock; industrial chemistry foundation |
| `graphite_ore` | Graphite Ore | Battery anode, lubricant feedstock |
| `salt` | Salt | Chemical processing, food preservation feedstock |
| `kaolin` | Kaolin (Clay) | Ceramics, paper coating, porcelain feedstock |

#### Biological

| Key | Display Name | Notes |
|---|---|---|
| `wheat` | Wheat | Grain; flour feedstock |
| `corn` | Corn (Maize) | Grain; starch, syrup, ethanol feedstock |
| `soybeans` | Soybeans | Oil and meal feedstock; pharmaceutical overlap |
| `cotton` | Cotton (Raw) | Fiber feedstock; labor-intensive cultivation |
| `natural_rubber` | Natural Rubber (Latex) | Industrial rubber feedstock; climate-constrained |
| `sugarcane` | Sugar Cane | Raw sugar, ethanol feedstock |
| `tobacco_leaf` | Tobacco Leaf | Processed tobacco feedstock |
| `coffee_beans_raw` | Coffee Beans (Raw) | Roasting feedstock |
| `cacao_beans` | Cacao Beans | Cocoa and chocolate feedstock |
| `palm_fruit` | Palm Fruit | Palm oil feedstock; high-yield tropical crop |
| `rapeseed` | Rapeseed (Canola) | Vegetable oil, biodiesel feedstock |
| `hops` | Hops | Beer brewing feedstock |
| `barley` | Barley | Brewing, animal feed feedstock |
| `rice` | Rice | Direct consumption; high demand in some regions |
| `cattle_live` | Cattle (Live) | Beef, leather, dairy feedstock |
| `pigs_live` | Pigs (Live) | Pork feedstock |
| `poultry_live` | Poultry (Live) | Chicken, eggs feedstock |
| `sheep_live` | Sheep (Live) | Wool, mutton feedstock |
| `fish_wild` | Fish — Wild-Caught | Seafood, fishmeal feedstock |
| `fish_farmed` | Fish — Farmed | Seafood, fishmeal feedstock; controllable supply |
| `softwood_logs` | Softwood Timber (Logs) | Lumber, pulp feedstock |
| `hardwood_logs` | Hardwood Timber (Logs) | Premium lumber, furniture feedstock |
| `pulpwood` | Pulpwood | Paper pulp feedstock |
| `hemp` | Hemp | Fiber, paper, CBD feedstock; legal status varies by region |
| `coca_leaf` | Coca Leaf | [V1] cocaine precursor; tropical climate; primary input for cocaine synthesis |
| `poppy` | Poppy | [V1] heroin precursor; temperate/subtropical climate; primary input for heroin synthesis |
| `milk_raw` | Raw Milk | Dairy processing feedstock |

---

### Tier 1 — Processed Materials

Produced by processing facilities. These are industrial inputs, not end consumer goods. Each one represents a market a player can enter with a processing facility.

#### Metals

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `steel` | Steel | iron_ore + coking_coal | Foundation of heavy industry; every chain touches this |
| `steel_sheet` | Steel Sheet | steel | Stamped/rolled; bodywork, appliances, packaging |
| `steel_pipe` | Steel Pipe | steel | Plumbing, structural, oil & gas infrastructure |
| `steel_wire_rod` | Steel Wire Rod | steel | Cable, reinforcement, fastener feedstock |
| `stainless_steel` | Stainless Steel | steel + chromite_ore | Corrosion-resistant alloy; food equipment, medical |
| `cast_iron` | Cast Iron | iron_ore + thermal_coal | Engine blocks, cookware, heavy machine components |
| `copper_wire_rod` | Copper Wire Rod | copper_ore | Electrical infrastructure, winding feedstock |
| `copper_pipe` | Copper Pipe | copper_ore | Plumbing, HVAC feedstock |
| `copper_foil` | Copper Foil | copper_ore | PCB feedstock |
| `aluminum_ingot` | Aluminum Ingot | bauxite | Electricity-intensive; location follows energy cost |
| `aluminum_sheet` | Aluminum Sheet | aluminum_ingot | Automotive, aerospace, packaging feedstock |
| `aluminum_extrusion` | Aluminum Extrusion | aluminum_ingot | Structural profiles; construction, frames |
| `tin_ingot` | Tin Ingot | tin_ore | Solder, tin plating, alloy feedstock |
| `zinc_ingot` | Zinc Ingot | zinc_ore | Galvanizing, die casting, brass feedstock |
| `lead_ingot` | Lead Ingot | lead_ore | Battery plates, shielding feedstock |
| `nickel_ingot` | Nickel Ingot | nickel_ore | Stainless alloy, battery cathode, superalloy feedstock |
| `cobalt_refined` | Refined Cobalt | cobalt_ore | Battery cathode, superalloy feedstock; rare |
| `lithium_carbonate` | Lithium Carbonate | lithium_ore | Battery electrolyte and cathode feedstock |
| `rare_earth_oxides` | Rare Earth Oxides | rare_earth_ore | Magnet and phosphor feedstock |
| `titanium_sponge` | Titanium Sponge | titanium_ore | Aerospace alloy feedstock |
| `brass` | Brass | copper_wire_rod + zinc_ingot | Fittings, valves, precision parts feedstock |
| `bronze` | Bronze | copper_wire_rod + tin_ingot | Bearings, marine hardware feedstock |
| `refined_gold` | Refined Gold | gold_ore | Financial, electronics, jewelry feedstock |
| `refined_silver` | Refined Silver | silver_ore | Electronics, photovoltaic, jewelry feedstock |

#### Petroleum Products

*The refinery produces all outputs simultaneously from crude oil. Output ratios are fixed by distillation physics — the refinery cannot be tuned to produce only one product. Every output enters the regional market whether there is demand or not.*

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `gasoline` | Gasoline | crude_oil | Consumer transport fuel |
| `diesel` | Diesel | crude_oil | Commercial transport fuel; supply chain critical |
| `jet_fuel` | Jet Fuel (Kerosene) | crude_oil | Aviation fuel |
| `heavy_fuel_oil` | Heavy Fuel Oil | crude_oil | Shipping, industrial boiler fuel |
| `naphtha` | Naphtha | crude_oil | Petrochemical feedstock; plastics and synthetics |
| `lubricants` | Lubricants | crude_oil | Industrial maintenance input |
| `bitumen` | Bitumen / Asphalt | crude_oil | Road construction material |
| `lpg` | LPG | crude_oil | Cooking, heating, vehicle fuel |
| `paraffin_wax` | Paraffin Wax | crude_oil | Candles, coatings, food processing |
| `petroleum_coke` | Petroleum Coke | crude_oil | Fuel, anode manufacturing byproduct |

#### Chemicals

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `ethylene` | Ethylene | naphtha | Plastics and synthetic fiber feedstock |
| `propylene` | Propylene | naphtha | Plastics (polypropylene) feedstock |
| `benzene` | Benzene | naphtha | Synthetic rubber, nylon, dye feedstock |
| `methanol` | Methanol | natural_gas | Chemical feedstock, biodiesel production |
| `ammonia` | Ammonia | natural_gas | Fertilizer synthesis foundation |
| `sulfuric_acid` | Sulfuric Acid | sulfur | Industrial chemistry foundation; most-produced chemical |
| `chlorine` | Chlorine | salt | PVC, water treatment, bleaching feedstock |
| `caustic_soda` | Caustic Soda (NaOH) | salt | Paper, textiles, alumina refining feedstock |
| `plastics_polymer` | Plastics (Polymer Resin) | ethylene / propylene | Universal manufacturing input |
| `synthetic_rubber` | Synthetic Rubber | benzene + ethylene | Tires, seals, hoses feedstock |
| `polyester_fiber` | Polyester Fiber | ethylene | Synthetic textiles feedstock |
| `fertilizer_npk` | Fertilizer (NPK Blend) | ammonia + potash + phosphate_rock | Agricultural input |
| `drug_precursors` | Drug Precursor Chemicals | various chemical inputs | Pharmaceutical and criminal overlap — same good, dual use |
| `epoxy_resin` | Epoxy Resin | benzene + chlorine | Composites, coatings, adhesives feedstock |
| `fiberglass` | Fiberglass | silica_sand + epoxy_resin | PCBs, insulation, composites feedstock |
| `paint_pigments` | Paint & Coatings | plastics_polymer + titanium_sponge | Industrial and consumer coatings |
| `industrial_gas` | Industrial Gases (O₂/N₂/Ar) | air (utilities input) | Welding, semiconductor fab, food packaging |
| `soap_base` | Soap Base | palm_fruit / soybeans + caustic_soda | Consumer and industrial cleaning feedstock |

#### Agricultural Processing

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `flour` | Flour | wheat | Food manufacturing feedstock |
| `corn_starch` | Corn Starch | corn | Food, industrial, biodegradable plastic feedstock |
| `corn_syrup` | Corn Syrup | corn | Food feedstock; sweetener |
| `soy_oil_crude` | Soy Oil (Crude) | soybeans | Refining feedstock |
| `soy_oil_refined` | Soy Oil (Refined) | soy_oil_crude | Cooking oil, food manufacturing input |
| `soy_meal` | Soy Meal | soybeans | Animal feed, aquaculture input |
| `palm_oil_crude` | Palm Oil (Crude) | palm_fruit | Versatile food and industrial oil feedstock |
| `palm_oil_refined` | Palm Oil (Refined) | palm_oil_crude | Food, personal care, biodiesel feedstock |
| `rapeseed_oil` | Rapeseed Oil | rapeseed | Cooking oil, biodiesel feedstock |
| `cotton_fiber` | Cotton Fiber | cotton | Textiles feedstock |
| `raw_sugar` | Raw Sugar | sugarcane | Refining feedstock |
| `refined_sugar` | Refined Sugar | raw_sugar | Food manufacturing input |
| `cured_tobacco` | Cured Tobacco | tobacco_leaf | Consumer product feedstock |
| `roasted_coffee` | Roasted Coffee | coffee_beans_raw | Consumer good; sold direct or used in packaged food |
| `cocoa_mass` | Cocoa Mass / Butter | cacao_beans | Chocolate and cosmetics feedstock |
| `beef` | Beef | cattle_live | Consumer food |
| `pork` | Pork | pigs_live | Consumer food |
| `poultry_meat` | Poultry Meat | poultry_live | Consumer food |
| `wool_raw` | Raw Wool | sheep_live | Textiles feedstock |
| `leather_raw` | Raw Leather / Hides | cattle_live | Leather goods feedstock |
| `seafood_processed` | Processed Seafood | fish_wild / fish_farmed | Consumer food |
| `fishmeal` | Fishmeal | fish_wild / fish_farmed | Animal feed, aquaculture input |
| `dairy_products` | Dairy Products | milk_raw | Butter, cheese, milk; consumer food |
| `malt` | Malt | barley | Brewing feedstock |

#### Timber Processing

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `lumber` | Lumber | softwood_logs / hardwood_logs | Construction, furniture feedstock |
| `paper_pulp` | Paper Pulp | pulpwood | Paper and packaging feedstock |
| `engineered_wood` | Engineered Wood (LVL/Plywood) | softwood_logs + epoxy_resin | Structural construction feedstock |
| `paper` | Paper | paper_pulp | Packaging, printing feedstock |
| `cardboard` | Cardboard | paper_pulp | Packaging feedstock |
| `wood_chips` | Wood Chips | softwood_logs | Biomass energy, paper pulp feedstock |
| `tannin_extract` | Tannin Extract | hardwood_logs | Leather tanning feedstock |

---

### Tier 2 — Sub-Assemblies and Components

*This is the new tier that was missing from v1. Sub-assemblies are manufactured intermediate goods — they are not raw materials and not consumer products. They exist as independent markets where players can specialize, and they are the chokepoints that make vertical integration strategically meaningful.*

*A player who controls engine production controls anyone who wants to manufacture vehicles. A player who controls PCB production controls anyone who wants to manufacture electronics. These goods create supplier leverage across entire industries.*

#### Structural and Mechanical Components

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `steel_casting` | Steel Casting | steel + industrial_gas | Engine blocks, heavy machine frames, raw castings |
| `aluminum_casting` | Aluminum Casting | aluminum_ingot + industrial_gas | Cylinder heads, wheels, housing castings |
| `structural_steel_section` | Structural Steel Section | steel | I-beams, columns, angles; construction and machinery |
| `precision_machined_parts` | Precision Machined Parts | steel / aluminum_ingot | Shafts, gears, brackets; high-tolerance components |
| `bearings` | Bearings | steel + brass | Rotational components; ubiquitous in all machinery |
| `fasteners` | Fasteners (Bolts, Nuts, Screws) | steel_wire_rod | Ubiquitous assembly input |
| `springs` | Springs | steel_wire_rod | Suspension, valves, mechanical systems |
| `hydraulic_components` | Hydraulic Components | steel + seals | Cylinders, pumps, valves for heavy machinery |
| `gear_set` | Gear Set / Transmission Components | precision_machined_parts + steel | Transmission and drivetrain feedstock |

#### Vehicle Sub-Assemblies

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `car_body_stamping` | Car Body Stamping | steel_sheet | Pressed steel panels; requires stamping press tooling |
| `car_chassis` | Car Chassis | structural_steel_section + steel_casting | Frame on which vehicle is assembled |
| `safety_glass` | Safety Glass (Laminated) | silica_sand + limestone + plastics_polymer | Windscreens, windows; tempered/laminated process |
| `tires` | Tires | synthetic_rubber / natural_rubber + steel_wire_rod | Wheels require tires separately |
| `wheel_rim` | Wheel Rim | aluminum_casting / steel | Rim onto which tire is mounted |
| `engine_block` | Engine Block | steel_casting + aluminum_casting | Core of the engine; machined to tolerance |
| `engine_assembled` | Assembled Engine | engine_block + precision_machined_parts + wiring_harness + electronic_components | Complete powertrain unit |
| `transmission` | Transmission | gear_set + precision_machined_parts + bearings | Manual or automatic; separate from engine |
| `wiring_harness` | Wiring Harness | copper_wire_rod + plastics_polymer | Complete electrical cable assembly for a vehicle |
| `vehicle_interior` | Vehicle Interior Assembly | leather_goods / textiles + plastics_polymer + electronic_components | Seats, dashboard, trim assembly |
| `brake_system` | Brake System | steel + hydraulic_components | Discs, calipers, lines; safety-critical |
| `exhaust_system` | Exhaust System | stainless_steel | Manifold to tailpipe; includes catalytic converter |

#### Electronics Sub-Assemblies

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `silicon_wafer` | Silicon Wafer | silica_sand + industrial_gas | Semiconductor fab feedstock; ultra-pure |
| `pcb_blank` | PCB (Blank) | fiberglass + copper_foil | Printed circuit board substrate; before component population |
| `pcb_populated` | PCB (Populated) | pcb_blank + electronic_components + solder | Functional circuit board ready for device assembly |
| `electronic_components` | Electronic Components | silicon_wafer + rare_earth_oxides + copper_wire_rod | Resistors, capacitors, chips; ubiquitous input across 20+ products |
| `display_panel` | Display Panel (LCD/OLED) | silica_sand + rare_earth_oxides + plastics_polymer | Screen used in phones, TVs, monitors |
| `battery_cell` | Battery Cell | lithium_carbonate + cobalt_refined + graphite_ore + aluminum_sheet | Single cell; assembled into packs |
| `battery_pack` | Battery Pack | battery_cell + electronic_components + plastics_polymer | Complete power unit for devices and EVs |
| `electric_motor` | Electric Motor | copper_wire_rod + rare_earth_oxides + steel + electronic_components | Used in EVs, appliances, industrial machinery |
| `power_supply_unit` | Power Supply Unit | pcb_populated + copper_wire_rod + steel | Voltage regulation for devices |
| `camera_module` | Camera Module | electronic_components + safety_glass | Optical sensor assembly for devices |
| `semiconductor_chip` | Semiconductor Chip | silicon_wafer + rare_earth_oxides + industrial_gas | CPU, GPU, memory — high-tech apex component |

#### Industrial and Construction Components

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `concrete` | Concrete | cement + construction_aggregate | Mixed on-site or pre-cast; construction fundamental |
| `cement` | Cement | limestone + thermal_coal | Concrete feedstock; high CO₂ byproduct |
| `construction_aggregate` | Construction Aggregate | limestone + silica_sand | Gravel, sand; bulk construction material |
| `glass_panel` | Glass Panel | silica_sand + limestone + soda_ash | Architectural glass; windows, facades |
| `ceramic_tiles` | Ceramic Tiles | kaolin + limestone | Construction finish material |
| `insulation_board` | Insulation Board | fiberglass / mineral_wool | Building insulation feedstock |
| `solder` | Solder | tin_ingot + lead_ingot / tin_ingot + silver_ore | Electronics assembly feedstock |
| `industrial_valve` | Industrial Valve | brass + steel | Piping system control components |
| `pump` | Industrial Pump | steel_casting + precision_machined_parts | Fluid movement in industrial systems |

#### Textiles and Soft Goods

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `yarn` | Yarn | cotton_fiber / wool_raw / polyester_fiber | Weaving and knitting feedstock |
| `woven_fabric` | Woven Fabric | yarn | Apparel, upholstery, industrial textile feedstock |
| `leather_goods` | Processed Leather | leather_raw + tannin_extract | Upholstery, apparel, accessories feedstock |
| `nonwoven_fabric` | Non-woven Fabric | polyester_fiber | Hygiene products, filtration, medical feedstock |

#### Chemical and Pharmaceutical Sub-Assemblies

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `active_pharma_ingredient` | Active Pharmaceutical Ingredient (API) | drug_precursors + R&D output | The medicinal compound; separate market from formulated drug. Criminal drug synthesis targets this step. |
| `polymer_compound` | Polymer Compound | plastics_polymer + paint_pigments | Colored, additive-blended plastics ready for molding |
| `adhesive` | Industrial Adhesive | epoxy_resin + plastics_polymer | Structural bonding for manufacturing |
| `detergent_base` | Detergent Base | soap_base + caustic_soda + palm_oil_refined | Consumer and industrial cleaning products feedstock |
| `cosmetic_base` | Cosmetic Base | cocoa_mass + palm_oil_refined + soap_base | Creams, lotions, personal care feedstock |

#### Food Processing Sub-Assemblies

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `refined_vegetable_oil` | Refined Vegetable Oil | soy_oil_crude / palm_oil_crude / rapeseed_oil | Cooking, food manufacturing input |
| `chocolate` | Chocolate | cocoa_mass + refined_sugar + dairy_products | Consumer good and confectionery feedstock |
| `beer_wort` | Beer Wort | malt + hops + water | Fermentation feedstock for brewing |
| `distilled_spirits_base` | Distilled Spirits (Base) | corn / barley + beer_wort | Spirits production feedstock |
| `flavor_concentrates` | Flavor Concentrates | various agricultural inputs | Processed food flavoring feedstock |

---

### Tier 3 — Finished Goods

End products sold to consumers or businesses. These are what retail outlets, distributors, and end-user NPCs purchase.

#### Vehicles

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `passenger_car` | Passenger Car | engine_assembled + car_chassis + car_body_stamping + vehicle_interior + tires + wheel_rim + brake_system + wiring_harness + safety_glass | 8 sub-assembly inputs. Full vertical chain requires 15+ facility types. |
| `commercial_truck` | Commercial Truck | engine_assembled + car_chassis + car_body_stamping + tires + wheel_rim + hydraulic_components | Logistics backbone; demand tied to economic activity |
| `electric_vehicle` | Electric Vehicle | electric_motor + battery_pack + car_chassis + car_body_stamping + vehicle_interior + tires + wheel_rim + brake_system + wiring_harness + safety_glass | Battery pack replaces engine; lithium supply chain bottleneck |
| `motorcycle` | Motorcycle | engine_assembled + car_chassis + tires + wheel_rim + exhaust_system | Simpler chain than passenger car |
| `agricultural_machinery` | Agricultural Machinery | engine_assembled + structural_steel_section + hydraulic_components + gear_set | Tractors, harvesters; farm productivity multiplier |
| `construction_equipment` | Construction Equipment | engine_assembled + structural_steel_section + hydraulic_components + steel_casting | Excavators, loaders; construction sector input |

#### Electronics and Devices

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `mobile_phone` | Mobile Phone | pcb_populated + display_panel + battery_pack + camera_module + plastics_polymer | High-demand consumer good; rapid obsolescence |
| `personal_computer` | Personal Computer | semiconductor_chip + pcb_populated + display_panel + power_supply_unit + plastics_polymer | Productivity device; business and consumer demand |
| `television` | Television | display_panel + pcb_populated + plastics_polymer + power_supply_unit | Mass-market consumer good |
| `server_unit` | Server Unit | semiconductor_chip + pcb_populated + power_supply_unit + steel | Business infrastructure; high margin |
| `industrial_electronics` | Industrial Electronics | pcb_populated + electronic_components + steel | Control systems, sensors, automation equipment |
| `household_appliance` | Household Appliance | electric_motor + steel_sheet + electronic_components + plastics_polymer | Washing machines, refrigerators, etc. |
| `solar_panel` | Solar Panel | silicon_wafer + refined_silver + aluminum_sheet + safety_glass | Energy generation component; EX — ties to power system |

#### Pharmaceuticals and Medical

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `pharmaceutical_otc` | Pharmaceutical (OTC) | active_pharma_ingredient + plastics_polymer | Over-the-counter drugs; high volume, regulated |
| `pharmaceutical_rx` | Pharmaceutical (Prescription) | active_pharma_ingredient + plastics_polymer | Controlled; FIU and health authority attention |
| `medical_device` | Medical Device | electronic_components + plastics_polymer + stainless_steel | Diagnostic, monitoring, treatment equipment |
| `personal_care_product` | Personal Care Product | cosmetic_base + detergent_base + plastics_polymer | Soaps, shampoos, lotions; mass consumer market |

#### Food and Beverages

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `packaged_food` | Packaged Food | flour + refined_vegetable_oil + refined_sugar + flavor_concentrates | Processed consumer food; high-volume |
| `dairy_consumer` | Consumer Dairy Products | dairy_products | Milk, cheese, yogurt for retail |
| `beer` | Beer | beer_wort | Fermented; regional brands have loyalty advantages |
| `spirits` | Spirits / Liquor | distilled_spirits_base | High-margin; premium segment brand-sensitive |
| `cigarettes` | Cigarettes | cured_tobacco + paper | High tax; money laundering via duty-free channels |
| `coffee_consumer` | Consumer Coffee | roasted_coffee + plastics_polymer | Retail and food service; brand-intensive |
| `chocolate_consumer` | Consumer Chocolate | chocolate + plastics_polymer | Confectionery; seasonal demand |

#### Apparel and Textiles

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `apparel_mass` | Mass Market Apparel | woven_fabric + yarn | Labor-intensive; location follows cheap labor |
| `apparel_premium` | Premium Apparel | woven_fabric + leather_goods | Margin through brand; less labor-sensitive |
| `industrial_textile` | Industrial Textile | nonwoven_fabric + woven_fabric | Filtration, protective gear, geotextiles |

#### Construction and Materials

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `prefab_building_component` | Prefabricated Building Component | structural_steel_section + cement + lumber | Modular construction input |
| `window_unit` | Window Unit | safety_glass + aluminum_extrusion + industrial_valve | Architectural product |
| `electrical_cable` | Electrical Cable | copper_wire_rod + plastics_polymer | Infrastructure material |
| `plumbing_system` | Plumbing System | copper_pipe + industrial_valve + pump | Infrastructure material |

#### Chemicals (Consumer and Industrial)

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `industrial_chemical` | Industrial Chemical | various processed chemicals | B2B sales to manufacturers |
| `agricultural_chemical` | Agricultural Chemical | fertilizer_npk + various | Pesticides, herbicides; farm sector |
| `cleaning_product` | Cleaning Product | detergent_base + plastics_polymer | Consumer and industrial |
| `adhesive_sealant` | Adhesive and Sealant | adhesive + plastics_polymer | Construction, consumer, industrial |

#### Heavy Industry Outputs

| Key | Display Name | Primary Inputs | Notes |
|---|---|---|---|
| `industrial_machinery` | Industrial Machinery | engine_assembled + structural_steel_section + hydraulic_components + bearings | Capital goods; feeds manufacturing sector |
| `mining_equipment` | Mining Equipment | engine_assembled + structural_steel_section + hydraulic_components + steel_casting | Extraction sector capital good |
| `aircraft_component` | Aircraft Component | titanium_sponge + structural_steel_section + electronic_components | Aerospace; EX scope, high tech tier requirement |

---

### Tier 4 — Criminal Goods

Criminal goods use the same supply chain and market infrastructure as legitimate goods but price through the informal economy layer. All have `drug_precursors` as a feedstock or adjacency. The same `RegionalMarket` struct holds both legitimate and criminal goods — the price calculation differs, not the architecture.

| Key | Display Name | Primary Inputs | V1/EX | Notes |
|---|---|---|---|---|
| `cannabis_raw` | Cannabis (Raw) | labor + energy + facility | V1 | Legal in some regions; same good, different regulatory status |
| `cannabis_processed` | Cannabis (Processed) | cannabis_raw | V1 | Packaged for distribution |
| `methamphetamine` | Methamphetamine | drug_precursors | V1 | Chemical synthesis; high waste signal |
| `synthetic_opioid` | Synthetic Opioid | drug_precursors | V1 | Pharmaceutical precursor diversion path |
| `designer_drug` | Designer Drug | drug_precursors + R&D | V1 | Stays ahead of scheduling; R&D-dependent |
| `cocaine` | Cocaine | coca_leaf + drug_precursors | V1 | Requires coca_leaf (Tier 0 biological, [V1]); tropical climate sourcing |
| `heroin` | Heroin | poppy + drug_precursors | V1 | Requires poppy (Tier 0 biological, [V1]); temperate/subtropical sourcing |
| `counterfeit_currency` | Counterfeit Currency | paper + printing_equipment | V1 minimal | Basic document fraud only in V1 |
| `stolen_goods` | Stolen Goods | varies | V1 | Re-enters market at discount; fencing operation |

---

### Tier 5 — Financial Instruments

Not physical goods. Traded through the same market infrastructure with spot prices, supply, and demand dynamics.

| Key | Display Name | Notes |
|---|---|---|
| `real_estate_residential` | Residential Real Estate | Regional price; money laundering vehicle |
| `real_estate_commercial` | Commercial Real Estate | Regional price; business investment |
| `company_shares` | Company Shares | Price derived from NPC business revenue/profit each tick |
| `government_bond` | Government Bond | Safe store of value; governs fiscal policy levers |
| `commodity_futures` | Commodity Futures | EX — derivative instruments on underlying goods prices |

---

### Adjustment Rate Reference

| Good Category | Rate / Tick | Rationale |
|---|---|---|
| Financial instruments | 0.25–0.50 | Trades instantly; price reflects information immediately |
| Criminal goods | 0.20–0.35 | Black market responds faster than formal commodity markets |
| Finished consumer goods | 0.05–0.10 | Moderate; demand signals propagate quickly |
| Processed materials / sub-assemblies | 0.04–0.08 | Industrial buyers adjust orders with lag |
| Raw resources (geological) | 0.03–0.06 | Extraction capacity fixed short-term |
| Agricultural raw | 0.03–0.07 | Seasonal production; climate-sensitive |
| Heavy finished goods | 0.02–0.04 | Capital goods; production and demand both slow to change |
| Real estate | 0.005–0.015 | Very slow; supply nearly fixed short-term |

---

## Part 3 — Factory Specification (Black-Box Model)

### Design Philosophy

A facility is a **recipe**. The facility design system (tile layout, bottlenecks, worker placement) adds an efficiency modifier on top. The recipe is the permanent economic truth underneath — it does not change when the design system ships.

```
actual_output = recipe.base_output
              × facility.efficiency_modifier     // from tile design (1.0 if not yet implemented)
              × tech_tier_multiplier             // (1.0 + TECH_TIER_BONUS × (tier - min_tier))
              × labor_saturation                 // actual_workers / required_workers, clamped 0.0–1.0
              × input_availability               // fraction of required inputs available this tick
```

Until the facility design system is built: `efficiency_modifier = 1.0`.

---

### Core Data Structures

```cpp
struct RecipeInput {
    std::string good_key;          // resolved to good_id at load time
    float quantity_per_tick;       // units consumed per tick at full operation
    bool is_optional;              // if true: absence degrades quality but doesn't halt production
    float optional_quality_penalty; // quality reduction if this input is absent
};

struct RecipeOutput {
    std::string good_key;
    float quantity_per_tick;
    float byproduct_fraction;      // 0.0 = primary output
                                   // > 0.0 = unavoidable byproduct — enters market regardless of demand
    bool environmental_waste;      // true = contributes to regional environmental conditions
};

struct FacilityRecipe {
    std::string recipe_key;
    std::string facility_type_key;
    std::string display_name;

    std::vector<RecipeInput>  inputs;    // max 4 base format; JSON extended allows more
    std::vector<RecipeOutput> outputs;

    float labor_requirement;          // workers at full output
    float energy_consumption;         // regional energy units per tick
    uint8_t min_tech_tier;
    float tech_output_bonus_per_tier; // e.g. 0.08 = 8% more output per tier above minimum
    std::string key_technology_node;  // R&D node_key whose maturation_level caps output quality
                                      // (see R&D doc §3.5 and ComputeOutputQuality below)
                                      // empty string = no maturation cap (commodity / mature recipes)
                                      // e.g. electric_vehicle recipe → "electric_vehicle"
                                      //      steel recipe → "" (no cap; process is mature)

    float waste_output_rate;          // 0.0–1.0; feeds environmental impact each tick
    float environmental_footprint;    // region condition delta per tick at full operation
    float explosion_risk_per_tick;    // probability of catastrophic facility incident each tick.
                                      // Read by random events system (TDD v23 §19, tick step 19).
                                      // 0.0 for standard facilities; elevated for criminal labs,
                                      // heavy chemical plants, other high-hazard facility types.
                                      // Not part of FacilitySignals — this is an event probability,
                                      // not an investigator-observable signal component.

    FacilitySignals signals;          // per-recipe base signal weights. Canonical struct: TDD v23 §16.
                                      // Universal — all facility types carry this field.
                                      // Legitimate operations have nonzero signals (power draw,
                                      // foot traffic, waste). Criminal facilities have elevated values.
                                      // base_signal_composite, scrutiny_mitigation, net_signal are
                                      // computed at runtime by tick step 13; do not populate from CSV.
};
```

---

### Facility Types Master List

All V1 facility types with baseline characteristics. Recipes are mapped to facilities via `facility_type_key`. Modders can add new facility types by adding rows to `facility_types.csv`.

| facility_type_key | category | base_cost | max_recipes | description |
|---|---|---|---|---|
| `mine` | extraction | 500000 | 3 | Resource extraction from geological deposits |
| `oil_well` | extraction | 750000 | 1 | Crude oil/natural gas extraction |
| `logging_camp` | extraction | 200000 | 2 | Timber harvesting |
| `farm` | agriculture | 150000 | 4 | Crop/livestock production; seasonal output model |
| `smelter` | processing | 400000 | 3 | Ore to metal processing |
| `refinery` | processing | 800000 | 4 | Crude to petroleum products |
| `chemical_plant` | processing | 600000 | 5 | Chemical manufacturing |
| `heavy_manufacturing` | manufacturing | 700000 | 3 | Large-scale assembly |
| `consumer_goods_factory` | manufacturing | 350000 | 4 | Consumer product manufacturing |
| `pharmaceutical_plant` | manufacturing | 900000 | 3 | Drug/medicine production |
| `office` | services | 100000 | 2 | Administrative/professional services |
| `restaurant` | services | 75000 | 1 | Food service |
| `retail_store` | services | 120000 | 2 | Consumer retail |
| `drug_lab` | criminal | 50000 | 3 | Illicit drug manufacturing; elevated signal weights |
| `distribution_point` | criminal | 30000 | 2 | Drug distribution |
| `counterfeiting_operation` | criminal | 80000 | 1 | Counterfeit currency production |
| `warehouse` | logistics | 200000 | 0 | Storage only; no recipes |
| `research_facility` | research | 500000 | 0 | R&D operations; no recipes |
| `power_plant` | infrastructure | 1000000 | 2 | Energy generation |

---

## Part 4 — Deep Supply Chain Recipes

*Quantities are preliminary estimates. Ratios represent design intent. Tuning happens during the simulation prototype phase against actual market price behavior.*

---

### Industry Chain: Steel and Vehicles

The vehicle chain runs 6+ facility stages from ore to finished car. At every stage there is a tradeable intermediate market.

**Stage 1: Iron ore extraction**
- Recipe key: `iron_mine_extraction`
- Inputs: (deposit draw)
- Outputs: `iron_ore` × 10/tick
- Labor: 50 | Energy: 2.0 | Tech tier: 1

**Stage 2: Coking coal extraction**
- Recipe key: `coal_mine_coking`
- Inputs: (deposit draw)
- Outputs: `coking_coal` × 8/tick
- Labor: 45 | Energy: 1.5 | Tech tier: 1

**Stage 3: Steel production**
- Recipe key: `steel_mill`
- Inputs: `iron_ore` × 5, `coking_coal` × 2
- Outputs: `steel` × 3 (primary), `slag` [byproduct, waste]
- Labor: 80 | Energy: 5.0 | Tech tier: 2

**Stage 4a: Steel sheet rolling**
- Recipe key: `steel_sheet_rolling`
- Inputs: `steel` × 2
- Outputs: `steel_sheet` × 1.8, `steel_wire_rod` × 0.2 [byproduct]
- Labor: 25 | Energy: 2.5 | Tech tier: 2

**Stage 4b: Steel casting**
- Recipe key: `foundry_steel_casting`
- Inputs: `steel` × 3, `industrial_gas` × 0.5
- Outputs: `steel_casting` × 2, `aluminum_casting` × 0 *(aluminum variant uses aluminum_ingot)*
- Labor: 35 | Energy: 3.5 | Tech tier: 2

**Stage 4c: Aluminum ingot (from bauxite)**
- Recipe key: `aluminum_smelter`
- Inputs: `bauxite` × 4
- Outputs: `aluminum_ingot` × 1
- Labor: 40 | Energy: 8.0 | Tech tier: 3
- *Highest energy consumption of any Stage 1 facility. Location is driven by energy price.*

**Stage 5a: Car body stamping**
- Recipe key: `body_stamping_plant`
- Inputs: `steel_sheet` × 3
- Outputs: `car_body_stamping` × 1
- Labor: 40 | Energy: 2.0 | Tech tier: 3

**Stage 5b: Chassis fabrication**
- Recipe key: `chassis_fabrication`
- Inputs: `structural_steel_section` × 2, `steel_casting` × 1
- Outputs: `car_chassis` × 1
- Labor: 30 | Energy: 2.5 | Tech tier: 3

**Stage 5c: Engine block casting and machining**
- Recipe key: `engine_block_manufacture`
- Inputs: `steel_casting` × 2, `aluminum_casting` × 1, `precision_machined_parts` × 1
- Outputs: `engine_block` × 1
- Labor: 60 | Energy: 3.0 | Tech tier: 3

**Stage 5d: Tire production**
- Requires: natural rubber or synthetic rubber (parallel supply options)
- Recipe key: `tire_plant`
- Inputs: `synthetic_rubber` × 2 (or `natural_rubber` × 2), `steel_wire_rod` × 0.5
- Outputs: `tires` × 4
- Labor: 50 | Energy: 2.0 | Tech tier: 2

**Stage 5e: Wiring harness**
- Recipe key: `wiring_harness_plant`
- Inputs: `copper_wire_rod` × 1, `plastics_polymer` × 0.5
- Outputs: `wiring_harness` × 1
- Labor: 60 (very labor-intensive; location follows cheap labor) | Energy: 0.8 | Tech tier: 2

**Stage 5f: Engine assembly**
- Recipe key: `engine_assembly`
- Inputs: `engine_block` × 1, `precision_machined_parts` × 1, `wiring_harness` × 0.3, `electronic_components` × 0.5
- Outputs: `engine_assembled` × 1
- Labor: 50 | Energy: 1.5 | Tech tier: 3

**Stage 5g: Safety glass**
- Recipe key: `safety_glass_plant`
- Inputs: `silica_sand` × 3, `limestone` × 0.5, `plastics_polymer` × 0.2
- Outputs: `safety_glass` × 2
- Labor: 20 | Energy: 3.0 | Tech tier: 3

**Stage 5h: Vehicle interior assembly**
- Recipe key: `interior_assembly`
- Inputs: `leather_goods` × 0.5 (or `woven_fabric` × 0.8), `plastics_polymer` × 1, `electronic_components` × 0.3
- Outputs: `vehicle_interior` × 1
- Labor: 40 | Energy: 0.8 | Tech tier: 2

**Stage 6: Final vehicle assembly**
- Recipe key: `vehicle_assembly_plant`
- Inputs: `engine_assembled` × 1, `car_chassis` × 1, `car_body_stamping` × 1, `vehicle_interior` × 1, `tires` × 4, `wheel_rim` × 4, `brake_system` × 1, `wiring_harness` × 1, `safety_glass` × 2
- Outputs: `passenger_car` × 1
- Labor: 120 | Energy: 3.0 | Tech tier: 3
- *9 sub-assembly inputs. Each is a separate market. Missing one halts production.*

---

### Industry Chain: Electronics and Semiconductors

**Stage 1: Silica sand + Rare earth extraction**
→ raw resources (see Tier 0)

**Stage 2: Silicon wafer production**
- Recipe key: `silicon_wafer_fab`
- Inputs: `silica_sand` × 3, `industrial_gas` × 1
- Outputs: `silicon_wafer` × 1
- Labor: 30 (skilled) | Energy: 5.0 | Tech tier: 4

**Stage 3: PCB blank production**
- Recipe key: `pcb_blank_plant`
- Inputs: `fiberglass` × 2, `copper_foil` × 1
- Outputs: `pcb_blank` × 3
- Labor: 20 | Energy: 1.5 | Tech tier: 3

**Stage 3b: Electronic components**
- Recipe key: `electronic_components_plant`
- Inputs: `silicon_wafer` × 1, `rare_earth_oxides` × 0.3, `copper_wire_rod` × 0.5
- Outputs: `electronic_components` × 2
- Labor: 40 (skilled) | Energy: 2.5 | Tech tier: 4
- *Feeds 20+ downstream products. Owning this is a supply chokepoint.*

**Stage 3c: Semiconductor chip fabrication**
- Recipe key: `semiconductor_fab`
- Inputs: `silicon_wafer` × 1, `rare_earth_oxides` × 0.5, `industrial_gas` × 2
- Outputs: `semiconductor_chip` × 0.5 *(low yield; significant scrap)*
- Labor: 100 (highly specialized) | Energy: 8.0 | Tech tier: 5
- *Highest tech tier in the base game. Requires the cleanroom facility type.*

**Stage 4a: PCB population**
- Recipe key: `pcb_assembly`
- Inputs: `pcb_blank` × 1, `electronic_components` × 1, `solder` × 0.2
- Outputs: `pcb_populated` × 1
- Labor: 30 | Energy: 1.0 | Tech tier: 3

**Stage 4b: Display panel production**
- Recipe key: `display_panel_plant`
- Inputs: `silica_sand` × 2, `rare_earth_oxides` × 0.2, `plastics_polymer` × 0.5
- Outputs: `display_panel` × 1
- Labor: 50 | Energy: 4.0 | Tech tier: 4

**Stage 4c: Battery cell production**
- Recipe key: `battery_cell_plant`
- Inputs: `lithium_carbonate` × 1, `cobalt_refined` × 0.5, `graphite_ore` × 1, `aluminum_sheet` × 0.3
- Outputs: `battery_cell` × 4
- Labor: 40 | Energy: 3.0 | Tech tier: 4
- *Lithium and cobalt are rare Tier 0 resources — controlling deposits creates leverage.*

**Stage 5: Final device assembly**
- Recipe key: `mobile_phone_assembly`
- Inputs: `pcb_populated` × 1, `display_panel` × 1, `battery_pack` × 1, `camera_module` × 1, `plastics_polymer` × 0.3
- Outputs: `mobile_phone` × 1
- Labor: 50 (can be highly automated) | Energy: 1.0 | Tech tier: 4

---

### Industry Chain: Pharmaceuticals (Legitimate and Criminal)

The pharmaceutical chain and the drug synthesis chain share the same precursor chemical inputs. The divergence point is Stage 3 — whether the precursor goes into an API synthesis reactor under pharmaceutical-grade conditions or into an illicit lab. The same investigator attention follows precursor purchases regardless of declared end use.

**Stage 1–2: Chemical processing**
→ `drug_precursors` produced as a byproduct of `chemical_plant` (see Tier 1)

**Stage 3a (Legitimate): API synthesis**
- Recipe key: `api_synthesis_plant`
- Inputs: `drug_precursors` × 2
- Outputs: `active_pharma_ingredient` × 1
- Labor: 50 (chemists) | Energy: 2.0 | Tech tier: 4
- Waste: 0.3 | Regulatory scrutiny: HIGH

**Stage 3b (Criminal): Direct synthesis**
- Recipe key: `drug_lab_meth`
- Inputs: `drug_precursors` × 2
- Outputs: `methamphetamine` × 1
- Labor: 5 | Energy: 1.5 | Tech tier: 2
- Signals: `chemical_waste_signal: 0.9`, `noise_signal: 0.85` | `explosion_risk_per_tick: 0.002`

**Stage 3c (Criminal): Synthetic opioids**
- Recipe key: `drug_lab_opioid`
- Inputs: `drug_precursors` × 1.5
- Outputs: `synthetic_opioid` × 1
- Labor: 5 | Energy: 1.0 | Tech tier: 3
- Signals: `chemical_waste_signal: 0.6`, `noise_signal: 0.3` | `explosion_risk_per_tick: 0.0005`
- *More concealable than meth. Higher tech tier required for synthesis.*

**Stage 4 (Legitimate): Drug formulation**
- Recipe key: `pharmaceutical_formulation`
- Inputs: `active_pharma_ingredient` × 1, `plastics_polymer` × 0.5
- Outputs: `pharmaceutical_otc` × 2 (or `pharmaceutical_rx` × 1.5)
- Labor: 40 | Energy: 1.5 | Tech tier: 3

---

### Industry Chain: Petroleum and Chemicals

**Stage 1: Oil extraction**
- Recipe key: `oil_wellfield`
- Inputs: (deposit draw)
- Outputs: `crude_oil` × 15 (primary), `natural_gas` × 8 [byproduct]
- Labor: 20 | Energy: 3.0 | Tech tier: 2

**Stage 2: Petroleum refinery**
*All outputs are produced simultaneously. The refinery cannot choose what to produce.*
- Recipe key: `petroleum_refinery`
- Inputs: `crude_oil` × 10
- Outputs:
  - `gasoline` × 3.5 [primary]
  - `diesel` × 2.5 [primary]
  - `naphtha` × 1.5 [byproduct fraction 0.3]
  - `jet_fuel` × 0.8 [byproduct fraction 0.2]
  - `heavy_fuel_oil` × 0.8 [byproduct fraction 0.2]
  - `lpg` × 0.4 [byproduct fraction 0.1]
  - `bitumen` × 0.3 [byproduct fraction 0.1]
  - `lubricants` × 0.2 [byproduct fraction 0.1]
  - `petroleum_coke` × 0.2 [byproduct, waste=true]
  - `paraffin_wax` × 0.1 [byproduct fraction 0.05]
- Labor: 60 | Energy: 4.0 (self-sourced) | Tech tier: 3

**Stage 3: Naphtha cracker**
- Recipe key: `naphtha_cracker`
- Inputs: `naphtha` × 3
- Outputs: `ethylene` × 1.5, `propylene` × 1, `benzene` × 0.5 [byproduct]
- Labor: 40 | Energy: 4.0 | Tech tier: 3

**Stage 4a: Plastics plant**
- Recipe key: `plastics_plant`
- Inputs: `ethylene` × 2 (or `propylene` × 2)
- Outputs: `plastics_polymer` × 1.5
- Labor: 20 | Energy: 2.5 | Tech tier: 3

**Stage 4b: Synthetic rubber plant**
- Recipe key: `synthetic_rubber_plant`
- Inputs: `benzene` × 1, `ethylene` × 1
- Outputs: `synthetic_rubber` × 1
- Labor: 15 | Energy: 2.0 | Tech tier: 3

**Stage 4c: Fertilizer plant**
- Recipe key: `fertilizer_plant`
- Inputs: `natural_gas` × 3, `potash` × 1, `phosphate_rock` × 1
- Outputs: `ammonia` × 1.5 [intermediate], `fertilizer_npk` × 3
- Labor: 25 | Energy: 2.0 | Tech tier: 2

**Stage 4d: Chemical plant (general)**
- Recipe key: `chemical_plant_general`
- Inputs: `naphtha` × 3, `sulfur` × 1, `salt` × 0.5
- Outputs: `sulfuric_acid` × 1.5, `chlorine` × 0.8, `drug_precursors` × 0.5 [byproduct fraction 0.2, environmental_waste=true]
- Labor: 50 | Energy: 4.0 | Tech tier: 3
- Waste: 0.5 (chemical waste — regulatory scrutiny)

---

### Industry Chain: Agriculture and Food

**Farm (Grain)**
- Inputs: `fertilizer_npk` × 1, climate-sourced water
- Outputs: `wheat` × 15 / `corn` × 15 / `soybeans` × 12 (crop set at construction)
- Labor: 20 | Energy: 0.5 | Tech tier: 1
- Climate modifier: output × (1.0 − climate_stress × 0.6)

**Grain Mill**
- Inputs: `wheat` × 5
- Outputs: `flour` × 4
- Labor: 15 | Energy: 0.8 | Tech tier: 1

**Oilseed Processor**
- Inputs: `soybeans` × 5 (or `rapeseed` × 5)
- Outputs: `soy_oil_crude` × 1.5, `soy_meal` × 3 [byproduct]

**Oil Refinery (Vegetable)**
- Inputs: `soy_oil_crude` × 2 (or `palm_oil_crude` × 2)
- Outputs: `refined_vegetable_oil` × 1.5, `soap_base` × 0.2 [byproduct]

**Sugar Mill → Refinery**
- Stage 1: `sugarcane` × 10 → `raw_sugar` × 1 (mill)
- Stage 2: `raw_sugar` × 2 → `refined_sugar` × 1.8 (refinery)

**Meat Processing**
- Inputs: `cattle_live` × 2 → `beef` × 1.5 + `leather_raw` × 0.3 [byproduct]
- Inputs: `pigs_live` × 4 → `pork` × 3
- Inputs: `poultry_live` × 6 → `poultry_meat` × 5

**Dairy Processing**
- Inputs: `milk_raw` × 5
- Outputs: `dairy_products` × 1.5 [primary], `dairy_consumer` × 0.5 [ready-to-retail]

**Food Processing Plant**
- Inputs: `flour` × 2, `refined_vegetable_oil` × 0.5, `refined_sugar` × 0.3, `flavor_concentrates` × 0.2
- Outputs: `packaged_food` × 3
- Labor: 40 | Energy: 1.5 | Tech tier: 1

**Brewery**
- Inputs: `malt` × 2, `hops` × 0.3
- Outputs: `beer_wort` × 3 [intermediate], `beer` × 2.5 [finished]
- Labor: 15 | Energy: 1.0 | Tech tier: 1

**Distillery**
- Inputs: `corn` × 3 (or `barley` × 3), `beer_wort` × 1
- Outputs: `distilled_spirits_base` × 1.5 → `spirits` × 1

---

### Supply Chain Depth Summary

| Industry | Ore to Product Stages | Sub-assembly Markets Created |
|---|---|---|
| Passenger car | 6 extraction + processing stages, 1 assembly | engine, chassis, body stamping, tires, glass, harness, interior, brakes |
| Consumer electronics | 5 stages | silicon wafer, PCB blank, populated PCB, display panel, battery pack |
| Semiconductors | 5 stages + cleanroom | silicon wafer, semiconductor chip (apex component) |
| Pharmaceuticals | 4 stages | drug precursors (shared), API, formulated drug |
| Petroleum products | 2 stages, 10 simultaneous outputs | gasoline, diesel, naphtha, jet fuel, HFO, LPG, bitumen, lubricants, wax, coke |
| Specialty steel | 4 stages | steel casting, steel sheet, structural section, precision parts |

---

## Part 5 — Moddability Specification

### Design Goals

Modders should be able to:

1. **Add new goods** without touching existing goods or their IDs
2. **Add new recipes** that use existing goods and new goods
3. **Add new facility types** that run new recipes
4. **Override base game recipes** to change conversion rates, energy costs, input ratios
5. **Add new supply chain tiers** that sit above or between existing tiers
6. **Add new industries** with no overlap to existing chains

Modders should **not** need to:
- Modify engine code
- Recompile the game
- Know C++

---

### Mod Structure

```
/mods/
  MyMod/
    mod.json                   ← manifest: name, version, author, load_after: [...]
    goods/
      my_new_goods.csv         ← adds new goods; keys must be unique
    recipes/
      my_new_recipes.csv       ← adds new recipes using old and new good keys
    facilities/
      my_new_facility_types.csv
    overrides/
      recipes/
        steel_mill.json        ← overrides base game recipe; same key = replacement
```

**mod.json example:**
```json
{
  "mod_name": "Advanced Automotive",
  "version": "1.0.0",
  "author": "modder_name",
  "load_after": ["base_game"],
  "description": "Adds luxury and electric vehicle chains with expanded component depth"
}
```

---

### Key Uniqueness and Conflict Resolution

Good keys must be globally unique. If two mods define a good with the same key, the engine logs a conflict warning and uses the **last-loaded** definition (mods are loaded in `load_after` dependency order).

Recipe keys use the same conflict resolution. A mod that intentionally wants to override a base game recipe uses the exact same key — this is the intended override mechanism.

Good keys should be namespaced to avoid accidental conflicts:
```
base_game:   iron_ore, steel, passenger_car
mod author:  advanced_auto:carbon_fiber_chassis, advanced_auto:v12_engine
```

The engine strips the namespace prefix for display purposes but uses the full namespaced key for lookup.

---

### Extended JSON Recipe Format

The base CSV recipe format supports 4 inputs and 4 outputs. For more complex recipes, use the JSON format:

```json
{
  "recipe_key": "advanced_auto:hypercar_assembly",
  "facility_type_key": "advanced_auto:hypercar_factory",
  "display_name": "Hypercar Assembly",
  "inputs": [
    { "good_key": "advanced_auto:carbon_fiber_monocoque", "quantity": 1.0 },
    { "good_key": "engine_assembled", "quantity": 1.0 },
    { "good_key": "advanced_auto:active_aerodynamics_unit", "quantity": 1.0 },
    { "good_key": "advanced_auto:racing_brake_system", "quantity": 1.0 },
    { "good_key": "tires", "quantity": 4.0, "quality_weight": 0.15 },
    { "good_key": "leather_goods", "quantity": 1.0, "is_optional": true, "optional_quality_penalty": 0.1 }
  ],
  "outputs": [
    { "good_key": "advanced_auto:hypercar", "quantity": 1.0 }
  ],
  "labor": 80,
  "energy": 3.0,
  "min_tech_tier": 5,
  "tech_output_bonus_per_tier": 0.0,
  "notes": "Output is 1 car regardless of tech tier — quality is the variable, not quantity"
}
```

---

### What the Engine Exposes to Mods

At load time the engine makes available:
- All loaded good keys and their resolved uint32 IDs
- All loaded recipe keys
- All facility type keys
- Current regional market data (read-only) for scripted scenario mods

The engine does **not** expose NPC behavior logic, simulation tick internals, or save file structures to mods. Mods are content mods, not behavior mods.

Behavior modding (new NPC motivation types, new consequence types, new facility design mechanics) requires a separate modding API layer that is not in scope for V1 but should be architecturally planned for — the enum-in-code structures for these systems should be moved to data files in a future pass.

---

### Modding and the Bootstrapper

The Bootstrapper generates interface specs and data structure headers only for base game content. Mods register their content at runtime — the Bootstrapper does not need to know about mods.

The `GoodType` enum in the Technical Design document — previously specified as a hardcoded C++ enum — should be replaced with a runtime-populated table loaded from the goods CSV files. The Bootstrapper should generate the loading infrastructure, not the enum values.

This is a **change to the TDD** required by this moddability architecture. The good_id mapping table becomes a runtime construct rather than a compile-time enum. Performance implications are minor: the table is populated once at startup and queried by integer index thereafter.

---

## Part 6 — Open Questions

These are design decisions not yet made. None block V1 simulation architecture but some block content production.

1. **Recipe quantity tuning.** All quantities are preliminary. Tuning happens in the prototype phase against actual price behavior. The structure is final; the numbers are not.

2. **Quality system.** Capitalism Lab has a quality rating per good that affects downstream product quality and retail price. EconLife does not yet specify whether goods have individual quality ratings or whether quality is a facility-level property. This affects recipe design significantly — a vehicle made from low-quality steel should be a worse vehicle. Placeholder: quality is a per-facility aggregate float, not per-unit.

3. ~~**R&D as recipe input.**~~ **RESOLVED (Pass 3 Session 17):** Technology nodes are not recipe inputs — they are prerequisite holdings. `FacilityRecipe.key_technology_node` (new field) identifies which R&D node's `maturation_level` caps output quality for that recipe. `min_tech_tier` remains as the hard facility gate. The two mechanisms are complementary: `min_tech_tier` controls whether the recipe can run at all; `maturation_level` controls how well it runs.

4. **Agricultural seasonal production.** Farms produce continuously in the current model. Real agriculture is seasonal — harvest once per year, store until used. Whether EconLife models seasonality or abstracts it to a continuous rate is unresolved. Seasonality creates warehouse demand and price volatility, which is interesting. Continuous production is simpler to simulate. Design decision needed.

5. **Perishable goods in transit.** `is_perishable = true` is defined in the goods schema but the decay mechanic is not specified. How fast does beef degrade? Does it degrade per tick in transit or only above a tick threshold?

6. ~~**Byproduct market flooding.**~~ **RESOLVED (Pass 3 Session 6G):** No price floor. Byproducts can fall to near-zero. This creates an economic incentive for downstream capacity to co-locate with byproduct producers — exactly as happened historically when petrochemical plants co-located with refineries. If no downstream capacity exists, disposal costs apply for goods with `environmental_waste = true`. The frustration is the intended design signal.

7. ~~**Criminal goods in the same market.**~~ **RESOLVED (Pass 3 Session 6G):** Formal and criminal markets are separate layers. Criminal goods (Tier 4) exist only in the informal market layer. The `RegionalMarket` struct holds both for architectural consistency, but the formal market display never shows criminal goods. Display separation is a UI concern, not an architecture concern.

8. ~~**Sub-assembly spot trading.**~~ **RESOLVED (Pass 3 Session 6G):** Tier 2 sub-assemblies trade on open regional markets. Criminal goods (Tier 4) exist only in the informal market layer. Open market for all Tier 2 goods — this creates the supplier leverage and B2B dynamics the GDD describes in Section 8.

---

*EconLife — Commodities and Factories v2.1 — Pass 3 Session 6G complete*
*Architecture and goods list are authoritative. Recipes are design-intent estimates requiring prototype tuning.*
*Moddability specification is V1-targeted — behavior modding deferred.*

---

## Part 6A — Precision Specifications (Pass 5 Session 7)

### COM-P1: Drug Lab Signal Notation — Standardized

All drug lab recipes must standardize signal fields to use numeric format: `signal_name: float_value`.

Examples:
- `chemical_waste_signal: 0.8` (scale 0.0–1.0, where 1.0 = maximum visibility signal)
- `power_draw: 0.6`
- `noise_signal: 0.7`
- `foot_traffic_signal: 0.2`
- `financial_anomaly_signal: 0.1`

Replace any categorical labels (high/medium/low) with numeric values using this mapping:
- `LOW = 0.3, MEDIUM = 0.5, HIGH = 0.7, VERY HIGH = 0.9`

All drug labs must use the same canonical field names from `FacilitySignals` struct (TDD v23 §16).

---

### COM-P2: Byproduct Hazmat Disposal Cost Model

Facilities producing environmental waste (`environmental_waste = true`) accumulate disposal liability.

```
disposal_cost_per_tick = waste_volume × DISPOSAL_COST_PER_UNIT
  × regional_regulation_multiplier

DISPOSAL_COST_PER_UNIT = 50 (currency units per unit volume per tick)
regional_regulation_multiplier = 1.0 to 3.0 (varies by province environmental regulations)
```

**Ownership & accountability:** Cost is paid by facility owner (player or NPC).

**Unpaid disposal consequences:** If disposal costs are not paid:
- Facility generates `environmental_violation` evidence token
- Evidence actionability = 0.6 (moderately investigable)
- Violation accrues per unpaid tick, stacking evidence over time
- Regional environmental condition worsens

---

### COM-P3: Quality Weights — Default Behavior

When `quality_weights` are not specified in a recipe CSV, the engine applies **equal weighting** across all inputs.

Formula for default weight:
```
default_weight_per_input = 1.0 / number_of_inputs
```

Example — 3-input recipe with no weights:
```
inputs: [iron_ore, coking_coal, limestone]
quality_weights: [0.333, 0.333, 0.333]  (computed, not specified)
```

**Schema note:** The `quality_weights` field is **optional** in recipe CSV. Absence means equal weighting. Extended JSON format allows explicit per-input specification; see Part 5 §Extended JSON Recipe Format.

---

### COM-P4: Labor Requirement Units — PERSON-TICKS

Labor requirements in recipes are specified in **person-tick units**.

Definition: A labor requirement of `50` means 50 workers must be assigned to the facility for one complete production cycle (one tick).

**Calculation:**
```
labor_cost_per_tick = assigned_workers × regional_wage_rate

labor_saturation = min(1.0, assigned_workers / recipe.labor_requirement)
  (clamped to [0.0, 1.0])

production_output *= labor_saturation
```

A facility with `labor_requirement = 50` operating at full efficiency requires exactly 50 assigned workers. Partial staffing (e.g., 30 workers) reduces output by 40% (`30/50 = 0.6 saturation`).

---

### COM-P5: Energy Self-Sourcing

Facilities that produce their own energy inputs (e.g., refinery producing fuel it consumes) may declare `energy_self_sourced: true`.

**Effect on cost:**
```
self_sourced_fraction = min(1.0, own_energy_output / energy_requirement)

effective_energy_cost = energy_requirement × (1.0 - self_sourced_fraction)
                        × regional_energy_price
```

Example: A refinery produces 4.0 units of energy (fuel) per tick and consumes 4.0 units, so `self_sourced_fraction = 1.0` and `effective_energy_cost = 0`.

A refinery that consumes 5.0 but produces 4.0 has `self_sourced_fraction = 0.8` and pays for 20% of its energy need externally.

---

### COM-P6: Recipe Tech Tier Ceiling — Unbounded Above Minimum

Recipes have `min_tech_tier` but **no maximum tier**. Output quality improvement from tech tier is **unbounded**.

Formula:
```
quality_bonus = (facility_tech_tier - recipe.min_tech_tier)
                × TECH_TIER_QUALITY_INCREMENT

TECH_TIER_QUALITY_INCREMENT = 0.05 (default; can be overridden per recipe)
```

This allows advanced technology to continuously improve older recipes. A Tier 1 recipe run in a Tier 5 facility benefits from `(5 - 1) × 0.05 = 0.20` quality bonus (capped by maturation ceiling and output ceiling, but no tier-based cap exists).

---

### COM-I1: Modding API Fields — Goods CSV Extensions

Additional moddable fields in goods CSV (all optional):

| Field | Type | Default | Description |
|---|---|---|---|
| `quality_premium_coeff` | float | 1.0 | Multiplier for price premium from quality (modifies `quality_premium_coefficient` in §Part 7) |
| `buyer_segments` | string (comma-separated) | "all" | List of BuyerType values; controls which NPC buyer segments purchase this good (e.g., "price_rational,performance_rational") |

Example row:
```csv
key,display_name,tier,category,base_price,...,quality_premium_coeff,buyer_segments
semiconductor_chip,Semiconductor Chip,2,electronics,450.00,...,0.65,performance_rational,brand_loyal
```

---

### COM-I2: Criminal Maturation Ceiling

Criminal goods use the same maturation ceiling mechanic as legitimate goods.

**Key technology nodes for criminal goods:**
- `synthesis_optimization` — applies to all chemical drug recipes (meth, opioids, etc.)
- `cutting_agent_chemistry` — applies to powder drug cutting and adulterant selection
- `designer_drug_synthesis` — applies to designer drug creation (R&D-intensive)

These nodes follow standard R&D progression (maturation rate, research cost, breakthrough probability) but are **only accessible via criminal research facilities** (see R&D document Part 3.5).

Output quality for criminal recipes:
```
criminal_output_quality = min(weighted_input_quality,
                              tech_tier_ceiling,
                              actor_state.maturation_of(key_technology_node))
```

If an actor has not researched the key_technology_node, `maturation_of()` returns 0.0 and the recipe cannot produce above quality 0.0 (complete failure).

---

### COM-I3: Technology Node Registry Validation

Recipe CSV references to `key_technology_node` are validated against the Technology Node Registry maintained in the R&D document (§Technology Tree).

**Validation behavior:**
- At data load time, engine cross-references each recipe's `key_technology_node` against the registry
- If `key_technology_node` is invalid (typo, unknown node): emit **MOD_LOAD_WARNING** (not fatal error)
- Affected recipe falls back to `min_tech_tier = 0` and no maturation ceiling (`maturation_ceiling = 1.0`)
- Warning includes mod name, recipe key, and invalid node name

This allows modders to reference R&D nodes that will be added later without blocking load.

---

### COM-SC1: Facility Types Master List — [Moved to Part 3]

See Part 3 §Facility Types Master List for the complete table of V1 facility types.

---

### COM-SC2: Coca_leaf and Poppy — [Resolved in Part 2]

Both `coca_leaf` and `poppy` are now listed in Tier 0 Biological resources with [V1] scope. See Part 2 §Tier 0 — Raw Resources §Biological.

---

### COM-S1: Circular Recipe Dependency Detection

The recipe data loader must perform **topological cycle detection** at load time.

Algorithm:
1. Build directed graph: nodes = recipes, edges = input→output relationships
2. Perform topological sort on dependency graph
3. If topological sort fails (cycle detected):
   - Log error with full cycle path (e.g., "recipe_a → recipe_b → recipe_a")
   - Reject offending recipes (do not load them)
4. Validation applies to both base game and mod-loaded recipes

**Example cyclic chain (rejected):**
```
Recipe A: inputs=[X], outputs=[Y]
Recipe B: inputs=[Y], outputs=[Z]
Recipe C: inputs=[Z], outputs=[X]  ← Cycle detected; all three rejected
```

This prevents runtime deadlock where no recipe can ever execute.

> Test: When a mod introduces a circular recipe dependency (Good A requires Good B, Good B requires Good A), the recipe data loader detects the cycle during topological sort and rejects BOTH recipes with a RECIPE_CYCLE_ERROR log entry. The game continues loading with the remaining valid recipes.
>
> Seed setup: Base game loads normally. Load mod that adds recipe_x (input: good_alpha → output: good_beta) and recipe_y (input: good_beta → output: good_alpha). Assert: both recipe_x and recipe_y rejected. RECIPE_CYCLE_ERROR logged with cycle path. All base game recipes unaffected.

---

### COM-S2: Mod Load Order & Conflict Resolution

**Load order:** Mods are loaded alphabetically by `mod_id`.

**Last-loaded wins:** If two mods define the same key (good or recipe), the last-loaded mod's definition overrides earlier definitions. This allows intentional override-by-name.

**Conflict notification:** When an override occurs, the loader emits `MOD_CONFLICT_WARNING` with:
- `mod_a` (earlier, overridden)
- `mod_b` (later, winning)
- `key` (good or recipe key)
- `field` (which CSV column was overridden)

**User resolution:** The `settings.json` file can specify explicit mod priority:
```json
{
  "mod_load_order": ["base_game", "advanced_auto", "renewable_energy", "water_systems"]
}
```

Explicit load order overrides alphabetical sorting. Any mod not listed defaults to alphabetical position after all listed mods.

> Test: When two mods both define a good with the same good_key, the mod loaded LAST (alphabetically by mod_id) wins. A MOD_CONFLICT_WARNING is emitted containing {mod_a_id, mod_b_id, good_key, conflicting_fields}.
>
> Seed setup: mod_alpha defines steel with base_price = 100. mod_beta defines steel with base_price = 150. Both loaded. Assert: steel.base_price = 150 (mod_beta wins alphabetically). MOD_CONFLICT_WARNING emitted with {mod_alpha, mod_beta, steel, base_price}.

---

### COM-S3: Agricultural Production — SEASONAL WITH CLIMATE

**RESOLVED (Pass 5 Session 7):** Farm production is SEASONAL, not continuous.

**Model:**
Each farm recipe has:
- `growing_season_start` (tick-of-year, 1–360)
- `growing_season_end` (tick-of-year, 1–360)

**Output calculation:**
```
production_output = 0 outside growing season

within_season:
  output = base_output × seasonal_modifier(current_tick)

seasonal_modifier(tick) = bell curve peaking at season midpoint
  (cosine-based interpolation from 0 → 1 → 0)
```

**Climate sourcing:**
Growing season parameters are seeded from province climate data (WorldGen Stage 4: precipitation, temperature indices). Provinces in tropical climates may have year-round growing seasons (`start=1, end=360`).

**Example:**
- Temperate wheat: `start=90 (Mar), end=270 (Sep)` — 6-month season
- Tropical cacao: `start=1, end=360` — year-round
- Cold-weather apple: `start=150, end=240` — 3-month season

> Test: When a farm facility is in a province with growing_season_start = 90 and growing_season_end = 270, production output = 0 for ticks outside this range (ticks 1-89 and 271-360) and follows a bell-curve seasonal_modifier within the range, peaking at the season midpoint (tick 180).
>
> Seed setup: Farm with base_output = 100 in temperate province (season 90-270). Assert: output at tick 45 = 0. Output at tick 90 > 0 (season start). Output at tick 180 = peak (seasonal_modifier ≈ 1.0). Output at tick 270 > 0 but declining. Output at tick 300 = 0 (season ended). Total annual output < 360 × base_output (seasonal reduction).

---

## Part 7 — Quality System

### Why Quality Is Not One Thing

The obvious model — every good has a quality float, high quality costs more — misses the interesting dynamics. Real markets have at least three distinct quality-related phenomena that interact in different ways depending on good type and buyer type:

**1. Grade / Specification Quality**
Objective, measurable, agreed-upon by both parties before purchase. High-grade copper ore has a higher copper content percentage. A Tier-5 semiconductor fab produces chips with smaller process nodes and lower defect rates. These quality differences directly affect downstream efficiency in a way that B2B buyers can calculate and will pay for.

**2. Brand Equity / Reputation**
Accumulated trust built (slowly) through consistent delivery of quality and destroyed (quickly) by failure. AMD has been producing technically superior CPUs for eight years and still only holds ~25% market share. Intel built 30+ years of brand equity in data centers and enterprises don't switch overnight even when the benchmark numbers favor AMD. Brand equity is separate from current product quality — it is the market's memory of past quality.

**3. Price Elasticity (Demand Inelasticity)**
The degree to which a buyer continues purchasing regardless of price. High brand equity reduces price elasticity — loyal customers absorb price increases. Luxury goods exhibit a paradox: below a certain price floor, demand *drops* because the price signal is itself part of the quality signal.

These three mechanics interact:
- Quality (spec) → over time → Brand equity
- Brand equity → demand inelasticity
- Demand inelasticity → pricing power → premium margin
- Quality failure → brand equity damage (faster than it was built)
- Price too low for premium product → brand damage (Veblen effect)

---

### Quality as a Property of Production Batches

Quality in EconLife is a float (0.0–1.0) attached to a production batch — not to the good type itself. The same good key (`steel`) can have quality 0.35 (basic grade, blast furnace) or quality 0.82 (specialty steel, electric arc furnace with precise alloying). These are different market goods in practice, trading at different prices, even though they share a key.

```cpp
struct ProductionBatch {
    uint32_t good_id;
    float    quantity;
    float    quality;            // 0.0–1.0; set by producing facility
    uint32_t producer_id;        // facility or NPC business that made this
    uint32_t produced_tick;
};
```

The regional market tracks a **quality-weighted average price** in addition to spot price:

```cpp
struct RegionalMarket {
    // ... existing fields ...
    float quality_weighted_avg_price;  // spot_price × average quality of recent batches
    float market_quality_avg;          // rolling average quality of goods in this market
    float quality_premium_coefficient; // how much each quality point is worth in price
                                       // varies by good category (see table below)
};
```

**Price effect of quality:**
```
effective_price = spot_price × (1.0 + quality_premium_coefficient × (batch_quality - market_quality_avg))
```

A batch with quality above the market average gets a premium. Below average gets a discount. The premium coefficient varies dramatically by good type.

---

### Quality Premium Coefficients by Good Category

| Category | Quality Premium Coeff. | Real-world analog | Notes |
|---|---|---|---|
| Commodities — geological raw | 0.05–0.15 | High-grade iron ore vs. low-grade | Small premium; processed out anyway |
| Commodities — agricultural raw | 0.10–0.25 | Arabica vs. Robusta coffee | Grade system; known to buyers |
| Processed materials (metals) | 0.15–0.30 | Specialty steel vs. standard | B2B buyers pay for consistent spec |
| Industrial components | 0.25–0.50 | Precision bearings, aerospace grade fasteners | Tolerance matters; high switching cost once specified |
| Sub-assemblies — electronics | 0.40–0.80 | Intel vs. AMD per benchmark | Performance is measurable; buyers are rational |
| Sub-assemblies — mechanical | 0.20–0.45 | Premium engine vs. budget engine | Hard to assess without testing; brand substitutes |
| Consumer electronics | 0.50–1.20 | iPhone vs. generic Android | Brand + quality; high premium possible |
| Consumer vehicles | 0.40–1.80 | Toyota vs. Rolls-Royce | Extreme range; luxury segment Veblen-adjacent |
| Pharmaceuticals | 0.30–0.60 | Generic vs. branded drug | Regulatory equivalence limits premium; brand matters anyway |
| Luxury goods | 1.00–3.00+ | Hermès, Rolex | Price is the quality signal; premium is the product |
| Food and beverages | 0.15–0.60 | Häagen-Dazs vs. generic ice cream | High variance by segment |
| Criminal goods | 0.30–0.80 | Street quality vs. pharmaceutical-grade | Purity is the spec; buyers will test |

---

### Quality Propagation Through the Supply Chain

Output quality is a weighted function of input qualities and facility tech tier.

```cpp
float ComputeOutputQuality(
    const std::vector<RecipeInput>& inputs,
    const std::vector<float>& input_qualities,  // parallel array
    const FacilityRecipe& recipe,
    uint8_t facility_tech_tier,
    const ActorTechnologyState& actor_state     // holder of this facility's business
) {
    // Step 1: Weighted average of input quality contributions
    float input_quality_sum = 0.0f;
    float weight_sum = 0.0f;
    for (int i = 0; i < inputs.size(); i++) {
        input_quality_sum += input_qualities[i] * recipe.quality_weights[i];
        weight_sum += recipe.quality_weights[i];
    }
    float weighted_input_quality = input_quality_sum / weight_sum;

    // Step 2a: Tech tier sets a quality ceiling
    // Below min_tier: cannot operate
    // At min_tier: quality ceiling = TECH_QUALITY_CEILING_BASE
    // Each tier above min: ceiling rises by TECH_QUALITY_CEILING_STEP
    float tier_ceiling = TECH_QUALITY_CEILING_BASE
        + TECH_QUALITY_CEILING_STEP * (facility_tech_tier - recipe.min_tech_tier);
    tier_ceiling = std::min(tier_ceiling, 1.0f);

    // Step 2b: Actor maturation level further caps quality for technology-intensive recipes.
    // Commodity recipes (key_technology_node == "") are unaffected (maturation_ceiling = 1.0).
    // For technology-intensive recipes, the actor must have researched the node
    // (maturation_of() returns 0.0 if not held → actor cannot produce above 0.0 quality).
    float maturation_ceiling = 1.0f;
    if (!recipe.key_technology_node.empty()) {
        maturation_ceiling = actor_state.maturation_of(recipe.key_technology_node);
    }

    float quality_ceiling = std::min(tier_ceiling, maturation_ceiling);
    quality_ceiling = std::min(quality_ceiling, 1.0f);

    // Step 3: Output quality = min(weighted inputs, ceiling)
    // Tech tier cannot conjure quality from bad inputs.
    // But neither bad tech nor low maturation can be overcome by good inputs.
    float output_quality = std::min(weighted_input_quality, quality_ceiling);

    return output_quality;
}
```

**Named constants:**
```
TECH_QUALITY_CEILING_BASE = 0.55     // At minimum tech tier, max achievable quality is 0.55
TECH_QUALITY_CEILING_STEP = 0.09     // Each tech tier above minimum raises ceiling by 0.09
// At min_tier + 5: ceiling = 0.55 + 0.45 = 1.00 (theoretical maximum)
```

**Practical implications:**

A Tier-1 steel mill can produce quality at most 0.55, regardless of how good the iron ore is. A Tier-4 steel mill can produce quality up to 0.91. This makes tech tier investment meaningful beyond just output quantity — it's the only way to access the high end of quality-sensitive markets.

**Maturation coupling:** For technology-intensive recipes (those with a non-empty `key_technology_node`), the quality ceiling is further capped by the actor's `maturation_level` on that node. A pioneer who has spent years maturing their EV platform to 0.80 will produce higher quality than a late entrant who just licensed the technology at 0.48 — even if both run identical tier facilities with identical inputs. This is the persistent quality advantage the R&D lifecycle model is designed to produce. Commodity recipes (steel, cement, basic chemicals) have `key_technology_node = ""` and are not subject to maturation capping.

**Quality weights per recipe** (added to recipe specification):
- Vehicle assembly: engine_assembled weight 0.35, chassis 0.20, body_stamping 0.10, tires 0.10, electronics 0.15, other 0.10
- Consumer electronics: semiconductor_chip weight 0.45, display_panel 0.25, pcb_populated 0.20, other 0.10
- Steel mill: iron_ore quality weight 0.60, coking_coal 0.40
- Pharmaceutical: active_pharma_ingredient weight 0.75, formulation 0.25

---

### Brand Equity: The Market's Memory

Brand equity is not a property of the good — it is a property of the **producer**. An NPCBusiness accumulates brand equity in each product category it operates in. So does the player's businesses.

```cpp
struct BrandEquityEntry {
    uint32_t good_id;               // which product category
    float    brand_rating;          // 0.0–1.0; the accumulated reputation
    float    quality_expectation;   // what buyers expect; converges toward recent quality avg
    uint32_t transactions_count;    // how many market transactions contributed
    uint32_t last_quality_failure_tick; // tick of most recent quality failure event
};

struct NPCBusiness {
    // ... existing fields ...
    std::vector<BrandEquityEntry> brand_equity;
};
```

**Brand equity update per tick:**

```
quality_gap = delivered_quality - quality_expectation

if quality_gap > 0:
    brand_delta = quality_gap × BRAND_BUILD_RATE         // builds slowly
else:
    brand_delta = quality_gap × BRAND_DAMAGE_RATE        // destroys quickly
                                                         // BRAND_DAMAGE_RATE > BRAND_BUILD_RATE

brand_rating = clamp(brand_rating + brand_delta, 0.0, 1.0)
quality_expectation converges toward delivered_quality at EMA_RATE
```

**Named constants:**
```
BRAND_BUILD_RATE   = 0.002 / tick    // 500 ticks of good delivery to build from 0 → 1
BRAND_DAMAGE_RATE  = 0.008 / tick    // 125 ticks of bad delivery to destroy from 1 → 0
EMA_RATE           = 0.05            // quality expectation EMA; ~20 tick window
```

The asymmetry (4× faster to destroy than build) is the core mechanic. This is the Intel/AMD dynamic made precise: AMD took 8 years to reach 25% market share through consistent delivery. Intel's stability issues damaged their brand in the consumer/gaming segment within a single product generation.

---

### How Brand Equity Affects Demand

Brand equity modifies demand quantity (how much buyers want) and price elasticity (how much they care about price).

```
brand_demand_multiplier = 1.0 + brand_rating × BRAND_DEMAND_BONUS × income_segment_weight

price_elasticity_effective = base_price_elasticity × (1.0 - brand_loyalty_factor)
brand_loyalty_factor = brand_rating × BRAND_LOYALTY_COEFFICIENT
```

**Named constants:**
```
BRAND_DEMAND_BONUS      = 0.40   // high brand rating can boost demand by up to 40%
BRAND_LOYALTY_COEFF     = 0.60   // high brand rating can reduce price elasticity by up to 60%
```

**What this means in practice:**

A player who builds high brand equity in passenger cars can:
- Charge 40% more than market price and still sell the same volume
- Raise prices 20% and only lose 8% of demand (vs. 20% for a no-brand producer)

A player who cuts costs by using cheaper inputs:
- Delivers lower quality → quality_gap negative → brand damage begins
- Takes ~500 ticks to build, 125 ticks to destroy → the math heavily punishes corner-cutting
- Competitor NPCs will notice brand damage and price-compete aggressively

---

### Buyer Type Segments

Different buyers respond to quality and brand very differently. Each NPC (or NPC demand cohort) has a `buyer_type` that weights quality vs. price vs. brand.

```cpp
enum class BuyerType : uint8_t {
    price_rational,      // buys cheapest adequate quality; no brand loyalty
                         // e.g., construction contractor buying steel
    performance_rational,// compares quality/price ratio; brand matters less than spec
                         // e.g., data center buying server CPUs (AMD/Intel dynamic)
    brand_loyal,         // pays premium for known brand; quality within expected range
                         // e.g., consumer buying their preferred smartphone brand
    luxury_seeker,       // price is a quality signal; low price reduces desire
                         // e.g., buyer of premium watches, luxury cars
    necessity_buyer,     // buys based on availability and price; minimal brand sensitivity
                         // e.g., emergency pharmaceutical purchase
};
```

**Buyer type by good category:**

| Good Category | Primary Buyer Type | Secondary |
|---|---|---|
| Industrial raw materials | price_rational | performance_rational |
| Server / datacenter components | performance_rational | price_rational |
| Consumer electronics | brand_loyal | performance_rational |
| Budget consumer goods | price_rational | necessity_buyer |
| Premium consumer goods | brand_loyal | luxury_seeker |
| Luxury goods | luxury_seeker | brand_loyal |
| Pharmaceuticals | necessity_buyer | brand_loyal |
| Agricultural inputs | price_rational | — |
| Criminal goods | performance_rational | price_rational (purity is the spec) |

**Effect on purchase decisions:**

```
// price_rational:
willingness_to_pay = spot_price × 1.0    // buys at or below market; no premium
switches_at_price  = spot_price × 1.05   // leaves for competitor at 5% above market

// performance_rational:
value_score = quality / price
buys_from_highest_value_score_supplier

// brand_loyal:
willingness_to_pay = spot_price × (1.0 + brand_premium_coefficient × brand_rating)
switches_at_price  = willingness_to_pay × 1.10   // tolerates 10% above WTP before switching

// luxury_seeker:
willingness_to_pay = spot_price × (1.0 + luxury_premium × brand_rating)
minimum_acceptable_price = LUXURY_PRICE_FLOOR_COEFFICIENT × category_average_price
// if price < floor: demand drops (Veblen effect)

// necessity_buyer:
willingness_to_pay = very_high   // will pay almost anything
switches_at_price  = essentially_never_unless_substitute_exists
```

---

### The Intel/AMD Dynamic in the Simulation

This is the design validation case. A player who wants to enter the semiconductor_chip market will encounter:

1. **Incumbent NPC producers** with high brand equity built over many game-years, serving performance_rational and brand_loyal data center buyers. These NPCs command premium prices and have inelastic demand.

2. **The entry path**: Build Tier-5 semiconductor fab. Produce chips with competitive quality (high quality ceiling). Enter market at a discount vs. incumbents to attract price_rational and performance_rational buyers.

3. **Slow brand building**: Even with objectively better performance_rational value (quality/price), brand_loyal buyers don't switch immediately. Takes ~500 ticks of consistent delivery to build brand equity from 0.

4. **The disruption event**: If an incumbent NPC experiences a quality failure (random event, industrial accident, bad input batch), their brand_damage_rate kicks in 4× faster than they built it. The player can opportunistically take share from destabilized customers while the incumbent's brand recovers.

5. **Revenue share vs. unit share**: A player with lower brand equity but high quality/price ratio will see revenue share above their unit share — they're winning the premium performance segment even without brand dominance.

This emergent dynamic is not scripted — it falls out of the quality, brand, and buyer-type mechanics interacting.

---

### Quality and Criminal Goods

Criminal goods have their own quality dynamics:

**Purity is the spec.** Street buyers and distribution NPCs use `performance_rational` logic — purity percentage is the quality measure, and they calculate value per unit of active compound. A high-purity batch commands a significant premium over cut product.

**Brand in criminal markets is reputation in the street network.** Criminal operators build brand equity the same way legitimate businesses do — consistent delivery builds trust, failures destroy it — but the reputation propagates through the NPC social graph rather than through consumer purchase behavior. A dealer NPC who receives a bad batch updates their `Relationship` trust score with the supplier.

**Quality failure in criminal goods has asymmetric consequences beyond economics.** A bad batch that hospitalizes or kills users generates:
- Evidence tokens (medical records, witness accounts)
- Law enforcement attention uplift
- Community response escalation
- Investigator meter fill increase
- NPC memory entries about the incident (permanent)

This is the design intent: criminal market operators face all the same quality management challenges as legitimate businesses, plus additional catastrophic consequences when quality fails.

---

### Quality and the Modding System

Modders can extend the quality system by:

1. **Adding quality weight fields to recipe files** — the quality_weights array in recipes is data-defined, not hardcoded
2. **Adding buyer_type assignments to good definitions** — via a `buyer_segments` field in the goods CSV
3. **Adding quality premium coefficients** per good — via `quality_premium_coeff` in goods CSV
4. **Defining luxury price floors** — via `luxury_price_floor_coeff` for Veblen goods

The engine mechanics (propagation formula, brand update rules, price elasticity modification) are not moddable in V1 — they are engine code. Content mods configure the parameters, not the logic.

---

*Quality system added to EconLife Commodities and Factories — v2.1*

---

## Part 8 — Era System Integration (v2.2 Addendum)

*Following the decision to start the game in January 2000, the goods list and factory specifications require era-gating. This section specifies what exists at game start, what must be researched, and how the era system modifies production.*

---

### Era-Locked Goods

Not all goods in the master list exist at game start. Era-locked goods are inaccessible until the specified era is reached AND the relevant technology node is researched by at least one player or NPC. The first successful researcher to unlock a node makes the associated good producible by anyone who acquires the technology (via license, reverse engineering, or independent R&D).

New column for the goods CSV: `era_available` (1–5, default 1 = available from start).

**Goods locked at Era 1 start:**

| Key | Era Available | Unlock Path |
|---|---|---|
| `mobile_phone` (smartphone) | Era 2 | ARM processor → mobile OS → smartphone R&D |
| `battery_cell` (Li-ion EV scale) | Era 2 | Li-ion chemistry research |
| `battery_pack` | Era 2 | Requires battery_cell (era 2) |
| `electric_vehicle` | Era 2 | Requires EV drivetrain R&D |
| `electric_motor` (EV-grade) | Era 2 | Requires EV powertrain research |
| `display_panel` (OLED) | Era 2 | OLED chemistry research |
| `solar_panel` (competitive) | Era 2–3 | Photovoltaic efficiency research (Era 2 unlocks the good; Era 3 makes it cost-competitive) |
| `semiconductor_chip` (sub-65nm) | Era 2+ | Successive process node research |
| `server_unit` (cloud-scale) | Era 2 | Cloud architecture research |
| `mrna_pharmaceutical` | Era 4 | mRNA delivery mechanism research |
| `ai_hardware_accelerator` | Era 4 | AI hardware research |
| `synthetic_cannabinoid` | Era 1* | Criminal R&D; legal until scheduled |
| `designer_drug` | Any era | Criminal R&D; always legal until scheduled |

*Era 1 goods may still require R&D if they were not commercially available in 2000.

**What this means for factory recipes:**

Recipes that require an era-locked input good cannot run until that good is available. A vehicle assembly plant built in 2000 that requires `battery_pack` as an input will simply not be able to produce electric vehicles until the Era 2 unlock occurs. The recipe is defined from the start (modders and planners can see it) — the inputs just don't exist yet.

---

### Era-Gated Tech Tiers

Tech tier availability also follows the era system. Operators in 2000 cannot build Tier 5 facilities — the process knowledge and equipment simply don't exist yet.

| Tech Tier | Era Available | Historical analog |
|---|---|---|
| Tier 1 | Era 1 (start) | Basic industrial (always existed) |
| Tier 2 | Era 1 (start) | Standard 2000-era industry |
| Tier 3 | Era 1 (start, frontier) | Cutting-edge 2000-era (early semiconductors, precision manufacturing) |
| Tier 4 | Era 2 | Post-2007 advanced manufacturing (advanced fabs, EV production) |
| Tier 5 | Era 4 | Post-2019 frontier (5nm semiconductors, AI hardware, advanced biotech) |

A Tier 3 facility at game start is therefore genuinely high-end — few NPC businesses operate at that level. A player who builds Tier 3 manufacturing in Era 1 has a genuine advantage over Era 1 competitors, and a genuine disadvantage against Era 4 Tier 5 entrants who arrive later but with better technology.

**Stranded asset mechanic:**
A Tier 2 steel mill built in 2000 will still be Tier 2 in 2025. The owner can upgrade it (costly; requires construction time and capital), or operate it at growing competitive disadvantage as newer players and NPC businesses build Tier 4+ facilities. This is how real industrial history works — older plants are exactly this problem, and incumbents who delay upgrading lose market position to newer entrants.

---

### Climate Effects on Agricultural Goods

*Resolving Open Question 2 from the original document.*

Agricultural goods now have explicit climate modifiers. The `farm_output_actual` formula is:

```
farm_output_actual = recipe.base_output
    × (1.0 - regional_climate_stress × FARM_STRESS_SENSITIVITY[crop_type])
    × drought_modifier       // 0.0–1.0; 1.0 = no drought; 0.3 = severe drought active
    × flood_modifier         // 0.0–1.0; flooding may destroy growing crops entirely
    × soil_health            // 0.5–1.0; degrades without rotation, improves with fertilizer
    × fertilizer_efficiency  // from fertilizer_npk input quality × application rate
```

**FARM_STRESS_SENSITIVITY by crop type:**

| Crop category | Stress sensitivity | Notes |
|---|---|---|
| Grain (wheat, corn, rice) | 0.55 | Yield-sensitive to heat and drought |
| Oilseeds (soybeans, rapeseed) | 0.50 | Drought-sensitive; some adaptation possible |
| Sugarcane | 0.35 | Tropical; benefits from warmth initially, but water-limited |
| Cotton | 0.60 | Very sensitive to both heat and water stress |
| Coffee | 0.70 | High climate sensitivity; very narrow growing conditions |
| Natural rubber | 0.65 | Tropical; vulnerable to temperature and humidity shifts |
| Livestock (cattle, pigs, poultry) | 0.30 | Heat stress reduces productivity; feed cost driven by grain |
| Timber (softwood/hardwood) | 0.25 | Slower; longer-horizon effect; wildfire risk is larger threat |
| Fish (farmed) | 0.20 | Temperature affects growth rates; ocean acidification long-run |

**Soil health mechanics:**

Soil health is a property of each farm tile, starting at region-dependent baseline. It:
- Degrades over time if the same crop is planted continuously (monoculture penalty)
- Recovers with crop rotation (alternating crops across seasons)
- Improves with organic inputs (manure, cover crops — not currently modeled as inputs, flagged for EX)
- Improves with fertilizer application (fertilizer_npk input to farm recipe)
- Degrades rapidly with extreme weather events (floods, droughts)

This creates genuine agricultural management depth — a player who farms intensively for profit will degrade soil health, require more fertilizer inputs to maintain yield, and become more vulnerable to climate stress over time.

---

### Perishable Goods in Transit

*Resolving Open Question 5.*

Goods with `is_perishable = true` degrade during inter-regional transport. The degradation model:

```
quality_on_arrival = quality_at_origin × (1.0 - PERISHABLE_DECAY_RATE ^ transport_ticks)
quantity_on_arrival = quantity_shipped × (1.0 - SPOILAGE_RATE × transport_ticks)
```

**Named constants by perishable category:**

| Good category | PERISHABLE_DECAY_RATE | SPOILAGE_RATE | Notes |
|---|---|---|---|
| Live animals | 0.04/tick | 0.02/tick | Stress reduces quality; mortality possible |
| Fresh meat / seafood | 0.08/tick | 0.05/tick | Refrigeration (tech upgrade) reduces both by 0.5× |
| Dairy products | 0.06/tick | 0.03/tick | Refrigeration dependent |
| Fresh produce | 0.07/tick | 0.04/tick | Highly perishable |
| Processed food (packaged) | 0.001/tick | 0.0001/tick | Essentially shelf-stable |
| Pharmaceutical | 0.002/tick | 0.001/tick | Temp-sensitive; cold chain adds cost |

**Cold chain as a facility upgrade:**
Transport and logistics hubs can install refrigeration (a tech upgrade, available from Era 1 but costs capital). Refrigerated transport halves all perishable decay and spoilage rates. This creates a meaningful infrastructure investment decision — the player who builds cold chain infrastructure in a region gains an advantage in food and pharmaceutical trade that competitors who skipped it cannot easily match.

---

### Byproduct Price Floor

*Resolving Open Question 6.*

Byproducts do NOT have a minimum price floor. They can fall to near-zero (but not negative — goods are never free; disposal costs exist). This is the correct market behavior:

- A petroleum refinery that produces naphtha nobody wants will find naphtha prices crash in that region
- The refinery still produces it (cannot suppress byproducts)
- This creates an incentive for someone to build downstream petrochemical capacity — exactly as happened historically when chemical plants co-located with refineries to consume their byproducts
- If nobody builds the downstream capacity, naphtha disposal becomes a cost (environmental waste charges apply to byproducts with `environmental_waste = true`)

**Environmental waste disposal:**

Byproducts with `environmental_waste = true` that have no buyers and no processing capacity become environmental liabilities:
```
if byproduct_market_demand < byproduct_supply AND no_disposal_facility:
    regional_environmental_damage += excess_byproduct × ENVIRONMENTAL_IMPACT_PER_UNIT
    NPC community_response_level += COMMUNITY_POLLUTION_SENSITIVITY × excess_byproduct
    Evidence token generated: "Environmental violation — [good_key] discharge"
```

This mechanic makes the petroleum refinery's byproduct management economically significant: the refiner must either find buyers for all byproducts, build downstream processing, or pay disposal costs and face community/regulatory consequences.

---

### Sub-Assembly Open Market Trading

*Resolving Open Question 8.*

Sub-assembly goods (Tier 2) **do** trade on open regional markets. This is the design decision that creates supplier leverage.

Any NPC business or player that produces a Tier 2 good can sell it on the regional market. Any NPC business or player that needs it as an input can buy from the market rather than producing it themselves. This is the standard market behavior that applies to all goods, and there is no reason to make sub-assemblies an exception.

**Consequences:**
- A player who controls engine production can sell engines to NPC vehicle assemblers
- An NPC vehicle assembler who cannot produce their own engines is dependent on market availability
- If the player restricts supply (produces only for their own assembly plants), the NPC assemblers face an input shortage and must either pay more, source from a different region, or vertically integrate (which takes time)
- This is exactly the kind of supply chain leverage the GDD describes as a core mechanic

**The only constraint:** Criminal goods (Tier 4) do NOT trade on formal regional markets. They exist in the informal market layer with criminal_spot_price and are not visible in the formal RegionalMarket data structure.

---

---

## Change Log — v2.3 (Pass 3 Session 18 — Cross-Doc Audit Fixes)

Resolves findings B-02 and P-02 from the Pass 3 post-Session 17 cross-document consistency audit.

### B-02 — Recipe CSV schema updated

- Added `key_technology_node` column to the recipe CSV format, positioned after `tech_bonus_per_tier`.
- Empty string is valid and expected for commodity/mature-process recipes.
- Removed `opsec_power_signal`, `opsec_chem_signal`, and `opsec_odor_signal` from the recipe CSV schema. Signal component values are facility-type properties, not recipe properties; they belong in `facility_types.csv` as per-type signal weights.
- Added `explosion_risk_per_tick` as a standalone CSV column (separate from signal fields).
- Closes R&D v2.1 Open Question 12.

### P-02 — OPSECProfile removed; FacilityRecipe updated with FacilitySignals

- Removed `OPSECProfile` struct definition from Commodities. Canonical signal struct is `FacilitySignals` in TDD v23 §16.
- Replaced `OPSECProfile opsec` field on `FacilityRecipe` with `FacilitySignals signals`. Field is universal — all facility types carry it; not criminal-only.
- Added standalone `float explosion_risk_per_tick` field to `FacilityRecipe`. Consumed by random events system (tick step 19), not by the scrutiny/signal system.
- Added comment clarifying that `base_signal_composite`, `scrutiny_mitigation`, and `net_signal` are computed at runtime by tick step 13 and must not be populated from CSV data.
- Updated drug lab recipe prose example (`drug_lab_meth`) to use canonical signal field names (`chemical_waste_signal`, `noise_signal`) and standalone `explosion_risk_per_tick` notation.
- Reconciled `drug_lab_opioid` example to use canonical signal field names and standalone `explosion_risk_per_tick` notation.

### Companion document header updated

- TDD v17 → v18.

---

## Pass 5 Session 3 — Signal Architecture Completion (COM-B1 BLOCKER FIX)

This session resolves the COM-B1 blocker: "Signal Architecture Split Without Complete Specification."

### COM-B1 — Complete facility_types.csv specification with signal columns

**Problem:** Signal fields were removed from recipe CSV and "moved to facility_types.csv" but the facility_types.csv format specification did not include those columns or document their format.

**Fix applied:**

1. **Completed facility_types.csv format specification** with all signal weight columns:
   - `facility_type_key, display_name, category, base_construction_cost, base_operating_cost, max_workers, power_draw_signal, chemical_waste_signal, foot_traffic_signal, noise_signal, financial_anomaly_signal`
   - Added full column definitions with explanation of signal weight semantics.

2. **Added canonical facility type examples** (drug_lab, factory, retail_store, office):
   - `drug_lab,Drug Manufacturing Lab,criminal,50000,2500,5,0.3,0.9,0.1,0.8,0.2`
   - `factory,General Factory,manufacturing,150000,5000,50,0.8,0.5,0.3,0.6,0.1`
   - `retail_store,Retail Store,commercial,30000,1000,20,0.4,0.1,0.95,0.2,0.05`
   - `office,Office Building,commercial,80000,2000,100,0.5,0.0,0.4,0.1,0.3`

3. **Added Signal Architecture section** explaining the design principle:
   - Signal weights are per-facility-type, not per-recipe.
   - Facility type defines baseline signals; recipe adds burden via `FacilityRecipe.signals`.
   - Runtime computation at tick step 13: base composite + recipe burden + mitigation = net signal.
   - Complementary architecture prevents duplication and allows meaningful variation (e.g., a large factory with high base signals can run a quiet recipe, while a hidden lab with low base signals becomes visible only when running a recipe with chemical_waste burden).

4. **Reconciled drug lab examples to canonical signal field names:**
   - `drug_lab_meth`: `chemical_waste_signal = VERY HIGH, noise_signal = VERY HIGH | explosion_risk_per_tick = 0.002`
   - `drug_lab_opioid`: `chemical_waste_signal = HIGH, noise_signal = LOW | explosion_risk_per_tick = 0.0005`
   - All old OPSEC notation removed; all signal references now use canonical TDD v23 §16 field names.

5. **Updated modding instructions:** Modders adding new facility types must specify all signal weight columns; unknown columns cause load error.

This fix ensures:
- facility_types.csv specification is complete and unambiguous.
- All signal references in the document use canonical TDD v23 field names.
- The relationship between facility-type signals and recipe-level signals is explicit and documented.
- Criminal facilities properly reflect their inherent visibility risk through elevated base signal weights.

---

*Commodities and Factories v2.3 — Era system integration, climate effects, perishables, open questions resolved; technology lifecycle integration complete; OPSECProfile → FacilitySignals migration complete; facility_types.csv signal specification complete (Pass 5 Session 3)*
