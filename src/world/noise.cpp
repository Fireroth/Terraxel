#include "noise.hpp"
#include "../core/saveManager.hpp"

ChunkNoises noiseInit() {
    ChunkNoises noises;

    int seed = SaveManager::getActiveSeed();

    noises.biomeNoise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    noises.biomeNoise.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);
    noises.biomeNoise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Hybrid);
    noises.biomeNoise.SetFrequency(0.0015f);
    noises.biomeNoise.SetSeed(seed + 150);

    noises.biomeDistortNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noises.biomeDistortNoise.SetFrequency(0.03f);
    noises.biomeDistortNoise.SetSeed(seed + 151);

    noises.randomNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noises.randomNoise.SetFrequency(1.00f);
    noises.randomNoise.SetSeed(seed + 152);

    noises.cavePathNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noises.cavePathNoise.SetFrequency(0.02f);
    noises.cavePathNoise.SetSeed(seed + 153);

    noises.caveRadiusNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noises.caveRadiusNoise.SetFrequency(0.05f);
    noises.caveRadiusNoise.SetSeed(seed + 154);

    return noises;
}
