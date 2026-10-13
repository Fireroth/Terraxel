#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <set>
#include <unordered_set>
#include <map>
#include <algorithm>
#include <mutex>
#include "chunk.hpp"
#include "world.hpp"
#include "../core/options.hpp"
#include "noise.hpp"
#include "chunkTerrain.hpp"
#include "modelDB.hpp"
#include "lighting.hpp"

struct pendingBlock {
    int x, y, z;
    uint16_t type;
    int srcChunkX = 0, srcChunkZ = 0;
};
struct pendingFillColumn {
    int x, z;
    int startY;
    uint16_t type;
    int srcChunkX = 0, srcChunkZ = 0;
};
static std::map<std::pair<int, int>, std::vector<pendingBlock >> pendingBlockPlacements;
static std::map<std::pair<int, int>, std::vector<pendingFillColumn>> pendingFillPlacements;
static std::mutex pendingPlacementsMutex;

static inline bool isAirOrLiquidBlock(uint16_t type) {
    if (type == 0) return true;
    const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(type);
    return info && info->liquid;
}

void clearPendingBlockPlacements() {
    std::lock_guard<std::mutex> lock(pendingPlacementsMutex);
    pendingBlockPlacements.clear();
    pendingFillPlacements.clear();
}

Chunk::Chunk(int x, int z, World* worldPtr) :
    chunkX(x), chunkZ(z), world(worldPtr), VAO(0), VBO(0), EBO(0), indexCount(0),
    crossVAO(0), crossVBO(0), crossEBO(0), crossIndexCount(0),
    translucentVAO(0), translucentVBO(0), translucentEBO(0), translucentIndexCount(0),
    translucentNeedsSort(true), lastSortCamPosLocal(0.0f) {

    noises = noiseInit();
    generateChunkTerrain(*this);
    if (world) {
        world->loadChunkFromSave(this);
    }

    applyPendingBlockPlacements();
}

Chunk::~Chunk() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteVertexArrays(1, &crossVAO);
    glDeleteBuffers(1, &crossVBO);
    glDeleteBuffers(1, &crossEBO);
    glDeleteVertexArrays(1, &translucentVAO);
    glDeleteBuffers(1, &translucentVBO);
    glDeleteBuffers(1, &translucentEBO);

    translucentFaceCentroids.clear();
    translucentIndexDataCPU.clear();
}

