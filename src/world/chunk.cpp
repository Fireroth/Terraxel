#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <set>
#include <algorithm>
#include "chunk.hpp"
#include "../core/options.hpp"
#include "noise.hpp"
#include "chunkTerrain.hpp"
#include "modelDB.hpp"
#include "../core/logger.hpp"

struct pendingBlock {
    int x, y, z;
    uint16_t type;
};
static std::map<std::pair<int, int>, std::vector<pendingBlock >> pendingBlockPlacements;

void clearPendingBlockPlacements() {
    pendingBlockPlacements.clear();
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

    // Apply any pending block placements for this chunk
    auto key = std::make_pair(chunkX, chunkZ);
    auto iterator = pendingBlockPlacements.find(key);
    if (iterator != pendingBlockPlacements.end()) {
        // Don't overwrite saved chunks
        if (!loadedFromSave) {
            for (const auto& pb : iterator->second) {
                if (pb.x >= 0 && pb.x < chunkWidth && pb.y >= 0 && pb.y < chunkHeight && pb.z >= 0 && pb.z < chunkDepth) {
                    blocks[pb.x][pb.y][pb.z].type = pb.type;
                }
            }
        }
        pendingBlockPlacements.erase(iterator);
    }
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
    std::set<Chunk*> chunksToRebuild;

    for (int y = 0; y < structHeight; y++) {
        for (int z = 0; z < structDepth; z++) {
            for (int x = 0; x < structWidth; x++) {
                uint32_t blockCode = structure.layers[y][z][x];
                uint8_t chance = blockCode / 100000;
                uint16_t blockType = blockCode % 100000;

                int worldX = baseX + x;
                int worldY = baseY + y;
                int worldZ = baseZ + z;

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
                int chunkOffsetX = 0, chunkOffsetZ = 0;
                int localX = worldX, localZ = worldZ;
                if (worldX < 0) {
                    chunkOffsetX = (worldX / chunkWidth) - (worldX % chunkWidth != 0 ? 1 : 0);
                    localX = worldX - chunkOffsetX * chunkWidth;
                } else if (worldX >= chunkWidth) {
                    chunkOffsetX = worldX / chunkWidth;
                    localX = worldX - chunkOffsetX * chunkWidth;
                }
                if (worldZ < 0) {
                    chunkOffsetZ = (worldZ / chunkDepth) - (worldZ % chunkDepth != 0 ? 1 : 0);
                    localZ = worldZ - chunkOffsetZ * chunkDepth;
                } else if (worldZ >= chunkDepth) {
                    chunkOffsetZ = worldZ / chunkDepth;
                    localZ = worldZ - chunkOffsetZ * chunkDepth;
                }

                int targetChunkX = chunkX + chunkOffsetX;
                int targetChunkZ = chunkZ + chunkOffsetZ;

                if (worldY >= 0 && worldY < chunkHeight) {
                    Chunk* targetChunk = nullptr;
                    if (chunkOffsetX == 0 && chunkOffsetZ == 0) {
                        targetChunk = this;
                    } else if (world) {
                        targetChunk = world->getChunk(targetChunkX, targetChunkZ);
                    }
                    if (targetChunk &&
                        localX >= 0 && localX < chunkWidth &&
                        localZ >= 0 && localZ < chunkDepth) {
                        // Do not overwrite chunks that already have player modified state.
                        if (forced || (!targetChunk->loadedFromSave && !targetChunk->isModified)) {
                            targetChunk->blocks[localX][worldY][localZ].type = blockType;
                            if (forced) {
                                targetChunk->isModified = true;
                            }
                            chunksToRebuild.insert(targetChunk);

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

                            /*if (localX == 0 && localZ == 0) {
                                Chunk* neighbor = world->getChunk(targetChunkX - 1, targetChunkZ - 1);
                                if (neighbor) chunksToRebuild.insert(neighbor);
                            }
                            if (localX == chunkWidth - 1 && localZ == 0) {
                                Chunk* neighbor = world->getChunk(targetChunkX + 1, targetChunkZ - 1);
                                if (neighbor) chunksToRebuild.insert(neighbor);
                            }
                            if (localX == 0 && localZ == chunkDepth - 1) {
                                Chunk* neighbor = world->getChunk(targetChunkX - 1, targetChunkZ + 1);
                                if (neighbor) chunksToRebuild.insert(neighbor);
                            }
                            if (localX == chunkWidth - 1 && localZ == chunkDepth - 1) {
                                Chunk* neighbor = world->getChunk(targetChunkX + 1, targetChunkZ + 1);
                                if (neighbor) chunksToRebuild.insert(neighbor);
                            }*/
                        }
                    } else {
                        // Chunk not loaded, defer placement
                        auto key = std::make_pair(targetChunkX, targetChunkZ);
                        pendingBlockPlacements[key].push_back({localX, worldY, localZ, blockType});
                    }
                }
            }
        }
    }
    if (forced) {
        for (Chunk* chunk : chunksToRebuild) {
            chunk->buildMesh();
        }
    }
}

