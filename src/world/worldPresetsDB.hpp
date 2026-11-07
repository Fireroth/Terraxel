#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmannJSON/json.hpp>

struct CaveParams {
    bool enabled = true;
    std::vector<uint16_t> blocksToNotCarve = {0, 6, 9, 10, 69, 164};
    std::vector<uint16_t> blocksToNotCarveAround = {9, 69, 164};
    int noCarveAroundSearchRadius = 2;
    float spawnProbability = 0.15f;
    int lavaLevel = 8;
};

struct BottomLayerParams {
    uint16_t block = 6;
    int height = 1;
};

struct SpawnPlatformParams {
    bool enabled = false;
    uint16_t block = 3;
    int height = 64; // Y level
};

struct WorldPreset {
    std::string id;
    std::string name;
    std::vector<std::string> biomeIDs;
    std::vector<int> biomeIndices;
    CaveParams caves;
    int waterLevel = 63;
    BottomLayerParams bottomLayer;
    SpawnPlatformParams spawnPlatform;
    nlohmann::json rawJson;
};

class WorldPresetsDB {
public:
    static void init();
    static const WorldPreset* getPreset(const std::string& id);
    static const WorldPreset* getPresetByIndex(size_t index);
    static size_t getPresetCount();
    static const std::vector<WorldPreset>& getAllPresets();
    static const WorldPreset* getDefaultPreset();
    static int getDefaultPresetIndex();

    static void setActivePreset(const std::string& id);
    static const WorldPreset* getActivePreset();
    static void clearActivePreset();

    static int sampleBiomeIndex(float noiseValue);
    static int getWaterLevel();
    static uint16_t getBottomLayerBlock();
    static int getBottomLayerHeight();
    static bool hasSpawnPlatform();
    static uint16_t getSpawnPlatformBlock();
    static int getSpawnPlatformHeight();
    static void resolveBiomeIndices();

private:
    static std::vector<WorldPreset> presets;
    static std::unordered_map<std::string, size_t> presetIdToIndex;
    static const WorldPreset* activePreset;
    static std::string activePresetId;
};

