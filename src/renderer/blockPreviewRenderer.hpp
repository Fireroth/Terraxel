#pragma once

#include <glad/glad.h>
#include <unordered_map>
#include <vector>
#include <cstdint>

class BlockPreviewRenderer {
public:
    static void init(GLuint textureAtlas);
    static void generatePreviews();
    static GLuint getPreviewTexture(uint16_t blockId);
    static void cleanup();

private:
    static GLuint atlas;
    static GLuint shaderProgram;
    static GLuint fbo;
    static GLuint depthRbo;
    static std::unordered_map<uint16_t, GLuint> previewTextures;

    static GLuint createPreviewShader();
    static void buildBlockMesh(uint16_t blockId, std::vector<float>& vertices, std::vector<unsigned int>& indices);
};