void Chunk::placeStructure(const Structure& structure, int baseX, int baseY, int baseZ, bool forced) {
    int structHeight = (int)structure.layers.size();
    int structDepth = (int)structure.layers[0].size();
    int structWidth = (int)structure.layers[0][0].size();
    std::unordered_set<Chunk*> chunksToRebuild;

    std::vector<std::pair<std::pair<int, int>, pendingBlock>> localPendingBlocks;
    std::vector<std::pair<std::pair<int, int>, pendingFillColumn>> localPendingFills;

    int cachedTargetChunkX = INT32_MIN;
    int cachedTargetChunkZ = INT32_MIN;
    Chunk* cachedTargetChunk = nullptr;

    auto getTargetChunk = [&](int targetChunkX, int targetChunkZ) -> Chunk* {
        if (targetChunkX == chunkX && targetChunkZ == chunkZ) {
            return this;
        }
        if (targetChunkX == cachedTargetChunkX && targetChunkZ == cachedTargetChunkZ) {
            return cachedTargetChunk;
        }
        cachedTargetChunkX = targetChunkX;
        cachedTargetChunkZ = targetChunkZ;
        cachedTargetChunk = world ? world->getChunk(targetChunkX, targetChunkZ) : nullptr;
        return cachedTargetChunk;
    };

    auto markChunkModifiedAndRebuild = [&](Chunk* targetChunk, int targetChunkX, int targetChunkZ, int localX, int localZ) {
        if (forced) {
            targetChunk->isModified = true;
        }
        chunksToRebuild.insert(targetChunk);
        if (targetChunk != this) {
            modifiedNeighborChunks.insert(targetChunk);
        }

        if (world) {
            if (localX == 0) {
                Chunk* neighbor = world->getChunk(targetChunkX - 1, targetChunkZ);
                if (neighbor) chunksToRebuild.insert(neighbor);
            }
            if (localX == chunkWidth - 1) {
                Chunk* neighbor = world->getChunk(targetChunkX + 1, targetChunkZ);
                if (neighbor) chunksToRebuild.insert(neighbor);
            }
            if (localZ == 0) {
                Chunk* neighbor = world->getChunk(targetChunkX, targetChunkZ - 1);
                if (neighbor) chunksToRebuild.insert(neighbor);
            }
            if (localZ == chunkDepth - 1) {
                Chunk* neighbor = world->getChunk(targetChunkX, targetChunkZ + 1);
                if (neighbor) chunksToRebuild.insert(neighbor);
            }
        }
    };

    for (int y = 0; y < structHeight; y++) {
        int worldY = baseY + y;
        if (worldY < 0 || worldY >= chunkHeight) {
            continue;
        }

        for (int z = 0; z < structDepth; z++) {
            int worldZ = baseZ + z;
            int chunkOffsetZ = worldZ >> 4;
            int localZ = worldZ & 15;
            int targetChunkZ = chunkZ + chunkOffsetZ;

            for (int x = 0; x < structWidth; x++) {
                uint32_t blockCode = structure.layers[y][z][x];
                uint8_t chance = blockCode / 100000;
                uint16_t blockType = blockCode % 100000;

                int worldX = baseX + x;

                if (blockType == 0) 
                    continue;
                if (blockType == 44) { // Structure air block
                    blockType = 0;
                } else if (BlockDB::getBlockInfo(blockType) == nullptr) {
                    blockType = 65000;
                }

                if (chance > 0) {
                    float randNoise = noises.randomNoise.GetNoise((double)worldX, (double)worldY, (double)worldZ);
                    float noiseValue = (randNoise + 1.0f) * 0.5f;

                    bool place = false;
                    switch (chance) {
                        case 1: place = !(noiseValue >= (1.0f / 2)); break;  // 1 in 2
                        case 2: place = !(noiseValue >= (1.0f / 5)); break;  // 1 in 5
                        case 3: place = !(noiseValue >= (1.0f / 20)); break;  // 1 in 20
                    }
                    if (!place) {
                        continue;
                    }
                }

                // Compute which chunk this block belongs to
                int chunkOffsetX = worldX >> 4;
                int localX = worldX & 15;
                int targetChunkX = chunkX + chunkOffsetX;

                Chunk* targetChunk = getTargetChunk(targetChunkX, targetChunkZ);
                if (targetChunk) {
                    // Do not overwrite chunks that already have player modified state.
                    if (forced || (!targetChunk->loadedFromSave && !targetChunk->isModified)) {
                        targetChunk->blocks[localX][worldY][localZ].type = blockType;
                        markChunkModifiedAndRebuild(targetChunk, targetChunkX, targetChunkZ, localX, localZ);
                    }
                } else {
                    // Chunk not loaded, defer placement locally
                    localPendingBlocks.push_back({std::make_pair(targetChunkX, targetChunkZ), {localX, worldY, localZ, blockType, chunkX, chunkZ}});
                }
            }
        }
    }

    if (structure.hasFillLayer && !structure.fillLayer.empty()) {
        int fillDepth = (int)structure.fillLayer.size();

        for (int z = 0; z < fillDepth; z++) {
            int worldZ = baseZ + z;
            int chunkOffsetZ = worldZ >> 4;
            int localZ = worldZ & 15;
            int targetChunkZ = chunkZ + chunkOffsetZ;

            int fillWidth = (int)structure.fillLayer[z].size();
            for (int x = 0; x < fillWidth; x++) {
                uint32_t fillCode = structure.fillLayer[z][x];
                uint8_t chance = fillCode / 100000;
                uint16_t fillBlockType = fillCode % 100000;

                if (fillBlockType == 0 || fillBlockType == 44) 
                    continue;
                if (BlockDB::getBlockInfo(fillBlockType) == nullptr) {
                    fillBlockType = 65000;
                }

                int worldX = baseX + x;

                if (chance > 0) {
                    float randNoise = noises.randomNoise.GetNoise((double)worldX, (double)(baseY - 1), (double)worldZ);
                    float noiseValue = (randNoise + 1.0f) * 0.5f;

                    bool place = false;
                    switch (chance) {
                        case 1: place = !(noiseValue >= (1.0f / 2)); break;  // 1 in 2
                        case 2: place = !(noiseValue >= (1.0f / 5)); break;  // 1 in 5
                        case 3: place = !(noiseValue >= (1.0f / 20)); break;  // 1 in 20
                    }
                    if (!place) {
                        continue;
                    }
                }

                // Compute which chunk this column belongs to
                int chunkOffsetX = worldX >> 4;
                int localX = worldX & 15;
                int targetChunkX = chunkX + chunkOffsetX;

                Chunk* targetChunk = getTargetChunk(targetChunkX, targetChunkZ);
                if (targetChunk) {
                    if (forced || (!targetChunk->loadedFromSave && !targetChunk->isModified)) {
                        int startY = std::min(baseY - 1, chunkHeight - 1);
                        bool anyPlaced = false;
                        for (int fillY = startY; fillY >= 0; fillY--) {
                            if (!isAirOrLiquidBlock(targetChunk->blocks[localX][fillY][localZ].type)) {
                                break; // Hit solid block - stop filling down
                            }
                            targetChunk->blocks[localX][fillY][localZ].type = fillBlockType;
                            anyPlaced = true;
                        }

                        if (anyPlaced) {
                            markChunkModifiedAndRebuild(targetChunk, targetChunkX, targetChunkZ, localX, localZ);
                        }
                    }
                } else {
                    // Chunk not loaded, defer placement
                    localPendingFills.push_back({std::make_pair(targetChunkX, targetChunkZ), {localX, localZ, baseY - 1, fillBlockType, chunkX, chunkZ}});
                }
            }
        }
    }

    if (!localPendingBlocks.empty() || !localPendingFills.empty()) {
        std::lock_guard<std::mutex> lock(pendingPlacementsMutex);
        for (const auto& item : localPendingBlocks) {
            pendingBlockPlacements[item.first].push_back(item.second);
        }
        for (const auto& item : localPendingFills) {
            pendingFillPlacements[item.first].push_back(item.second);
        }
    }

    if (forced) {
        for (Chunk* chunk : chunksToRebuild) {
            chunk->clearLight();
            chunk->isLightCalculated.store(false, std::memory_order_release);
        }
        for (Chunk* chunk : chunksToRebuild) {
            VoxelLighting::calculateFullLighting(world, chunk);
        }
        for (Chunk* chunk : chunksToRebuild) {
            chunk->buildMesh();
        }
    } else if (world) {
        for (Chunk* chunk : chunksToRebuild) {
            if (chunk != this) {
                world->queueMeshComputation(chunk->chunkX, chunk->chunkZ);
            }
        }
    }
}

void Chunk::applyPendingBlockPlacements() {
    std::lock_guard<std::mutex> lock(pendingPlacementsMutex);
    auto key = std::make_pair(chunkX, chunkZ);
    auto iterator = pendingBlockPlacements.find(key);
    if (iterator != pendingBlockPlacements.end()) {
        // Don't overwrite saved chunks
        if (!loadedFromSave) {
            for (const auto& pb : iterator->second) {
                if (pb.x >= 0 && pb.x < chunkWidth && pb.y >= 0 && pb.y < chunkHeight && pb.z >= 0 && pb.z < chunkDepth) {
                    blocks[pb.x][pb.y][pb.z].type = pb.type;
                }
                if (world) {
                    Chunk* srcChunk = world->getChunk(pb.srcChunkX, pb.srcChunkZ);
                    if (srcChunk && srcChunk != this) {
                        modifiedNeighborChunks.insert(srcChunk);
                    }
                }
            }
        }
        pendingBlockPlacements.erase(iterator);
    }

    auto fillIt = pendingFillPlacements.find(key);
    if (fillIt != pendingFillPlacements.end()) {
        if (!loadedFromSave) {
            for (const auto& col : fillIt->second) {
                if (col.x >= 0 && col.x < chunkWidth && col.z >= 0 && col.z < chunkDepth) {
                    int startY = std::min(col.startY, chunkHeight - 1);
                    for (int fillY = startY; fillY >= 0; fillY--) {
                        if (!isAirOrLiquidBlock(blocks[col.x][fillY][col.z].type)) {
                            break; // Stop at first solid block
                        }
                        blocks[col.x][fillY][col.z].type = col.type;
                    }
                }
                if (world) {
                    Chunk* srcChunk = world->getChunk(col.srcChunkX, col.srcChunkZ);
                    if (srcChunk && srcChunk != this) {
                        modifiedNeighborChunks.insert(srcChunk);
                    }
                }
            }
        }
        pendingFillPlacements.erase(fillIt);
    }
}

void Chunk::buildMesh() {
    computeMesh();
    uploadMesh();
}

