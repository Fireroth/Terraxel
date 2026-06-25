#pragma once

#include <optional>
#include "chunk.hpp"

void generateChunkTerrain(Chunk& chunk);
void generateChunkBiomeFeatures(Chunk& chunk, float treshold, std::optional<int> xOffset, std::optional<int> zOffset, std::string structureName, int allowedBlockID, int seedOffset, std::optional<int> yOffset);
void generateChunkBiomeBlocks(Chunk& chunk, float treshold, int blockID, int allowedBlockID, int seedOffset, int yOffset);
void generateChunkBiomeOres(Chunk& chunk, float threshold, int blockID, int allowedBlockID, int seedOffset, int minCount, int maxCount, int yMin, int yMax, float spread);
