#include <filesystem>
#include <fstream>
#include <algorithm>
#include <nlohmannJSON/json.hpp>
#include "biomeDB.hpp"
#include "../core/logger.hpp"

std::vector<BiomeData> BiomeDB::biomes;
std::unordered_map<std::string, int> BiomeDB::biomeNameToIndex;


void BiomeDB::init() {
    LOG_INFO("BiomeDB: Initializing...");
    biomes.clear();
    biomeNameToIndex.clear();

    namespace fs = std::filesystem;
    fs::path biomesDir = fs::current_path() / "biomes";
    bool hasBiomesDir = fs::exists(biomesDir) && fs::is_directory(biomesDir);

    if (!hasBiomesDir) {
        LOG_ERROR("BiomeDB::init: could not find 'biomes' directory at ", biomesDir.string());
    } else {
        std::vector<fs::path> files;
        for (auto& entry : fs::directory_iterator(biomesDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        for (auto& filePath : files) {
            std::ifstream in(filePath);
            if (!in.is_open()) {
                LOG_WARN("BiomeDB::init: failed to open ", filePath.string());
                continue;
            }

            try {
                nlohmann::json j;
                in >> j;

                BiomeData biome;
                biome.name = j.value("name", "Unknown");
                biome.id = j.value("id", "unknown");
                biome.waterBlock = j.value("waterBlock", 9);
                biome.waterLevel = j.value("waterLevel", 37);

                // Parse terrain params
                if (j.contains("terrain") && j["terrain"].is_object()) {
                    auto& t = j["terrain"];
                    biome.terrain.heightScale = t.value("heightScale", 1.0f);
                    biome.terrain.detailWeight = t.value("detailWeight", 0.3f);
                    biome.terrain.detail2Weight = t.value("detail2Weight", 0.2f);
                    biome.terrain.power = t.value("power", 1.3f);
                    biome.terrain.baseHeight = t.value("baseHeight", 30.0f);
                    biome.terrain.heightMultiplier = t.value("heightMultiplier", 24.0f);
                    biome.terrain.deepenBelowY = t.value("deepenBelowY", 37.0f);
                    biome.terrain.deepenFactor = t.value("deepenFactor", 0.5f);
                    biome.terrain.flattenAboveY = t.value("flattenAboveY", -1.0f);
                } else {
                    LOG_WARN("BiomeDB: biome '", biome.id, "' has no terrain block, using defaults");
                }

                // Parse layers
                if (j.contains("layers") && j["layers"].is_array()) {
                    for (auto& layerJson : j["layers"]) {
                        BiomeLayer layer;
                        layer.block = layerJson.value("block", 3);
                        layer.depth = layerJson.value("depth", 1);
                        layer.position = layerJson.value("position", std::string("fill"));
                        layer.aboveY = layerJson.value("aboveY", -1);
                        layer.belowY = layerJson.value("belowY", -1);
                        layer.fallbackBlock = layerJson.value("fallbackBlock", -1);
                        biome.layers.push_back(layer);
                        LOG_TRACE("BiomeDB: biome '", biome.id, "' parsed layer: ", layer.block, " with depth ", layer.depth);
                    }
                    LOG_DEBUG("BiomeDB: biome '", biome.id, "' parsed ", biome.layers.size(), " layers");
                } else {
                    LOG_WARN("BiomeDB: biome '", biome.id, "' has no layers block, using defaults");
                }

                // Parse features
                if (j.contains("features") && j["features"].is_array()) {
                    for (auto& featureJson : j["features"]) {
                        BiomeFeature feature;
                        feature.type = featureJson.value("type", std::string("block"));
                        feature.structure = featureJson.value("structure", std::string(""));
                        feature.block = featureJson.value("block", 0);
                        feature.threshold = featureJson.value("threshold", 0.99f);
                        if (featureJson.contains("xOffset")) {
                            feature.xOffset = featureJson["xOffset"].get<int>();
                        }
                        if (featureJson.contains("zOffset")) {
                            feature.zOffset = featureJson["zOffset"].get<int>();
                        }
                        if (featureJson.contains("yOffset")) {
                            feature.yOffset = featureJson["yOffset"].get<int>();
                        }
                        feature.allowedBlock = featureJson.value("allowedBlock", 1);
                        feature.seedOffset = featureJson.value("seedOffset", 0);
                        feature.minCount = featureJson.value("minCount", 1);
                        feature.maxCount = featureJson.value("maxCount", 8);
                        feature.yMin = featureJson.value("yMin", 0);
                        feature.yMax = featureJson.value("yMax", 256);
                        feature.spread = featureJson.value("spread", 1.0f);
                        biome.features.push_back(feature);
                        LOG_TRACE("BiomeDB: biome '", biome.id, "' parsed feature: type=", feature.type,
                                  feature.type == "structure" ? ", structure=" + feature.structure : ", block=" + std::to_string(feature.block));
                    }
                    LOG_DEBUG("BiomeDB: biome '", biome.id, "' parsed ", biome.features.size(), " features");
                }

                int index = static_cast<int>(biomes.size());
                biomeNameToIndex[biome.id] = index;
                biomes.push_back(biome);

                LOG_DEBUG("BiomeDB: loaded biome '", biome.name, "' (id: ", biome.id, ")");

            } catch (std::exception& e) {
                LOG_ERROR("BiomeDB: JSON parse error in ", filePath.string(), ": ", e.what());
                continue;
            }
        }
    }

    if (biomes.empty()) {
        LOG_WARN("BiomeDB: no biomes found, registering fallback biome.");
        BiomeData defaultBiome;
        defaultBiome.name = "Fallback Biome";
        defaultBiome.id = "default";
        defaultBiome.waterBlock = 65000;
        defaultBiome.waterLevel = 37;

        defaultBiome.terrain.heightScale = 0.0f;
        defaultBiome.terrain.detailWeight = 0.0f;
        defaultBiome.terrain.detail2Weight = 0.0f;
        defaultBiome.terrain.power = 1.0f;
        defaultBiome.terrain.baseHeight = 30.0f;
        defaultBiome.terrain.heightMultiplier = 24.0f;
        defaultBiome.terrain.deepenBelowY = 37.0f;
        defaultBiome.terrain.deepenFactor = 0.0f;
        defaultBiome.terrain.flattenAboveY = -1.0f;

        BiomeLayer layer1;
        layer1.block = 65000;
        layer1.depth = 1;
        layer1.position = "top";

        BiomeLayer layer2;
        layer2.block = 65000;
        layer2.depth = 1;
        layer2.position = "fill";

        defaultBiome.layers.push_back(layer1);
        defaultBiome.layers.push_back(layer2);

        int index = static_cast<int>(biomes.size());
        biomeNameToIndex[defaultBiome.id] = index;
        biomes.push_back(defaultBiome);
    }

    LOG_INFO("BiomeDB: loaded ", biomes.size(), " biomes");
}

const BiomeData* BiomeDB::getBiome(int index) {
    if (index < 0 || index >= static_cast<int>(biomes.size())) {
        if (!biomes.empty()) return &biomes[0];
        return nullptr;
    }
    return &biomes[index];
}

int BiomeDB::getBiomeCount() {
    return static_cast<int>(biomes.size());
}

const BiomeData* BiomeDB::getBiomeByName(const std::string& id) {
    auto it = biomeNameToIndex.find(id);
    if (it != biomeNameToIndex.end())
        return &biomes[it->second];
    return nullptr;
}
