#include "noise.hpp"
#include "../core/saveManager.hpp"

ChunkNoises noiseInit() {
    ChunkNoises noises;

    int seed = SaveManager::getActiveSeed();

    noises.biomeNoise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    noises.biomeNoise.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);
    noises.biomeNoise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Hybrid);
    noises.biomeNoise.SetFrequency(0.0020f);
    noises.biomeNoise.SetSeed(seed + 10);

    noises.biomeDistortNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noises.biomeDistortNoise.SetFrequency(0.03f);
    noises.biomeDistortNoise.SetSeed(seed + 11);

    noises.baseNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noises.baseNoise.SetFrequency(0.005f);
    noises.baseNoise.SetSeed(seed);

    noises.detailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noises.detailNoise.SetFrequency(0.02f);
    noises.detailNoise.SetSeed(seed + 1);

    noises.detail2Noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noises.detail2Noise.SetFrequency(0.05f);
    noises.detail2Noise.SetSeed(seed + 2);

    noises.randomNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noises.randomNoise.SetFrequency(1.00f);
    noises.randomNoise.SetSeed(seed + 3);

    noises.cavePathNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noises.cavePathNoise.SetFrequency(0.02f);
    noises.cavePathNoise.SetSeed(seed + 100);

    noises.caveRadiusNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noises.caveRadiusNoise.SetFrequency(0.05f);
    noises.caveRadiusNoise.SetSeed(seed + 101);

    return noises;
}
