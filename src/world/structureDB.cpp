#include <filesystem>
#include <fstream>
#include <algorithm>
#include <nlohmannJSON/json.hpp>
#include "structureDB.hpp"
#include "../core/logger.hpp"

// Chances of block spawning
// 1xxxxx = 1 in 2
// 2xxxxx = 1 in 5
// 3xxxxx = 1 in 20

std::unordered_map<std::string, Structure> StructureDB::structures;

void StructureDB::init() {
    LOG_INFO("StructureDB: Initializing...");
    structures.clear();

    namespace fs = std::filesystem;
    fs::path structuresDir = fs::current_path() / "structures";

    if (!fs::exists(structuresDir) || !fs::is_directory(structuresDir)) {
        LOG_ERROR("StructureDB: could not find 'structures' directory at ", structuresDir.string());
        return;
    }

    std::vector<fs::path> files;
    for (auto& entry : fs::directory_iterator(structuresDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (auto& filePath : files) {
        std::string name = filePath.stem().string();

        std::ifstream file(filePath);
        if (!file.is_open()) {
            LOG_WARN("StructureDB: failed to open ", filePath.filename().string());
            continue;
        }

        try {
            nlohmann::json j;
            file >> j;

            std::vector<StructureLayer> layers;
            if (j.contains("layers") && j["layers"].is_array()) {
                for (const auto& layer : j["layers"]) {
                    StructureLayer l;
                    for (const auto& row : layer) {
                        std::vector<uint32_t> r;
                        for (const auto& cell : row)
                            r.push_back(cell.get<uint32_t>());
                        l.push_back(r);
                    }
                    layers.push_back(l);
                    LOG_TRACE("StructureDB: structure '", name, "' parsed layer ", layers.size(), " with dimensions ", l.size(), "x", l[0].size());
                }
            } else {
                LOG_WARN("StructureDB: '", name, "' has no layers array, skipping");
                continue;
            }

            int defaultXOffset = j.value("defaultXOffset", 0);
            int defaultYOffset = j.value("defaultYOffset", 0);
            int defaultZOffset = j.value("defaultZOffset", 0);

            structures[name] = Structure(name, layers, defaultXOffset, defaultYOffset, defaultZOffset);

            LOG_DEBUG("StructureDB: loaded '", name, "' offset=(", defaultXOffset, ",", defaultYOffset, ",", defaultZOffset, ") layers=", layers.size());

        } catch (std::exception& e) {
            LOG_ERROR("StructureDB: JSON parse error in ", filePath.filename().string(), ": ", e.what());
            continue;
        }
    }

    LOG_INFO("StructureDB: loaded ", structures.size(), " structures");
}

const Structure* StructureDB::get(const std::string& name) {
    auto iterator = structures.find(name);
    if (iterator != structures.end())
        return &iterator->second;
    LOG_WARN("StructureDB: unknown structure '", name, "'");
    return nullptr;
}