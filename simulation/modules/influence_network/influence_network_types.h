#pragma once

// influence_network module types.
// Module-specific types for the influence_network module (Tier 10).
// InfluenceNetworkHealth is in shared_types.h.

#include <cstdint>

namespace econlife {

enum class InfluenceType : uint8_t { trust_based, fear_based, obligation_based, movement_based };

}  // namespace econlife
