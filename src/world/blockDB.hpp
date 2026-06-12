#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <array>
#include <string>
#include <vector>

class BlockDB {
public:
    struct BlockInfo {
        glm::vec2 textureCoords[6];
        std::vector<std::array<glm::vec2,6>> multiTextureCoords;
        bool transparent;
        bool translucent;
        bool liquid;
        float drag;
        std::string name;
        std::string modelName;
        bool renderFacesInBetween;
        std::string tabName;
        uint8_t lightEmission;
    };

    static void init();
    static const BlockInfo* getBlockInfo(const uint16_t& blockName);
    static const std::vector<uint16_t>& getRegisteredBlockIDs();

private:
    static std::unordered_map<uint16_t, BlockInfo> blockData;
    static const BlockInfo* blockDataArray[65536];
    static std::vector<uint16_t> registeredBlockIDs;
};
