#pragma once

// module_state_io — the byte primitives for ITickModule::serialize_state.
//
// Every module that opted into the module-state section grew its own local copy
// of these four functions. That was tolerable while the set was small; it is not
// a reason for the next module to write a fifth. New serializers use these.
//
// Little-endian fixed-width throughout, matching the existing blocks, so the
// save stays byte-identical across platforms and the determinism harness can
// compare images directly.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace econlife::state_io {

inline void put_u8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

inline void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

inline void put_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, bits);
}

inline void put_str(std::vector<uint8_t>& out, const std::string& s) {
    put_u32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

// A bounds-checked cursor. Every length in a save file is untrusted input, so
// a short or corrupt block sets `error` and yields zeroes rather than reading
// past the end.
struct Reader {
    const uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;
    bool error = false;

    Reader(const uint8_t* d, std::size_t n) : data(d), size(n) {}

    bool need(std::size_t n) {
        if (pos + n > size) {
            error = true;
            return false;
        }
        return true;
    }

    uint8_t u8() {
        if (!need(1))
            return 0;
        return data[pos++];
    }

    uint32_t u32() {
        if (!need(4))
            return 0;
        const uint32_t v = static_cast<uint32_t>(data[pos]) |
                           (static_cast<uint32_t>(data[pos + 1]) << 8) |
                           (static_cast<uint32_t>(data[pos + 2]) << 16) |
                           (static_cast<uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }

    float f32() {
        const uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    std::string str() {
        const uint32_t n = u32();
        if (!need(n))
            return {};
        std::string s(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return s;
    }
};

}  // namespace econlife::state_io
