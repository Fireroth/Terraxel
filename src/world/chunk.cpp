#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <set>
#include <algorithm>
#include "chunk.hpp"
#include "../core/options.hpp"
#include "noise.hpp"
#include "chunkTerrain.hpp"
#include "modelDB.hpp"

struct pendingBlock {
    int x, y, z;
    uint8_t type;
};
static std::map<std::pair<int, int>, std::vector<pendingBlock >> pendingBlockPlacements;

void clearPendingBlockPlacements() {
    pendingBlockPlacements.clear();
}

Chunk::Chunk(int x, int z, World* worldPtr) :
    chunkX(x), chunkZ(z), world(worldPtr), VAO(0), VBO(0), EBO(0), indexCount(0),
    crossVAO(0), crossVBO(0), crossEBO(0), crossIndexCount(0),
    liquidVAO(0), liquidVBO(0), liquidEBO(0), liquidIndexCount(0) {

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
            buildMesh();
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
    glDeleteVertexArrays(1, &liquidVAO);
    glDeleteBuffers(1, &liquidVBO);
    glDeleteBuffers(1, &liquidEBO);

    liquidVertexDataCPU.clear();
    liquidIndexDataCPU.clear();
}

void Chunk::placeStructure(const Structure& structure, int baseX, int baseY, int baseZ) {
    int structHeight = (int)structure.layers.size();
    int structDepth = (int)structure.layers[0].size();
    int structWidth = (int)structure.layers[0][0].size();
    std::set<Chunk*> affectedChunks; // Track which chunks are affected

    for (int y = 0; y < structHeight; y++) {
        for (int z = 0; z < structDepth; z++) {
            for (int x = 0; x < structWidth; x++) {
                uint16_t blockCode = structure.layers[y][z][x];
                uint8_t chance = blockCode / 1000;
                uint8_t blockType = blockCode % 1000;

                int worldX = baseX + x;
                int worldY = baseY + y;
                int worldZ = baseZ + z;

                if (blockType == 0) 
                    continue;
                if (blockType == 44) // Structure air block
                    blockType = 0;

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
                        if (!targetChunk->loadedFromSave && !targetChunk->isModified) {
                            targetChunk->blocks[localX][worldY][localZ].type = blockType;
                            affectedChunks.insert(targetChunk);
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
    // Rebuild mesh for all affected chunks
    for (Chunk* chunk : affectedChunks) {
        chunk->buildMesh();
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

    std::vector<float> vertices;
    std::vector<float> crossVertices;
    std::vector<float> liquidVertices;
    std::vector<unsigned int> indices;
    std::vector<unsigned int> crossIndices;
    std::vector<unsigned int> liquidIndices;
    unsigned int indexOffset = 0;
    unsigned int crossIndexOffset = 0;
    unsigned int liquidIndexOffset = 0;

    for (int x = 0; x < chunkWidth; x++) {
        for (int y = 0; y < chunkHeight; y++) {
            for (int z = 0; z < chunkDepth; z++) {
                const uint8_t& type = blocks[x][y][z].type;
                if (type == 0) continue;

                const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(type);
                if (!info) continue;

                const Model* m = ModelDB::getModel(info->modelName);
                if (m && !m->planes.empty()) {
                    for (int face = 0; face < (int)m->planes.size(); face++) {
                        addFace(crossVertices, crossIndices, x, y, z, face, info, crossIndexOffset);
                    }
                } else if (info->liquid) {
                    for (int face = 0; face < 6; face++) {
                        if (isBlockVisible(x, y, z, face)) {
                            addFace(liquidVertices, liquidIndices, x, y, z, face, info, liquidIndexOffset);
                        }
                    }
                } else {
                    for (int face = 0; face < 6;face++) {
                        if (isBlockVisible(x, y, z, face)) {
                            addFace(vertices, indices, x, y, z, face, info, indexOffset);
                        }
                    }
                }
            }
        }
    }

    indexCount = static_cast<GLsizei>(indices.size());
    crossIndexCount = static_cast<GLsizei>(crossIndices.size());
    liquidIndexCount = static_cast<GLsizei>(liquidIndices.size());

    if (VAO == 0) {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);

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
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
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
    
    if (liquidVAO == 0) {
        glGenVertexArrays(1, &liquidVAO);
        glGenBuffers(1, &liquidVBO);
        glGenBuffers(1, &liquidEBO);

        glBindVertexArray(liquidVAO);

        glBindBuffer(GL_ARRAY_BUFFER, liquidVBO);
        glBufferData(GL_ARRAY_BUFFER, liquidVertices.size() * sizeof(float), liquidVertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, liquidEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, liquidIndices.size() * sizeof(unsigned int), liquidIndices.data(), GL_DYNAMIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(3);

        glBindVertexArray(0);
    } else {
        glBindVertexArray(liquidVAO);

        glBindBuffer(GL_ARRAY_BUFFER, liquidVBO);
        glBufferData(GL_ARRAY_BUFFER, liquidVertices.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, liquidVertices.size() * sizeof(float), liquidVertices.data());

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, liquidEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, liquidIndices.size() * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, liquidIndices.size() * sizeof(unsigned int), liquidIndices.data());

        glBindVertexArray(0);
    }

    // Store CPU side copies for sorting
    liquidVertexDataCPU = std::move(liquidVertices);
    liquidIndexDataCPU = std::move(liquidIndices);
}

bool Chunk::isBlockVisible(int x, int y, int z, int face) const {
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

    Chunk* neighbor = const_cast<Chunk*>(this);
    int neighborLocalX = neighborX;
    int neighborLocalZ = neighborZ;
    int neighborChunkX = chunkX;
    int neighborChunkZ = chunkZ;

    bool outsideChunk =
        neighborX < 0 || neighborX >= chunkWidth ||
        neighborZ < 0 || neighborZ >= chunkDepth;

    if (outsideChunk) {
        if (neighborLocalX < 0) {
            neighborChunkX -= 1;
            neighborLocalX += chunkWidth;
        } else if (neighborLocalX >= chunkWidth) {
            neighborChunkX += 1;
            neighborLocalX -= chunkWidth;
        }

        if (neighborLocalZ < 0) {
            neighborChunkZ -= 1;
            neighborLocalZ += chunkDepth;
        } else if (neighborLocalZ >= chunkDepth) {
            neighborChunkZ += 1;
            neighborLocalZ -= chunkDepth;
        }

        neighbor = world->getChunk(neighborChunkX, neighborChunkZ);
        if (!neighbor)
            return true;
    }

    uint8_t neighborType = neighbor->blocks[neighborLocalX][neighborY][neighborLocalZ].type;

    if (neighborType == 0)
        return true;

    const BlockDB::BlockInfo* neighborInfo = BlockDB::getBlockInfo(neighborType);
    const BlockDB::BlockInfo* thisInfo = BlockDB::getBlockInfo(blocks[x][y][z].type);

    static int fasterTrees = getOptionInt("faster_trees", 0);
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

void Chunk::addFace(std::vector<float>& vertices, std::vector<unsigned int>& indices,
                    int x, int y, int z, int face, const BlockDB::BlockInfo* blockInfo, unsigned int& offset) {
    const Model* model = ModelDB::getModel(blockInfo->modelName);
    if (!model || (model->cuboids.empty() && model->planes.empty())) return;

    static const char* faceNames[6] = {"north", "south", "west", "east", "up", "down"};
    std::string faceName = faceNames[face];

    constexpr float atlasSize = 16.0f;

    if (!model->planes.empty()) {
        int planeIndex = face;
        if (planeIndex < 0 || planeIndex >= (int)model->planes.size()) planeIndex = 0;
        const auto& plane = model->planes[planeIndex];
        if (!plane.faces.empty()) {
            const auto& faceData = plane.faces.begin()->second;
            if (faceData.uv.size() == 4) {
                glm::vec2 atlasOffset = blockInfo->textureCoords[0];

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
                    glm::vec2 uv = (atlasOffset + glm::vec2(faceData.uv[i].first, faceData.uv[i].second)) / atlasSize;\
                    vertices.insert(vertices.end(), {pos.x, pos.y, pos.z, uv.x, uv.y, 0.0f});
                }

                indices.insert(indices.end(), {offset, offset + 1, offset + 2, offset + 2, offset + 3, offset});
                offset += 4;
            }
        }
        return;
    }

    for (size_t cuboidIndex = 0; cuboidIndex < model->cuboids.size(); cuboidIndex++) {
        const auto& cuboid = model->cuboids[cuboidIndex];
        auto it = cuboid.faces.find(faceName);
        if (it == cuboid.faces.end()) continue;
        const auto& faceData = it->second;
        if (faceData.uv.size() != 4) continue;

        glm::vec3 faceVerts[4];
        switch (face) {
            case 0: // north (z+)
                faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.to.z);
                faceVerts[1] = glm::vec3(cuboid.to.x, cuboid.from.y, cuboid.to.z);
                faceVerts[2] = glm::vec3(cuboid.to.x, cuboid.to.y, cuboid.to.z);
                faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.to.y, cuboid.to.z);
                break;
            case 1: // south (z-)
                faceVerts[0] = glm::vec3(cuboid.to.x, cuboid.from.y, cuboid.from.z);
                faceVerts[1] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.from.z);
                faceVerts[2] = glm::vec3(cuboid.from.x, cuboid.to.y, cuboid.from.z);
                faceVerts[3] = glm::vec3(cuboid.to.x, cuboid.to.y, cuboid.from.z);
                break;
            case 2: // west (x-)
                faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.from.z);
                faceVerts[1] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.to.z);
                faceVerts[2] = glm::vec3(cuboid.from.x, cuboid.to.y, cuboid.to.z);
                faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.to.y, cuboid.from.z);
                break;
            case 3: // east (x+)
                faceVerts[0] = glm::vec3(cuboid.to.x, cuboid.from.y, cuboid.to.z);
                faceVerts[1] = glm::vec3(cuboid.to.x, cuboid.from.y, cuboid.from.z);
                faceVerts[2] = glm::vec3(cuboid.to.x, cuboid.to.y, cuboid.from.z);
                faceVerts[3] = glm::vec3(cuboid.to.x, cuboid.to.y, cuboid.to.z);
                break;
            case 4: // up (y+)
                faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.to.y, cuboid.to.z);
                faceVerts[1] = glm::vec3(cuboid.to.x, cuboid.to.y, cuboid.to.z);
                faceVerts[2] = glm::vec3(cuboid.to.x, cuboid.to.y, cuboid.from.z);
                faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.to.y, cuboid.from.z);
                break;
            case 5: // down (y-)
                faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.from.z);
                faceVerts[1] = glm::vec3(cuboid.to.x, cuboid.from.y, cuboid.from.z);
                faceVerts[2] = glm::vec3(cuboid.to.x, cuboid.from.y, cuboid.to.z);
                faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.to.z);
                break;
            default:
                continue;
        }

        glm::vec2 atlasOffset = blockInfo->textureCoords[face];
        if (!blockInfo->multiTextureCoords.empty()) {
            if (cuboidIndex < blockInfo->multiTextureCoords.size()) {
                atlasOffset = blockInfo->multiTextureCoords[cuboidIndex][face];
            } else {
                atlasOffset = blockInfo->textureCoords[face];
            }
        }

        bool isLiquid = blockInfo->liquid;
        bool liquidAbove = false;
        int aboveY = y + 1;

        if (aboveY >= 0 && aboveY < chunkHeight) {
            uint8_t aboveType = blocks[x][aboveY][z].type;
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
        for (int i = 0; i < 4; ++i) {
            glm::vec3 pos = faceVerts[i] + glm::vec3(x, y, z);
            glm::vec2 uv = (atlasOffset + glm::vec2(faceData.uv[i].first, faceData.uv[i].second)) / atlasSize;

            if (isLiquid) {
                float isTop = 0.0f;
                bool isTopFace = (face == 4) && !liquidAbove;
                if (!liquidAbove) {
                    if (isTopFace || (face <= 3 && std::abs(faceVerts[i].y - faceMaxY) < eps))
                        isTop = 1.0f;
                }
                vertices.insert(vertices.end(), {pos.x, pos.y, pos.z, uv.x, uv.y, static_cast<float>(face), isTop});
            } else {
                vertices.insert(vertices.end(), {pos.x, pos.y, pos.z, uv.x, uv.y, static_cast<float>(face)});
            }
        }

        indices.insert(indices.end(), {
            offset, offset + 1, offset + 2,
            offset + 2, offset + 3, offset
        });
        offset += 4;
    }
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

