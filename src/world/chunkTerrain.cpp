#include <map>
#include <cstdint>
#include <random>
#include <cmath>
#include <algorithm>
#include "structureDB.hpp"
#include "noise.hpp"
#include "chunkTerrain.hpp"
#include "biomeDB.hpp"
#include "../core/saveManager.hpp"

void generateCaves(Chunk& chunk) {
    constexpr float PI = 3.14159265358979f;
    constexpr int SEARCH_RADIUS = 5;
    constexpr float STEP_SIZE = 1.2f;
    constexpr float CAVE_SPAWN_PROBABILITY = 0.15f;
    constexpr int LAVA_LEVEL = 8;
    constexpr int WATER_CHECK_RANGE = 4;

    const int worldSeed = SaveManager::getActiveSeed();
    const int chunkX = chunk.chunkX;
    const int chunkZ = chunk.chunkZ;
    const int chunkWidth = Chunk::chunkWidth;
    const int chunkDepth = Chunk::chunkDepth;
    const int chunkHeight = Chunk::chunkHeight;

    const double chunkMinX = static_cast<double>(chunkX) * chunkWidth;
    const double chunkMaxX = chunkMinX + chunkWidth;
    const double chunkMinZ = static_cast<double>(chunkZ) * chunkDepth;
    const double chunkMaxZ = chunkMinZ + chunkDepth;

    std::uniform_real_distribution<float> spawnDist (0.0f,  1.0f);
    std::uniform_real_distribution<float> posYDist  (15.0f, 60.0f);
    std::uniform_real_distribution<float> lengthDist(40.0f, 80.0f);
    std::uniform_real_distribution<float> angleDist (0.0f,  2.0f * PI);
    std::uniform_real_distribution<float> pitchDist (-0.1f * PI, 0.1f * PI);
    std::uniform_real_distribution<float> radiusDist(1.5f,  3.5f);
    std::uniform_int_distribution<int> numWormsDist(1, 3);

    for (int cx = chunkX - SEARCH_RADIUS; cx <= chunkX + SEARCH_RADIUS; ++cx) {
        for (int cz = chunkZ - SEARCH_RADIUS; cz <= chunkZ + SEARCH_RADIUS; ++cz) {
            const uint64_t chunkSeed = static_cast<uint64_t>(cx) * 341873128712ULL + static_cast<uint64_t>(cz) * 132897987541ULL + static_cast<uint64_t>(worldSeed);
            std::mt19937 rng(chunkSeed);

            if (spawnDist(rng) >= CAVE_SPAWN_PROBABILITY)
                continue;

            std::uniform_real_distribution<double> posXDist(static_cast<double>(cx) * chunkWidth, static_cast<double>(cx) * chunkWidth + chunkWidth - 1);
            std::uniform_real_distribution<double> posZDist(static_cast<double>(cz) * chunkDepth, static_cast<double>(cz) * chunkDepth + chunkDepth - 1);
            const int numWorms = numWormsDist(rng);

            for (int i = 0; i < numWorms; ++i) {
                double x = posXDist(rng);
                double y = static_cast<double>(posYDist(rng));
                double z = posZDist(rng);
                const int steps = static_cast<int>(lengthDist(rng));
                float yaw = angleDist(rng);
                float pitch = pitchDist(rng);
                const float baseRadius = radiusDist(rng);

                for (int s = 0; s < steps; ++s) {
                    const float nYaw   = chunk.noises.cavePathNoise.GetNoise(x, y, z);
                    const float nPitch = chunk.noises.cavePathNoise.GetNoise(x + 31329.0, y + 31329.0, z + 31329.0);

                    yaw  += nYaw * 0.35f;
                    pitch = pitch * 0.7f + nPitch * 0.15f;

                    const float nRadius     = chunk.noises.caveRadiusNoise.GetNoise(x, y, z);
                    const float currentRadius = std::max(1.0f, baseRadius + nRadius * 1.5f);
                    const float r2            = currentRadius * currentRadius;

                    // Chunk AABB early exit
                    if (x + currentRadius < chunkMinX || x - currentRadius >= chunkMaxX || z + currentRadius < chunkMinZ || z - currentRadius >= chunkMaxZ) 
                        goto advance_worm;

                    {
                        const int txStart = chunkX * chunkWidth;
                        const int tzStart = chunkZ * chunkDepth;
                        const int minLocalX = std::max(0, static_cast<int>(std::floor(x - currentRadius)) - txStart);
                        const int maxLocalX = std::min(chunkWidth - 1, static_cast<int>(std::floor(x + currentRadius)) - txStart);
                        const int minLocalZ = std::max(0, static_cast<int>(std::floor(z - currentRadius)) - tzStart);
                        const int maxLocalZ = std::min(chunkDepth - 1, static_cast<int>(std::floor(z + currentRadius)) - tzStart);
                        const int minY = std::max(1, static_cast<int>(std::floor(y - currentRadius)));
                        const int maxY = std::min(chunkHeight - 2, static_cast<int>(std::floor(y + currentRadius)));

                        if (minLocalX <= maxLocalX && minLocalZ <= maxLocalZ) {
                            for (int lx = minLocalX; lx <= maxLocalX; ++lx) {
                                const double dx = static_cast<double>(txStart + lx) - x;
                                const double dx2 = dx * dx;
                                if (dx2 >= r2) continue; // entire column outside sphere

                                for (int lz = minLocalZ; lz <= maxLocalZ; ++lz) {
                                    const double dz = static_cast<double>(tzStart + lz) - z;
                                    const double dz2 = dz * dz;
                                    if (dx2 + dz2 >= r2) continue;

                                    for (int wy = minY; wy <= maxY; ++wy) {
                                        const double dy = static_cast<double>(wy) - y;
                                        if (dx2 + dy * dy + dz2 >= r2) continue;

                                        const uint16_t blockType = chunk.blocks[lx][wy][lz].type;

                                        if (blockType == 0 || blockType == 6 || blockType == 9 || blockType == 10)
                                            continue;

                                        bool nearWater = false;

                                        // Check Y axis
                                        {
                                            const int checkUp  = std::min(chunkHeight - 1, wy + WATER_CHECK_RANGE);
                                            const int checkDn  = std::max(0, wy - WATER_CHECK_RANGE);
                                            for (int cy = checkDn; cy <= checkUp && !nearWater; ++cy)
                                                if (chunk.blocks[lx][cy][lz].type == 9)
                                                    nearWater = true;
                                        }

                                        // Check X axis
                                        if (!nearWater) {
                                            const int checkXMin = std::max(0, lx - WATER_CHECK_RANGE);
                                            const int checkXMax = std::min(chunkWidth-1, lx + WATER_CHECK_RANGE);
                                            for (int cx2 = checkXMin; cx2 <= checkXMax && !nearWater; ++cx2)
                                                if (chunk.blocks[cx2][wy][lz].type == 9)
                                                    nearWater = true;
                                        }

                                        // Check Z axis
                                        if (!nearWater) {
                                            const int checkZMin = std::max(0, lz - WATER_CHECK_RANGE);
                                            const int checkZMax = std::min(chunkDepth-1, lz + WATER_CHECK_RANGE);
                                            for (int cz2 = checkZMin; cz2 <= checkZMax && !nearWater; ++cz2)
                                                if (chunk.blocks[lx][wy][cz2].type == 9)
                                                    nearWater = true;
                                        }
                                        if (nearWater) continue;

                                        chunk.blocks[lx][wy][lz].type = (wy < LAVA_LEVEL) ? 10 : 0;
                                    }
                                }
                            }
                        }
                    }

                    advance_worm:
                    x += std::cos(pitch) * std::sin(yaw) * STEP_SIZE;
                    y += std::sin(pitch) * STEP_SIZE;
                    z += std::cos(pitch) * std::cos(yaw) * STEP_SIZE;

                    if (y < 5.0f || y > 85.0f)
                        break;
                }
            }
        }
    }
}

