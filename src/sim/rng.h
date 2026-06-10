// PCG32 — the single seeded PRNG living inside SimulationState (research.md §3.2).
// All sim randomness routes through this; no rand(), no time().
#pragma once
#include <cstdint>

namespace neg {

struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL;
    uint64_t inc   = 0xda3e39cb94b95bdbULL;

    void seed(uint64_t initstate, uint64_t initseq) {
        state = 0u;
        inc = (initseq << 1u) | 1u;
        next();
        state += initstate;
        next();
    }

    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // Uniform in [0, bound) without modulo bias.
    uint32_t next_below(uint32_t bound) {
        uint32_t threshold = (uint32_t)(-(int32_t)bound) % bound;
        for (;;) {
            uint32_t r = next();
            if (r >= threshold) return r % bound;
        }
    }
};

} // namespace neg
