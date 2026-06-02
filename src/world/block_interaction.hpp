#pragma once

#include <glm/glm.hpp>
#include <string>

class Camera;
class Chunk;
class World;

bool rayAABBIntersect(const glm::dvec3& rayOrigin, const glm::dvec3& rayDir, const glm::dvec3& boxMin, const glm::dvec3& boxMax, double& tmin, double maxDistance);

struct RaycastResult {
    bool hit = false;
    glm::ivec3 hitBlockPos;
    Chunk* hitChunk = nullptr;

    bool hasPlacePos = false;
    glm::ivec3 placeBlockPos;
    Chunk* placeChunk = nullptr;

    glm::ivec3 faceNormal = glm::ivec3(0);
};

glm::ivec3 getAABBHitNormal(const glm::dvec3& hitPoint, const glm::dvec3& boxMin, const glm::dvec3& boxMax);
int worldToChunkCoord(int x, int chunkSize);
RaycastResult raycast(World* world, const glm::dvec3& origin, const glm::vec3& dir, float maxDistance);
void placeBreakBlockOnClick(World* world, const Camera& camera, char action, uint16_t blockType);

struct BlockInfo {
    bool valid = false;
    glm::ivec3 worldPos;
    uint16_t type;
};

BlockInfo getLookedAtBlockInfo(World* world, const Camera& camera);