// Helper function to get biome index based on noise value
int getBiomeIndex(float b, int count) {
    if (count == 0) return 0;

    float normalized = (b + 1.0f) / 2.0f;
    int index = static_cast<int>(normalized * static_cast<float>(count));

    if (index >= count)
        index = count - 1;

    return index;
}

void generateChunkTerrain(Chunk& chunk) {
    const int transitionRadius = 5; // blend over 5 blocks (from each side)
    const float biomeDistortStrength = 8.0f;

    const int chunkWidth = Chunk::chunkWidth;
    const int chunkHeight = Chunk::chunkHeight;
    const int chunkDepth = Chunk::chunkDepth;
    auto& noises = chunk.noises;
    int chunkX = chunk.chunkX;
    int chunkZ = chunk.chunkZ;
    
    int biomeCount = BiomeDB::getBiomeCount();

    // Get the "main" biome for this chunk for feature generation
    double chunkWorldX = static_cast<double>(chunkX) * static_cast<double>(chunkWidth);
    double chunkWorldZ = static_cast<double>(chunkZ) * static_cast<double>(chunkDepth);
    float b = noises.biomeNoise.GetNoise(
        chunkWorldX + noises.biomeDistortNoise.GetNoise(chunkWorldX, chunkWorldZ) * biomeDistortStrength,
        chunkWorldZ + noises.biomeDistortNoise.GetNoise(chunkWorldX + 1000.0, chunkWorldZ + 1000.0) * biomeDistortStrength
    );
    int mainBiomeIndex = getBiomeIndex(b, biomeCount);

    // Precompute biome and height values for the blending
    std::vector<std::vector<int>> biomeCache(chunkWidth + 2 * transitionRadius, std::vector<int>(chunkDepth + 2 * transitionRadius));
    std::vector<std::vector<float>> heightCache(chunkWidth + 2 * transitionRadius, std::vector<float>(chunkDepth + 2 * transitionRadius));

    for (int localOffsetX = -transitionRadius; localOffsetX < chunkWidth + transitionRadius; localOffsetX++) {
        for (int localOffsetZ = -transitionRadius; localOffsetZ < chunkDepth + transitionRadius; localOffsetZ++) {
            double worldX = chunkWorldX + static_cast<double>(localOffsetX);
            double worldZ = chunkWorldZ + static_cast<double>(localOffsetZ);

            // Distort biome noise coordinates
            float distortX = noises.biomeDistortNoise.GetNoise((double)worldX, (double)worldZ) * biomeDistortStrength;
            float distortY = noises.biomeDistortNoise.GetNoise((double)worldX + 1000.0, (double)worldZ + 1000.0) * biomeDistortStrength;
            float biomeNoise = noises.biomeNoise.GetNoise((double)worldX + distortX, (double)worldZ + distortY);
            int biomeIdx = getBiomeIndex(biomeNoise, biomeCount);
            biomeCache[localOffsetX + transitionRadius][localOffsetZ + transitionRadius] = biomeIdx;

            float base = noises.baseNoise.GetNoise((double)worldX, (double)worldZ) * 0.5f + 0.5f;
            float detail = noises.detailNoise.GetNoise((double)worldX, (double)worldZ) * 0.5f + 0.5f;
            float detail2 = noises.detail2Noise.GetNoise((double)worldX, (double)worldZ) * 0.5f + 0.5f;

            const BiomeData* biomeData = BiomeDB::getBiome(biomeIdx);
            float heightScale = 1.0f;
            float detailWeight = 0.3f;
            float detail2Weight = 0.2f;
            float power = 1.3f;
            float baseHeight = 30.0f;
            float heightMultiplier = 24.0f;
            float deepenBelowY = 37.0f;
            float deepenFactor = 0.5f;
            float flattenAboveY = -1.0f;

            if (biomeData) {
                heightScale = biomeData->terrain.heightScale;
                detailWeight = biomeData->terrain.detailWeight;
                detail2Weight = biomeData->terrain.detail2Weight;
                power = biomeData->terrain.power;
                baseHeight = biomeData->terrain.baseHeight;
                heightMultiplier = biomeData->terrain.heightMultiplier;
                deepenBelowY = biomeData->terrain.deepenBelowY;
                deepenFactor = biomeData->terrain.deepenFactor;
                flattenAboveY = biomeData->terrain.flattenAboveY;
            }

            float combined = base + detail * detailWeight + detail2 * detail2Weight;
            combined = std::pow(combined, power);

            float height = combined * heightMultiplier * heightScale + baseHeight;
            if (height < deepenBelowY)
                height = height - ((deepenBelowY - height) * deepenFactor);
            if (flattenAboveY >= 0.0f && height > flattenAboveY)
                height = flattenAboveY;

            heightCache[localOffsetX + transitionRadius][localOffsetZ + transitionRadius] = height;
        }
    }

    for (int x = 0; x < chunkWidth; x++) {
        for (int z = 0; z < chunkDepth; z++) {
            int centerBiomeIdx = biomeCache[x + transitionRadius][z + transitionRadius];
            float centerHeight = heightCache[x + transitionRadius][z + transitionRadius];

            // Blending
            bool hasDifferentBiome = false;
            for (int localOffsetX = -transitionRadius; localOffsetX <= transitionRadius && !hasDifferentBiome; localOffsetX++) {
                for (int localOffsetZ = -transitionRadius; localOffsetZ <= transitionRadius && !hasDifferentBiome; localOffsetZ++) {
                    int nBiome = biomeCache[x + localOffsetX + transitionRadius][z + localOffsetZ + transitionRadius];
                    if (nBiome != centerBiomeIdx) {
                        hasDifferentBiome = true;
                    }
                }
            }

            float blendedHeight = 0.0f;
            int finalBiomeIdx = centerBiomeIdx;

            if (hasDifferentBiome) {
                float totalWeight = 0.0f;
                std::map<int, float> biomeWeights;

                for (int localOffsetX = -transitionRadius; localOffsetX <= transitionRadius; localOffsetX++) {
                    for (int localOffsetZ = -transitionRadius; localOffsetZ <= transitionRadius; localOffsetZ++) {
                        float dist2 = static_cast<float>(localOffsetX * localOffsetX + localOffsetZ * localOffsetZ);
                        float weight = 1.0f / (dist2 + 1.0f);

                        int nBiome = biomeCache[x + localOffsetX + transitionRadius][z + localOffsetZ + transitionRadius];
                        float nHeight = heightCache[x + localOffsetX + transitionRadius][z + localOffsetZ + transitionRadius];

                        biomeWeights[nBiome] += weight;
                        blendedHeight += nHeight * weight;
                        totalWeight += weight;
                    }
                }

                blendedHeight /= totalWeight;

                float maxWeight = -1.0f;
                for (auto& [b, w] : biomeWeights) {
                    if (w > maxWeight) {
                        maxWeight = w;
                        finalBiomeIdx = b;
                    }
                }
            } else {
                // No blending needed
                blendedHeight = centerHeight;
                finalBiomeIdx = centerBiomeIdx;
            }

            int height = static_cast<int>(blendedHeight);

            const BiomeData* finalBiome = BiomeDB::getBiome(finalBiomeIdx);
            int waterLevel = finalBiome ? finalBiome->waterLevel : 63;
            int waterBlock = finalBiome ? finalBiome->waterBlock : 9;
            bool hasLayers = finalBiome && !finalBiome->layers.empty();

            for (int y = 0; y < chunkHeight; y++) {
                if (y == 0) {
                    chunk.blocks[x][y][z].type = 6; // Bedrock
                } else if (y > height) {
                    // Above terrain: water or air
                    chunk.blocks[x][y][z].type = (y < waterLevel) ? static_cast<uint16_t>(waterBlock) : 0;
                } else if (hasLayers) {
                    // Layer placement
                    int depthFromTop = height - y; // 0 = surface, 1 = one below, etc.
                    bool placed = false;
                    int layerStartDepth = 0;

                    for (const auto& layer : finalBiome->layers) {
                        if (layer.position == "top") {
                            if (depthFromTop == 0) {
                                // Check Y conditions
                                bool conditionMet = true;
                                if (layer.aboveY >= 0 && y < layer.aboveY)
                                    conditionMet = false;
                                if (layer.belowY >= 0 && y > layer.belowY)
                                    conditionMet = false;

                                if (conditionMet) {
                                    chunk.blocks[x][y][z].type = static_cast<uint16_t>(layer.block);
                                } else if (layer.fallbackBlock >= 0) {
                                    chunk.blocks[x][y][z].type = static_cast<uint16_t>(layer.fallbackBlock);
                                } else {
                                    chunk.blocks[x][y][z].type = static_cast<uint16_t>(layer.block);
                                }
                                placed = true;
                                layerStartDepth = 1;
                                break;
                            }
                        } else if (layer.position == "below_top") {
                            if (depthFromTop >= layerStartDepth && depthFromTop < layerStartDepth + layer.depth) {
                                chunk.blocks[x][y][z].type = static_cast<uint16_t>(layer.block);
                                placed = true;
                                break;
                            }
                            layerStartDepth += layer.depth;
                        } else if (layer.position == "fill") {
                            if (depthFromTop >= layerStartDepth) {
                                chunk.blocks[x][y][z].type = static_cast<uint16_t>(layer.block);
                                placed = true;
                                break;
                            }
                        }
                    }

                    if (!placed) {
                        chunk.blocks[x][y][z].type = 3; // Stone fallback
                    }
                } else {
                    chunk.blocks[x][y][z].type = 3; // Stone fallback
                }
            }
        }
    }

    generateCaves(chunk);

    // Biome specific features
    chunk.biomeIndex = mainBiomeIndex;
    const BiomeData* mainBiome = BiomeDB::getBiome(mainBiomeIndex);
    for (const auto& feature : mainBiome->features) {
        if (feature.type == "structure") {
            generateChunkBiomeFeatures(chunk, feature.threshold, feature.xOffset, feature.zOffset, feature.structure, feature.allowedBlock, feature.seedOffset, feature.yOffset);
        } else if (feature.type == "block") {
            generateChunkBiomeBlocks(chunk, feature.threshold, feature.block, feature.allowedBlock, feature.seedOffset, feature.yOffset.value_or(0));
        } else if (feature.type == "ore") {
            generateChunkBiomeOres(chunk, feature.threshold, feature.block, feature.allowedBlock, feature.seedOffset, feature.minCount, feature.maxCount, feature.yMin, feature.yMax, feature.spread);
        }
    }
}    