void Chunk::computeMesh() {
    // Defer mesh generation if any neighbor chunk is missing
    for (int face = 0; face < 6; face++) {
        int neighborX = 0, neighborY = 0, neighborZ = 0;
        switch (face) {
            case 0: neighborX = chunkX;     neighborY = 0; neighborZ = chunkZ + 1; break; // front
            case 1: neighborX = chunkX;     neighborY = 0; neighborZ = chunkZ - 1; break; // back
            case 2: neighborX = chunkX - 1; neighborY = 0; neighborZ = chunkZ;     break; // left
            case 3: neighborX = chunkX + 1; neighborY = 0; neighborZ = chunkZ;     break; // right
            case 4: continue; // top face (no neighbor needed)
            case 5: continue; // bottom face (no neighbor needed)
        }
        if (world->getChunk(neighborX, neighborZ) == nullptr) { // Neighbor chunk missing = skip mesh generation for now
            return;
        }
    }

    if (getOptionInt("enable_lighting", 1) != 0 && !isLightCalculated.load(std::memory_order_acquire)) {
        return;
    }

    // Cache neighboring chunks to avoid tree map lookups in loops
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            if (dx == 0 && dz == 0) {
                neighborCache[1][1] = this;
            } else {
                neighborCache[dx + 1][dz + 1] = world ? world->getChunk(chunkX + dx, chunkZ + dz) : nullptr;
            }
        }
    }

    bool fasterModels = (getOptionInt("faster_models", 0) != 0);
    bool useAO = (getOptionInt("ambient_occlusion", 1) != 0);
    bool useLighting = (getOptionInt("enable_lighting", 1) != 0);

    ChunkMeshData data;
    unsigned int indexOffset = 0;
    unsigned int crossIndexOffset = 0;
    unsigned int translucentIndexOffset = 0;

    for (int x = 0; x < chunkWidth; x++) {
        for (int y = 0; y < chunkHeight; y++) {
            for (int z = 0; z < chunkDepth; z++) {
                const uint16_t& type = blocks[x][y][z].type;
                if (type == 0) continue;

                const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(type);
                if (!info) continue;

                const Model* m = ModelDB::getModel(info->modelName);
                if (m) {
                    if (!m->planes.empty()) {
                        for (int planeIndex = 0; planeIndex < (int)m->planes.size(); planeIndex++) {
                            addPlaneFace(data.crossVertices, data.crossIndices, x, y, z, planeIndex, info, crossIndexOffset, useLighting);
                        }
                    }
                    if (!m->cuboids.empty()) {
                        auto& targetVerts = (info->liquid || info->translucent) ? data.translucentVertices : data.vertices;
                        auto& targetIndices = (info->liquid || info->translucent) ? data.translucentIndices : data.indices;
                        unsigned int& targetOffset = (info->liquid || info->translucent) ? translucentIndexOffset : indexOffset;
                        for (int face = 0; face < 6; face++) {
                            if (isBlockVisible(x, y, z, face, fasterModels, info)) {
                                for (size_t cuboidIndex = 0; cuboidIndex < m->cuboids.size(); cuboidIndex++) {
                                    addCuboidFace(targetVerts, targetIndices, x, y, z, face, cuboidIndex, info, targetOffset, useAO, useLighting);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Compute face centroids for sorting
    data.translucentFaceCentroids.clear();
    data.translucentFaceCentroids.reserve(data.translucentIndices.size() / 6);
    for (size_t i = 0; i + 5 < data.translucentIndices.size(); i += 6) {
        unsigned int base = data.translucentIndices[i + 0];
        for (size_t k = 1; k < 6; ++k) {
            if (data.translucentIndices[i + k] < base) {
                base = data.translucentIndices[i + k];
            }
        }
        unsigned int ia = base;
        unsigned int ib = base + 1;
        unsigned int ic = base + 2;
        unsigned int id = base + 3;

        glm::vec3 a = data.translucentVertices[ia].getPosition();
        glm::vec3 b = data.translucentVertices[ib].getPosition();
        glm::vec3 c = data.translucentVertices[ic].getPosition();
        glm::vec3 d = data.translucentVertices[id].getPosition();

        if (data.translucentVertices[ia].isTop()) a.y -= 0.18f;
        if (data.translucentVertices[ib].isTop()) b.y -= 0.18f;
        if (data.translucentVertices[ic].isTop()) c.y -= 0.18f;
        if (data.translucentVertices[id].isTop()) d.y -= 0.18f;

        data.translucentFaceCentroids.push_back((a + b + c + d) * 0.25f);
    }

    // Clear neighbor cache
    for (int dx = 0; dx < 3; dx++) {
        for (int dz = 0; dz < 3; dz++) {
            neighborCache[dx][dz] = nullptr;
        }
    }

    {
        std::lock_guard<std::mutex> lock(meshMutex);
        pendingMeshData = std::move(data);
        pendingMeshData.hasData = true;
    }

    if (world) {
        world->queueChunkMeshUpload(this);
    }
}

void Chunk::uploadMesh() {
    ChunkMeshData data;
    {
        std::lock_guard<std::mutex> lock(meshMutex);
        if (!pendingMeshData.hasData) return;
        data = std::move(pendingMeshData);
        pendingMeshData.hasData = false;
    }

    indexCount = static_cast<GLsizei>(data.indices.size());
    crossIndexCount = static_cast<GLsizei>(data.crossIndices.size());
    translucentIndexCount = static_cast<GLsizei>(data.translucentIndices.size());

    if (VAO == 0) {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(PackedVertex), data.vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(unsigned int), data.indices.data(), GL_STATIC_DRAW);

        glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(PackedVertex), (void*)0);
        glEnableVertexAttribArray(0);

        glBindVertexArray(0);
    } else {
        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(PackedVertex), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, data.vertices.size() * sizeof(PackedVertex), data.vertices.data());

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(unsigned int), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, data.indices.size() * sizeof(unsigned int), data.indices.data());

        glBindVertexArray(0);
    }

    //--------------------------------------------------------------
    
    if (crossVAO == 0) {
        glGenVertexArrays(1, &crossVAO);
        glGenBuffers(1, &crossVBO);
        glGenBuffers(1, &crossEBO);

        glBindVertexArray(crossVAO);

        glBindBuffer(GL_ARRAY_BUFFER, crossVBO);
        glBufferData(GL_ARRAY_BUFFER, data.crossVertices.size() * sizeof(PackedVertex), data.crossVertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, crossEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.crossIndices.size() * sizeof(unsigned int), data.crossIndices.data(), GL_STATIC_DRAW);

        glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(PackedVertex), (void*)0);
        glEnableVertexAttribArray(0);

        glBindVertexArray(0);
    } else {
        glBindVertexArray(crossVAO);

        glBindBuffer(GL_ARRAY_BUFFER, crossVBO);
        glBufferData(GL_ARRAY_BUFFER, data.crossVertices.size() * sizeof(PackedVertex), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, data.crossVertices.size() * sizeof(PackedVertex), data.crossVertices.data());

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, crossEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.crossIndices.size() * sizeof(unsigned int), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, data.crossIndices.size() * sizeof(unsigned int), data.crossIndices.data());

        glBindVertexArray(0);
    }

    //--------------------------------------------------------------
    
    if (translucentVAO == 0) {
        glGenVertexArrays(1, &translucentVAO);
        glGenBuffers(1, &translucentVBO);
        glGenBuffers(1, &translucentEBO);

        glBindVertexArray(translucentVAO);

        glBindBuffer(GL_ARRAY_BUFFER, translucentVBO);
        glBufferData(GL_ARRAY_BUFFER, data.translucentVertices.size() * sizeof(PackedVertex), data.translucentVertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, translucentEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.translucentIndices.size() * sizeof(unsigned int), data.translucentIndices.data(), GL_DYNAMIC_DRAW);

        glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(PackedVertex), (void*)0);
        glEnableVertexAttribArray(0);

        glBindVertexArray(0);
    } else {
        glBindVertexArray(translucentVAO);

        glBindBuffer(GL_ARRAY_BUFFER, translucentVBO);
        glBufferData(GL_ARRAY_BUFFER, data.translucentVertices.size() * sizeof(PackedVertex), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, data.translucentVertices.size() * sizeof(PackedVertex), data.translucentVertices.data());

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, translucentEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.translucentIndices.size() * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, data.translucentIndices.size() * sizeof(unsigned int), data.translucentIndices.data());

        glBindVertexArray(0);
    }

    translucentFaceCentroids = std::move(data.translucentFaceCentroids);
    translucentIndexDataCPU = std::move(data.translucentIndices);
    translucentNeedsSort = true;
}

