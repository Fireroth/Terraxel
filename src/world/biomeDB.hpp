#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

struct BiomeTerrainParams {
    float heightScale = 1.0f;
    float detailWeight = 0.3f;
    float detail2Weight = 0.2f;
    float power = 1.3f;
    float baseHeight = 30.0f;
    float heightMultiplier = 24.0f;
    float deepenBelowY = 37.0f;
    float deepenFactor = 0.5f;
    float flattenAboveY = -1.0f;
};

struct BiomeLayer {
    int block = 3;
    int depth = 1;
    std::string position = "fill"; // "top", "below_top" or "fill"
    int aboveY = -1;
    int belowY = -1;
    int fallbackBlock = -1;
};

struct BiomeFeature {
    std::string type; // "structure", "block", or "ore"
    std::string structure; // for type "structure"
    int block = 0; // for type "block" and "ore"
    float threshold = 0.99f;
    std::optional<int> xOffset;
    std::optional<int> yOffset;
    std::optional<int> zOffset;
    int allowedBlock = 1;
    int seedOffset = 0;

    // Ore specific parameters
    int minCount = 1;
    int maxCount = 8;
    int yMin = 0;
    int yMax = 256;
    float spread = 1.0f;
};

struct BiomeData {
    std::string name;
    std::string id;
    BiomeTerrainParams terrain;
    std::vector<BiomeLayer> layers;
    std::vector<BiomeFeature> features;
    int waterBlock = 9;
    int waterLevel = 37;
};

class BiomeDB {
public:
    static void init();
    static const BiomeData* getBiome(int index);
    static int getBiomeCount();
    static const BiomeData* getBiomeByName(const std::string& id);

private:
    static std::vector<BiomeData> biomes;
    static std::unordered_map<std::string, int> biomeNameToIndex;
};