StructureLayer rotateLayer(const StructureLayer& layer, int rot) {
    int h = static_cast<int>(layer.size());
    int w = static_cast<int>(layer[0].size());
    StructureLayer out;

    switch (rot) {
        case 0: // 0deg
            return layer;

        case 1: // 90°
            out.assign(w, std::vector<uint32_t>(h));
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    out[x][h - 1 - y] = layer[y][x];
            return out;

        case 2: // 180deg
            out.assign(h, std::vector<uint32_t>(w));
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    out[h - 1 - y][w - 1 - x] = layer[y][x];
            return out;

        case 3: // 270deg
            out.assign(w, std::vector<uint32_t>(h));
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    out[w - 1 - x][y] = layer[y][x];
            return out;
    }
    return layer;
}

Structure rotateStructure(const Structure& in, int rot) {
    Structure out = in;
    out.layers.clear();
    out.layers.reserve(in.layers.size());

    for (const StructureLayer& layer : in.layers) {
        out.layers.push_back(rotateLayer(layer, rot));
    }
    return out;
}

static inline float seededHash(int wx, int wz, int seed) {
    uint32_t h = static_cast<uint32_t>(seed);
    h ^= static_cast<uint32_t>(wx) * 2246822519u;
    h ^= static_cast<uint32_t>(wz) * 3266489917u;
    h *= 668265263u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    // Map to [-1, 1]
    return (static_cast<float>(h) / static_cast<float>(UINT32_MAX)) * 2.0f - 1.0f;
}