bool Chunk::isBlockVisible(int x, int y, int z, int face, bool fasterTrees, const BlockDB::BlockInfo* thisInfo) const {
    static const int offsets[6][3] = {
        { 0,  0,  1},  // front
        { 0,  0, -1},  // back
        {-1,  0,  0},  // left
        { 1,  0,  0},  // right
        { 0,  1,  0},  // top
        { 0, -1,  0}   // bottom
    };

    int neighborX = x + offsets[face][0];
    int neighborY = y + offsets[face][1];
    int neighborZ = z + offsets[face][2];

    // Check height bounds
    if (neighborY < 0 || neighborY >= chunkHeight)
        return true;

    int neighborLocalX = neighborX;
    int neighborLocalZ = neighborZ;
    int rx = 1;
    int rz = 1;

    if (neighborLocalX < 0) {
        neighborLocalX += chunkWidth;
        rx = 0;
    } else if (neighborLocalX >= chunkWidth) {
        neighborLocalX -= chunkWidth;
        rx = 2;
    }

    if (neighborLocalZ < 0) {
        neighborLocalZ += chunkDepth;
        rz = 0;
    } else if (neighborLocalZ >= chunkDepth) {
        neighborLocalZ -= chunkDepth;
        rz = 2;
    }

    const Chunk* neighbor = neighborCache[rx][rz];
    if (!neighbor)
        return true;

    uint16_t neighborType = neighbor->blocks[neighborLocalX][neighborY][neighborLocalZ].type;

    if (neighborType == 0)
        return true;

    const BlockDB::BlockInfo* neighborInfo = BlockDB::getBlockInfo(neighborType);

    if (!fasterTrees && thisInfo->renderFacesInBetween)
        return true;

    if ((neighborInfo->modelName != "cube" && neighborInfo->modelName != "liquid") || (thisInfo->modelName != "cube" && thisInfo->modelName != "liquid"))
        return true;

    if ((neighborInfo->liquid && !thisInfo->liquid) || (thisInfo->liquid && !neighborInfo->liquid && face == 4))
        return true;

    if (neighborInfo->transparent && !thisInfo->transparent)
        return true;

    return false;
}

static inline PackedVertex packVertex(
    const glm::vec3& pos,
    float u, float v,
    float layer,
    int face,
    float ao,
    float skyLight, float blockLight,
    bool isTop
) {
    uint32_t px = std::clamp(static_cast<uint32_t>(std::max(0.0f, std::round(pos.x * 16.0f))), 0u, 511u);
    uint32_t py = std::clamp(static_cast<uint32_t>(std::max(0.0f, std::round(pos.y * 16.0f))), 0u, 16383u);
    uint32_t pz = std::clamp(static_cast<uint32_t>(std::max(0.0f, std::round(pos.z * 16.0f))), 0u, 511u);
    uint32_t d0 = px | (py << 9u) | (pz << 23u);

    uint32_t pu = std::clamp(static_cast<uint32_t>(std::max(0.0f, std::round(u * 16.0f))), 0u, 31u);
    uint32_t pv = std::clamp(static_cast<uint32_t>(std::max(0.0f, std::round(v * 16.0f))), 0u, 31u);
    uint32_t player = std::clamp(static_cast<uint32_t>(std::max(0.0f, layer)), 0u, 255u);
    uint32_t pface = std::clamp(static_cast<uint32_t>(face), 0u, 7u);
    uint32_t pao = std::clamp(static_cast<uint32_t>(std::max(0.0f, std::round(ao))), 0u, 3u);
    uint32_t psky = std::clamp(static_cast<uint32_t>(std::max(0.0f, std::round(skyLight))), 0u, 15u);
    uint32_t pblk = std::clamp(static_cast<uint32_t>(std::max(0.0f, std::round(blockLight))), 0u, 15u);
    uint32_t ptop = isTop ? 1u : 0u;

    uint32_t d1 = pu | (pv << 5u) | (player << 10u) | (pface << 18u) |
                  (pao << 21u) | (psky << 23u) | (pblk << 27u) | (ptop << 31u);

    return {d0, d1};
}

