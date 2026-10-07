#pragma once

#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "../world/biomeDB.hpp"

struct CloudVertex {
    float x, y, z;
    float brightness;
};

class Sky {
public:
    std::string name = "Sky";
    glm::vec3 skyColor = glm::vec3(0.45f, 0.68f, 1.0f);
    glm::vec3 horizonColor = glm::vec3(0.72f, 0.84f, 0.95f);
    glm::vec3 fogColor = glm::vec3(0.72f, 0.84f, 0.95f);

    float cloudHeight = 190.1f;
    float cloudSpeed = 0.0015f;
    float cloudScale = 0.0015f;
    float cloudThreshold = 0.78f;
    float cloudPixelSize = 10.0f;
    float cloudThickness = 4.0f;
    int cloudTextureSize = 256;
    glm::vec3 cloudColor = glm::vec3(1.0f, 1.0f, 1.0f);
    GLuint cloudTexture = 0;
    GLuint cloudVAO = 0;
    GLuint cloudVBO = 0;
    int cloudVertexCount = 0;

    // 3D
    std::vector<uint8_t> cloudNoiseData;
    GLuint cloud3DVAO = 0;
    GLuint cloud3DVBO = 0;
    int cloud3DVertexCount = 0;
    int lastCenterCX = -999999;
    int lastCenterCZ = -999999;
    int lastCloudRadius = -1;
    glm::vec3 cloud3DOffset = glm::vec3(0.0f);
    float cloudRenderDistance = 384.0f;

    void init(int seed);
    void updateSeed(int seed);
    void update3DMesh(const glm::dvec3& camPos, double time);
    void compute2DCloudOrigin(const glm::dvec3& camPos, double time, glm::vec2& outOriginFrac, glm::vec2& outBaseUV) const;
    void cleanup();

private:
    int currentSeed = 0;
    std::vector<BiomeNoise> noises;

    void loadJson(const std::string& path);
    void generateNoiseTexture();
    void initMesh();
    bool isCloudCell(int cx, int cz) const;
};