void generateChunkBiomeFeatures(Chunk& chunk, float threshold, std::optional<int> xOffset, std::optional<int> zOffset, std::string structureName, int allowedBlockID, int seedOffset, std::optional<int> yOffset) {
    const Structure* original = StructureDB::get(structureName);
    if (!original) return;

    int actualXOffset = xOffset.value_or(original->defaultXOffset);
    int actualYOffset = yOffset.value_or(original->defaultYOffset);
    int actualZOffset = zOffset.value_or(original->defaultZOffset);

    int chunkWorldX = chunk.chunkX * Chunk::chunkWidth;
    int chunkWorldZ = chunk.chunkZ * Chunk::chunkDepth;

    struct Placement {
        int x, y, z;
    };
    std::vector<Placement> placements;

    for (int x = 0; x < Chunk::chunkWidth; x++) {
        for (int z = 0; z < Chunk::chunkDepth; z++) {
            int worldX = chunkWorldX + x;
            int worldZ = chunkWorldZ + z;
            float n = seededHash(worldX, worldZ, seedOffset);
            if (n > threshold) {
                int y = Chunk::chunkHeight - 2;
                while (y > 0 && chunk.blocks[x][y][z].type == 0) y--;

                if (chunk.blocks[x][y][z].type == allowedBlockID) {
                    placements.push_back({x, y, z});
                }
            }
        }
    }

    if (placements.empty()) return;

    int chunkSeed = seedOffset ^ (chunk.chunkX * 1619) ^ (chunk.chunkZ * 31337);
    float r = seededHash(chunk.chunkX, chunk.chunkZ, chunkSeed);
    int rot = static_cast<int>((r + 1.0f) * 0.5f * 4.0f) % 4;
    Structure rotated = rotateStructure(*original, rot);

    for (const auto& p : placements) {
        chunk.placeStructure(rotated, p.x - actualXOffset, (p.y + 1) + actualYOffset, p.z - actualZOffset);
    }
}

