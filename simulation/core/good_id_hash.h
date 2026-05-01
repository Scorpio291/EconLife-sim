#pragma once

// Canonical good_id string -> uint32 hash.
//
// Used wherever a string good_id needs to be packed into a Recipe-derived
// MarketDelta or NPCBusiness. Any code that compares MarketDelta.good_id
// across modules MUST use this helper — divergent local hashes silently
// route supply/demand to the wrong market.
//
// Implementation is FNV-1a (32-bit), chosen for:
//   - Pure: same input -> same output, byte-identical across runs.
//   - Defensible distribution: well-studied collision resistance for
//     short ASCII keys at the V1 goods-catalog scale (~250 entries).
//   - No allocation, branch-free hot loop.
//
// This is a transitional shim. The proper long-term fix is to look up
// numeric ids from GoodsCatalog (which already assigns sequential ids
// at load time), eliminating the per-call hash. That migration is
// tracked separately — switching to catalog ids requires threading a
// catalog reference through the modules that currently call this helper.

#include <cstdint>
#include <string_view>

namespace econlife {

inline uint32_t good_id_hash(std::string_view good_id) noexcept {
    constexpr uint32_t FNV_OFFSET_BASIS = 0x811c9dc5u;
    constexpr uint32_t FNV_PRIME = 0x01000193u;
    uint32_t h = FNV_OFFSET_BASIS;
    for (unsigned char c : good_id) {
        h ^= c;
        h *= FNV_PRIME;
    }
    return h;
}

}  // namespace econlife