void Chunk::addPlaneFace(std::vector<PackedVertex>& vertices, std::vector<unsigned int>& indices, int x, int y, int z, int planeIndex, const BlockDB::BlockInfo* blockInfo, unsigned int& offset, bool useLighting) {
    const Model* model = ModelDB::getModel(blockInfo->modelName);
    if (!model || planeIndex < 0 || planeIndex >= (int)model->planes.size()) return;

    constexpr float atlasSize = 16.0f;
    const auto& plane = model->planes[planeIndex];
    if (plane.faces.empty()) return;

    const auto& faceData = plane.faces.begin()->second;

    size_t texIndex = planeIndex;
    glm::vec2 atlasOffset = blockInfo->textureCoords[0];
    if (!blockInfo->multiTextureCoords.empty()) {
        if (texIndex < blockInfo->multiTextureCoords.size()) {
            atlasOffset = blockInfo->multiTextureCoords[texIndex][0];
        } else {
            atlasOffset = blockInfo->textureCoords[0];
        }
    }

    float cz = (plane.from.z + plane.to.z) * 0.5f;
    glm::vec3 quadVerts[4];
    quadVerts[0] = glm::vec3(plane.from.x, plane.from.y, cz);
    quadVerts[1] = glm::vec3(plane.to.x,   plane.from.y, cz);
    quadVerts[2] = glm::vec3(plane.to.x,   plane.to.y,   cz);
    quadVerts[3] = glm::vec3(plane.from.x, plane.to.y,   cz);

    bool applyRotation = (plane.rotationAxis != '\0' && std::abs(plane.rotationAngle) > 1e-6f);
    glm::mat4 rotMat(1.0f);
    if (applyRotation) {
        glm::vec3 axis(0.0f);
        if (plane.rotationAxis == 'x') axis = glm::vec3(1.0f, 0.0f, 0.0f);
        else if (plane.rotationAxis == 'y') axis = glm::vec3(0.0f, 1.0f, 0.0f);
        else if (plane.rotationAxis == 'z') axis = glm::vec3(0.0f, 0.0f, 1.0f);
        rotMat = glm::translate(glm::mat4(1.0f), plane.rotationOrigin) *
                 glm::rotate(glm::mat4(1.0f), glm::radians(plane.rotationAngle), axis) *
                 glm::translate(glm::mat4(1.0f), -plane.rotationOrigin);
    }

    for (int i = 0; i < 4; ++i) {
        glm::vec3 pos = quadVerts[i];
        if (applyRotation) {
            glm::vec4 p = rotMat * glm::vec4(pos, 1.0f);
            pos = glm::vec3(p.x, p.y, p.z);
        }
        if (plane.positionDirection != '\0' && std::abs(plane.positionOffset) > 1e-6f) {
            if (plane.positionDirection == 'x') pos += glm::vec3(plane.positionOffset, 0.0f, 0.0f);
            else if (plane.positionDirection == 'y') pos += glm::vec3(0.0f, plane.positionOffset, 0.0f);
            else if (plane.positionDirection == 'z') pos += glm::vec3(0.0f, 0.0f, plane.positionOffset);
        }
        pos += glm::vec3(x, y, z);
        float local_u = (i == 1 || i == 2) ? faceData.uvTo.x : faceData.uvFrom.x;
        float local_v = (i == 2 || i == 3) ? faceData.uvTo.y : faceData.uvFrom.y;
        float layer = atlasOffset.y * 16.0f + atlasOffset.x;
        glm::vec2 light = useLighting ? getVoxelLight(x, y, z) : glm::vec2(15.0f, 0.0f);
        if (blockInfo->lightEmission > 0) {
            light.y = std::max(light.y, static_cast<float>(blockInfo->lightEmission));
        }
        vertices.push_back(packVertex(pos, local_u, local_v, layer, /*face=*/6, /*ao=*/3.0f, light.x, light.y, /*isTop=*/false));
    }

    indices.insert(indices.end(), {offset, offset + 1, offset + 2, offset + 2, offset + 3, offset});
    offset += 4;
}

