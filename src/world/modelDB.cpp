#include "modelDB.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <nlohmannJSON/json.hpp>
#include "../core/logger.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

std::unordered_map<std::string, Model> ModelDB::models;

void ModelDB::init() {
    LOG_INFO("ModelDB: Initializing...");
    models.clear();

    fs::path modelsDir = fs::current_path() / "models";
    if (!fs::exists(modelsDir) || !fs::is_directory(modelsDir)) {
        LOG_ERROR("ModelDB: could not find 'models' directory at ", modelsDir);
        return;
    }

    for (const auto& entry : fs::directory_iterator(modelsDir)) {
        if (!entry.is_regular_file())
            continue;
        auto path = entry.path();
        if (path.extension() != ".json")
            continue;

        std::ifstream file(path);
        if (!file) {
            LOG_WARN("ModelDB: failed to open ", path.filename().string());
            continue;
        }

        json j;
        try {
            file >> j;
        } catch (std::exception& e) {
            LOG_ERROR("ModelDB: JSON parse error in ", path.filename().string(), ": ", e.what());
            continue;
        }

        Model model;

        if (j.contains("cuboids")) {
            for (const auto& cuboidJ : j["cuboids"]) {
                Cuboid cuboid;
                auto from = cuboidJ["from"];
                auto to = cuboidJ["to"];
                cuboid.from = glm::vec3(from[0], from[1], from[2]);
                cuboid.to = glm::vec3(to[0], to[1], to[2]);
                if (cuboidJ.contains("faces")) {
                    for (auto& [faceName, faceJ] : cuboidJ["faces"].items()) {
                        Face face;
                        if (faceJ.contains("uv")) {
                            auto uvJ = faceJ["uv"];
                            if (uvJ.is_object() && uvJ.contains("from") && uvJ.contains("to")) {
                                auto from = uvJ["from"];
                                auto to = uvJ["to"];
                                face.uvFrom = glm::vec2(from[0], from[1]);
                                face.uvTo = glm::vec2(to[0], to[1]);
                            } else {
                                face.uvFrom = glm::vec2(0.0f, 0.0f);
                                face.uvTo = glm::vec2(1.0f, 1.0f);
                            }
                        } else {
                            face.uvFrom = glm::vec2(0.0f, 0.0f);
                            face.uvTo = glm::vec2(1.0f, 1.0f);
                        }
                        cuboid.faces[faceName] = face;
                    }
                    LOG_TRACE("ModelDB: '", path.stem().string(), "' parsed ", cuboid.faces.size(), " faces");
                }
                if (cuboidJ.contains("collisions")) {
                    const auto &colJ = cuboidJ["collisions"];
                    if (colJ.contains("enabled")) cuboid.collisions.enabled = colJ["enabled"].get<bool>();
                    if (colJ.contains("box") && colJ["box"].is_array()) {
                        for (const auto &boxJ : colJ["box"]) {
                            Cuboid::CollisionBox cb;
                            auto bf = boxJ["from"];
                            auto bt = boxJ["to"];
                            cb.from = glm::vec3(bf[0], bf[1], bf[2]);
                            cb.to = glm::vec3(bt[0], bt[1], bt[2]);
                            cuboid.collisions.box.push_back(cb);
                        }
                    }
                    LOG_TRACE("ModelDB: '", path.stem().string(), "' parsed ", cuboid.collisions.box.size(), " collision boxes");
                }
                model.cuboids.push_back(cuboid);
            }
            LOG_DEBUG("ModelDB: '", path.stem().string(), "' parsed ", model.cuboids.size(), " cuboids");
        }

        if (j.contains("plane") || j.contains("planes")) {
            const auto &planesJ = j.contains("planes") ? j["planes"] : j["plane"];
            for (const auto& planeJ : planesJ) {
                Plane plane;
                auto from = planeJ["from"];
                auto to = planeJ["to"];
                plane.from = glm::vec3(from[0], from[1], from[2]);
                plane.to = glm::vec3(to[0], to[1], to[2]);
                if (planeJ.contains("rotation")) {
                    const auto &r = planeJ["rotation"];
                    if (r.contains("origin")) {
                        auto o = r["origin"];
                        plane.rotationOrigin = glm::vec3(o[0], o[1], o[2]);
                    }
                    if (r.contains("axis")) {
                        std::string a = r["axis"].get<std::string>();
                        if (!a.empty()) plane.rotationAxis = a[0];
                    }
                    if (r.contains("angle")) {
                        plane.rotationAngle = r["angle"].get<float>();
                    }
                    if (r.contains("position") && r["position"].is_number()) {
                        plane.positionOffset = r["position"].get<float>();
                        if (plane.rotationAxis != '\0' && plane.positionDirection == '\0') {
                            plane.positionDirection = plane.rotationAxis;
                        }
                    }
                }
                if (planeJ.contains("position")) {
                    const auto &pp = planeJ["position"];
                    if (pp.is_number()) {
                        plane.positionOffset = pp.get<float>();
                        if (plane.rotationAxis != '\0' && plane.positionDirection == '\0') {
                            plane.positionDirection = plane.rotationAxis;
                        }
                    } else if (pp.is_object()) {
                        if (pp.contains("direction")) {
                            std::string d = pp["direction"].get<std::string>();
                            if (!d.empty()) plane.positionDirection = d[0];
                        }
                        if (pp.contains("offset")) {
                            plane.positionOffset = pp["offset"].get<float>();
                        }
                    }
                }
                if (planeJ.contains("faces")) {
                    for (auto& [faceName, faceJ] : planeJ["faces"].items()) {
                        Face face;
                        if (faceJ.contains("uv")) {
                            auto uvJ = faceJ["uv"];
                            if (uvJ.is_object() && uvJ.contains("from") && uvJ.contains("to")) {
                                auto from = uvJ["from"];
                                auto to = uvJ["to"];
                                face.uvFrom = glm::vec2(from[0], from[1]);
                                face.uvTo = glm::vec2(to[0], to[1]);
                            } else {
                                face.uvFrom = glm::vec2(0.0f, 0.0f);
                                face.uvTo = glm::vec2(1.0f, 1.0f);
                            }
                        } else {
                            face.uvFrom = glm::vec2(0.0f, 0.0f);
                            face.uvTo = glm::vec2(1.0f, 1.0f);
                        }
                        plane.faces[faceName] = face;
                    }
                }
                if (planeJ.contains("collisions")) {
                    const auto &colJ = planeJ["collisions"];
                    if (colJ.contains("enabled")) plane.collisions.enabled = colJ["enabled"].get<bool>();
                    if (colJ.contains("box") && colJ["box"].is_array()) {
                        for (const auto &boxJ : colJ["box"]) {
                            Plane::CollisionBox cb;
                            auto bf = boxJ["from"];
                            auto bt = boxJ["to"];
                            cb.from = glm::vec3(bf[0], bf[1], bf[2]);
                            cb.to = glm::vec3(bt[0], bt[1], bt[2]);
                            plane.collisions.box.push_back(cb);
                        }
                    }
                }
                model.planes.push_back(plane);
            }
            LOG_DEBUG("ModelDB: '", path.stem().string(), "' parsed ", model.planes.size(), " planes");
        }

        std::string name = path.stem().string();
        models[name] = std::move(model);
        LOG_DEBUG("ModelDB: loaded model '", name, "'");
    }

    LOG_INFO("ModelDB: loaded ", models.size(), " models");
}

