#include "lighting.hpp"
#include "chunk.hpp"
#include "world.hpp"
#include "../core/options.hpp"

#include <array>
#include <unordered_map>

namespace VoxelLighting {
    std::mutex lightingMutex;

    bool isLightPassable(uint16_t blockType) {
        if (blockType == 0) return true;

        static std::array<int8_t, 65536> cache = [] {
            std::array<int8_t, 65536> c;
            c.fill(-1); // -1 = not yet computed
            return c;
        }();

        int8_t& cached = cache[blockType];
        if (cached != -1) {
            return cached != 0;
        }

        const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(blockType);
        bool result;
        if (!info) {
            result = true;
        } else {
            result = info->transparent || info->translucent || info->liquid || (info->modelName != "cube");
        }

        cached = result ? 1 : 0;
        return result;
    }

    struct ChunkNeighbors {
        Chunk* negX = nullptr;
        Chunk* posX = nullptr;
        Chunk* negZ = nullptr;
        Chunk* posZ = nullptr;
        Chunk* negXnegZ = nullptr;
        Chunk* posXnegZ = nullptr;
        Chunk* negXposZ = nullptr;
        Chunk* posXposZ = nullptr;
    };

    class NeighborCache {
    public:
        explicit NeighborCache(World* world) : world_(world) {}

        const ChunkNeighbors& get(Chunk* c) {
            if (c == lastChunk_) return *lastNeighbors_;

            auto it = cache_.find(c);
            if (it != cache_.end()) {
                lastChunk_ = c;
                lastNeighbors_ = &it->second;
                return *lastNeighbors_;
            }

            ChunkNeighbors n;
            n.negX = world_->getChunk(c->chunkX - 1, c->chunkZ);
            n.posX = world_->getChunk(c->chunkX + 1, c->chunkZ);
            n.negZ = world_->getChunk(c->chunkX,     c->chunkZ - 1);
            n.posZ = world_->getChunk(c->chunkX,     c->chunkZ + 1);
            n.negXnegZ = world_->getChunk(c->chunkX - 1, c->chunkZ - 1);
            n.posXnegZ = world_->getChunk(c->chunkX + 1, c->chunkZ - 1);
            n.negXposZ = world_->getChunk(c->chunkX - 1, c->chunkZ + 1);
            n.posXposZ = world_->getChunk(c->chunkX + 1, c->chunkZ + 1);

            auto& inserted = cache_.emplace(c, n).first->second;
            lastChunk_ = c;
            lastNeighbors_ = &inserted;
            return inserted;
        }

    private:
        World* world_;
        std::unordered_map<Chunk*, ChunkNeighbors> cache_;
        Chunk* lastChunk_ = nullptr;
        const ChunkNeighbors* lastNeighbors_ = nullptr;
    };

    inline void markChunkAndNeighborsForRemesh(Chunk* chunk, int x, int z, std::set<Chunk*>& chunksToRemesh, NeighborCache& neighbors) {
        if (!chunk) return;
        chunksToRemesh.insert(chunk);

        const ChunkNeighbors& n = neighbors.get(chunk);

        bool atMinX = (x == 0);
        bool atMaxX = (x == Chunk::chunkWidth - 1);
        bool atMinZ = (z == 0);
        bool atMaxZ = (z == Chunk::chunkDepth - 1);

        if (atMinX && n.negX) chunksToRemesh.insert(n.negX);
        if (atMaxX && n.posX) chunksToRemesh.insert(n.posX);
        if (atMinZ && n.negZ) chunksToRemesh.insert(n.negZ);
        if (atMaxZ && n.posZ) chunksToRemesh.insert(n.posZ);

        // Diagonals
        if (atMinX && atMinZ && n.negXnegZ) chunksToRemesh.insert(n.negXnegZ);
        if (atMaxX && atMinZ && n.posXnegZ) chunksToRemesh.insert(n.posXnegZ);
        if (atMinX && atMaxZ && n.negXposZ) chunksToRemesh.insert(n.negXposZ);
        if (atMaxX && atMaxZ && n.posXposZ) chunksToRemesh.insert(n.posXposZ);
    }