void generateChunkBiomeBlocks(Chunk& chunk, float threshold, int blockID, int allowedBlockID, int seedOffset, int yOffset) {
    int chunkWorldX = chunk.chunkX * Chunk::chunkWidth;
    int chunkWorldZ = chunk.chunkZ * Chunk::chunkDepth;
    for (int x = 0; x < Chunk::chunkWidth; x++) {
        for (int z = 0; z < Chunk::chunkDepth; z++) {
            int worldX = chunkWorldX + x;
            int worldZ = chunkWorldZ + z;

            float n = seededHash(worldX, worldZ, seedOffset);
            if (n > threshold) {
                int y = Chunk::chunkHeight - 2;
                while (y > 0 && chunk.blocks[x][y][z].type == 0) y--;

                if (chunk.blocks[x][y][z].type == allowedBlockID) {
                    int ty = (y + 1) + yOffset;
                    if (ty >= 0 && ty < Chunk::chunkHeight) 
                        chunk.blocks[x][ty][z].type = static_cast<uint16_t>(blockID);
                }
            }
        }
    }
}

void generateChunkBiomeOres(Chunk& chunk, float threshold, int blockID, int allowedBlockID, int seedOffset, int minCount, int maxCount, int yMin, int yMax, float spread) {
    const int mainBiomeIndex = chunk.biomeIndex;
    const int chunkWidth = Chunk::chunkWidth;
    const int chunkDepth = Chunk::chunkDepth;
    const int chunkHeight = Chunk::chunkHeight;
    const int chunkWorldX = chunk.chunkX * chunkWidth;
    const int chunkWorldZ = chunk.chunkZ * chunkDepth;

    const float biomeDistortStrength = 8.0f;
    const int biomeCount = BiomeDB::getBiomeCount();
    auto& noises = chunk.noises;

    // Search radius of 6 blocks around the current chunk
    const int R = 6;

    for (int rx = -R; rx < chunkWidth + R; rx++) {
        for (int rz = -R; rz < chunkDepth + R; rz++) {
            int worldX = chunkWorldX + rx;
            int worldZ = chunkWorldZ + rz;
            float biomeDistortX = noises.biomeDistortNoise.GetNoise((double)worldX, (double)worldZ) * biomeDistortStrength;
            float biomeDistortY = noises.biomeDistortNoise.GetNoise((double)worldX + 1000.0, (double)worldZ + 1000.0) * biomeDistortStrength;
            float b = noises.biomeNoise.GetNoise((double)worldX + biomeDistortX, (double)worldZ + biomeDistortY);
            int colBiomeIdx = getBiomeIndex(b, biomeCount);

            if (colBiomeIdx != mainBiomeIndex) 
                continue;

            float n = seededHash(worldX, worldZ, seedOffset);
            if (n > threshold) {
                uint64_t veinSeed = static_cast<uint64_t>(worldX) * 341873128712ULL + static_cast<uint64_t>(worldZ) * 132897987541ULL + static_cast<uint64_t>(seedOffset) + static_cast<uint64_t>(SaveManager::getActiveSeed());
                std::mt19937 rng(veinSeed);

                std::uniform_int_distribution<int> yDist(yMin, yMax);
                int startY = yDist(rng);
                std::uniform_int_distribution<int> countDist(minCount, maxCount);
                int count = countDist(rng);

                std::vector<glm::ivec3> candidates;
                std::vector<glm::ivec3> visited;

                auto isVisited = [&](const glm::ivec3& pos) {
                    for (const auto& v : visited) {
                        if (v.x == pos.x && v.y == pos.y && v.z == pos.z) return true;
                    }
                    return false;
                };

                glm::ivec3 center(worldX, startY, worldZ);
                candidates.push_back(center);
                visited.push_back(center);

                int placedCount = 0;
                glm::ivec3 lastPlaced = center;

                auto addNeighbors = [&](int wx, int wy, int wz) {
                    static const glm::ivec3 dirs[6] = {
                        { 0,  0,  1}, { 0,  0, -1},
                        {-1,  0,  0}, { 1,  0,  0},
                        { 0,  1,  0}, { 0, -1,  0}
                    };
                    for (int i = 0; i < 6; ++i) {
                        int nx = wx + dirs[i].x;
                        int ny = wy + dirs[i].y;
                        int nz = wz + dirs[i].z;
                        if (ny >= yMin && ny <= yMax &&
                            ny >= 1 && ny < chunkHeight - 1) {
                            glm::ivec3 nextPos(nx, ny, nz);
                            if (!isVisited(nextPos)) {
                                visited.push_back(nextPos);
                                candidates.push_back(nextPos);
                            }
                        }
                    }
                };

                std::uniform_real_distribution<float> floatDist(0.0f, 1.0f);
                while (placedCount < count && !candidates.empty()) {
                    int chosenIndex = -1;
                    float r = floatDist(rng);

                    if (r >= spread) {
                        float minDistSq = 1e9f;
                        float perturbation = 4.0f;
                        for (size_t i = 0; i < candidates.size(); ++i) {
                            glm::vec3 diff = glm::vec3(candidates[i] - center);
                            float d2 = glm::dot(diff, diff);
                            float score = d2 + floatDist(rng) * perturbation;
                            if (score < minDistSq) {
                                minDistSq = score;
                                chosenIndex = static_cast<int>(i);
                            }
                        }
                    } else {
                        std::vector<int> adjacentCandidates;
                        for (size_t i = 0; i < candidates.size(); ++i) {
                            glm::ivec3 diff = candidates[i] - lastPlaced;
                            if (std::abs(diff.x) + std::abs(diff.y) + std::abs(diff.z) == 1) {
                                adjacentCandidates.push_back(static_cast<int>(i));
                            }
                        }

                        if (!adjacentCandidates.empty()) {
                            std::uniform_int_distribution<int> adjDist(0, (int)adjacentCandidates.size() - 1);
                            chosenIndex = adjacentCandidates[adjDist(rng)];
                        } else {
                            std::uniform_int_distribution<int> candDist(0, (int)candidates.size() - 1);
                            chosenIndex = candDist(rng);
                        }
                    }

                    if (chosenIndex != -1) {
                        glm::ivec3 nextPos = candidates[chosenIndex];
                        candidates.erase(candidates.begin() + chosenIndex);

                        int localX = nextPos.x - chunkWorldX;
                        int localZ = nextPos.z - chunkWorldZ;

                        if (localX >= 0 && localX < chunkWidth &&
                            localZ >= 0 && localZ < chunkDepth) {
                            if (chunk.blocks[localX][nextPos.y][localZ].type == allowedBlockID) {
                                chunk.blocks[localX][nextPos.y][localZ].type = static_cast<uint16_t>(blockID);
                                placedCount++;
                                lastPlaced = nextPos;
                                addNeighbors(nextPos.x, nextPos.y, nextPos.z);
                            }
                        } else {
                            placedCount++;
                            lastPlaced = nextPos;
                            addNeighbors(nextPos.x, nextPos.y, nextPos.z);
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    }
}
