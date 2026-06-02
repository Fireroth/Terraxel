#include <map>
#include <cstdint>
#include "structureDB.hpp"
#include "noise.hpp"
#include "chunkTerrain.hpp"
#include "biomeDB.hpp"

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

    // Biome specific features
    chunk.biomeIndex = mainBiomeIndex;
    const BiomeData* mainBiome = BiomeDB::getBiome(mainBiomeIndex);
    for (const auto& feature : mainBiome->features) {
        if (feature.type == "structure") {
            generateChunkBiomeFeatures(chunk, feature.threshold, feature.xOffset, feature.zOffset, feature.structure, feature.allowedBlock, feature.seedOffset, feature.yOffset);
        } else if (feature.type == "block") {
            generateChunkBiomeBlocks(chunk, feature.threshold, feature.block, feature.allowedBlock, feature.seedOffset, feature.yOffset.value_or(0));
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