    void propagateSkyLight(World* world, std::vector<LightNode>& sunlightQueue, std::set<Chunk*>& chunksToRemesh) {
        static const int dx[6] = { 0,  0, -1,  1,  0,  0};
        static const int dy[6] = {-1,  1,  0,  0,  0,  0};
        static const int dz[6] = { 0,  0,  0,  0, -1,  1};

        NeighborCache neighbors(world);
        size_t head = 0;

        while (head < sunlightQueue.size()) {
            LightNode node = sunlightQueue[head++];

            int x = node.x;
            int y = node.y;
            int z = node.z;
            Chunk* currentChunk = node.chunk;
            if (!currentChunk) continue;

            int skyLightLevel = currentChunk->getSkyLight(x, y, z);
            if (skyLightLevel <= 1)
                continue;

            const ChunkNeighbors& cn = neighbors.get(currentChunk);

            for (int i = 0; i < 6; ++i) {
                int nx = x + dx[i];
                int ny = y + dy[i];
                int nz = z + dz[i];

                if (ny < 0 || ny >= Chunk::chunkHeight)
                    continue;

                Chunk* targetChunk = currentChunk;
                if (nx < 0) {
                    targetChunk = cn.negX;
                    nx += Chunk::chunkWidth;
                } else if (nx >= Chunk::chunkWidth) {
                    targetChunk = cn.posX;
                    nx -= Chunk::chunkWidth;
                }

                if (nz < 0) {
                    targetChunk = cn.negZ;
                    nz += Chunk::chunkDepth;
                } else if (nz >= Chunk::chunkDepth) {
                    targetChunk = cn.posZ;
                    nz -= Chunk::chunkDepth;
                }

                if (targetChunk) {
                    if (isLightPassable(targetChunk->blocks[nx][ny][nz].type)) {
                        if (i == 0) { // Negative Y
                            int targetLevel = (skyLightLevel == 15) ? 15 : (skyLightLevel - 1);
                            if (targetChunk->getSkyLight(nx, ny, nz) < targetLevel) {
                                targetChunk->setSkyLight(nx, ny, nz, targetLevel);
                                markChunkAndNeighborsForRemesh(targetChunk, nx, nz, chunksToRemesh, neighbors);
                                sunlightQueue.emplace_back(nx, ny, nz, targetChunk);
                            }
                        } else {
                            if (targetChunk->getSkyLight(nx, ny, nz) + 2 <= skyLightLevel) {
                                targetChunk->setSkyLight(nx, ny, nz, skyLightLevel - 1);
                                markChunkAndNeighborsForRemesh(targetChunk, nx, nz, chunksToRemesh, neighbors);
                                sunlightQueue.emplace_back(nx, ny, nz, targetChunk);
                            }
                        }
                    }
                }
            }
        }
    }

    void propagateBlockLight(World* world, std::vector<LightNode>& lightQueue, std::set<Chunk*>& chunksToRemesh) {
        static const int dx[6] = {-1,  1,  0,  0,  0,  0};
        static const int dy[6] = { 0,  0, -1,  1,  0,  0};
        static const int dz[6] = { 0,  0,  0,  0, -1,  1};

        NeighborCache neighbors(world);
        size_t head = 0;

        while (head < lightQueue.size()) {
            LightNode node = lightQueue[head++];

            int x = node.x;
            int y = node.y;
            int z = node.z;
            Chunk* currentChunk = node.chunk;
            if (!currentChunk) continue;

            int lightLevel = currentChunk->getBlockLight(x, y, z);
            if (lightLevel <= 1)
                continue;

            const ChunkNeighbors& cn = neighbors.get(currentChunk);

            for (int i = 0; i < 6; ++i) {
                int nx = x + dx[i];
                int ny = y + dy[i];
                int nz = z + dz[i];

                if (ny < 0 || ny >= Chunk::chunkHeight)
                    continue;

                Chunk* targetChunk = currentChunk;
                if (nx < 0) {
                    targetChunk = cn.negX;
                    nx += Chunk::chunkWidth;
                } else if (nx >= Chunk::chunkWidth) {
                    targetChunk = cn.posX;
                    nx -= Chunk::chunkWidth;
                }

                if (nz < 0) {
                    targetChunk = cn.negZ;
                    nz += Chunk::chunkDepth;
                } else if (nz >= Chunk::chunkDepth) {
                    targetChunk = cn.posZ;
                    nz -= Chunk::chunkDepth;
                }

                if (targetChunk) {
                    if (isLightPassable(targetChunk->blocks[nx][ny][nz].type)) {
                        if (targetChunk->getBlockLight(nx, ny, nz) + 2 <= lightLevel) {
                            targetChunk->setBlockLight(nx, ny, nz, lightLevel - 1);
                            markChunkAndNeighborsForRemesh(targetChunk, nx, nz, chunksToRemesh, neighbors);
                            lightQueue.emplace_back(nx, ny, nz, targetChunk);
                        }
                    }
                }
            }
        }
    }

