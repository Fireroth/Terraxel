#pragma once

#include <vector>
#include <set>
#include <mutex>
#include <glm/glm.hpp>

class Chunk;
class World;

namespace VoxelLighting {
    extern std::mutex lightingMutex;

    struct LightNode {
        int x;
        int y;
        int z;
        Chunk* chunk;
        LightNode(int x, int y, int z, Chunk* chunk)
            : x(x), y(y), z(z), chunk(chunk) {}
    };

    struct LightRemovalNode {
        int x;
        int y;
        int z;
        int value;
        Chunk* chunk;
        LightRemovalNode(int x, int y, int z, Chunk* chunk, int value)
            : x(x), y(y), z(z), value(value), chunk(chunk) {}
    };

    bool isLightPassable(uint16_t blockType);

    void propagateSkyLight(World* world, std::vector<LightNode>& sunlightQueue, std::set<Chunk*>& chunksToRemesh);
    void propagateBlockLight(World* world, std::vector<LightNode>& lightQueue, std::set<Chunk*>& chunksToRemesh);
    void getHighestSkyLightNeighbor(World* world, Chunk* chunkData, int x, int y, int z, int& outLevel, Chunk*& outChunk, int& outX, int& outY, int& outZ);

    std::set<Chunk*> calculateFullLighting(World* world, Chunk* chunkData);

    std::set<Chunk*> addSkyLightBlocker(World* world, Chunk* chunkData, int x, int y, int z);
    std::set<Chunk*> removeSkyLightBlocker(World* world, Chunk* chunkData, int x, int y, int z);

    std::set<Chunk*> addLightEmitter(World* world, Chunk* chunkData, int x, int y, int z, int lightLevel);
    std::set<Chunk*> removeLightEmitter(World* world, Chunk* chunkData, int x, int y, int z);

    std::set<Chunk*> addLightBlocker(World* world, Chunk* chunkData, int x, int y, int z);
    std::set<Chunk*> removeLightBlocker(World* world, Chunk* chunkData, int x, int y, int z);
}
