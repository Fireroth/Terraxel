#include "worldPresetsDB.hpp"
#include "biomeDB.hpp"
#include "../core/logger.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::vector<WorldPreset> WorldPresetsDB::presets;
std::unordered_map<std::string, size_t> WorldPresetsDB::presetIdToIndex;
const WorldPreset* WorldPresetsDB::activePreset = nullptr;
std::string WorldPresetsDB::activePresetId = "";

void WorldPresetsDB::init() {
    LOG_INFO("WorldPresetsDB: Initializing...");
    presets.clear();
    presetIdToIndex.clear();
    activePreset = nullptr;
    activePresetId = "";

    fs::path presetsDir = fs::current_path() / "assets/worldPresets";
    if (!fs::exists(presetsDir) || !fs::is_directory(presetsDir)) {
        presetsDir = fs::current_path() / "worldPresets";
    }

    if (!fs::exists(presetsDir) || !fs::is_directory(presetsDir)) {
        LOG_ERROR("WorldPresetsDB: could not find 'worldPresets' directory at ", presetsDir.string());
        return;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(presetsDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& filePath : files) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            LOG_WARN("WorldPresetsDB: failed to open ", filePath.string());
            continue;
        }

        try {
            json j;
            file >> j;

            WorldPreset preset;
            preset.name = j.value("name", filePath.stem().string());
            preset.id = j.value("id", filePath.stem().string());
            preset.rawJson = j;

            if (j.contains("biomes") && j["biomes"].is_array()) {
                for (const auto& b : j["biomes"]) {
                    if (b.is_string()) {
                        preset.biomeIDs.push_back(b.get<std::string>());
                    }
                }
            }

            // Parse cave parameters
            if (j.contains("caves") && j["caves"].is_object()) {
                const auto& cavesJson = j["caves"];
                preset.caves.enabled = cavesJson.value("enabled", true);
                preset.caves.spawnProbability = cavesJson.value("spawnProbability", 0.15f);
                preset.caves.lavaLevel = cavesJson.value("lavaLevel", 8);
                preset.caves.noCarveAroundSearchRadius = cavesJson.value("noCarveAroundSearchRadius", 2);

                if (cavesJson.contains("blocksToNotCarve") && cavesJson["blocksToNotCarve"].is_array()) {
                    preset.caves.blocksToNotCarve.clear();
                    for (const auto& blockId : cavesJson["blocksToNotCarve"]) {
                        if (blockId.is_number_unsigned()) {
                            preset.caves.blocksToNotCarve.push_back(blockId.get<uint16_t>());
                        }
                    }
                }

                if (cavesJson.contains("blocksToNotCarveAround") && cavesJson["blocksToNotCarveAround"].is_array()) {
                    preset.caves.blocksToNotCarveAround.clear();
                    for (const auto& blockId : cavesJson["blocksToNotCarveAround"]) {
                        if (blockId.is_number_unsigned()) {
                            preset.caves.blocksToNotCarveAround.push_back(blockId.get<uint16_t>());
                        }
                    }
                }

                LOG_DEBUG("WorldPresetsDB: caves for '", preset.id, "': enabled=", preset.caves.enabled,
                          ", spawnProbability=", preset.caves.spawnProbability,
                          ", lavaLevel=", preset.caves.lavaLevel,
                          ", blocksToNotCarve count=", preset.caves.blocksToNotCarve.size(),
                          ", blocksToNotCarveAround count=", preset.caves.blocksToNotCarveAround.size(),
                          ", noCarveAroundSearchRadius=", preset.caves.noCarveAroundSearchRadius);
            }

            // Parse water level
            preset.waterLevel = j.value("waterLevel", 63);

            // Parse bottom layer parameters
            if (j.contains("bottomLayer") && j["bottomLayer"].is_object()) {
                const auto& bl = j["bottomLayer"];
                preset.bottomLayer.block = bl.value("block", static_cast<uint16_t>(6));
                preset.bottomLayer.height = bl.value("height", 1);
            }

            // Parse spawn platform parameters
            if (j.contains("spawnPlatform") && j["spawnPlatform"].is_object()) {
                const auto& sp = j["spawnPlatform"];
                preset.spawnPlatform.enabled = sp.value("enabled", false);
                preset.spawnPlatform.block = sp.value("block", static_cast<uint16_t>(3));
                preset.spawnPlatform.height = sp.value("height", 64);
            }

            size_t index = presets.size();
            presetIdToIndex[preset.id] = index;
            presets.push_back(preset);

            LOG_DEBUG("WorldPresetsDB: loaded preset '", preset.name, "' (id: ", preset.id, ") with ", preset.biomeIDs.size(), " biomes");
        } catch (const std::exception& e) {
            LOG_ERROR("WorldPresetsDB: JSON parse error in ", filePath.string(), ": ", e.what());
        }
    }

    resolveBiomeIndices();
    LOG_INFO("WorldPresetsDB: Loaded ", presets.size(), " presets");
}

