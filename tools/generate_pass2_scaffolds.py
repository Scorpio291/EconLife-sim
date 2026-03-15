#!/usr/bin/env python3
"""Generate Pass 2 scaffold files for Tier 6-12 modules (27 modules).
Creates: *_module.cpp, *_types.h, CLAUDE.md, CMakeLists.txt for each module.
INTERFACE.md files are generated separately by subagents reading TDD/GDD."""

import os
from pathlib import Path

PROJ = Path(r"C:\Users\chris\Documents\Prosjekter\EconLife-sim")
MOD_DIR = PROJ / "simulation" / "modules"
IFACE_DIR = PROJ / "docs" / "interfaces"

# Module definitions: (name, tier, class_name, province_parallel, cmake_deps, runs_after, description)
MODULES = [
    # Tier 6
    ("npc_spending", 6, "NpcSpendingModule", True,
     ["econlife_npc_behavior"], ["npc_behavior"],
     "Processes NPC consumer spending decisions based on needs, income, and market prices. Province-parallel."),
    ("evidence", 6, "EvidenceModule", False,
     ["econlife_npc_behavior"], ["npc_behavior"],
     "Manages evidence token creation, accumulation, decay, and discovery. Tracks financial, testimonial, documentary, and physical evidence per NPC."),
    ("obligation_network", 6, "ObligationNetworkModule", False,
     ["econlife_npc_behavior"], ["npc_behavior"],
     "Tracks debts, favors, and obligation relationships between NPCs. Manages obligation creation, satisfaction, and expiry."),
    ("community_response", 6, "CommunityResponseModule", False,
     ["econlife_npc_behavior"], ["npc_behavior"],
     "Evaluates community-level reactions to events: protests, support movements, collective action thresholds."),

    # Tier 7
    ("facility_signals", 7, "FacilitySignalsModule", True,
     ["econlife_evidence"], ["evidence"],
     "Generates observable signals from business facilities that may indicate illegal activity. Signal strength varies by operation type and concealment."),
    ("criminal_operations", 7, "CriminalOperationsModule", False,
     ["econlife_evidence"], ["evidence"],
     "Manages criminal enterprise operations: drug production, money laundering fronts, protection racket enforcement, territory control."),
    ("media_system", 7, "MediaSystemModule", False,
     ["econlife_evidence"], ["evidence"],
     "Simulates journalism, media coverage, and public information flow. Journalists investigate evidence, publish stories affecting public opinion."),
    ("antitrust", 7, "AntitrustModule", False,
     ["econlife_evidence"], ["evidence"],
     "Monitors market concentration and triggers regulatory responses. Evaluates HHI indices, merger reviews, and enforcement actions."),

    # Tier 8
    ("investigator_engine", 8, "InvestigatorEngineModule", False,
     ["econlife_criminal_operations"], ["criminal_operations"],
     "Drives NPC investigators (police, regulatory, journalist) through evidence gathering, case building, and prosecution/publication."),
    ("money_laundering", 8, "MoneyLaunderingModule", False,
     ["econlife_criminal_operations"], ["criminal_operations"],
     "Processes illicit cash through laundering layers: placement, layering, integration. Tracks exposure risk per transaction."),
    ("drug_economy", 8, "DrugEconomyModule", True,
     ["econlife_criminal_operations"], ["criminal_operations"],
     "Simulates drug production, distribution, and consumption markets. Province-parallel drug supply chains with quality and purity tracking."),
    ("weapons_trafficking", 8, "WeaponsTraffickingModule", False,
     ["econlife_criminal_operations"], ["criminal_operations"],
     "Manages weapons sourcing, trafficking routes, and distribution. Tracks weapon types, quantities, and territorial control."),
    ("protection_rackets", 8, "ProtectionRacketsModule", True,
     ["econlife_criminal_operations"], ["criminal_operations"],
     "Simulates extortion of businesses: territory assignment, payment collection, enforcement, and resistance."),

    # Tier 9
    ("legal_process", 9, "LegalProcessModule", False,
     ["econlife_investigator_engine"], ["investigator_engine"],
     "Manages legal proceedings from investigation to trial. Tracks charges, evidence presentation, verdicts, and sentencing."),
    ("informant_system", 9, "InformantSystemModule", False,
     ["econlife_investigator_engine"], ["investigator_engine"],
     "Manages NPC informant recruitment, information flow, reliability scoring, and exposure risk."),
    ("alternative_identity", 9, "AlternativeIdentityModule", False,
     ["econlife_investigator_engine"], ["investigator_engine"],
     "Manages creation and maintenance of false identities for evading investigation. Tracks identity quality and discovery risk."),
    ("designer_drug", 9, "DesignerDrugModule", False,
     ["econlife_investigator_engine"], ["investigator_engine"],
     "Simulates R&D and production of novel drug compounds. Tracks formulation quality, market demand, and regulatory response."),

    # Tier 10
    ("political_cycle", 10, "PoliticalCycleModule", False,
     ["econlife_community_response"], ["community_response"],
     "Manages election cycles, political campaigns, policy platforms, voting, and government formation."),
    ("influence_network", 10, "InfluenceNetworkModule", False,
     ["econlife_community_response"], ["community_response"],
     "Tracks and propagates influence relationships between NPCs, organizations, and political entities."),
    ("trust_updates", 10, "TrustUpdatesModule", True,
     ["econlife_community_response"], ["community_response"],
     "Processes trust score changes between NPCs based on observed actions, kept promises, and betrayals. Province-parallel."),
    ("addiction", 10, "AddictionModule", True,
     ["econlife_community_response"], ["community_response"],
     "Simulates substance addiction progression, treatment, and relapse for NPCs. Tracks dependency levels and regional impact."),

    # Tier 11
    ("regional_conditions", 11, "RegionalConditionsModule", True,
     ["econlife_political_cycle", "econlife_influence_network"], ["political_cycle", "influence_network"],
     "Aggregates province-level conditions: stability index, inequality, crime rate, economic health. Feeds LOD transitions."),
    ("population_aging", 11, "PopulationAgingModule", True,
     ["econlife_healthcare"], ["healthcare"],
     "Processes demographic aging, mortality, birth rates, and population cohort transitions per province."),
    ("currency_exchange", 11, "CurrencyExchangeModule", False,
     ["econlife_commodity_trading", "econlife_government_budget"], ["commodity_trading", "government_budget"],
     "Manages currency exchange rates between nations based on trade balance, interest rates, and market sentiment."),
    ("lod_system", 11, "LodSystemModule", False,
     ["econlife_regional_conditions"], ["regional_conditions"],
     "Manages Level of Detail transitions for nations: LOD 0 (full sim), LOD 1 (simplified), LOD 2 (statistical)."),

    # Tier 12
    ("persistence", 12, "PersistenceModule", False,
     ["econlife_core"], ["world_state"],
     "Serializes and deserializes WorldState for save/load. LZ4 compression, round-trip determinism verification."),
]


