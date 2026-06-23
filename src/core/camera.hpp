#pragma once

#include <glm/glm.hpp>

class Camera {
public:
    Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch);

    glm::mat4 getViewMatrix() const;
    void processKeyboard(const char* direction, float deltaTime, float speedMultiplier = 1.0f, bool ignoreAirControl = false);
    void processMouseMovement(float xOffset, float yOffset);

    glm::dvec3 getPositionDouble() const { return position + glm::dvec3(0.0, stepViewOffset, 0.0); }
    glm::vec3 getPosition() const { return glm::vec3(position) + glm::vec3(0.0f, static_cast<float>(stepViewOffset), 0.0f); }
    glm::vec3 getFront() const { return front; }
    glm::vec3 getUp() const { return up; }
    glm::dvec3 getVelocity() const { return velocity; }
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    float getPlayerRadius() const { return playerRadius; }
    float getPlayerHeight() const { return playerHeight; }
    float getEyeHeight() const { return eyeHeight; }

    void updateVelocity(float deltaTime, class World* world = nullptr);
    void stepVelocity(float deltaTime, class World* world);
    void updateVelocityFlight(float deltaTime);
    void applyAcceleration(const glm::vec3& acceleration, float deltaTime, bool ignoreAirControl = false);
    void jump();
    bool isGrounded() const { return grounded; }
    bool isInLiquidCached() const { return inLiquid; }
    float getLiquidDragCached() const { return liquidDrag; }
    bool isInLiquid(class World* world, float& outDrag) const;
    uint16_t getEyeBlock(class World* world) const;

    void setPosition(const glm::dvec3& pos);
    void setRotation(float newYaw, float newPitch);

private:
    void updateCameraVectors();

    glm::dvec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;
    float pitch;

    float movementSpeed;
    float mouseSensitivity;

    glm::dvec3 velocity = glm::dvec3(0.0);
    
    const float gravity = -30.0f;
    const float jumpPower = 8.73f;
    const float playerHeight = 1.8f;
    const float eyeHeight = 1.62f;
    const float playerRadius = 0.3f;
    const float stepHeight = 0.56f;
    const float airControlFactor = 0.3f;
    const float airDragFactor = 0.32f;
    const float airJumpBoostFactor = 1.25f;
    const float coyoteTime = 0.05f; // seconds to allow jump after walking off an edge
    const float jumpBufferTime = 0.1f; // seconds to buffer a jump input
    const float stepViewOffsetSpeed = 35.0f;

    float coyoteTimer = 0.0f;
    float jumpBufferTimer = 0.0f;
    double stepViewOffset = 0.0;
    bool jumpBuffered = false;
    bool grounded = false;
    bool inLiquid = false;
    float liquidDrag = 0.0f;
};