#pragma once

#include <FastNoiseLite.h>

struct ChunkNoises {
    FastNoiseLite biomeNoise;
    FastNoiseLite biomeDistortNoise;
    FastNoiseLite randomNoise;
    FastNoiseLite cavePathNoise;
    FastNoiseLite caveRadiusNoise;
};

ChunkNoises noiseInit();
