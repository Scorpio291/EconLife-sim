// golden_state_dump — cross-platform determinism gate.
//
// Builds a deterministic WorldState (fixed seed, NPC count, province count),
// runs the full base-game module pipeline for N ticks, serializes the final
// state through the existing PersistenceModule (which already enforces
// canonical ordering), then SHA-256s the resulting bytes and writes the
// hex digest to a file.
//
// The CI cross-platform-determinism job builds this tool on every matrix cell
// and asserts that all platforms produce the same hash. A divergence means
// "same seed + same inputs" produced different bytes on Linux vs Windows,
// which violates the project's core determinism contract.
//
// Usage:
//   econlife_golden_dump --seed 42 --ticks 30 --npcs 200 --provinces 4
//                        --out hash.txt
//
// Notes:
//   * Uses test_world_factory rather than WorldGenerator so the harness has
//     no dependency on packages/base_game/ CSV files — those are loaded by
//     the CLI but would add cross-platform filesystem variability we don't
//     want to chase here.
//   * SHA-256 implementation is inline (FIPS 180-4, ~80 LOC) to avoid
//     introducing a new third-party dependency.
//   * Single-threaded ThreadPool for stricter determinism (matches the
//     "modules" determinism test). Province-parallel determinism is
//     covered separately in determinism_test.cpp.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "../test_world_factory.h"
#include "modules/persistence/persistence_module.h"
#include "modules/register_base_game_modules.h"

using namespace econlife;
using namespace econlife::test;

namespace {

// ── SHA-256 (FIPS 180-4) ────────────────────────────────────────────────────
// Public-domain reference implementation, transcribed inline so the harness
// has no external crypto dependency. Produces a 32-byte digest.

struct Sha256 {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    uint32_t buflen;

    static constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void init() {
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
        bitlen = 0;
        buflen = 0;
    }

