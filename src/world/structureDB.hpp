#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>

using StructureLayer = std::vector<std::vector<uint32_t>>;

class Structure {
public:
    std::string name;
    std::vector<StructureLayer> layers;
    int defaultXOffset = 0;
    int defaultYOffset = 0;
    int defaultZOffset = 0;

    Structure() = default;
    Structure(const std::string& name, const std::vector<StructureLayer>& layers, int xOffset = 0, int yOffset = 0, int zOffset = 0)
        : name(name), layers(layers), defaultXOffset(xOffset), defaultYOffset(yOffset), defaultZOffset(zOffset) {}
};

class StructureDB {
public:
    static void init();
    static const Structure* get(const std::string& name);

private:
    static std::unordered_map<std::string, Structure> structures;
};