void Chunk::renderLiquid(const Camera& camera, GLint uLiquidModelLoc) {
    glm::dvec3 chunkWorldPos = glm::dvec3(chunkX * chunkWidth, 0, chunkZ * chunkDepth);
    glm::dvec3 relativePos = chunkWorldPos - camera.getPositionDouble();
    glm::mat4 liquidModel = glm::translate(glm::mat4(1.0f), glm::vec3(relativePos));
    glUniformMatrix4fv(uLiquidModelLoc, 1, GL_FALSE, &liquidModel[0][0]);

    // If there are no liquid indices, nothing to do
    if (liquidIndexCount == 0 || liquidVertexDataCPU.empty() || liquidIndexDataCPU.empty()) 
        return;

    glm::dvec3 camPosWorld = camera.getPositionDouble();
    glm::vec3 camPosLocal = glm::vec3(camPosWorld - glm::dvec3(chunkX * chunkWidth, 0.0f, chunkZ * chunkDepth));

    const size_t stride = 7; // pos(3) uv(2) faceID(1) isTop(1)
    const size_t vertsCount = liquidVertexDataCPU.size() / stride;

    struct FaceInfo { size_t baseIdx; float dist2; };
    std::vector<FaceInfo> faces;
    faces.reserve(liquidIndexDataCPU.size() / 6);

    auto getVertex = [&](unsigned int idx) -> glm::vec3 {
        size_t base = static_cast<size_t>(idx) * stride;
        return glm::vec3(liquidVertexDataCPU[base + 0], liquidVertexDataCPU[base + 1], liquidVertexDataCPU[base + 2]);
    };

    for (size_t i = 0; i + 5 < liquidIndexDataCPU.size(); i += 6) {
        unsigned int ia = liquidIndexDataCPU[i + 0];
        unsigned int ib = liquidIndexDataCPU[i + 1];
        unsigned int ic = liquidIndexDataCPU[i + 2];
        unsigned int id = liquidIndexDataCPU[i + 4];

        if (ia >= vertsCount || ib >= vertsCount || ic >= vertsCount || id >= vertsCount) 
            continue;

        glm::vec3 a = getVertex(ia);
        glm::vec3 b = getVertex(ib);
        glm::vec3 c = getVertex(ic);
        glm::vec3 d = getVertex(id);

        glm::vec3 centroid = (a + b + c + d) / 4.0f;
        float d2 = glm::dot(centroid - camPosLocal, centroid - camPosLocal);

        faces.push_back({i, d2});
    }

    // Sort faces back to front
    std::sort(faces.begin(), faces.end(), [](const FaceInfo& A, const FaceInfo& B) {
        return A.dist2 > B.dist2;
    });

    std::vector<unsigned int> sortedIndices;
    sortedIndices.reserve(faces.size() * 6);
    for (const auto& f : faces) {
        for (size_t k = 0; k < 6; ++k) {
            sortedIndices.push_back(liquidIndexDataCPU[f.baseIdx + k]);
        }
    }

    glBindVertexArray(liquidVAO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, liquidEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sortedIndices.size() * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sortedIndices.size() * sizeof(unsigned int), sortedIndices.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sortedIndices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}