#include <glad/glad.h>
#include "chunk.hpp"
#include "../core/camera.hpp"
#include "world.hpp"
#include "blockDB.hpp"
#include "modelDB.hpp"
#include "block_interaction.hpp"
#include "../renderer/imguiOverlay.hpp"


// Helper function to get Hitbox for a block model
bool getModelHitBoxes(uint8_t blockId, std::vector<std::pair<glm::vec3, glm::vec3>>& outBoxes) {
    const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(blockId);
    if (!info) return false;

    if (!ModelDB::getHitBoxes(info->modelName, outBoxes) || outBoxes.empty()) {
        outBoxes.clear();
        outBoxes.emplace_back(glm::vec3(0.0f), glm::vec3(1.0f));
    }
    return true;
}

// Ray-AABB intersection helper
bool rayAABBIntersect(const glm::dvec3& rayOrigin, const glm::dvec3& rayDir, const glm::dvec3& boxMin, const glm::dvec3& boxMax, double& hitDist, double maxRayDist) {
    double nearestEntry = 0.0;
    double farthestExit = maxRayDist;
    for (int axis = 0; axis < 3; axis++) {
        double inverseDir = 1.0 / rayDir[axis];
        double entryDist = (boxMin[axis] - rayOrigin[axis]) * inverseDir;
        double exitDist  = (boxMax[axis] - rayOrigin[axis]) * inverseDir;
        if (entryDist > exitDist) std::swap(entryDist, exitDist);
        nearestEntry = std::max(nearestEntry, entryDist);
        farthestExit = std::min(farthestExit, exitDist);
        if (farthestExit < nearestEntry) return false;
    }

    hitDist = nearestEntry;
    return nearestEntry <= maxRayDist && farthestExit >= 0.0;
}

// Helper function that returns the face normal of the AABB that was hit
glm::ivec3 getAABBHitNormal(const glm::dvec3& hitPoint, const glm::dvec3& boxMin, const glm::dvec3& boxMax) {
    const double helper = 0.0001;
    if (fabs(hitPoint.x - boxMin.x) < helper) return glm::ivec3(-1, 0, 0);
    if (fabs(hitPoint.x - boxMax.x) < helper) return glm::ivec3(1, 0, 0);
    if (fabs(hitPoint.y - boxMin.y) < helper) return glm::ivec3(0, -1, 0);
    if (fabs(hitPoint.y - boxMax.y) < helper) return glm::ivec3(0, 1, 0);
    if (fabs(hitPoint.z - boxMin.z) < helper) return glm::ivec3(0, 0, -1);
    if (fabs(hitPoint.z - boxMax.z) < helper) return glm::ivec3(0, 0, 1);
    return glm::ivec3(0, 0, 0); // fallback
}

// Helper function for correct chunk coordinate calculation
int worldToChunkCoord(int x, int chunkSize) {
    return (x >= 0) ? (x / chunkSize) : ((x - chunkSize + 1) / chunkSize);
}