    void getHighestSkyLightNeighbor(World* world, Chunk* chunkData, int x, int y, int z, int& outLevel, Chunk*& outChunk, int& outX, int& outY, int& outZ) {
        static const int dx[6] = { 0,  0, -1,  1,  0,  0};
        static const int dy[6] = { 1, -1,  0,  0,  0,  0};
        static const int dz[6] = { 0,  0,  0,  0, -1,  1};

        outLevel = 0;
        outChunk = chunkData;
        outX = x;
        outY = y;
        outZ = z;

        NeighborCache neighbors(world);
        const ChunkNeighbors& cn = neighbors.get(chunkData);

        for (int i = 0; i < 6; ++i) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            int nz = z + dz[i];

            if (ny < 0 || ny >= Chunk::chunkHeight)
                continue;

            Chunk* targetChunk = chunkData;
            if (nx < 0) {
                targetChunk = cn.negX;
                nx += Chunk::chunkWidth;
            } else if (nx >= Chunk::chunkWidth) {
                targetChunk = cn.posX;
                nx -= Chunk::chunkWidth;
            }

            if (nz < 0) {
                targetChunk = cn.negZ;
                nz += Chunk::chunkDepth;
            } else if (nz >= Chunk::chunkDepth) {
                targetChunk = cn.posZ;
                nz -= Chunk::chunkDepth;
            }

            if (targetChunk) {
                int neighborLightLevel = targetChunk->getSkyLight(nx, ny, nz);
                if (neighborLightLevel > outLevel) {
                    outLevel = neighborLightLevel;
                    outChunk = targetChunk;
                    outX = nx;
                    outY = ny;
                    outZ = nz;
                }
            }
        }
    }

    std::set<Chunk*> calculateFullLighting(World* world, Chunk* chunkData) {
        if (getOptionInt("enable_lighting", 1) == 0) {
            chunkData->isLightCalculated.store(true, std::memory_order_release);
            return {};
        }

        std::lock_guard<std::mutex> lock(lightingMutex);
        chunkData->isLightCalculated.store(false, std::memory_order_release);
        chunkData->clearLight();

        std::vector<LightNode> skyLightQueue;
        std::vector<LightNode> blockLightQueue;
        std::set<Chunk*> chunksToRemesh;
        NeighborCache neighbors(world);

        const ChunkNeighbors& cn = neighbors.get(chunkData);
        chunksToRemesh.insert(chunkData);
        if (cn.negX) chunksToRemesh.insert(cn.negX);
        if (cn.posX) chunksToRemesh.insert(cn.posX);
        if (cn.negZ) chunksToRemesh.insert(cn.negZ);
        if (cn.posZ) chunksToRemesh.insert(cn.posZ);
        if (cn.negXnegZ) chunksToRemesh.insert(cn.negXnegZ);
        if (cn.posXnegZ) chunksToRemesh.insert(cn.posXnegZ);
        if (cn.negXposZ) chunksToRemesh.insert(cn.negXposZ);
        if (cn.posXposZ) chunksToRemesh.insert(cn.posXposZ);

        int minPassableY[Chunk::chunkWidth][Chunk::chunkDepth];
        int maxTerrainY = 0;

        for (int x = 0; x < Chunk::chunkWidth; ++x) {
            for (int z = 0; z < Chunk::chunkDepth; ++z) {
                int y = Chunk::chunkHeight - 1;
                while (y >= 0 && isLightPassable(chunkData->blocks[x][y][z].type)) {
                    chunkData->setSkyLight(x, y, z, 15);
                    --y;
                }
                minPassableY[x][z] = y + 1;
                if (y > maxTerrainY) {
                    maxTerrainY = y;
                }
            }
        }

        if (cn.negX) {
            for (int z = 0; z < Chunk::chunkDepth; ++z) {
                for (int y = Chunk::chunkHeight - 1; y > maxTerrainY; --y) {
                    if (!isLightPassable(cn.negX->blocks[Chunk::chunkWidth - 1][y][z].type)) {
                        maxTerrainY = y;
                        break;
                    }
                }
            }
        }
        if (cn.posX) {
            for (int z = 0; z < Chunk::chunkDepth; ++z) {
                for (int y = Chunk::chunkHeight - 1; y > maxTerrainY; --y) {
                    if (!isLightPassable(cn.posX->blocks[0][y][z].type)) {
                        maxTerrainY = y;
                        break;
                    }
                }
            }
        }
        if (cn.negZ) {
            for (int x = 0; x < Chunk::chunkWidth; ++x) {
                for (int y = Chunk::chunkHeight - 1; y > maxTerrainY; --y) {
                    if (!isLightPassable(cn.negZ->blocks[x][y][Chunk::chunkDepth - 1].type)) {
                        maxTerrainY = y;
                        break;
                    }
                }
            }
        }
        if (cn.posZ) {
            for (int x = 0; x < Chunk::chunkWidth; ++x) {
                for (int y = Chunk::chunkHeight - 1; y > maxTerrainY; --y) {
                    if (!isLightPassable(cn.posZ->blocks[x][y][0].type)) {
                        maxTerrainY = y;
                        break;
                    }
                }
            }
        }

        for (int x = 0; x < Chunk::chunkWidth; ++x) {
            for (int z = 0; z < Chunk::chunkDepth; ++z) {
                int startY = minPassableY[x][z];
                int endY = std::min(maxTerrainY, Chunk::chunkHeight - 1);
                for (int y = startY; y <= endY; ++y) {
                    bool canPropagate = false;

                    // -X
                    if (x > 0) {
                        if (y < minPassableY[x - 1][z] && isLightPassable(chunkData->blocks[x - 1][y][z].type) && chunkData->getSkyLight(x - 1, y, z) < 14)
                            canPropagate = true;
                    } else if (cn.negX) {
                        if (isLightPassable(cn.negX->blocks[Chunk::chunkWidth - 1][y][z].type) && cn.negX->getSkyLight(Chunk::chunkWidth - 1, y, z) < 14)
                            canPropagate = true;
                    }

                    // +X
                    if (!canPropagate) {
                        if (x < Chunk::chunkWidth - 1) {
                            if (y < minPassableY[x + 1][z] && isLightPassable(chunkData->blocks[x + 1][y][z].type) && chunkData->getSkyLight(x + 1, y, z) < 14)
                                canPropagate = true;
                        } else if (cn.posX) {
                            if (isLightPassable(cn.posX->blocks[0][y][z].type) && cn.posX->getSkyLight(0, y, z) < 14)
                                canPropagate = true;
                        }
                    }

                    // -Z
                    if (!canPropagate) {
                        if (z > 0) {
                            if (y < minPassableY[x][z - 1] && isLightPassable(chunkData->blocks[x][y][z - 1].type) && chunkData->getSkyLight(x, y, z - 1) < 14)
                                canPropagate = true;
                        } else if (cn.negZ) {
                            if (isLightPassable(cn.negZ->blocks[x][y][Chunk::chunkDepth - 1].type) && cn.negZ->getSkyLight(x, y, Chunk::chunkDepth - 1) < 14)
                                canPropagate = true;
                        }
                    }

                    // +Z
                    if (!canPropagate) {
                        if (z < Chunk::chunkDepth - 1) {
                            if (y < minPassableY[x][z + 1] && isLightPassable(chunkData->blocks[x][y][z + 1].type) && chunkData->getSkyLight(x, y, z + 1) < 14)
                                canPropagate = true;
                        } else if (cn.posZ) {
                            if (isLightPassable(cn.posZ->blocks[x][y][0].type) && cn.posZ->getSkyLight(x, y, 0) < 14)
                                canPropagate = true;
                        }
                    }

                    if (canPropagate) {
                        skyLightQueue.emplace_back(x, y, z, chunkData);
                    }
                }
            }
        }

        for (int x = 0; x < Chunk::chunkWidth; ++x) {
            for (int z = 0; z < Chunk::chunkDepth; ++z) {
                for (int y = 0; y < Chunk::chunkHeight; ++y) {
                    uint16_t type = chunkData->blocks[x][y][z].type;
                    if (type != 0) {
                        const auto* info = BlockDB::getBlockInfo(type);
                        if (info && info->lightEmission > 0) {
                            chunkData->setBlockLight(x, y, z, info->lightEmission);
                            blockLightQueue.emplace_back(x, y, z, chunkData);
                        }
                    }
                }
            }
        }

        // Neighbor -X
        if (cn.negX) {
            for (int z = 0; z < Chunk::chunkDepth; ++z) {
                for (int y = 0; y < Chunk::chunkHeight; ++y) {
                    if (isLightPassable(chunkData->blocks[0][y][z].type)) {
                        int sl = cn.negX->getSkyLight(Chunk::chunkWidth - 1, y, z);
                        if (sl > 1 && chunkData->getSkyLight(0, y, z) < sl - 1)
                            skyLightQueue.emplace_back(Chunk::chunkWidth - 1, y, z, cn.negX);
                        int bl = cn.negX->getBlockLight(Chunk::chunkWidth - 1, y, z);
                        if (bl > 1 && chunkData->getBlockLight(0, y, z) < bl - 1)
                            blockLightQueue.emplace_back(Chunk::chunkWidth - 1, y, z, cn.negX);
                    }
                }
            }
        }
        // Neighbor +X
        if (cn.posX) {
            for (int z = 0; z < Chunk::chunkDepth; ++z) {
                for (int y = 0; y < Chunk::chunkHeight; ++y) {
                    if (isLightPassable(chunkData->blocks[Chunk::chunkWidth - 1][y][z].type)) {
                        int sl = cn.posX->getSkyLight(0, y, z);
                        if (sl > 1 && chunkData->getSkyLight(Chunk::chunkWidth - 1, y, z) < sl - 1)
                            skyLightQueue.emplace_back(0, y, z, cn.posX);
                        int bl = cn.posX->getBlockLight(0, y, z);
                        if (bl > 1 && chunkData->getBlockLight(Chunk::chunkWidth - 1, y, z) < bl - 1)
                            blockLightQueue.emplace_back(0, y, z, cn.posX);
                    }
                }
            }
        }
        // Neighbor -Z
        if (cn.negZ) {
            for (int x = 0; x < Chunk::chunkWidth; ++x) {
                for (int y = 0; y < Chunk::chunkHeight; ++y) {
                    if (isLightPassable(chunkData->blocks[x][y][0].type)) {
                        int sl = cn.negZ->getSkyLight(x, y, Chunk::chunkDepth - 1);
                        if (sl > 1 && chunkData->getSkyLight(x, y, 0) < sl - 1)
                            skyLightQueue.emplace_back(x, y, Chunk::chunkDepth - 1, cn.negZ);
                        int bl = cn.negZ->getBlockLight(x, y, Chunk::chunkDepth - 1);
                        if (bl > 1 && chunkData->getBlockLight(x, y, 0) < bl - 1)
                            blockLightQueue.emplace_back(x, y, Chunk::chunkDepth - 1, cn.negZ);
                    }
                }
            }
        }
        // Neighbor +Z
        if (cn.posZ) {
            for (int x = 0; x < Chunk::chunkWidth; ++x) {
                for (int y = 0; y < Chunk::chunkHeight; ++y) {
                    if (isLightPassable(chunkData->blocks[x][y][Chunk::chunkDepth - 1].type)) {
                        int sl = cn.posZ->getSkyLight(x, y, 0);
                        if (sl > 1 && chunkData->getSkyLight(x, y, Chunk::chunkDepth - 1) < sl - 1)
                            skyLightQueue.emplace_back(x, y, 0, cn.posZ);
                        int bl = cn.posZ->getBlockLight(x, y, 0);
                        if (bl > 1 && chunkData->getBlockLight(x, y, Chunk::chunkDepth - 1) < bl - 1)
                            blockLightQueue.emplace_back(x, y, 0, cn.posZ);
                    }
                }
            }
        }

        propagateSkyLight(world, skyLightQueue, chunksToRemesh);
        propagateBlockLight(world, blockLightQueue, chunksToRemesh);

        chunkData->isLightCalculated.store(true, std::memory_order_release);
        return chunksToRemesh;
    }

    std::set<Chunk*> addSkyLightBlocker(World* world, Chunk* chunkData, int x, int y, int z) {
        if (getOptionInt("enable_lighting", 1) == 0) return {};
        std::lock_guard<std::mutex> lock(lightingMutex);
        std::set<Chunk*> chunksToRemesh;
        NeighborCache neighbors(world);
        markChunkAndNeighborsForRemesh(chunkData, x, z, chunksToRemesh, neighbors);

        std::vector<LightRemovalNode> lightRemovalQueue;
        std::vector<LightNode> lightPropagationQueue;

        int lightLevel = chunkData->getSkyLight(x, y, z);
        lightRemovalQueue.emplace_back(x, y, z, chunkData, lightLevel);
        chunkData->setSkyLight(x, y, z, 0);

        static const int dx[6] = {-1,  1,  0,  0,  0,  0};
        static const int dy[6] = { 0,  0, -1,  1,  0,  0};
        static const int dz[6] = { 0,  0,  0,  0, -1,  1};

        size_t removeHead = 0;
        while (removeHead < lightRemovalQueue.size()) {
            LightRemovalNode node = lightRemovalQueue[removeHead++];

            int nx0 = node.x;
            int ny0 = node.y;
            int nz0 = node.z;
            int nodeLight = node.value;
            Chunk* currentChunk = node.chunk;
            if (!currentChunk) continue;

            const ChunkNeighbors& cn = neighbors.get(currentChunk);

            for (int i = 0; i < 6; ++i) {
                int nx = nx0 + dx[i];
                int ny = ny0 + dy[i];
                int nz = nz0 + dz[i];

                if (ny < 0 || ny >= Chunk::chunkHeight) continue;

                Chunk* targetChunk = currentChunk;
                if (nx < 0) {
                    targetChunk = cn.negX;
                    nx += Chunk::chunkWidth;
                } else if (nx >= Chunk::chunkWidth) {
                    targetChunk = cn.posX;
                    nx -= Chunk::chunkWidth;
                }

                if (nz < 0) {
                    targetChunk = cn.negZ;
                    nz += Chunk::chunkDepth;
                } else if (nz >= Chunk::chunkDepth) {
                    targetChunk = cn.posZ;
                    nz -= Chunk::chunkDepth;
                }

                if (targetChunk) {
                    int neighborLight = targetChunk->getSkyLight(nx, ny, nz);
                    if (neighborLight != 0) {
                        bool isDownward = (i == 2);
                        if ((isDownward && neighborLight <= nodeLight) || (!isDownward && neighborLight < nodeLight)) {
                            targetChunk->setSkyLight(nx, ny, nz, 0);
                            markChunkAndNeighborsForRemesh(targetChunk, nx, nz, chunksToRemesh, neighbors);
                            lightRemovalQueue.emplace_back(nx, ny, nz, targetChunk, neighborLight);
                        } else if (neighborLight >= nodeLight) {
                            lightPropagationQueue.emplace_back(nx, ny, nz, targetChunk);
                        }
                    }
                }
            }
        }

        propagateSkyLight(world, lightPropagationQueue, chunksToRemesh);
        return chunksToRemesh;
    }

    std::set<Chunk*> removeSkyLightBlocker(World* world, Chunk* chunkData, int x, int y, int z) {
        if (getOptionInt("enable_lighting", 1) == 0) return {};
        std::lock_guard<std::mutex> lock(lightingMutex);
        std::set<Chunk*> chunksToRemesh;
        NeighborCache neighbors(world);
        markChunkAndNeighborsForRemesh(chunkData, x, z, chunksToRemesh, neighbors);
        std::vector<LightNode> sunlightQueue;

        bool directSky = false;
        if (y == Chunk::chunkHeight - 1) {
            directSky = true;
        } else if (chunkData->getSkyLight(x, y + 1, z) == 15) {
            directSky = true;
        }

        if (directSky) {
            chunkData->setSkyLight(x, y, z, 15);
            sunlightQueue.emplace_back(x, y, z, chunkData);
        } else {
            int skyLightLevel = 0;
            int skyLightX, skyLightY, skyLightZ;
            Chunk* skyLightChunk = chunkData;
            getHighestSkyLightNeighbor(world, chunkData, x, y, z, skyLightLevel, skyLightChunk, skyLightX, skyLightY, skyLightZ);

            if (skyLightLevel > 1) {
                sunlightQueue.emplace_back(skyLightX, skyLightY, skyLightZ, skyLightChunk);
            } else {
                return chunksToRemesh;
            }
        }

        propagateSkyLight(world, sunlightQueue, chunksToRemesh);
        return chunksToRemesh;
    }

    std::set<Chunk*> addLightEmitter(World* world, Chunk* chunkData, int x, int y, int z, int lightLevel) {
        if (getOptionInt("enable_lighting", 1) == 0) return {};
        std::lock_guard<std::mutex> lock(lightingMutex);
        std::set<Chunk*> chunksToRemesh;
        NeighborCache neighbors(world);
        markChunkAndNeighborsForRemesh(chunkData, x, z, chunksToRemesh, neighbors);

        std::vector<LightNode> lightQueue;
        chunkData->setBlockLight(x, y, z, std::max((int)chunkData->getBlockLight(x, y, z), lightLevel));
        lightQueue.emplace_back(x, y, z, chunkData);

        propagateBlockLight(world, lightQueue, chunksToRemesh);
        return chunksToRemesh;
    }

    std::set<Chunk*> removeLightEmitter(World* world, Chunk* chunkData, int x, int y, int z) {
        if (getOptionInt("enable_lighting", 1) == 0) return {};
        std::lock_guard<std::mutex> lock(lightingMutex);
        std::set<Chunk*> chunksToRemesh;
        NeighborCache neighbors(world);
        markChunkAndNeighborsForRemesh(chunkData, x, z, chunksToRemesh, neighbors);

        std::vector<LightRemovalNode> lightRemovalQueue;
        std::vector<LightNode> lightPropagationQueue;

        int lightLevel = chunkData->getBlockLight(x, y, z);
        lightRemovalQueue.emplace_back(x, y, z, chunkData, lightLevel);
        chunkData->setBlockLight(x, y, z, 0);

        static const int dx[6] = {-1,  1,  0,  0,  0,  0};
        static const int dy[6] = { 0,  0, -1,  1,  0,  0};
        static const int dz[6] = { 0,  0,  0,  0, -1,  1};

        size_t removeHead = 0;
        while (removeHead < lightRemovalQueue.size()) {
            LightRemovalNode node = lightRemovalQueue[removeHead++];

            int nx0 = node.x;
            int ny0 = node.y;
            int nz0 = node.z;
            int nodeLight = node.value;
            Chunk* currentChunk = node.chunk;
            if (!currentChunk) continue;

            const ChunkNeighbors& cn = neighbors.get(currentChunk);

            for (int i = 0; i < 6; ++i) {
                int nx = nx0 + dx[i];
                int ny = ny0 + dy[i];
                int nz = nz0 + dz[i];

                if (ny < 0 || ny >= Chunk::chunkHeight) continue;

                Chunk* targetChunk = currentChunk;
                if (nx < 0) {
                    targetChunk = cn.negX;
                    nx += Chunk::chunkWidth;
                } else if (nx >= Chunk::chunkWidth) {
                    targetChunk = cn.posX;
                    nx -= Chunk::chunkWidth;
                }

                if (nz < 0) {
                    targetChunk = cn.negZ;
                    nz += Chunk::chunkDepth;
                } else if (nz >= Chunk::chunkDepth) {
                    targetChunk = cn.posZ;
                    nz -= Chunk::chunkDepth;
                }

                if (targetChunk) {
                    int neighborLight = targetChunk->getBlockLight(nx, ny, nz);
                    uint16_t type = targetChunk->blocks[nx][ny][nz].type;
                    int emission = 0;
                    if (type != 0) {
                        const auto* info = BlockDB::getBlockInfo(type);
                        if (info) emission = info->lightEmission;
                    }

                    if (emission > 0) {
                        targetChunk->setBlockLight(nx, ny, nz, emission);
                        markChunkAndNeighborsForRemesh(targetChunk, nx, nz, chunksToRemesh, neighbors);
                        if (neighborLight > emission && neighborLight < nodeLight) {
                            lightRemovalQueue.emplace_back(nx, ny, nz, targetChunk, neighborLight);
                        }
                        lightPropagationQueue.emplace_back(nx, ny, nz, targetChunk);
                    } else if (neighborLight != 0 && neighborLight < nodeLight) {
                        targetChunk->setBlockLight(nx, ny, nz, 0);
                        markChunkAndNeighborsForRemesh(targetChunk, nx, nz, chunksToRemesh, neighbors);
                        lightRemovalQueue.emplace_back(nx, ny, nz, targetChunk, neighborLight);
                    } else if (neighborLight >= nodeLight) {
                        lightPropagationQueue.emplace_back(nx, ny, nz, targetChunk);
                    }
                }
            }
        }

        propagateBlockLight(world, lightPropagationQueue, chunksToRemesh);
        return chunksToRemesh;
    }

    std::set<Chunk*> addLightBlocker(World* world, Chunk* chunkData, int x, int y, int z) {
        if (getOptionInt("enable_lighting", 1) == 0) return {};
        int lightLevel = chunkData->getBlockLight(x, y, z);
        if (lightLevel > 0) {
            return removeLightEmitter(world, chunkData, x, y, z);
        }
        return {};
    }

    std::set<Chunk*> removeLightBlocker(World* world, Chunk* chunkData, int x, int y, int z) {
        if (getOptionInt("enable_lighting", 1) == 0) return {};
        static const int dx[6] = {-1,  1,  0,  0,  0,  0};
        static const int dy[6] = { 0,  0, -1,  1,  0,  0};
        static const int dz[6] = { 0,  0,  0,  0, -1,  1};

        NeighborCache neighbors(world);
        const ChunkNeighbors& cn = neighbors.get(chunkData);

        int maxLightLevel = 0;
        Chunk* maxLightChunk = chunkData;
        int maxLightX = x, maxLightY = y, maxLightZ = z;

        for (int i = 0; i < 6; ++i) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            int nz = z + dz[i];

            if (ny < 0 || ny >= Chunk::chunkHeight) continue;

            Chunk* targetChunk = chunkData;
            if (nx < 0) {
                targetChunk = cn.negX;
                nx += Chunk::chunkWidth;
            } else if (nx >= Chunk::chunkWidth) {
                targetChunk = cn.posX;
                nx -= Chunk::chunkWidth;
            }

            if (nz < 0) {
                targetChunk = cn.negZ;
                nz += Chunk::chunkDepth;
            } else if (nz >= Chunk::chunkDepth) {
                targetChunk = cn.posZ;
                nz -= Chunk::chunkDepth;
            }

            if (targetChunk) {
                int neighborLight = targetChunk->getBlockLight(nx, ny, nz);
                if (neighborLight > maxLightLevel) {
                    maxLightLevel = neighborLight;
                    maxLightChunk = targetChunk;
                    maxLightX = nx;
                    maxLightY = ny;
                    maxLightZ = nz;
                }
            }
        }

        if (maxLightLevel <= 1)
            return {};

        return addLightEmitter(world, maxLightChunk, maxLightX, maxLightY, maxLightZ, maxLightLevel);
    }
}