const Model* ModelDB::getModel(const std::string& name) {
    auto iterator = models.find(name);
    if (iterator != models.end()) return &iterator->second;
    LOG_WARN("ModelDB: unknown model '", name, "', registering fallback cube model");

    auto cubeIt = models.find("cube");
    if (cubeIt != models.end()) {
        models[name] = cubeIt->second;
    } else {
        Model fallbackCube;
        Cuboid cuboid;
        cuboid.from = glm::vec3(0.0f, 0.0f, 0.0f);
        cuboid.to = glm::vec3(1.0f, 1.0f, 1.0f);
        Face face;
        face.uvFrom = glm::vec2(0.0f, 0.0f);
        face.uvTo = glm::vec2(1.0f, 1.0f);
        cuboid.faces["north"] = face;
        cuboid.faces["south"] = face;
        cuboid.faces["east"] = face;
        cuboid.faces["west"] = face;
        cuboid.faces["up"] = face;
        cuboid.faces["down"] = face;
        cuboid.collisions.enabled = true;
        fallbackCube.cuboids.push_back(cuboid);
        models[name] = fallbackCube;
    }
    return &models[name];
}

bool ModelDB::getCollisionBoxes(const std::string& modelName, std::vector<std::pair<glm::vec3, glm::vec3>>& outBoxes) {
    auto iterator = models.find(modelName);
    if (iterator == models.end()) {
        getModel(modelName);
        iterator = models.find(modelName);
    }
    const Model& model = iterator->second;

    for (const auto& cub : model.cuboids) {
        if (!cub.collisions.enabled) continue;
        if (!cub.collisions.box.empty()) {
            for (const auto& b : cub.collisions.box)
                outBoxes.emplace_back(b.from, b.to);
        } else {
            outBoxes.emplace_back(cub.from, cub.to);
        }
    }
    for (const auto& plane : model.planes) {
        if (!plane.collisions.enabled) continue;
        if (!plane.collisions.box.empty()) {
            for (const auto& b : plane.collisions.box)
                outBoxes.emplace_back(b.from, b.to);
        } else {
            outBoxes.emplace_back(plane.from, plane.to);
        }
    }

    return !outBoxes.empty();
}

bool ModelDB::getHitBoxes(const std::string& modelName, std::vector<std::pair<glm::vec3, glm::vec3>>& outBoxes) {
    auto iterator = models.find(modelName);
    if (iterator == models.end()) {
        getModel(modelName);
        iterator = models.find(modelName);
    }
    const Model& model = iterator->second;

    bool hasCustomBox = false;
    for (const auto& cub : model.cuboids)
        if (!cub.collisions.box.empty()) { hasCustomBox = true; break; }
    if (!hasCustomBox)
        for (const auto& plane : model.planes)
            if (!plane.collisions.box.empty()) { hasCustomBox = true; break; }

    for (const auto& cub : model.cuboids) {
        if (!cub.collisions.box.empty()) {
            for (const auto& b : cub.collisions.box)
                outBoxes.emplace_back(b.from, b.to);
        } else {
            if (!hasCustomBox || cub.collisions.enabled)
                outBoxes.emplace_back(cub.from, cub.to);
        }
    }
    for (const auto& plane : model.planes) {
        if (!plane.collisions.box.empty()) {
            for (const auto& b : plane.collisions.box)
                outBoxes.emplace_back(b.from, b.to);
        } else {
            if (!hasCustomBox || plane.collisions.enabled)
                outBoxes.emplace_back(plane.from, plane.to);
        }
    }
    return !outBoxes.empty();
}