#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <array>

using StructureLayer = std::vector<std::vector<uint32_t>>;

class Structure {
public:
    std::string name;
    std::vector<StructureLayer> layers;
    StructureLayer fillLayer;
    bool hasFillLayer = false;
    int defaultXOffset = 0;
    int defaultYOffset = 0;
    int defaultZOffset = 0;

    Structure() = default;
    Structure(const std::string& name, const std::vector<StructureLayer>& layers, int xOffset = 0, int yOffset = 0, int zOffset = 0, const StructureLayer& fillLayer = {}, bool hasFillLayer = false)
        : name(name), layers(layers), fillLayer(fillLayer), hasFillLayer(hasFillLayer || !fillLayer.empty()), defaultXOffset(xOffset), defaultYOffset(yOffset), defaultZOffset(zOffset) {}
};

class StructureDB {
public:
    static void init();
    static const Structure* get(const std::string& name);
    static const Structure* getRotated(const std::string& name, int rot);

private:
    static std::unordered_map<std::string, std::array<Structure, 4>> structures;
};
