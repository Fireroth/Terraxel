#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include "camera.hpp"
#include "../world/world.hpp"
#include "../world/chunk.hpp"
#include "../world/blockDB.hpp"
#include "../world/modelDB.hpp"
#include "../core/options.hpp"

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : position(glm::dvec3(position)), worldUp(up), yaw(yaw), pitch(pitch), movementSpeed(2.5f), stepViewOffset(0.0) {
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    // Use camera relative rendering, position camera is always at origin
    // This prevents floating-point precision issues when far from world origin
    return glm::lookAt(glm::vec3(0.0f), front, up);
}

void Camera::processKeyboard(const char *direction, float deltaTime, float speedMultiplier, bool ignoreAirControl) {
    float acceleration = movementSpeed * speedMultiplier * 11.0f;
    glm::vec3 accel(0.0f);

    if (direction[0] == 'F' && direction[1] == 'O')   // FORWARD
        accel += glm::normalize(glm::vec3(front.x, 0.0f, front.z)) * acceleration;
     else if (direction[0] == 'B')   // BACKWARD
        accel -= glm::normalize(glm::vec3(front.x, 0.0f, front.z)) * acceleration;
     else if (direction[0] == 'L')   // LEFT
        accel -= right * acceleration;
     else if (direction[0] == 'R')   // RIGHT
        accel += right * acceleration;
     else if (direction[0] == 'U')   // UP
        accel += worldUp * acceleration;
     else if (direction[0] == 'D')  // DOWN
        accel -= worldUp * acceleration;

    applyAcceleration(accel, deltaTime, ignoreAirControl);
}