void Chunk::addCuboidFace(std::vector<PackedVertex>& vertices, std::vector<unsigned int>& indices, int x, int y, int z, int face, size_t cuboidIndex, const BlockDB::BlockInfo* blockInfo, unsigned int& offset, bool useAO, bool useLighting) {
    const Model* model = ModelDB::getModel(blockInfo->modelName);
    if (!model || cuboidIndex >= model->cuboids.size()) return;

    static const char* faceNames[6] = {"north", "south", "west", "east", "up", "down"};
    std::string faceName = faceNames[face];
    constexpr float atlasSize = 16.0f;

    const auto& cuboid = model->cuboids[cuboidIndex];
    auto it = cuboid.faces.find(faceName);
    if (it == cuboid.faces.end()) return;
    const auto& faceData = it->second;

    glm::vec3 faceVerts[4];
    switch (face) {
        case 0: // north (z+)
            faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.to.z);
            faceVerts[1] = glm::vec3(cuboid.to.x,   cuboid.from.y, cuboid.to.z);
            faceVerts[2] = glm::vec3(cuboid.to.x,   cuboid.to.y,   cuboid.to.z);
            faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.to.y,   cuboid.to.z);
            break;
        case 1: // south (z-)
            faceVerts[0] = glm::vec3(cuboid.to.x,   cuboid.from.y, cuboid.from.z);
            faceVerts[1] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.from.z);
            faceVerts[2] = glm::vec3(cuboid.from.x, cuboid.to.y,   cuboid.from.z);
            faceVerts[3] = glm::vec3(cuboid.to.x,   cuboid.to.y,   cuboid.from.z);
            break;
        case 2: // west (x-)
            faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.from.z);
            faceVerts[1] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.to.z);
            faceVerts[2] = glm::vec3(cuboid.from.x, cuboid.to.y,   cuboid.to.z);
            faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.to.y,   cuboid.from.z);
            break;
        case 3: // east (x+)
            faceVerts[0] = glm::vec3(cuboid.to.x,   cuboid.from.y, cuboid.to.z);
            faceVerts[1] = glm::vec3(cuboid.to.x,   cuboid.from.y, cuboid.from.z);
            faceVerts[2] = glm::vec3(cuboid.to.x,   cuboid.to.y,   cuboid.from.z);
            faceVerts[3] = glm::vec3(cuboid.to.x,   cuboid.to.y,   cuboid.to.z);
            break;
        case 4: // up (y+)
            faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.to.y,   cuboid.to.z);
            faceVerts[1] = glm::vec3(cuboid.to.x,   cuboid.to.y,   cuboid.to.z);
            faceVerts[2] = glm::vec3(cuboid.to.x,   cuboid.to.y,   cuboid.from.z);
            faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.to.y,   cuboid.from.z);
            break;
        case 5: // down (y-)
            faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.from.z);
            faceVerts[1] = glm::vec3(cuboid.to.x,   cuboid.from.y, cuboid.from.z);
            faceVerts[2] = glm::vec3(cuboid.to.x,   cuboid.from.y, cuboid.to.z);
            faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.to.z);
            break;
        default:
            return;
    }

    size_t texIndex = model->planes.size() + cuboidIndex;
    glm::vec2 atlasOffset = blockInfo->textureCoords[face];
    if (!blockInfo->multiTextureCoords.empty()) {
        if (texIndex < blockInfo->multiTextureCoords.size()) {
            atlasOffset = blockInfo->multiTextureCoords[texIndex][face];
        } else {
            atlasOffset = blockInfo->textureCoords[face];
        }
    }

    bool isLiquid = blockInfo->liquid;
    bool liquidAbove = false;
    int aboveY = y + 1;

    if (aboveY >= 0 && aboveY < chunkHeight) {
        uint16_t aboveType = blocks[x][aboveY][z].type;
        if (aboveType != 0) {
            const auto* aboveInfo = BlockDB::getBlockInfo(aboveType);
            liquidAbove = (aboveInfo && aboveInfo->liquid);
        }
    }

    float faceMaxY = 0.0f;
    const float eps = 1e-6f;
    if (isLiquid) {
        faceMaxY = std::max({faceVerts[0].y, faceVerts[1].y, faceVerts[2].y, faceVerts[3].y});
    }

    bool lowerBottom = false;
    if (isLiquid && face <= 3 && y > 0) {
        static const int offsets[4][2] = {
            { 0,  1}, // z+
            { 0, -1}, // z-
            {-1,  0}, // x-
            { 1,  0}  // x+
        };
        int nx = x + offsets[face][0];
        int nz = z + offsets[face][1];
        if (isLiquidBlock(x, y - 1, z) && isLiquidBlock(nx, y - 1, nz)) {
            lowerBottom = true;
        }
    }

    float ao[4];
    glm::vec2 vertLight[4];
    for (int i = 0; i < 4; ++i) {
        ao[i] = calculateVertexAO(x, y, z, face, faceVerts[i], useAO, isLiquid);
        if (useLighting) {
            vertLight[i] = calculateVertexLight(x, y, z, face, faceVerts[i], isLiquid);
            if (blockInfo->lightEmission > 0) {
                vertLight[i].y = std::max(vertLight[i].y, static_cast<float>(blockInfo->lightEmission));
            }
        } else {
            vertLight[i] = glm::vec2(15.0f, 0.0f);
        }
    }

    for (int i = 0; i < 4; ++i) {
        glm::vec3 pos = faceVerts[i] + glm::vec3(x, y, z);
        float local_u = (i == 1 || i == 2) ? faceData.uvTo.x : faceData.uvFrom.x;
        float local_v = (i == 2 || i == 3) ? faceData.uvTo.y : faceData.uvFrom.y;
        float layer = atlasOffset.y * 16.0f + atlasOffset.x;

        bool isTop = false;
        if (isLiquid) {
            if (!liquidAbove) {
                bool isTopFace = (face == 4);
                if (isTopFace || (face <= 3 && std::abs(faceVerts[i].y - faceMaxY) < eps))
                    isTop = true;
            }
            if (lowerBottom && face <= 3 && faceVerts[i].y < eps) {
                isTop = true;
            }
        }
        vertices.push_back(packVertex(pos, local_u, local_v, layer, face, ao[i], vertLight[i].x, vertLight[i].y, isTop));
    }

    bool flipDiagonal = (ao[0] + ao[2] < ao[1] + ao[3]);
    if (flipDiagonal) {
        indices.insert(indices.end(), {
            offset, offset + 1, offset + 3,
            offset + 1, offset + 2, offset + 3
        });
        if (blockInfo->liquid) {
            indices.insert(indices.end(), {
                offset, offset + 3, offset + 1,
                offset + 1, offset + 3, offset + 2
            });
        }
    } else {
        indices.insert(indices.end(), {
            offset, offset + 1, offset + 2,
            offset + 2, offset + 3, offset
        });
        if (blockInfo->liquid) {
            indices.insert(indices.end(), {
                offset, offset + 2, offset + 1,
                offset + 2, offset, offset + 3
            });
        }
    }
    offset += 4;
}

