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

std::unordered_map<std::string, std::array<Structure, 4>> StructureDB::structures;

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

            StructureLayer fillLayer;
            bool hasFillLayer = false;
            if (j.contains("fillLayer") && j["fillLayer"].is_array() && !j["fillLayer"].empty()) {
                try {
                    // Check if 3D array: [ [ [row] ] ]
                    if (j["fillLayer"][0].is_array() && !j["fillLayer"][0].empty() && j["fillLayer"][0][0].is_array()) {
                        for (const auto& row : j["fillLayer"][0]) {
                            std::vector<uint32_t> r;
                            for (const auto& cell : row)
                                r.push_back(cell.get<uint32_t>());
                            fillLayer.push_back(r);
                        }
                        hasFillLayer = !fillLayer.empty();
                    } else if (j["fillLayer"][0].is_array()) { // 2D array: [ [row] ]
                        for (const auto& row : j["fillLayer"]) {
                            std::vector<uint32_t> r;
                            for (const auto& cell : row)
                                r.push_back(cell.get<uint32_t>());
                            fillLayer.push_back(r);
                        }
                        hasFillLayer = !fillLayer.empty();
                    }
                } catch (const std::exception& e) {
                    LOG_WARN("StructureDB: failed to parse fillLayer for '", name, "': ", e.what());
                }
            }

            int defaultXOffset = j.value("defaultXOffset", 0);
            int defaultYOffset = j.value("defaultYOffset", 0);
            int defaultZOffset = j.value("defaultZOffset", 0);

            Structure baseStruct(name, layers, defaultXOffset, defaultYOffset, defaultZOffset, fillLayer, hasFillLayer);
            
            auto rotateLayer = [](const StructureLayer& layer, int rot) -> StructureLayer {
                if (layer.empty() || layer[0].empty() || rot == 0) return layer;
                int h = static_cast<int>(layer.size());
                int w = static_cast<int>(layer[0].size());
                StructureLayer out;
                switch (rot) {
                    case 1: // 90 deg
                        out.assign(w, std::vector<uint32_t>(h));
                        for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++)
                                out[x][h - 1 - y] = layer[y][x];
                        return out;
                    case 2: // 180 deg
                        out.assign(h, std::vector<uint32_t>(w));
                        for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++)
                                out[h - 1 - y][w - 1 - x] = layer[y][x];
                        return out;
                    case 3: // 270 deg
                        out.assign(w, std::vector<uint32_t>(h));
                        for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++)
                                out[w - 1 - x][y] = layer[y][x];
                        return out;
                }
                return layer;
            };

            auto rotateStructure = [&](const Structure& in, int rot) -> Structure {
                if (rot == 0) return in;
                Structure out = in;
                out.layers.clear();
                out.layers.reserve(in.layers.size());
                for (const StructureLayer& layer : in.layers) {
                    out.layers.push_back(rotateLayer(layer, rot));
                }
                if (in.hasFillLayer && !in.fillLayer.empty()) {
                    out.fillLayer = rotateLayer(in.fillLayer, rot);
                }
                return out;
            };

            std::array<Structure, 4> rotated;
            rotated[0] = baseStruct;
            rotated[1] = rotateStructure(baseStruct, 1);
            rotated[2] = rotateStructure(baseStruct, 2);
            rotated[3] = rotateStructure(baseStruct, 3);

            structures[name] = std::move(rotated);

            LOG_DEBUG("StructureDB: loaded '", name, "' offset=(", defaultXOffset, ",", defaultYOffset, ",", defaultZOffset, ") layers=", layers.size(), " hasFillLayer=", hasFillLayer);

        } catch (std::exception& e) {
            LOG_ERROR("StructureDB: JSON parse error in ", filePath.filename().string(), ": ", e.what());
            continue;
        }
    }

    LOG_INFO("StructureDB: loaded ", structures.size(), " structures");
}

const Structure* StructureDB::get(const std::string& name) {
    return getRotated(name, 0);
}

const Structure* StructureDB::getRotated(const std::string& name, int rot) {
    auto iterator = structures.find(name);
    if (iterator != structures.end()) {
        int r = (rot % 4 + 4) % 4;
        return &iterator->second[r];
    }
    LOG_WARN("StructureDB: unknown structure '", name, "'");
    return nullptr;
}