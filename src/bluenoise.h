#pragma once
#include <cstdint>
#include <vector>

// Blue-noise threshold mask by Ulichney's void-and-cluster method (1993).
// Returns size*size ranks scaled to [0, 1): neighbouring pixels get values that
// are as different as possible, so noise built on it looks even rather than
// clumpy. Deterministic for a given seed. ~20 ms for 64x64.
std::vector<float> makeBlueNoise(int size, uint32_t seed);