void Camera::processMouseMovement(float xOffset, float yOffset) {
    xOffset *= getOptionInt("mouse_sensitivity", 10) / 100.0f;
    yOffset *= getOptionInt("mouse_sensitivity", 10) / 100.0f;

    yaw += xOffset;
    pitch += yOffset;

    // Avoid screen flipping
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    if (yaw > 180.0f) yaw = -180.0f;
    if (yaw < -180.0f) yaw = 180.0f;

    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(newFront);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

static void getBlockAABBs(uint16_t blockId, std::vector<std::pair<glm::vec3, glm::vec3>>& outBoxes) {
    const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(blockId);
    if (!info) {
        outBoxes.push_back({glm::vec3(0.0f), glm::vec3(1.0f)});
        return;
    }

    if (!ModelDB::getCollisionBoxes(info->modelName, outBoxes)) {
        outBoxes.push_back({glm::vec3(0.0f), glm::vec3(1.0f)});
    }
}

static const double COLLISION_EPS = 1e-6;
static bool aabbOverlap(const glm::dvec3& amin, const glm::dvec3& amax, const glm::dvec3& bmin, const glm::dvec3& bmax) {
    return (amin.x <= bmax.x - COLLISION_EPS && amax.x >= bmin.x + COLLISION_EPS) &&
           (amin.y <= bmax.y - COLLISION_EPS && amax.y >= bmin.y + COLLISION_EPS) &&
           (amin.z <= bmax.z - COLLISION_EPS && amax.z >= bmin.z + COLLISION_EPS);
}

static bool aabbOverlapStrict(const glm::dvec3& amin, const glm::dvec3& amax, const glm::dvec3& bmin, const glm::dvec3& bmax) {
    return (amin.x < bmax.x - COLLISION_EPS && amax.x > bmin.x + COLLISION_EPS) &&
           (amin.y < bmax.y - COLLISION_EPS && amax.y > bmin.y + COLLISION_EPS) &&
           (amin.z < bmax.z - COLLISION_EPS && amax.z > bmin.z + COLLISION_EPS);
}

bool Camera::isInLiquid(World* world, float& outDrag) const {
    if (!world) return false;
    double feetY = position.y - eyeHeight;
    double shrinkRadius = playerRadius * 0.8;
    glm::dvec3 aabbMin(position.x - shrinkRadius, feetY, position.z - shrinkRadius);
    glm::dvec3 aabbMax(position.x + shrinkRadius, feetY + playerHeight * 0.9, position.z + shrinkRadius);

    bool foundLiquid = false;
    float maxDrag = 0.0f;

    for (int blockX = static_cast<int>(std::floor(aabbMin.x)); blockX <= static_cast<int>(std::floor(aabbMax.x)); ++blockX) {
        for (int blockZ = static_cast<int>(std::floor(aabbMin.z)); blockZ <= static_cast<int>(std::floor(aabbMax.z)); ++blockZ) {
            int chunkX = (blockX >= 0) ? (blockX / Chunk::chunkWidth) : ((blockX - Chunk::chunkWidth + 1) / Chunk::chunkWidth);
            int chunkZ = (blockZ >= 0) ? (blockZ / Chunk::chunkDepth) : ((blockZ - Chunk::chunkDepth + 1) / Chunk::chunkDepth);
            Chunk* chunk = world->getChunk(chunkX, chunkZ);
            if (!chunk) continue;

            for (int blockY = std::max(0, static_cast<int>(std::floor(aabbMin.y)));
                 blockY <= std::min(Chunk::chunkHeight - 1, static_cast<int>(std::floor(aabbMax.y)));
                 ++blockY) {
                int localX = blockX - chunkX * Chunk::chunkWidth;
                int localY = blockY;
                int localZ = blockZ - chunkZ * Chunk::chunkDepth;
                if (localX < 0 || localX >= Chunk::chunkWidth ||
                    localY < 0 || localY >= Chunk::chunkHeight ||
                    localZ < 0 || localZ >= Chunk::chunkDepth) continue;

                uint16_t type = chunk->blocks[localX][localY][localZ].type;
                if (type == 0) continue;
                const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(type);
                if (info && info->liquid) {
                    foundLiquid = true;
                    if (info->drag > maxDrag) {
                        maxDrag = info->drag;
                    }
                }
            }
        }
    }
    outDrag = maxDrag;
    return foundLiquid;
}

void Camera::updateVelocity(float deltaTime, World* world) {
    // Fix deltaTime spikes (resizing/moving the window)
    if (deltaTime > 0.05f) deltaTime = 0.05f;

    // Substep for stability at low FPS
    const float maxSubstep = 1.0f / 200.0f;
    int numSteps = static_cast<int>(ceil(deltaTime / maxSubstep));
    float subDelta = deltaTime / numSteps;

    for (int i = 0; i < numSteps; ++i) {
        stepVelocity(subDelta, world);
    }

    // Smoothly decay the camera step view offset
    if (std::abs(stepViewOffset) > 0.0001) {
        stepViewOffset *= std::exp(-stepViewOffsetSpeed * static_cast<double>(deltaTime));
        if (std::abs(stepViewOffset) < 0.0001) {
            stepViewOffset = 0.0;
        }
    }
}

void Camera::stepVelocity(float deltaTime, World* world) {
    if (jumpBuffered) {
        jumpBufferTimer -= deltaTime;
        if (jumpBufferTimer <= 0.0f) {
            jumpBuffered = false;
            jumpBufferTimer = 0.0f;
        }
    }
    if (!grounded) {
        coyoteTimer -= deltaTime;
        if (coyoteTimer < 0.0f) coyoteTimer = 0.0f;
    } else {
        coyoteTimer = coyoteTime;
    }

    inLiquid = world ? isInLiquid(world, liquidDrag) : false;

    if (inLiquid) {
        velocity.y += (gravity * 0.15f) * deltaTime;
    } else {
        velocity.y += gravity * deltaTime;
    }

    glm::dvec3 proposedPos = position;
    glm::dvec3 horizMove = glm::dvec3(velocity.x, 0.0, velocity.z) * static_cast<double>(deltaTime);
    double feetY_current = position.y - eyeHeight;

    auto isBlockSolid = [&](uint16_t type) -> bool {
        if (type == 0) return false;
        const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(type);
        if (!info) return true;
        std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
        return ModelDB::getCollisionBoxes(info->modelName, boxes);
    };

    auto collidesWithTop = [&](const glm::dvec3& aabbMin, const glm::dvec3& aabbMax, World* world, double& outBlockTop) -> bool {
        for (int blockX = static_cast<int>(std::floor(aabbMin.x)); blockX <= static_cast<int>(std::floor(aabbMax.x)); ++blockX) {
            for (int blockZ = static_cast<int>(std::floor(aabbMin.z)); blockZ <= static_cast<int>(std::floor(aabbMax.z)); ++blockZ) {
                int chunkX = (blockX >= 0) ? (blockX / Chunk::chunkWidth) : ((blockX - Chunk::chunkWidth + 1) / Chunk::chunkWidth);
                int chunkZ = (blockZ >= 0) ? (blockZ / Chunk::chunkDepth) : ((blockZ - Chunk::chunkDepth + 1) / Chunk::chunkDepth);
                Chunk* chunk = world->getChunk(chunkX, chunkZ);
                if (!chunk) continue;

                for (int blockY = std::max(0, static_cast<int>(std::floor(aabbMin.y)));
                     blockY <= std::min(Chunk::chunkHeight - 1, static_cast<int>(std::floor(aabbMax.y)));
                     ++blockY) {
                    int localX = blockX - chunkX * Chunk::chunkWidth;
                    int localY = blockY;
                    int localZ = blockZ - chunkZ * Chunk::chunkDepth;
                    if (localX < 0 || localX >= Chunk::chunkWidth ||
                        localY < 0 || localY >= Chunk::chunkHeight ||
                        localZ < 0 || localZ >= Chunk::chunkDepth) continue;

                    uint16_t type = chunk->blocks[localX][localY][localZ].type;
                    if (!isBlockSolid(type)) continue;

                    std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
                    getBlockAABBs(type, boxes);

                    for (const auto& [minF, maxF] : boxes) {
                        glm::dvec3 bmin = glm::dvec3(minF) + glm::dvec3(blockX, blockY, blockZ);
                        glm::dvec3 bmax = glm::dvec3(maxF) + glm::dvec3(blockX, blockY, blockZ);

                        if (aabbOverlapStrict(aabbMin, aabbMax, bmin, bmax)) {
                            outBlockTop = bmax.y;
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    };

    auto tryMoveOrStep = [&](const glm::dvec3& tryPos, glm::dvec3& outPos) -> bool {
        double feetY = tryPos.y - eyeHeight;
        glm::dvec3 aabbMin(tryPos.x - playerRadius, feetY, tryPos.z - playerRadius);
        glm::dvec3 aabbMax(tryPos.x + playerRadius, feetY + playerHeight, tryPos.z + playerRadius);

        double blockTop = 0.0;
        if (!collidesWithTop(aabbMin, aabbMax, world, blockTop)) {
            outPos = tryPos;
            return true;
        }

        // step up check
        double stepDiff = blockTop - feetY_current;
        if (grounded && !inLiquid && stepDiff > -0.01 - COLLISION_EPS && stepDiff <= stepHeight + COLLISION_EPS) {
            glm::dvec3 steppedPos = tryPos;
            steppedPos.y = blockTop + eyeHeight;
            double steppedFeetY = steppedPos.y - eyeHeight;

            glm::dvec3 sMin(steppedPos.x - playerRadius, steppedFeetY, steppedPos.z - playerRadius);
            glm::dvec3 sMax(steppedPos.x + playerRadius, steppedFeetY + playerHeight, steppedPos.z + playerRadius);

            double dummyTop;
            if (!collidesWithTop(sMin, sMax, world, dummyTop)) {
                outPos = steppedPos;
                velocity.y = 0.0;
                grounded = true;
                return true;
            }
        }

        return false; // blocked
    };

    if (world) {
        glm::dvec3 stepPos;

        // full X+Z
        if (tryMoveOrStep(position + horizMove, stepPos)) {
            if (stepPos.y > position.y) {
                stepViewOffset -= (stepPos.y - position.y);
            }
            proposedPos = stepPos;
        }
        // X only
        else if (tryMoveOrStep(position + glm::dvec3(horizMove.x, 0.0, 0.0), stepPos)) {
            if (stepPos.y > position.y) {
                stepViewOffset -= (stepPos.y - position.y);
            }
            proposedPos.x = stepPos.x;
            proposedPos.y = stepPos.y;
            velocity.z = 0.0;
        }
        // Z only
        else if (tryMoveOrStep(position + glm::dvec3(0.0, 0.0, horizMove.z), stepPos)) {
            if (stepPos.y > position.y) {
                stepViewOffset -= (stepPos.y - position.y);
            }
            proposedPos.z = stepPos.z;
            proposedPos.y = stepPos.y;
            velocity.x = 0.0;
        } else {
            velocity.x = velocity.z = 0.0;
        }

        // vertical movement
        proposedPos.y += velocity.y * deltaTime;
        double feetY = proposedPos.y - eyeHeight;
        glm::dvec3 aabbMin(proposedPos.x - playerRadius, feetY, proposedPos.z - playerRadius);
        glm::dvec3 aabbMax(proposedPos.x + playerRadius, feetY + playerHeight, proposedPos.z + playerRadius);

        grounded = false;
        double blockTop;
        if (collidesWithTop(aabbMin, aabbMax, world, blockTop)) {
            if (feetY <= blockTop + 0.01 + COLLISION_EPS &&
                feetY >= blockTop - stepHeight - COLLISION_EPS) {
                proposedPos.y = blockTop + eyeHeight + COLLISION_EPS;
                velocity.y = 0.0;
                grounded = true;
            } else {
                proposedPos.y = position.y;
                velocity.y = 0.0;
            }
        }
    } else {
        proposedPos += horizMove;
        proposedPos.y += velocity.y * deltaTime;
    }

    if (!inLiquid && jumpBuffered && (grounded || coyoteTimer > 0.0f)) {
        velocity.y = jumpPower;
        grounded = false;
        jumpBuffered = false;
        jumpBufferTimer = 0.0f;
        coyoteTimer = 0.0f;
        proposedPos.y += 0.001f;
    }

    position = proposedPos;

    // drag
    if (inLiquid) {
        velocity -= velocity * glm::min(static_cast<double>(liquidDrag * deltaTime), 1.0);
        if (glm::length(velocity) < 0.01) velocity = glm::dvec3(0.0);
    } else {
        glm::dvec3 horizVel = glm::dvec3(velocity.x, 0.0, velocity.z);
        float drag = grounded ? 9.0f : 9.0f * airDragFactor;
        horizVel -= horizVel * glm::min(static_cast<double>(drag * deltaTime), 1.0);
        if (glm::length(horizVel) < 0.01) horizVel = glm::dvec3(0.0);
        velocity.x = horizVel.x;
        velocity.z = horizVel.z;
    }
}

void Camera::updateVelocityFlight(float deltaTime) {
    position += velocity * static_cast<double>(deltaTime);

    inLiquid = false;
    liquidDrag = 0.0f;

    float drag = 9.0f;
    velocity -= velocity * glm::min(static_cast<double>(drag * deltaTime), 1.0);

    if (glm::length(velocity) < 0.01)
        velocity = glm::dvec3(0.0);
}

void Camera::applyAcceleration(const glm::vec3& acceleration, float deltaTime, bool ignoreAirControl) {
    glm::dvec3 accel = glm::dvec3(acceleration) * static_cast<double>(deltaTime);
    if (inLiquid && !ignoreAirControl) {
        double factor = 0.25f;
        accel.x *= factor;
        accel.z *= factor;
        accel.y *= factor;
    } else if (!grounded && !ignoreAirControl) {
        double factor = static_cast<double>(airControlFactor);
        if (velocity.y > 0.0) {
            factor *= static_cast<double>(airJumpBoostFactor);
        }
        accel.x *= factor;
        accel.z *= factor;
    }
    velocity += accel;
}

void Camera::jump() {
    if (inLiquid) return;

    jumpBuffered = true;
    jumpBufferTimer = jumpBufferTime;

    if (grounded) {
        velocity.y = jumpPower;
        grounded = false;
        jumpBuffered = false;
        jumpBufferTimer = 0.0f;
        coyoteTimer = 0.0f;
    }
}

void Camera::setPosition(const glm::dvec3& pos) {
    position = glm::dvec3(pos);
    velocity = glm::dvec3(0.0);
    grounded = false;
    stepViewOffset = 0.0;
}

void Camera::setRotation(float newYaw, float newPitch) {
    yaw = newYaw;
    pitch = newPitch;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    if (yaw > 180.0f) yaw = -180.0f;
    if (yaw < -180.0f) yaw = 180.0f;

    updateCameraVectors();
}

uint16_t Camera::getEyeBlock(class World* world) const {
    if (!world) return 0;
    glm::dvec3 eyePos = getPositionDouble();
    int blockX = static_cast<int>(std::floor(eyePos.x));
    int blockY = static_cast<int>(std::floor(eyePos.y));
    int blockZ = static_cast<int>(std::floor(eyePos.z));

    if (blockY < 0 || blockY >= Chunk::chunkHeight) return 0;

    int chunkX = (blockX >= 0) ? (blockX / Chunk::chunkWidth) : ((blockX - Chunk::chunkWidth + 1) / Chunk::chunkWidth);
    int chunkZ = (blockZ >= 0) ? (blockZ / Chunk::chunkDepth) : ((blockZ - Chunk::chunkDepth + 1) / Chunk::chunkDepth);
    Chunk* chunk = world->getChunk(chunkX, chunkZ);
    if (!chunk) return 0;

    int localX = blockX - chunkX * Chunk::chunkWidth;
    int localY = blockY;
    int localZ = blockZ - chunkZ * Chunk::chunkDepth;

    if (localX < 0 || localX >= Chunk::chunkWidth ||
        localY < 0 || localY >= Chunk::chunkHeight ||
        localZ < 0 || localZ >= Chunk::chunkDepth) return 0;

    return chunk->blocks[localX][localY][localZ].type;
}