def write_file(path: Path, content: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print(f"  Created: {path.relative_to(PROJ)}")


def gen_stub_cpp(name, tier, class_name, province_parallel, cmake_deps, runs_after, desc):
    runs_after_str = ", ".join(f'"{r}"' for r in runs_after)
    scope = "v1" if tier <= 10 else "ex" if tier <= 11 else "v1"
    # Tier 12 persistence is V1
    if name == "persistence":
        scope = "v1"
    # LOD system, currency exchange, population_aging are V1 per feature list
    if name in ("lod_system", "currency_exchange", "population_aging", "regional_conditions"):
        scope = "v1"

    pp_methods = ""
    if province_parallel:
        pp_methods = f"""
    void execute_province(uint32_t province_idx,
                          const WorldState& state,
                          DeltaBuffer& province_delta) override {{
        // Stub: no-op province-parallel implementation.
    }}
"""

    return f'''#include "core/tick/tick_module.h"

namespace econlife {{

class {class_name} : public ITickModule {{
public:
    std::string_view name() const noexcept override {{ return "{name}"; }}
    std::string_view package_id() const noexcept override {{ return "base_game"; }}
    ModuleScope scope() const noexcept override {{ return ModuleScope::{scope}; }}

    std::vector<std::string_view> runs_after() const override {{
        return {{{runs_after_str}}};
    }}

    bool is_province_parallel() const noexcept override {{ return {"true" if province_parallel else "false"}; }}
{pp_methods}
    void execute(const WorldState& state, DeltaBuffer& delta) override {{
        // Stub: no-op implementation. Will be replaced during Orchestrator implementation.
    }}
}};

}}  // namespace econlife
'''


def gen_types_h(name, tier, class_name, province_parallel, cmake_deps, runs_after, desc):
    guard = f"ECONLIFE_{name.upper()}_TYPES_H"
    return f'''#pragma once

// {name} module types.
// Module-specific types for the {name} module (Tier {tier}).
// Core shared types are in their respective core headers.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {{

// Placeholder — types will be defined during implementation based on INTERFACE.md.

}}  // namespace econlife
'''


def gen_cmake(name, tier, class_name, province_parallel, cmake_deps, runs_after, desc):
    deps_str = " ".join(cmake_deps)
    return f'''add_library(econlife_{name} STATIC
    {name}_module.cpp
)
target_link_libraries(econlife_{name} PUBLIC {deps_str})
'''


def gen_claude_md(name, tier, class_name, province_parallel, cmake_deps, runs_after, desc):
    pp_str = "Province-parallel." if province_parallel else "Sequential (global)."
    runs_after_str = ", ".join(runs_after) if runs_after else "(none)"
    return f'''# {name} — Developer Context

## What This Module Does
{desc}

## Tier: {tier} | {pp_str}

## Key Dependencies
- runs_after: [{runs_after_str}]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/{name}/INTERFACE.md
'''


def main():
    print(f"Generating Pass 2 scaffold files for {len(MODULES)} modules...\n")

    for mod in MODULES:
        name = mod[0]
        mod_path = MOD_DIR / name
        iface_path = IFACE_DIR / name

        # Ensure directories exist
        mod_path.mkdir(parents=True, exist_ok=True)
        iface_path.mkdir(parents=True, exist_ok=True)

        # Generate 4 files per module
        write_file(mod_path / f"{name}_module.cpp", gen_stub_cpp(*mod))
        write_file(mod_path / f"{name}_types.h", gen_types_h(*mod))
        write_file(mod_path / "CMakeLists.txt", gen_cmake(*mod))
        write_file(mod_path / "CLAUDE.md", gen_claude_md(*mod))

    print(f"\nDone! Generated {len(MODULES) * 4} files ({len(MODULES)} modules x 4 files each).")
    print("INTERFACE.md files will be generated separately by subagents.")


if __name__ == "__main__":
    main()
