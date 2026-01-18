
#pragma once

#include <thread>
#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
# include <immintrin.h>
#endif

namespace util {

// Low-level spin hint used inside short spin loops.
// On x86 use _mm_pause(); otherwise fall back to std::this_thread::yield().
inline void cpu_relax() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

// Monotonic nanosecond timestamp (steady_clock)
inline uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace util