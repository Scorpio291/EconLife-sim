#include "h3_utils.h"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace econlife::h3_utils {

namespace {

void check(H3Error err, const char* context) {
    if (err != E_SUCCESS) {
        throw std::runtime_error(std::string("H3 error in ") + context +
                                 ": code " + std::to_string(static_cast<int>(err)));
    }
}

}  // namespace

H3Index lat_lng_to_cell(double lat_deg, double lng_deg, int resolution) {
    LatLng coords{};
    coords.lat = degsToRads(lat_deg);
    coords.lng = degsToRads(lng_deg);
    H3Index out = 0;
    check(latLngToCell(&coords, resolution, &out), "lat_lng_to_cell");
    return out;
}

std::vector<H3Index> grid_neighbors(H3Index cell) {
    // gridDisk(cell, 1) returns the cell itself plus all neighbors.
    // Max neighbors at k=1 is 7 (center + 6); 6 for pentagons (center + 5).
    int64_t sz = 0;
    check(maxGridDiskSize(1, &sz), "maxGridDiskSize");

    std::vector<H3Index> buf(static_cast<size_t>(sz), 0);
    check(gridDisk(cell, 1, buf.data()), "gridDisk");

    // Remove the center cell itself and any invalid (0) entries.
    std::vector<H3Index> neighbors;
    neighbors.reserve(6);
    for (H3Index c : buf) {
        if (c != 0 && c != cell) {
            neighbors.push_back(c);
        }
    }
    std::sort(neighbors.begin(), neighbors.end());
    return neighbors;
}

std::vector<H3Index> grid_compact_group(H3Index center, uint32_t count) {
    if (count == 0) return {};

    // BFS from center, collecting cells in H3Index-sorted order per frontier ring.
    std::vector<H3Index> result;
    result.reserve(count);

    std::unordered_set<H3Index> visited;
    visited.insert(center);
    result.push_back(center);

    // Frontier: current ring of cells to expand from.
    std::vector<H3Index> frontier{center};

    while (result.size() < count && !frontier.empty()) {
        std::vector<H3Index> next_frontier;
        for (H3Index c : frontier) {
            auto nbrs = grid_neighbors(c);
            for (H3Index n : nbrs) {
                if (visited.insert(n).second) {
                    next_frontier.push_back(n);
                }
            }
        }
        // Sort next_frontier for determinism before adding.
        std::sort(next_frontier.begin(), next_frontier.end());
        for (H3Index c : next_frontier) {
            if (result.size() >= count) break;
            result.push_back(c);
        }
        frontier = std::move(next_frontier);
    }

    if (result.size() < count) {
        throw std::runtime_error("grid_compact_group: could not find " +
                                 std::to_string(count) + " contiguous H3 cells");
    }

    return result;
}

double cell_area_km2(H3Index cell) {
    double area = 0.0;
    check(cellAreaKm2(cell, &area), "cellAreaKm2");
    return area;
}

bool is_pentagon(H3Index cell) {
    return isPentagon(cell) != 0;
}

float shared_border_km(H3Index a, H3Index b) {
    // Check adjacency first.
    int adjacent = 0;
    H3Error err = areNeighborCells(a, b, &adjacent);
    if (err != E_SUCCESS || !adjacent) return 0.0f;

    // Average edge length at resolution 4 is approximately 43.3 km.
    // H3 provides exactEdgeLengthKm for directed edges.
    H3Index edge = 0;
    err = getDirectedEdge(a, b, &edge);
    if (err != E_SUCCESS) return 43.3f;  // fallback to res-4 average

    double len_km = 0.0;
    err = exactEdgeLengthKm(edge, &len_km);
    if (err != E_SUCCESS) return 43.3f;

    return static_cast<float>(len_km);
}

}  // namespace econlife::h3_utils