void Chunk::buildMesh() {
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

    bool fasterTrees = (getOptionInt("faster_trees", 0) != 0);
    bool useAO = (getOptionInt("ambient_occlusion", 1) != 0);

    std::vector<float> vertices;
    std::vector<float> crossVertices;
    std::vector<float> translucentVertices;
    std::vector<unsigned int> indices;
    std::vector<unsigned int> crossIndices;
    std::vector<unsigned int> translucentIndices;
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
                            addPlaneFace(crossVertices, crossIndices, x, y, z, planeIndex, info, crossIndexOffset);
                        }
                    }
                    if (!m->cuboids.empty()) {
                        auto& targetVerts = (info->liquid || info->translucent) ? translucentVertices : vertices;
                        auto& targetIndices = (info->liquid || info->translucent) ? translucentIndices : indices;
                        auto& targetOffset = (info->liquid || info->translucent) ? translucentIndexOffset : indexOffset;
                        for (int face = 0; face < 6; face++) {
                            if (isBlockVisible(x, y, z, face, fasterTrees, info)) {
                                for (size_t cuboidIndex = 0; cuboidIndex < m->cuboids.size(); cuboidIndex++) {
                                    addCuboidFace(targetVerts, targetIndices, x, y, z, face, cuboidIndex, info, targetOffset, useAO);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    indexCount = static_cast<GLsizei>(indices.size());
    crossIndexCount = static_cast<GLsizei>(crossIndices.size());
    translucentIndexCount = static_cast<GLsizei>(translucentIndices.size());

    if (VAO == 0) {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(7 * sizeof(float)));
        glEnableVertexAttribArray(3);

        glBindVertexArray(0);
    } else {
        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices.size() * sizeof(unsigned int), indices.data());

        glBindVertexArray(0);
    }

    //--------------------------------------------------------------
    
    if (crossVAO == 0) {
        glGenVertexArrays(1, &crossVAO);
        glGenBuffers(1, &crossVBO);
        glGenBuffers(1, &crossEBO);

        glBindVertexArray(crossVAO);

        glBindBuffer(GL_ARRAY_BUFFER, crossVBO);
        glBufferData(GL_ARRAY_BUFFER, crossVertices.size() * sizeof(float), crossVertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, crossEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, crossIndices.size() * sizeof(unsigned int), crossIndices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    } else {
        glBindVertexArray(crossVAO);

        glBindBuffer(GL_ARRAY_BUFFER, crossVBO);
        glBufferData(GL_ARRAY_BUFFER, crossVertices.size() * sizeof(float), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, crossVertices.size() * sizeof(float), crossVertices.data());

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, crossEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, crossIndices.size() * sizeof(unsigned int), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, crossIndices.size() * sizeof(unsigned int), crossIndices.data());

        glBindVertexArray(0);
    }

    //--------------------------------------------------------------
    
    if (translucentVAO == 0) {
        glGenVertexArrays(1, &translucentVAO);
        glGenBuffers(1, &translucentVBO);
        glGenBuffers(1, &translucentEBO);

        glBindVertexArray(translucentVAO);

        glBindBuffer(GL_ARRAY_BUFFER, translucentVBO);
        glBufferData(GL_ARRAY_BUFFER, translucentVertices.size() * sizeof(float), translucentVertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, translucentEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, translucentIndices.size() * sizeof(unsigned int), translucentIndices.data(), GL_DYNAMIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(7 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(4);

        glBindVertexArray(0);
    } else {
        glBindVertexArray(translucentVAO);

        glBindBuffer(GL_ARRAY_BUFFER, translucentVBO);
        glBufferData(GL_ARRAY_BUFFER, translucentVertices.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, translucentVertices.size() * sizeof(float), translucentVertices.data());

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, translucentEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, translucentIndices.size() * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, translucentIndices.size() * sizeof(unsigned int), translucentIndices.data());

        glBindVertexArray(0);
    }

    // Compute face centroids for sorting
    translucentFaceCentroids.clear();
    translucentFaceCentroids.reserve(translucentIndices.size() / 6);
    const size_t stride = 9;
    for (size_t i = 0; i + 5 < translucentIndices.size(); i += 6) {
        unsigned int base = translucentIndices[i + 0];
        for (size_t k = 1; k < 6; ++k) {
            if (translucentIndices[i + k] < base) {
                base = translucentIndices[i + k];
            }
        }
        unsigned int ia = base;
        unsigned int ib = base + 1;
        unsigned int ic = base + 2;
        unsigned int id = base + 3;

        float ya = translucentVertices[ia * stride + 1];
        float yb = translucentVertices[ib * stride + 1];
        float yc = translucentVertices[ic * stride + 1];
        float yd = translucentVertices[id * stride + 1];

        if (translucentVertices[ia * stride + 7] > 0.5f) ya -= 0.18f;
        if (translucentVertices[ib * stride + 7] > 0.5f) yb -= 0.18f;
        if (translucentVertices[ic * stride + 7] > 0.5f) yc -= 0.18f;
        if (translucentVertices[id * stride + 7] > 0.5f) yd -= 0.18f;

        glm::vec3 a(translucentVertices[ia * stride + 0], ya, translucentVertices[ia * stride + 2]);
        glm::vec3 b(translucentVertices[ib * stride + 0], yb, translucentVertices[ib * stride + 2]);
        glm::vec3 c(translucentVertices[ic * stride + 0], yc, translucentVertices[ic * stride + 2]);
        glm::vec3 d(translucentVertices[id * stride + 0], yd, translucentVertices[id * stride + 2]);

        translucentFaceCentroids.push_back((a + b + c + d) / 4.0f);
    }

    // Store CPU side copies for sorting
    translucentIndexDataCPU = std::move(translucentIndices);
    translucentNeedsSort = true;

    // Clear neighbor cache
    for (int dx = 0; dx < 3; dx++) {
        for (int dz = 0; dz < 3; dz++) {
            neighborCache[dx][dz] = nullptr;
        }
    }
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

void Chunk::addPlaneFace(std::vector<float>& vertices, std::vector<unsigned int>& indices, int x, int y, int z, int planeIndex, const BlockDB::BlockInfo* blockInfo, unsigned int& offset) {
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
        vertices.insert(vertices.end(), {pos.x, pos.y, pos.z, local_u, local_v, layer});
    }

    indices.insert(indices.end(), {offset, offset + 1, offset + 2, offset + 2, offset + 3, offset});
    offset += 4;
}

void Chunk::addCuboidFace(std::vector<float>& vertices, std::vector<unsigned int>& indices, int x, int y, int z, int face, size_t cuboidIndex, const BlockDB::BlockInfo* blockInfo, unsigned int& offset, bool useAO) {
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

    float ao[4];
    for (int i = 0; i < 4; ++i) {
        ao[i] = calculateVertexAO(x, y, z, face, faceVerts[i], useAO, isLiquid);
    }

    for (int i = 0; i < 4; ++i) {
        glm::vec3 pos = faceVerts[i] + glm::vec3(x, y, z);
        float local_u = (i == 1 || i == 2) ? faceData.uvTo.x : faceData.uvFrom.x;
        float local_v = (i == 2 || i == 3) ? faceData.uvTo.y : faceData.uvFrom.y;
        float layer = atlasOffset.y * 16.0f + atlasOffset.x;

        if (isLiquid || blockInfo->translucent) {
            float isTop = 0.0f;
            if (isLiquid && !liquidAbove) {
                bool isTopFace = (face == 4);
                if (isTopFace || (face <= 3 && std::abs(faceVerts[i].y - faceMaxY) < eps))
                    isTop = 1.0f;
            }
            vertices.insert(vertices.end(), {pos.x, pos.y, pos.z, local_u, local_v, layer, static_cast<float>(face), isTop, ao[i]});
        } else {
            vertices.insert(vertices.end(), {pos.x, pos.y, pos.z, local_u, local_v, layer, static_cast<float>(face), ao[i]});
        }
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
    glm::dvec3 chunkWorldPos = glm::dvec3(chunkX * chunkWidth, 0, chunkZ * chunkDepth);
    glm::dvec3 relativePos = chunkWorldPos - camera.getPositionDouble();
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(relativePos));
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, &model[0][0]);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Chunk::renderCross(const Camera& camera, GLint uCrossModelLoc) {
    glm::dvec3 chunkWorldPos = glm::dvec3(chunkX * chunkWidth, 0, chunkZ * chunkDepth);
    glm::dvec3 relativePos = chunkWorldPos - camera.getPositionDouble();
    glm::mat4 crossModel = glm::translate(glm::mat4(1.0f), glm::vec3(relativePos));
    glUniformMatrix4fv(uCrossModelLoc, 1, GL_FALSE, &crossModel[0][0]);

    glBindVertexArray(crossVAO);
    glDrawElements(GL_TRIANGLES, crossIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Chunk::renderTranslucent(const Camera& camera, GLint uModelLoc) {
    glm::dvec3 chunkWorldPos = glm::dvec3(chunkX * chunkWidth, 0, chunkZ * chunkDepth);
    glm::dvec3 relativePos = chunkWorldPos - camera.getPositionDouble();
    glm::mat4 translucentModel = glm::translate(glm::mat4(1.0f), glm::vec3(relativePos));
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, &translucentModel[0][0]);

    // If there are no translucent indices, nothing to do
    if (translucentIndexCount == 0 || translucentIndexDataCPU.empty()) 
        return;

    glm::dvec3 camPosWorld = camera.getPositionDouble();
    glm::vec3 camPosLocal = glm::vec3(camPosWorld - glm::dvec3(chunkX * chunkWidth, 0.0f, chunkZ * chunkDepth));

    glm::vec3 diff = camPosLocal - lastSortCamPosLocal;
    float distSq = glm::dot(diff, diff);

    if (translucentNeedsSort || distSq > 1.0f) {
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
    } else {
        glBindVertexArray(translucentVAO);
    }

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