RaycastResult raycast(World* world, const glm::dvec3& origin, const glm::vec3& dir, float maxDist) {
    RaycastResult result;

    glm::dvec3 rayPos = origin;
    glm::ivec3 blockPos = glm::floor(rayPos);

    glm::dvec3 deltaDist = glm::abs(glm::dvec3(1.0) / glm::dvec3(dir));
    glm::ivec3 step = glm::sign(dir);

    glm::dvec3 sideDist;
    for (int i = 0; i < 3; i++) {
        double offset = (step[i] > 0 ? (blockPos[i] + 1.0 - rayPos[i]) : (rayPos[i] - blockPos[i]));
        sideDist[i] = offset * deltaDist[i];
    }

    double traveledDist = 0.0;
    for (int i = 0; i < 128 && traveledDist <= maxDist; i++) {
        int chunkX = worldToChunkCoord(blockPos.x, Chunk::chunkWidth);
        int chunkZ = worldToChunkCoord(blockPos.z, Chunk::chunkDepth);
        Chunk* chunk = world->getChunk(chunkX, chunkZ);
        if (chunk) {
            int localX = blockPos.x - chunkX * Chunk::chunkWidth;
            int localY = blockPos.y;
            int localZ = blockPos.z - chunkZ * Chunk::chunkDepth;
            if (localX >= 0 && localX < Chunk::chunkWidth &&
                localY >= 0 && localY < Chunk::chunkHeight &&
                localZ >= 0 && localZ < Chunk::chunkDepth) {
                uint8_t type = chunk->blocks[localX][localY][localZ].type;
                if (type != 0) {
                    std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
                    getModelHitBoxes(type, boxes);

                    double bestT = std::numeric_limits<double>::infinity();
                    glm::ivec3 nearestNormal(0);
                    bool found = false;

                    for (const auto& p : boxes) {
                        glm::dvec3 boxMin = glm::dvec3(blockPos) + glm::dvec3(p.first);
                        glm::dvec3 boxMax = glm::dvec3(blockPos) + glm::dvec3(p.second);
                        double hitT;
                        if (rayAABBIntersect(origin, glm::dvec3(dir), boxMin, boxMax, hitT, static_cast<double>(maxDist))) {
                            if (hitT >= 0.0 && hitT < bestT) {
                                bestT = hitT;
                                nearestNormal = getAABBHitNormal(origin + glm::dvec3(dir) * hitT, boxMin, boxMax);
                                found = true;
                            }
                        }
                    }

                    if (found) {
                        result.hit = true;
                        result.hitChunk = chunk;
                        result.hitBlockPos = glm::ivec3(localX, localY, localZ);
                        result.faceNormal = nearestNormal;

                        glm::ivec3 placeWorldPos = blockPos + result.faceNormal;
                        int placeChunkX = worldToChunkCoord(placeWorldPos.x, Chunk::chunkWidth);
                        int placeChunkZ = worldToChunkCoord(placeWorldPos.z, Chunk::chunkDepth);
                        Chunk* placeChunk = world->getChunk(placeChunkX, placeChunkZ);
                        if (placeChunk) {
                            int placeLocalX = placeWorldPos.x - placeChunkX * Chunk::chunkWidth;
                            int placeLocalY = placeWorldPos.y;
                            int placeLocalZ = placeWorldPos.z - placeChunkZ * Chunk::chunkDepth;
                            if (placeLocalX >= 0 && placeLocalX < Chunk::chunkWidth &&
                                placeLocalY >= 0 && placeLocalY < Chunk::chunkHeight &&
                                placeLocalZ >= 0 && placeLocalZ < Chunk::chunkDepth) {
                                result.hasPlacePos = true;
                                result.placeBlockPos = glm::ivec3(placeLocalX, placeLocalY, placeLocalZ);
                                result.placeChunk = placeChunk;
                            }
                        }

                        return result;
                    }
                }
            }
        }

        // Step to next voxel
        if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
            blockPos.x += step.x;
            traveledDist = sideDist.x;
            sideDist.x += deltaDist.x;
        } else if (sideDist.y < sideDist.z) {
            blockPos.y += step.y;
            traveledDist = sideDist.y;
            sideDist.y += deltaDist.y;
        } else {
            blockPos.z += step.z;
            traveledDist = sideDist.z;
            sideDist.z += deltaDist.z;
        }
    }
    return result;
}