void WorldPresetsDB::resolveBiomeIndices() {
    for (auto& preset : presets) {
        preset.biomeIndices.clear();
        for (const auto& biomeId : preset.biomeIDs) {
            int idx = BiomeDB::getBiomeIndexByID(biomeId);
            if (idx >= 0) {
                preset.biomeIndices.push_back(idx);
            } else {
                LOG_WARN("WorldPresetsDB: Biome ID '", biomeId, "' not found in BiomeDB for preset '", preset.id, "'");
            }
        }
    }
}

const WorldPreset* WorldPresetsDB::getPreset(const std::string& id) {
    auto it = presetIdToIndex.find(id);
    if (it != presetIdToIndex.end() && it->second < presets.size()) {
        return &presets[it->second];
    }
    return nullptr;
}

const WorldPreset* WorldPresetsDB::getPresetByIndex(size_t index) {
    if (index < presets.size()) {
        return &presets[index];
    }
    return nullptr;
}

size_t WorldPresetsDB::getPresetCount() {
    return presets.size();
}

const std::vector<WorldPreset>& WorldPresetsDB::getAllPresets() {
    return presets;
}

const WorldPreset* WorldPresetsDB::getDefaultPreset() {
    auto it = presetIdToIndex.find("classic");
    if (it != presetIdToIndex.end() && it->second < presets.size()) {
        return &presets[it->second];
    }
    if (!presets.empty()) {
        return &presets[0];
    }
    return nullptr;
}

int WorldPresetsDB::getDefaultPresetIndex() {
    auto it = presetIdToIndex.find("classic");
    if (it != presetIdToIndex.end() && it->second < presets.size()) {
        return static_cast<int>(it->second);
    }
    return 0;
}

void WorldPresetsDB::setActivePreset(const std::string& id) {
    activePresetId = id;
    activePreset = getPreset(id);
    if (!activePreset) {
        LOG_WARN("WorldPresetsDB: Preset '", id, "' not found, falling back to default");
        activePreset = getDefaultPreset();
    }
    if (activePreset) {
        LOG_INFO("WorldPresetsDB: Active preset set to '", activePreset->name, "' (id: ", activePreset->id, ")");
    }
}

const WorldPreset* WorldPresetsDB::getActivePreset() {
    return activePreset;
}

void WorldPresetsDB::clearActivePreset() {
    activePreset = nullptr;
    activePresetId.clear();
}

int WorldPresetsDB::getWaterLevel() {
    if (activePreset) {
        return activePreset->waterLevel;
    }
    return 63;
}

uint16_t WorldPresetsDB::getBottomLayerBlock() {
    if (activePreset) {
        return activePreset->bottomLayer.block;
    }
    return 6;
}

int WorldPresetsDB::getBottomLayerHeight() {
    if (activePreset) {
        return activePreset->bottomLayer.height;
    }
    return 1;
}

bool WorldPresetsDB::hasSpawnPlatform() {
    if (activePreset) {
        return activePreset->spawnPlatform.enabled;
    }
    return false;
}

uint16_t WorldPresetsDB::getSpawnPlatformBlock() {
    if (activePreset) {
        return activePreset->spawnPlatform.block;
    }
    return 3;
}

int WorldPresetsDB::getSpawnPlatformHeight() {
    if (activePreset) {
        return activePreset->spawnPlatform.height;
    }
    return 64;
}

int WorldPresetsDB::sampleBiomeIndex(float noiseValue) {
    auto mapNoise = [](float b, int count) -> int {
        if (count <= 0) return 0;
        float normalized = (b + 1.0f) * 0.5f;
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        int index = static_cast<int>(normalized * static_cast<float>(count));
        if (index >= count) index = count - 1;
        if (index < 0) index = 0;
        return index;
    };

    if (activePreset) {
        if (activePreset->biomeIndices.empty() && !activePreset->biomeIDs.empty()) {
            resolveBiomeIndices();
        }
        if (!activePreset->biomeIndices.empty()) {
            int slot = mapNoise(noiseValue, static_cast<int>(activePreset->biomeIndices.size()));
            return activePreset->biomeIndices[slot];
        }
    }

    int totalBiomeCount = BiomeDB::getBiomeCount();
    return mapNoise(noiseValue, totalBiomeCount);
}