void Chunk::render(const Camera& camera, GLint uModelLoc) {
    if (indexCount == 0 || VAO == 0) return;

    glm::dvec3 chunkWorldPos = glm::dvec3(chunkX * chunkWidth, 0, chunkZ * chunkDepth);
    glm::dvec3 relativePos = chunkWorldPos - camera.getPositionDouble();
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(relativePos));
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, &model[0][0]);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Chunk::renderCross(const Camera& camera, GLint uModelLoc) {
    if (crossIndexCount == 0 || crossVAO == 0) return;

    glm::dvec3 chunkWorldPos = glm::dvec3(chunkX * chunkWidth, 0, chunkZ * chunkDepth);
    glm::dvec3 relativePos = chunkWorldPos - camera.getPositionDouble();
    glm::mat4 crossModel = glm::translate(glm::mat4(1.0f), glm::vec3(relativePos));
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, &crossModel[0][0]);

    glBindVertexArray(crossVAO);
    glDrawElements(GL_TRIANGLES, crossIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

bool Chunk::needsTranslucentSort(const glm::dvec3& camPosWorld) const {
    if (translucentIndexCount == 0 || translucentIndexDataCPU.empty())
        return false;

    if (translucentNeedsSort)
        return true;

    const glm::dvec3 chunkOrigin(static_cast<double>(chunkX) * chunkWidth, 0.0, static_cast<double>(chunkZ) * chunkDepth);
    glm::vec3 camPosLocal = glm::vec3(camPosWorld - chunkOrigin);
    glm::vec3 diff = camPosLocal - lastSortCamPosLocal;
    return glm::dot(diff, diff) > 1.0f;
}

void Chunk::translucentSortFaces(const glm::dvec3& camPosWorld) {
    if (translucentIndexCount == 0 || translucentIndexDataCPU.empty())
        return;

    const glm::dvec3 chunkOrigin(static_cast<double>(chunkX) * chunkWidth, 0.0, static_cast<double>(chunkZ) * chunkDepth);
    glm::vec3 camPosLocal = glm::vec3(camPosWorld - chunkOrigin);
    lastSortCamPosLocal = camPosLocal;
    translucentNeedsSort = false;

    struct FaceInfo { size_t baseIdx; float dist2; };
    static thread_local std::vector<FaceInfo> faces;
    static thread_local std::vector<unsigned int> sortedIndices;

    faces.clear();
    const size_t numFaces = translucentFaceCentroids.size();
    faces.reserve(numFaces);

    for (size_t fIdx = 0; fIdx < numFaces; ++fIdx) {
        const glm::vec3& centroid = translucentFaceCentroids[fIdx];
        float d2 = glm::dot(centroid - camPosLocal, centroid - camPosLocal);
        faces.push_back({fIdx * 6, d2});
    }

    // Sort faces back to front
    std::sort(faces.begin(), faces.end(), [](const FaceInfo& A, const FaceInfo& B) {
        return A.dist2 > B.dist2;
    });

    sortedIndices.clear();
    sortedIndices.reserve(numFaces * 6);
    for (const auto& f : faces) {
        size_t base = f.baseIdx;
        for (size_t k = 0; k < 6; ++k) {
            sortedIndices.push_back(translucentIndexDataCPU[base + k]);
        }
    }

    glBindVertexArray(translucentVAO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, translucentEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sortedIndices.size() * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sortedIndices.size() * sizeof(unsigned int), sortedIndices.data());
    glBindVertexArray(0);
}

void Chunk::renderTranslucent(const Camera& camera, GLint uModelLoc) {
    if (translucentIndexCount == 0 || translucentVAO == 0 || translucentIndexDataCPU.empty()) 
        return;

    glm::dvec3 chunkWorldPos = glm::dvec3(chunkX * chunkWidth, 0, chunkZ * chunkDepth);
    glm::dvec3 relativePos = chunkWorldPos - camera.getPositionDouble();
    glm::mat4 translucentModel = glm::translate(glm::mat4(1.0f), glm::vec3(relativePos));
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, &translucentModel[0][0]);

    glBindVertexArray(translucentVAO);
    glDrawElements(GL_TRIANGLES, translucentIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

bool Chunk::isOpaque(int nx, int ny, int nz) const {
    if (ny < 0 || ny >= chunkHeight)
        return false;

    int localX = nx;
    int localZ = nz;
    int rx = 1;
    int rz = 1;

    if (localX < 0) {
        localX += chunkWidth;
        rx = 0;
    } else if (localX >= chunkWidth) {
        localX -= chunkWidth;
        rx = 2;
    }

    if (localZ < 0) {
        localZ += chunkDepth;
        rz = 0;
    } else if (localZ >= chunkDepth) {
        localZ -= chunkDepth;
        rz = 2;
    }

    const Chunk* targetChunk = neighborCache[rx][rz];
    if (!targetChunk)
        return false;

    uint16_t type = targetChunk->blocks[localX][ny][localZ].type;
    if (type == 0)
        return false;

    const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(type);
    if (!info)
        return false;

    if (info->liquid || info->transparent || info->modelName != "cube")
        return false;

    return true;
}

bool Chunk::isLiquidBlock(int nx, int ny, int nz) const {
    if (ny < 0 || ny >= chunkHeight)
        return false;

    int localX = nx;
    int localZ = nz;
    int rx = 1;
    int rz = 1;

    if (localX < 0) {
        localX += chunkWidth;
        rx = 0;
    } else if (localX >= chunkWidth) {
        localX -= chunkWidth;
        rx = 2;
    }

    if (localZ < 0) {
        localZ += chunkDepth;
        rz = 0;
    } else if (localZ >= chunkDepth) {
        localZ -= chunkDepth;
        rz = 2;
    }

    const Chunk* targetChunk = neighborCache[rx][rz];
    if (!targetChunk)
        return false;

    uint16_t type = targetChunk->blocks[localX][ny][localZ].type;
    if (type == 0)
        return false;

    const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(type);
    return info && info->liquid;
}

float Chunk::calculateVertexAO(int x, int y, int z, int face, const glm::vec3& cornerPos, bool useAO, bool isLiquid) const {
    if (!useAO)
        return 3.0f;
    constexpr float eps = 1e-4f;
    if (face < 4 && cornerPos.y > eps && cornerPos.y < 1.0f - eps) {
        glm::vec3 bottomPos = cornerPos;
        bottomPos.y = 0.0f;
        glm::vec3 topPos = cornerPos;
        topPos.y = 1.0f;
        float ao_bottom = calculateVertexAO(x, y, z, face, bottomPos, useAO, isLiquid);
        float ao_top = calculateVertexAO(x, y, z, face, topPos, useAO, isLiquid);
        return glm::mix(ao_bottom, ao_top, cornerPos.y);
    }

    static const int offsets[6][3] = {
        { 0,  0,  1},  // face 0
        { 0,  0, -1},  // face 1
        {-1,  0,  0},  // face 2
        { 1,  0,  0},  // face 3
        { 0,  1,  0},  // face 4
        { 0, -1,  0}   // face 5
    };

    int static_nx = offsets[face][0];
    int static_ny = offsets[face][1];
    int static_nz = offsets[face][2];

    int nx = 0, ny = 0, nz = 0;

    // Calculate normal offsets
    if (static_nx > 0)       nx = (cornerPos.x + eps > 1.0f) ? 1 : 0;
    else if (static_nx < 0)  nx = (cornerPos.x - eps < 0.0f) ? -1 : 0;

    if (static_ny > 0)       ny = (cornerPos.y + eps > 1.0f) ? 1 : 0;
    else if (static_ny < 0)  ny = (cornerPos.y - eps < 0.0f) ? -1 : 0;

    if (static_nz > 0)       nz = (cornerPos.z + eps > 1.0f) ? 1 : 0;
    else if (static_nz < 0)  nz = (cornerPos.z - eps < 0.0f) ? -1 : 0;

    int tx = 0, ty = 0, tz = 0; // tangent 1
    int ux = 0, uy = 0, uz = 0; // tangent 2

    bool has_t1 = false;
    bool has_t2 = false;

    switch (face) {
        case 0: // north (z+)
        case 1: // south (z-)
            tx = (cornerPos.x + eps > 1.0f) ? 1 : ((cornerPos.x - eps < 0.0f) ? -1 : 0);
            uy = (cornerPos.y + eps > 1.0f) ? 1 : ((cornerPos.y - eps < 0.0f) ? -1 : 0);
            has_t1 = (tx != 0);
            has_t2 = (uy != 0);
            break;
        case 2: // west (x-)
        case 3: // east (x+)
            tz = (cornerPos.z + eps > 1.0f) ? 1 : ((cornerPos.z - eps < 0.0f) ? -1 : 0);
            uy = (cornerPos.y + eps > 1.0f) ? 1 : ((cornerPos.y - eps < 0.0f) ? -1 : 0);
            has_t1 = (tz != 0);
            has_t2 = (uy != 0);
            break;
        case 4: // up (y+)
        case 5: // down (y-)
            tx = (cornerPos.x + eps > 1.0f) ? 1 : ((cornerPos.x - eps < 0.0f) ? -1 : 0);
            uz = (cornerPos.z + eps > 1.0f) ? 1 : ((cornerPos.z - eps < 0.0f) ? -1 : 0);
            has_t1 = (tx != 0);
            has_t2 = (uz != 0);
            break;
    }

    bool side1 = has_t1 && isOpaque(x + nx + tx, y + ny + ty, z + nz + tz);
    bool side2 = has_t2 && isOpaque(x + nx + ux, y + ny + uy, z + nz + uz);

    int ao = 3;
    if (side1 && side2) {
        ao = 0;
    } else {
        bool corner = has_t1 && has_t2 && isOpaque(x + nx + tx + ux, y + ny + ty + uy, z + nz + tz + uz);
        ao = 3 - (side1 + side2 + corner);
    }

    if (isLiquid && ao > 0) {
        bool blockAboveOpaque = isOpaque(x, y + 1, z);
        if (blockAboveOpaque) {
            if (face == 4) {
                ao = std::max(0, ao - 1);
            } else if (face < 4 && cornerPos.y + eps > 1.0f) {
                ao = std::max(0, ao - 1);
            }
        }
    }

    return static_cast<float>(ao);
}

glm::vec2 Chunk::getVoxelLight(int nx, int ny, int nz) const {
    if (ny < 0 || ny >= chunkHeight)
        return glm::vec2(15.0f, 0.0f);

    int localX = nx;
    int localZ = nz;
    int rx = 1;
    int rz = 1;

    if (localX < 0) {
        localX += chunkWidth;
        rx = 0;
    } else if (localX >= chunkWidth) {
        localX -= chunkWidth;
        rx = 2;
    }

    if (localZ < 0) {
        localZ += chunkDepth;
        rz = 0;
    } else if (localZ >= chunkDepth) {
        localZ -= chunkDepth;
        rz = 2;
    }

    const Chunk* targetChunk = neighborCache[rx][rz];
    if (!targetChunk || !targetChunk->isLightCalculated.load(std::memory_order_acquire)) {
        int cx = std::clamp(nx, 0, chunkWidth - 1);
        int cz = std::clamp(nz, 0, chunkDepth - 1);
        return glm::vec2(static_cast<float>(getSkyLight(cx, ny, cz)),
                         static_cast<float>(getBlockLight(cx, ny, cz)));
    }

    return glm::vec2(static_cast<float>(targetChunk->getSkyLight(localX, ny, localZ)),
                     static_cast<float>(targetChunk->getBlockLight(localX, ny, localZ)));
}

glm::vec2 Chunk::calculateVertexLight(int x, int y, int z, int face, const glm::vec3& cornerPos, bool isLiquid) const {
    constexpr float eps = 1e-4f;
    if (face < 4 && cornerPos.y > eps && cornerPos.y < 1.0f - eps) {
        glm::vec3 bottomPos = cornerPos;
        bottomPos.y = 0.0f;
        glm::vec3 topPos = cornerPos;
        topPos.y = 1.0f;
        glm::vec2 light_bottom = calculateVertexLight(x, y, z, face, bottomPos, isLiquid);
        glm::vec2 light_top = calculateVertexLight(x, y, z, face, topPos, isLiquid);
        return glm::mix(light_bottom, light_top, cornerPos.y);
    }

    static const int offsets[6][3] = {
        { 0,  0,  1},  // face 0
        { 0,  0, -1},  // face 1
        {-1,  0,  0},  // face 2
        { 1,  0,  0},  // face 3
        { 0,  1,  0},  // face 4
        { 0, -1,  0}   // face 5
    };

    int static_nx = offsets[face][0];
    int static_ny = offsets[face][1];
    int static_nz = offsets[face][2];

    int nx = 0, ny = 0, nz = 0;

    if (static_nx > 0)       nx = (cornerPos.x + eps > 1.0f) ? 1 : 0;
    else if (static_nx < 0)  nx = (cornerPos.x - eps < 0.0f) ? -1 : 0;

    if (static_ny > 0)       ny = (cornerPos.y + eps > 1.0f) ? 1 : 0;
    else if (static_ny < 0)  ny = (cornerPos.y - eps < 0.0f) ? -1 : 0;

    if (static_nz > 0)       nz = (cornerPos.z + eps > 1.0f) ? 1 : 0;
    else if (static_nz < 0)  nz = (cornerPos.z - eps < 0.0f) ? -1 : 0;

    int tx = 0, ty = 0, tz = 0;
    int ux = 0, uy = 0, uz = 0;

    bool has_t1 = false;
    bool has_t2 = false;

    switch (face) {
        case 0: // north (z+)
        case 1: // south (z-)
            tx = (cornerPos.x + eps > 1.0f) ? 1 : ((cornerPos.x - eps < 0.0f) ? -1 : 0);
            uy = (cornerPos.y + eps > 1.0f) ? 1 : ((cornerPos.y - eps < 0.0f) ? -1 : 0);
            has_t1 = (tx != 0);
            has_t2 = (uy != 0);
            break;
        case 2: // west (x-)
        case 3: // east (x+)
            tz = (cornerPos.z + eps > 1.0f) ? 1 : ((cornerPos.z - eps < 0.0f) ? -1 : 0);
            uy = (cornerPos.y + eps > 1.0f) ? 1 : ((cornerPos.y - eps < 0.0f) ? -1 : 0);
            has_t1 = (tz != 0);
            has_t2 = (uy != 0);
            break;
        case 4: // up (y+)
        case 5: // down (y-)
            tx = (cornerPos.x + eps > 1.0f) ? 1 : ((cornerPos.x - eps < 0.0f) ? -1 : 0);
            uz = (cornerPos.z + eps > 1.0f) ? 1 : ((cornerPos.z - eps < 0.0f) ? -1 : 0);
            has_t1 = (tx != 0);
            has_t2 = (uz != 0);
            break;
    }

    glm::vec2 l0 = getVoxelLight(x + nx, y + ny, z + nz);
    glm::vec2 totalLight = l0;
    int sampleCount = 1;

    bool side1Opaque = has_t1 && isOpaque(x + nx + tx, y + ny + ty, z + nz + tz);
    bool side2Opaque = has_t2 && isOpaque(x + nx + ux, y + ny + uy, z + nz + uz);

    if (has_t1 && !side1Opaque) {
        totalLight += getVoxelLight(x + nx + tx, y + ny + ty, z + nz + tz);
        sampleCount++;
    }
    if (has_t2 && !side2Opaque) {
        totalLight += getVoxelLight(x + nx + ux, y + ny + uy, z + nz + uz);
        sampleCount++;
    }
    if (has_t1 && has_t2 && (!side1Opaque || !side2Opaque)) {
        if (!isOpaque(x + nx + tx + ux, y + ny + ty + uy, z + nz + tz + uz)) {
            totalLight += getVoxelLight(x + nx + tx + ux, y + ny + ty + uy, z + nz + tz + uz);
            sampleCount++;
        }
    }

    return totalLight / static_cast<float>(sampleCount);
}