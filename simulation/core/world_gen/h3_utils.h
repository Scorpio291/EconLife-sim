#pragma once

// C++ wrappers around the H3 C library (uber/h3 v4.x).
// All functions operate at resolution 4 (province scale, ~1,770 km²).
// These are called only during world generation — never per-tick.

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <h3/h3api.h>

#include "core/world_state/geography.h"  // for H3Index typedef

namespace econlife::h3_utils {

// Convert latitude/longitude (degrees) to an H3 cell at the given resolution.
// Throws std::runtime_error if H3 returns an error code.
H3Index lat_lng_to_cell(double lat_deg, double lng_deg, int resolution);

// Return all immediate neighbors of a cell (grid ring at distance 1).
// Returns 5 cells for pentagons, 6 cells for regular hexagons.
// Throws std::runtime_error on H3 error.
std::vector<H3Index> grid_neighbors(H3Index cell);

// Return a contiguous, deterministically-ordered group of `count` cells
// starting from `center` using BFS. Output is sorted by H3Index value
// for determinism across platforms.
// Throws std::runtime_error if H3 returns an error or count == 0.
std::vector<H3Index> grid_compact_group(H3Index center, uint32_t count);

// Return the area of a cell in km². Uses H3's exact formula.
double cell_area_km2(H3Index cell);

// Return true if the cell is one of H3's 12 fixed pentagons at its resolution.
bool is_pentagon(H3Index cell);

// Approximate shared border length in km between two adjacent res-4 cells.
// Returns 0.0f if the cells are not neighbours.
// Uses the average edge length at resolution 4 (~43 km).
float shared_border_km(H3Index a, H3Index b);

}  // namespace econlife::h3_utils
