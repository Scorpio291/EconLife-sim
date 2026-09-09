#pragma once

// test_process_id — the current process id, on both toolchains.
//
// ctest runs each TEST_CASE as its own process and -j6 runs several at once, so
// tests that write CSV fixtures put them in a PER-PROCESS temp directory: a
// shared fixed path is a race, and two cases writing the same fixture once left
// one of them reading a half-written file. It passed alone and failed in the
// suite, which is the signature.
//
// The pid is what makes that directory unique, and it is the one piece of it
// that is not portable: `getpid()` lives in <unistd.h>, which MSVC does not
// have. On Linux the tests compiled without including it at all, because
// libstdc++ pulls <unistd.h> in transitively — so the missing include was
// invisible until Windows said so.

#include <string>

#if defined(_WIN32)
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

namespace econlife::test {

inline long process_id() {
#if defined(_WIN32)
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

// "<prefix><pid>" — a directory name unique to this process.
inline std::string process_scoped_name(const std::string& prefix) {
    return prefix + std::to_string(process_id());
}

}  // namespace econlife::test
