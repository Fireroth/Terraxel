#include <nlohmannJSON/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include "saveManager.hpp"
#include "logger.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

WorldInfo SaveManager::activeWorld;
bool SaveManager::hasActiveWorld = false;

static const std::string SAVES_DIR = "./saves";

std::string SaveManager::generateUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    const char* hex = "0123456789abcdef";
    std::string uuid;
    uuid.reserve(32);
    for (int i = 0; i < 32; i++) {
        uuid += hex[dis(gen)];
    }
    return uuid;
}

std::string SaveManager::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    #ifdef _WIN32
        localtime_s(&tm, &time_t_val);
    #else
        localtime_r(&time_t_val, &tm);
    #endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%b %d, %Y at %H:%M");
    return oss.str();
}

std::vector<WorldInfo> SaveManager::listWorlds() {
    std::vector<WorldInfo> worlds;

    if (!fs::exists(SAVES_DIR)) return worlds;

    for (const auto& entry : fs::directory_iterator(SAVES_DIR)) {
        if (!entry.is_directory()) continue;

        std::string worldJsonPath = entry.path().string() + "/world.json";
        if (!fs::exists(worldJsonPath)) continue;

        std::ifstream file(worldJsonPath);
        if (!file.is_open()) continue;

        try {
            json j;
            file >> j;

            WorldInfo info;
            info.uuid = j.value("uuid", "");
            info.name = j.value("name", "Unnamed World");
            info.seed = j.value("seed", 0);
            info.createdAt = j.value("createdAt", "");
            info.savePath = entry.path().string();

            worlds.push_back(info);
        } catch (std::exception& e) {
            LOG_ERROR("SaveManager: Error parsing world info for ", entry.path().string(), ": ", e.what());
            continue;
        }
    }

    // Sort by creation time
    std::sort(worlds.begin(), worlds.end(), [](const WorldInfo& a, const WorldInfo& b) {
        return a.createdAt > b.createdAt;
    });

    return worlds;
}

WorldInfo SaveManager::createWorld(const std::string& name, int seed) {
    WorldInfo info;
    info.uuid = generateUUID();
    info.name = name;
    info.seed = seed;
    info.createdAt = getCurrentTimestamp();
    info.savePath = SAVES_DIR + "/" + info.uuid;

    fs::create_directories(info.savePath);
    fs::create_directories(info.savePath + "/chunks");

    json j;
    j["uuid"] = info.uuid;
    j["name"] = info.name;
    j["seed"] = info.seed;
    j["createdAt"] = info.createdAt;

    std::ofstream file(info.savePath + "/world.json");
    file << j.dump(4);
    LOG_INFO("SaveManager: Created world '", info.name, "' with UUID ", info.uuid, " at ", info.createdAt);

    return info;
}

bool SaveManager::deleteWorld(const std::string& uuid) {
    std::string path = SAVES_DIR + "/" + uuid;
    if (fs::exists(path)) {
        fs::remove_all(path);
        LOG_INFO("SaveManager: Deleted world '", uuid, "'");
        return true;
    }
    LOG_WARN("SaveManager: World '", uuid, "' not found");
    return false;
}

bool SaveManager::renameWorld(const std::string& uuid, const std::string& newName) {
    std::string worldJsonPath = SAVES_DIR + "/" + uuid + "/world.json";
    if (!fs::exists(worldJsonPath)) return false;

    try {
        std::ifstream file(worldJsonPath);
        json j;
        file >> j;
        file.close();

        j["name"] = newName;

        std::ofstream outFile(worldJsonPath);
        outFile << j.dump(4);
        outFile.close();
        return true;
    } catch (std::exception& e) {
        LOG_ERROR("SaveManager: Error renaming world: ", e.what());
        return false;
    }
}

void SaveManager::setActiveWorld(const WorldInfo& world) {
    activeWorld = world;
    hasActiveWorld = true;
}

int SaveManager::getActiveSeed() {
    if (hasActiveWorld) return activeWorld.seed;
    return 1234;
}

const WorldInfo* SaveManager::getActiveWorld() {
    if (hasActiveWorld) return &activeWorld;
    return nullptr;
}

void SaveManager::clearActiveWorld() {
    hasActiveWorld = false;
}

bool SaveManager::savePlayerState(const glm::dvec3& position, float yaw, float pitch, const std::array<uint16_t, 9>& hotbar, bool flyEnabled, bool isSneaking) {
    if (!hasActiveWorld) {
        return false;
    }

    try {
        const std::string playerPath = activeWorld.savePath + "/player.json";
        json j;
        j["position"] = {
            {"x", position.x},
            {"y", position.y},
            {"z", position.z}
        };
        j["yaw"] = yaw;
        j["pitch"] = pitch;
        j["flyEnabled"] = flyEnabled;
        j["isSneaking"] = isSneaking;
        j["hotbar"] = hotbar;

        std::ofstream file(playerPath);
        if (!file.is_open()) {
            return false;
        }
        file << j.dump(4);
        LOG_DEBUG("Player state: ", j.dump());
        LOG_INFO("SaveManager: Saved player state at ", playerPath);
        return true;
    } catch (std::exception& e) {
        LOG_ERROR("SaveManager: Error saving player state: ", e.what());
        return false;
    }
}

bool SaveManager::loadPlayerState(glm::dvec3& position, float& yaw, float& pitch, std::array<uint16_t, 9>& hotbar, bool& flyEnabled, bool& isSneaking) {
    if (!hasActiveWorld) {
        return false;
    }

    const std::string playerPath = activeWorld.savePath + "/player.json";
    if (!fs::exists(playerPath)) {
        return false;
    }

    try {
        std::ifstream file(playerPath);
        if (!file.is_open()) {
            return false;
        }

        json j;
        file >> j;

        const auto& pos = j.at("position");
        if (!pos.is_object()) {
            return false;
        }

        const auto& hotbarJson = j.at("hotbar");
        if (!hotbarJson.is_array() || hotbarJson.size() != hotbar.size()) {
            return false;
        }

        position.x = pos.at("x").get<double>();
        position.y = pos.at("y").get<double>();
        position.z = pos.at("z").get<double>();
        yaw = j.at("yaw").get<float>();
        pitch = j.at("pitch").get<float>();
        flyEnabled = j.at("flyEnabled").get<bool>();
        isSneaking = j.value("isSneaking", false);
        for (size_t i = 0; i < hotbar.size(); i++) {
            hotbar[i] = hotbarJson[i].get<uint16_t>();
        }

        return true;
    } catch (std::exception& e) {
        LOG_ERROR("SaveManager: Error loading player state: ", e.what());
        return false;
    }
}