void placeBreakBlockOnClick(World* world, const Camera& camera, char action, uint8_t blockType) {
    glm::dvec3 origin = camera.getPositionDouble();
    glm::vec3 dir = camera.getFront();

    RaycastResult hit = raycast(world, origin, dir, 6.0f);

    int chunkX = 0, chunkZ = 0, x = 0, z = 0;

    // p = place, b = break
    if (action == 'b') {
        if (!hit.hit || !hit.hitChunk) return;
        hit.hitChunk->blocks[hit.hitBlockPos.x][hit.hitBlockPos.y][hit.hitBlockPos.z].type = 0;
        hit.hitChunk->isModified = true;
        hit.hitChunk->buildMesh();

        chunkX = hit.hitChunk->chunkX;
        chunkZ = hit.hitChunk->chunkZ;
        x = hit.hitBlockPos.x;
        z = hit.hitBlockPos.z;
    }
    else if (action == 'p') {
        if (hit.hit) {
            int placeY = hit.hitBlockPos.y + hit.faceNormal.y;
            if (placeY < 0 || placeY >= Chunk::chunkHeight) {
                showMessage("Cannot place block outside world bounds!", ImVec4(1.0f, 0.5f, 0.5f, 1.0f), 2.0f);
                return;
            }
        }
        if (!hit.hasPlacePos || !hit.placeChunk) return;
        auto& block = hit.placeChunk->blocks[hit.placeBlockPos.x][hit.placeBlockPos.y][hit.placeBlockPos.z];
        if (block.type != 0) return;

        // Prevent placing inside player
        std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
        getModelHitBoxes(blockType, boxes);

        glm::dvec3 playerPos = camera.getPositionDouble();
        float playerRadius = camera.getPlayerRadius();
        float playerHeight = camera.getPlayerHeight();
        float eyeHeight = camera.getEyeHeight();
        glm::dvec3 playerAABBMin = glm::dvec3(playerPos.x - playerRadius, playerPos.y - eyeHeight, playerPos.z - playerRadius);
        glm::dvec3 playerAABBMax = glm::dvec3(playerPos.x + playerRadius, playerPos.y - eyeHeight + playerHeight, playerPos.z + playerRadius);
        for (const auto& p : boxes) {
            glm::dvec3 blockWorldMin = glm::dvec3(
                hit.placeChunk->chunkX * Chunk::chunkWidth + hit.placeBlockPos.x,
                hit.placeBlockPos.y,
                hit.placeChunk->chunkZ * Chunk::chunkDepth + hit.placeBlockPos.z
            ) + glm::dvec3(p.first);
            glm::dvec3 blockWorldMax = glm::dvec3(
                hit.placeChunk->chunkX * Chunk::chunkWidth + hit.placeBlockPos.x,
                hit.placeBlockPos.y,
                hit.placeChunk->chunkZ * Chunk::chunkDepth + hit.placeBlockPos.z
            ) + glm::dvec3(p.second);

            bool overlap = (blockWorldMin.x < playerAABBMax.x && blockWorldMax.x > playerAABBMin.x) &&
                           (blockWorldMin.y < playerAABBMax.y && blockWorldMax.y > playerAABBMin.y) &&
                           (blockWorldMin.z < playerAABBMax.z && blockWorldMax.z > playerAABBMin.z);
            if (overlap) return;
        }

        block.type = blockType;
        hit.placeChunk->isModified = true;
        hit.placeChunk->buildMesh();

        chunkX = hit.placeChunk->chunkX;
        chunkZ = hit.placeChunk->chunkZ;
        x = hit.placeBlockPos.x;
        z = hit.placeBlockPos.z;
    }

    // Rebuild neighbor chunk mesh if at chunk edge
    if (x == 0) {
        Chunk* neighbor = world->getChunk(chunkX - 1, chunkZ);
        if (neighbor) neighbor->buildMesh();
    }
    if (x == Chunk::chunkWidth - 1) {
        Chunk* neighbor = world->getChunk(chunkX + 1, chunkZ);
        if (neighbor) neighbor->buildMesh();
    }
    if (z == 0) {
        Chunk* neighbor = world->getChunk(chunkX, chunkZ - 1);
        if (neighbor) neighbor->buildMesh();
    }
    if (z == Chunk::chunkDepth - 1) {
        Chunk* neighbor = world->getChunk(chunkX, chunkZ + 1);
        if (neighbor) neighbor->buildMesh();
    }
}

// For imgui ----------------------------------------------------------------------------
BlockInfo getLookedAtBlockInfo(World* world, const Camera& camera) {
    glm::dvec3 origin = camera.getPositionDouble();
    glm::vec3 dir = camera.getFront();

    RaycastResult hit = raycast(world, origin, dir, 6.0f);
    if (!hit.hit || !hit.hitChunk) return {};

    BlockInfo info;
    info.valid = true;
    info.worldPos = glm::ivec3(
        hit.hitChunk->chunkX * Chunk::chunkWidth + hit.hitBlockPos.x,
        hit.hitBlockPos.y,
        hit.hitChunk->chunkZ * Chunk::chunkDepth + hit.hitBlockPos.z
    );
    info.type = hit.hitChunk->blocks[hit.hitBlockPos.x][hit.hitBlockPos.y][hit.hitBlockPos.z].type;

    return info;
}