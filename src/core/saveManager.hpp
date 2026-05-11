#pragma once

#include <string>
#include <vector>
#include <array>
#include <glm/glm.hpp>

struct WorldInfo {
    std::string uuid;
    std::string name;
    int seed;
    std::string createdAt;
    std::string savePath;
};

class SaveManager {
public:
    static std::vector<WorldInfo> listWorlds();
    static WorldInfo createWorld(const std::string& name, int seed);
    static bool deleteWorld(const std::string& uuid);
    static bool renameWorld(const std::string& uuid, const std::string& newName);

    static void setActiveWorld(const WorldInfo& world);
    static int getActiveSeed();
    static const WorldInfo* getActiveWorld();
    static void clearActiveWorld();
    static bool savePlayerState(const glm::dvec3& position, float yaw, float pitch, const std::array<uint8_t, 9>& hotbar, bool flyEnabled);
    static bool loadPlayerState(glm::dvec3& position, float& yaw, float& pitch, std::array<uint8_t, 9>& hotbar, bool& flyEnabled);

private:
    static std::string generateUUID();
    static std::string getCurrentTimestamp();
    static WorldInfo activeWorld;
    static bool hasActiveWorld;
};