    void transform(const uint8_t block[64]) {
        uint32_t w[64];
        for (uint32_t i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
                   (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
        }
        for (uint32_t i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buffer[buflen++] = data[i];
            if (buflen == 64) {
                transform(buffer);
                bitlen += 512;
                buflen = 0;
            }
        }
    }

    void finalize(uint8_t out[32]) {
        uint64_t total_bits = bitlen + uint64_t(buflen) * 8;
        buffer[buflen++] = 0x80;
        if (buflen > 56) {
            while (buflen < 64)
                buffer[buflen++] = 0;
            transform(buffer);
            buflen = 0;
        }
        while (buflen < 56)
            buffer[buflen++] = 0;
        for (int i = 7; i >= 0; --i) {
            buffer[buflen++] = static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF);
        }
        transform(buffer);
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
            out[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
            out[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
            out[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
        }
    }
};

constexpr uint32_t Sha256::K[64];

std::string sha256_hex(const std::vector<uint8_t>& bytes) {
    Sha256 ctx;
    ctx.init();
    if (!bytes.empty()) {
        ctx.update(bytes.data(), bytes.size());
    }
    uint8_t digest[32];
    ctx.finalize(digest);

    static const char* hex = "0123456789abcdef";
    std::string out(64, '0');
    for (size_t i = 0; i < 32; ++i) {
        out[i * 2] = hex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    return out;
}

// ── Per-tick trace ──────────────────────────────────────────────────────────
// When two cells disagree on the final hash, the hash alone says only THAT
// they diverged. The trace says WHEN and WHERE: one line per tick with the
// full-state hash and an FNV-1a fingerprint of each major state family, then
// (for the first ticks) the raw bits of every market and province scalar.
// The CI comparison job diffs traces of disagreeing cells, so the first
// differing line names the tick and the field family that split first.

struct Fnv {
    uint64_t h = 1469598103934665603ULL;
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    }
    void f(float v) { bytes(&v, sizeof(v)); }
    void u(uint64_t v) { bytes(&v, sizeof(v)); }
};

uint32_t bits(float v) {
    uint32_t b;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

void province_scalars(const Province& p, std::vector<std::pair<const char*, float>>& out) {
    out.clear();
    out.push_back({"pop_median_age", p.demographics.median_age});
    out.push_back({"education", p.demographics.education_level});
    out.push_back({"infrastructure", p.infrastructure_rating});
    out.push_back({"agri_productivity", p.agricultural_productivity});
    out.push_back({"cohesion", p.community.cohesion});
    out.push_back({"grievance", p.community.grievance_level});
    out.push_back({"trust", p.community.institutional_trust});
    out.push_back({"resource_access", p.community.resource_access});
    out.push_back({"approval", p.political.approval_rating});
    out.push_back({"corruption", p.political.corruption_index});
    out.push_back({"stability", p.conditions.stability_score});
    out.push_back({"inequality", p.conditions.inequality_index});
    out.push_back({"compliance", p.conditions.regulatory_compliance_index});
    out.push_back({"drought", p.conditions.drought_modifier});
    out.push_back({"flood", p.conditions.flood_modifier});
    out.push_back({"trauma", p.historical_trauma_index});
    out.push_back({"fish_stock", p.fisheries.current_stock});
    if (const auto* c = p.cohort_stats.get()) {
        out.push_back({"c.addiction", c->addiction_rate});
        out.push_back({"c.crime", c->crime_rate});
        out.push_back({"c.formal_employment", c->formal_employment_rate});
        out.push_back({"c.sick", c->sick_rate});
        out.push_back({"c.homeless", c->homeless_rate});
        out.push_back({"c.unemployment", c->unemployment_rate});
        out.push_back({"c.subsistence", c->subsistence_surplus_ratio});
        out.push_back({"c.food_store", c->food_store});
        out.push_back({"c.capital", c->productive_capital});
        out.push_back({"c.soil", c->soil_health});
        out.push_back({"c.knowledge", c->knowledge_level});
        out.push_back({"c.mean_income", c->mean_income});
        out.push_back({"c.gini", c->gini_coefficient});
        out.push_back({"c.wage_anchor", c->regional_wage_anchor});
        out.push_back({"c.nutrition", c->nutrition});
        out.push_back({"c.health", c->health});
        out.push_back({"c.schooling", c->schooling});
        out.push_back({"c.total_pop", static_cast<float>(c->total_population)});
    }
}

void write_trace_tick(std::FILE* f, const WorldState& w, uint32_t detail_through_tick) {
    Fnv nations, provinces, npcs, markets, businesses;
    for (const auto& n : w.nations) {
        nations.u(n.id);
        nations.f(n.political_cycle.national_approval);
        nations.f(n.political_cycle.national_legitimacy);
    }
    std::vector<std::pair<const char*, float>> scal;
    for (const auto& p : w.provinces) {
        province_scalars(p, scal);
        provinces.u(p.id);
        for (const auto& kv : scal)
            provinces.f(kv.second);
    }
    for (const auto& n : w.significant_npcs) {
        npcs.u(n.id);
        npcs.f(n.capital);
        npcs.f(n.social_capital);
        npcs.f(n.risk_tolerance);
        npcs.u(n.memory_log.size());
        npcs.u(n.relationships.size());
    }
    for (const auto& m : w.regional_markets) {
        markets.u(m.good_id);
        markets.u(m.province_id);
        markets.f(m.spot_price);
        markets.f(m.supply);
        markets.f(m.demand_buffer);
        markets.f(m.import_price_ceiling);
        markets.f(m.export_price_floor);
    }
    for (const auto& b : w.npc_businesses) {
        businesses.u(b.id);
        businesses.f(b.cash);
        businesses.f(b.revenue_per_tick);
        businesses.f(b.cost_per_tick);
        businesses.f(b.market_share);
    }
    std::string full = sha256_hex(PersistenceModule::serialize(w));
    std::fprintf(
        f,
        "tick %u state=%.16s nations=%016llx provinces=%016llx npcs=%016llx "
        "markets=%016llx businesses=%016llx\n",
        w.current_tick, full.c_str(), static_cast<unsigned long long>(nations.h),
        static_cast<unsigned long long>(provinces.h), static_cast<unsigned long long>(npcs.h),
        static_cast<unsigned long long>(markets.h), static_cast<unsigned long long>(businesses.h));
    if (w.current_tick > detail_through_tick)
        return;
    for (const auto& p : w.provinces) {
        province_scalars(p, scal);
        for (const auto& kv : scal)
            std::fprintf(f, "  t%u province %u %s %08x %.9g\n", w.current_tick, p.id, kv.first,
                         bits(kv.second), static_cast<double>(kv.second));
    }
    for (const auto& m : w.regional_markets) {
        std::fprintf(f, "  t%u market g%u p%u spot %08x supply %08x demand %08x\n", w.current_tick,
                     m.good_id, m.province_id, bits(m.spot_price), bits(m.supply),
                     bits(m.demand_buffer));
    }
    for (const auto& n : w.significant_npcs) {
        std::fprintf(f, "  t%u npc %u capital %08x social %08x mem %zu rel %zu\n", w.current_tick,
                     n.id, bits(n.capital), bits(n.social_capital), n.memory_log.size(),
                     n.relationships.size());
    }
}

// ── CLI parsing ─────────────────────────────────────────────────────────────

struct Args {
    uint64_t seed = 42;
    uint32_t ticks = 30;
    uint32_t npcs = 200;
    uint32_t provinces = 4;
    std::string out_path;
    std::string trace_path;  // optional: per-tick fingerprints for locating a divergence
};

void print_usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s --seed N --ticks N --npcs N --provinces N --out PATH\n"
                 "  --seed N        World seed (default: 42)\n"
                 "  --ticks N       Ticks to simulate (default: 30)\n"
                 "  --npcs N        Significant NPC count (default: 200)\n"
                 "  --provinces N   Province count (default: 4)\n"
                 "  --out PATH      Output file for SHA-256 hex digest (required)\n"
                 "  --trace PATH    Also write per-tick fingerprints (optional)\n",
                 prog);
}

bool parse_args(int argc, char** argv, Args& out) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %s\n", a);
            return false;
        }
        const char* v = argv[++i];
        if (std::strcmp(a, "--seed") == 0) {
            out.seed = std::strtoull(v, nullptr, 10);
        } else if (std::strcmp(a, "--ticks") == 0) {
            out.ticks = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (std::strcmp(a, "--npcs") == 0) {
            out.npcs = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (std::strcmp(a, "--provinces") == 0) {
            out.provinces = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (std::strcmp(a, "--trace") == 0) {
            out.trace_path = v;
        } else if (std::strcmp(a, "--out") == 0) {
            out.out_path = v;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a);
            return false;
        }
    }
    if (out.out_path.empty()) {
        std::fprintf(stderr, "--out is required\n");
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        print_usage(argv[0]);
        return 2;
    }

    std::fprintf(stderr, "[golden_dump] seed=%llu ticks=%u npcs=%u provinces=%u out=%s\n",
                 static_cast<unsigned long long>(args.seed), args.ticks, args.npcs, args.provinces,
                 args.out_path.c_str());

    auto wall_start = std::chrono::steady_clock::now();

    // Build world via the test factory — keeps the harness independent of
    // CSV-driven WorldGenerator so the only thing varying across platforms
    // is the simulation core itself.
    auto world = create_test_world(args.seed, args.npcs, args.provinces, /*goods_count=*/15);

    // Register the full base-game module pipeline. Single-threaded ThreadPool
    // is intentional: cross-platform divergence in the modules is what we're
    // hunting; thread-pool determinism is exercised elsewhere.
    PackageConfig config{};
    TickOrchestrator orchestrator;
    register_base_game_modules(orchestrator, config);
    orchestrator.set_config(config);
    orchestrator.finalize_registration();

    ThreadPool pool(1);

    std::FILE* trace = nullptr;
    if (!args.trace_path.empty()) {
        trace = std::fopen(args.trace_path.c_str(), "w");
        if (!trace) {
            std::fprintf(stderr, "[golden_dump] could not open %s for writing\n",
                         args.trace_path.c_str());
            return 1;
        }
        write_trace_tick(trace, world, /*detail_through_tick=*/3);
    }
    for (uint32_t i = 0; i < args.ticks; ++i) {
        run_ticks(world, orchestrator, pool, 1);
        if (trace)
            write_trace_tick(trace, world, /*detail_through_tick=*/3);
    }
    if (trace)
        std::fclose(trace);

    // Serialize through the persistence layer — it walks every WorldState
    // field in canonical order and emits a flat byte stream (the LZ4 wrapper
    // is also deterministic given identical input bytes). Hashing this is
    // strictly stronger than hashing the test_world_factory's narrow
    // `serialize_world_state` helper.
    auto bytes = PersistenceModule::serialize(world);

    std::string hex = sha256_hex(bytes);

    std::ofstream out(args.out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "[golden_dump] could not open %s for writing\n",
                     args.out_path.c_str());
        return 1;
    }
    out << hex << '\n';
    out.close();
    if (!out) {
        std::fprintf(stderr, "[golden_dump] write to %s failed\n", args.out_path.c_str());
        return 1;
    }

    auto wall_end = std::chrono::steady_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    std::fprintf(stderr, "[golden_dump] tick=%u serialized=%zu bytes wall=%.1f ms sha256=%s\n",
                 world.current_tick, bytes.size(), wall_ms, hex.c_str());

    // Echo to stdout for human eyeballing (CI consumes the file, not stdout).
    std::printf("%s\n", hex.c_str());
    return 0;
}
