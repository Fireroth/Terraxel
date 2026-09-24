#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <FastNoiseLite.h>

enum class BiomeNoiseOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    Min,
    Max,
    Set
};

struct BiomeNoise {
    FastNoiseLite noise;
    std::string type = "OpenSimplex2";
    float frequency = 0.01f;
    int seedOffset = 0;
    float weight = 1.0f;
    float offset = 0.0f;
    float power = 1.0f;
    BiomeNoiseOp operation = BiomeNoiseOp::Add;
    bool normalize = true;

    std::string fractalType = "None";
    int octaves = 3;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float weightedStrength = 0.0f;
    float pingPongStrength = 2.0f;

    std::string cellularDistanceFunction = "EuclideanSq";
    std::string cellularReturnType = "Distance";
    float cellularJitter = 1.0f;

    std::string domainWarpType = "None";
    float domainWarpAmp = 1.0f;

    void initNoise(int worldSeed);
    float evaluate(double worldX, double worldZ) const;
};

struct BiomeTerrainParams {
    float baseHeight = 30.0f;
};

enum class BiomeModifierType {
    Deepen,
    Clamp,
    Add,
    Multiply,
    Power
};

struct BiomeModifier {
    BiomeModifierType type = BiomeModifierType::Deepen;
    float belowY = 0.0f;
    float aboveY = 0.0f;
    float factor = 0.0f;
    float value = 0.0f;
    bool hasBelowY = false;
    bool hasAboveY = false;

    float apply(float height) const;
};

struct BiomeLayerOverride {
    std::optional<int> minY;
    std::optional<int> maxY;
    std::optional<int> block;
    std::optional<int> depth;
};

struct BiomeLayer {
    std::string position = "fill"; // "top", "below_top" or "fill"
    int block = 3;
    int depth = 1;
    std::vector<BiomeLayerOverride> overrides;

    std::pair<int, int> resolve(int height) const {
        int effBlock = block;
        int effDepth = depth;
        for (const auto& ov : overrides) {
            if (ov.minY.has_value() && height < *ov.minY) continue;
            if (ov.maxY.has_value() && height > *ov.maxY) continue;
            if (ov.block.has_value()) effBlock = *ov.block;
            if (ov.depth.has_value()) effDepth = *ov.depth;
            break;
        }
        if (effDepth < 0) effDepth = 0;
        return {effBlock, effDepth};
    }
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
    std::vector<BiomeNoise> noises;
    std::vector<BiomeModifier> modifiers;
    std::vector<BiomeLayer> layers;
    std::vector<BiomeFeature> features;
    int waterBlock = 9;
    int waterLevel = 37;

    float evaluateTerrainNoise(double worldX, double worldZ) const;
    float applyModifiers(float height) const;
};

class BiomeDB {
public:
    static void init();
    static const BiomeData* getBiome(int index);
    static int getBiomeCount();
    static const BiomeData* getBiomeByName(const std::string& id);
    static void setWorldSeed(int seed);

private:
    static std::vector<BiomeData> biomes;
    static std::unordered_map<std::string, int> biomeNameToIndex;
};
