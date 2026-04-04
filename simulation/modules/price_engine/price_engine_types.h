#pragma once

// Price engine module types.
// Core market types (RegionalMarket) are in economy_types.h.
// This header defines price-engine-specific calculation types.

#include <cstdint>

namespace econlife {

// Legacy constants struct — DEPRECATED.
// Use PriceEngineConfig from core/config/package_config.h instead.
// Retained as empty struct to avoid breaking includes.
struct PriceEngineConstants {};

}  // namespace econlife
