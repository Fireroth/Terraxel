#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#include <set>
#include "blockPreviewRenderer.hpp"
#include "shader.hpp"
#include "../world/blockDB.hpp"
#include "../world/modelDB.hpp"
#include "../core/logger.hpp"

GLuint BlockPreviewRenderer::atlas = 0;
GLuint BlockPreviewRenderer::shaderProgram = 0;
GLuint BlockPreviewRenderer::fbo = 0;
GLuint BlockPreviewRenderer::depthRbo = 0;
std::unordered_map<uint16_t, GLuint> BlockPreviewRenderer::previewTextures;

static const int PREVIEW_SIZE = 64;

// Models listed here will render as a flat 2D atlas face instead of a 3D preview.
static const std::set<std::string> flatRenderModels = {
    "cross"
};

GLuint BlockPreviewRenderer::createPreviewShader() {
    std::string vertSrc = loadShaderSource("shaders/preview_vertex.glsl");
    std::string fragSrc = loadShaderSource("shaders/preview_fragment.glsl");
    return createShaderProgram(vertSrc.c_str(), fragSrc.c_str());
}

void BlockPreviewRenderer::init(GLuint textureAtlas) {
    LOG_INFO("BlockPreviewRenderer: Initializing...");
    atlas = textureAtlas;
    shaderProgram = createPreviewShader();

    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &depthRbo);

    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, PREVIEW_SIZE, PREVIEW_SIZE);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void BlockPreviewRenderer::buildBlockMesh(uint16_t blockId, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    const BlockDB::BlockInfo* info = BlockDB::getBlockInfo(blockId);
    if (!info) return;

    const Model* model = ModelDB::getModel(info->modelName);
    if (!model) return;

    static const char* faceNames[6] = {"north", "south", "west", "east", "up", "down"};
    constexpr float atlasSize = 16.0f;
    unsigned int offset = 0;

    // Render cuboid faces
    for (size_t cuboidIndex = 0; cuboidIndex < model->cuboids.size(); cuboidIndex++) {
        const auto& cuboid = model->cuboids[cuboidIndex];

        for (int face = 0; face < 6; face++) {
            if (face == 3 || face == 5 || face == 0 ) continue;

            std::string faceName = faceNames[face];
            auto iterator = cuboid.faces.find(faceName);
            if (iterator == cuboid.faces.end()) continue;
            const auto& faceData = iterator->second;

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
                    faceVerts[0] = glm::vec3(cuboid.to.x, cuboid.from.y, cuboid.to.z);
                    faceVerts[1] = glm::vec3(cuboid.to.x, cuboid.from.y, cuboid.from.z);
                    faceVerts[2] = glm::vec3(cuboid.to.x, cuboid.to.y,   cuboid.from.z);
                    faceVerts[3] = glm::vec3(cuboid.to.x, cuboid.to.y,   cuboid.to.z);
                    break;
                case 4: // up (y+)
                    faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.to.y, cuboid.to.z);
                    faceVerts[1] = glm::vec3(cuboid.to.x,   cuboid.to.y, cuboid.to.z);
                    faceVerts[2] = glm::vec3(cuboid.to.x,   cuboid.to.y, cuboid.from.z);
                    faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.to.y, cuboid.from.z);
                    break;
                case 5: // down (y-)
                    faceVerts[0] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.from.z);
                    faceVerts[1] = glm::vec3(cuboid.to.x,   cuboid.from.y, cuboid.from.z);
                    faceVerts[2] = glm::vec3(cuboid.to.x,   cuboid.from.y, cuboid.to.z);
                    faceVerts[3] = glm::vec3(cuboid.from.x, cuboid.from.y, cuboid.to.z);
                    break;
            }

            size_t texIndex = model->planes.size() + cuboidIndex;
            glm::vec2 atlasOffset = info->textureCoords[face];
            if (!info->multiTextureCoords.empty()) {
                if (texIndex < info->multiTextureCoords.size()) {
                    atlasOffset = info->multiTextureCoords[texIndex][face];
                } else {
                    atlasOffset = info->textureCoords[face];
                }
            }

            for (int i = 0; i < 4; ++i) {
                glm::vec3 pos = faceVerts[i];
                float local_u = (i == 1 || i == 2) ? faceData.uvTo.x : faceData.uvFrom.x;
                float local_v = (i == 2 || i == 3) ? faceData.uvTo.y : faceData.uvFrom.y;
                glm::vec2 uv = (atlasOffset + glm::vec2(local_u, local_v)) / atlasSize;
                vertices.insert(vertices.end(), {pos.x, pos.y, pos.z, uv.x, uv.y, static_cast<float>(face)});
            }

            indices.insert(indices.end(), {offset, offset + 1, offset + 2, offset + 2, offset + 3, offset});
            offset += 4;
        }
    }

    // Render plane faces
    for (size_t planeIndex = 0; planeIndex < model->planes.size(); planeIndex++) {
        const auto& plane = model->planes[planeIndex];
        if (plane.faces.empty()) continue;

        const auto& faceData = plane.faces.begin()->second;

        size_t texIndex = planeIndex;
        glm::vec2 atlasOffset = info->textureCoords[0];
        if (!info->multiTextureCoords.empty()) {
            if (texIndex < info->multiTextureCoords.size()) {
                atlasOffset = info->multiTextureCoords[texIndex][0];
            } else {
                atlasOffset = info->textureCoords[0];
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
            float local_u = (i == 1 || i == 2) ? faceData.uvTo.x : faceData.uvFrom.x;
            float local_v = (i == 2 || i == 3) ? faceData.uvTo.y : faceData.uvFrom.y;
            glm::vec2 uv = (atlasOffset + glm::vec2(local_u, local_v)) / atlasSize;
            vertices.insert(vertices.end(), {pos.x, pos.y, pos.z, uv.x, uv.y, 0.0f});
        }

        indices.insert(indices.end(), {offset, offset + 1, offset + 2, offset + 2, offset + 3, offset});
        offset += 4;
    }
}

void BlockPreviewRenderer::generatePreviews() {
    for (auto& [id, tex] : previewTextures) {
        glDeleteTextures(1, &tex);
    }
    previewTextures.clear();

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glm::vec3 center(0.5f, 0.5f, 0.5f);

    float azimuth = glm::radians(225.0f);
    float elevation = glm::radians(30.0f);
    float dist = 2.0f;
    glm::vec3 eye = center + glm::vec3(
        dist * cos(elevation) * sin(azimuth),
        dist * sin(elevation),
        dist * cos(elevation) * cos(azimuth)
    );

    glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));

    float orthoSize = 0.9f;
    glm::mat4 projection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, 10.0f);

    GLint uModelLoc = glGetUniformLocation(shaderProgram, "model");
    GLint uViewLoc = glGetUniformLocation(shaderProgram, "view");
    GLint uProjLoc = glGetUniformLocation(shaderProgram, "projection");
    GLint uAtlasLoc = glGetUniformLocation(shaderProgram, "atlas");
    GLint uFogDensityLoc = glGetUniformLocation(shaderProgram, "fogDensity");
    GLint uCamPosLoc = glGetUniformLocation(shaderProgram, "cameraPos");

    glm::mat4 model = glm::mat4(1.0f);

    for (uint16_t id : BlockDB::getRegisteredBlockIDs()) {
        if (id == 0) continue;
        const auto* blockInfo = BlockDB::getBlockInfo(id);
        if (!blockInfo) continue;

        if (flatRenderModels.count(blockInfo->modelName)) continue;

        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        buildBlockMesh(id, vertices, indices);
        if (indices.empty()) continue;

        const Model* blockModel = ModelDB::getModel(blockInfo->modelName);
        bool isPlaneOnly = blockModel && blockModel->cuboids.empty() && !blockModel->planes.empty();
        float ortho = isPlaneOnly ? 0.70f : orthoSize;
        glm::mat4 projection = glm::ortho(-ortho, ortho, -ortho, ortho, 0.1f, 10.0f);

        GLuint vao, vbo, ebo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        // pos(3), uv(2), faceID(1)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);

        GLuint colorTex;
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PREVIEW_SIZE, PREVIEW_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

        glViewport(0, 0, PREVIEW_SIZE, PREVIEW_SIZE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_CULL_FACE);

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas);
        glUniform1i(uAtlasLoc, 0);

        if (uFogDensityLoc != -1)
            glUniform1f(uFogDensityLoc, 0.0f);
        if (uCamPosLoc != -1)
            glUniform3fv(uCamPosLoc, 1, glm::value_ptr(eye));

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        previewTextures[id] = colorTex;

        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    //glClearColor(0.6f, 1.0f, 1.0f, 1.0f); // Restore default clear color
}

GLuint BlockPreviewRenderer::getPreviewTexture(uint16_t blockId) {
    auto iterator = previewTextures.find(blockId);
    if (iterator != previewTextures.end()) return iterator->second;
    return 0;
}

void BlockPreviewRenderer::cleanup() {
    for (auto& [id, tex] : previewTextures) {
        glDeleteTextures(1, &tex);
    }
    previewTextures.clear();

    if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if (depthRbo) { glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    if (shaderProgram) { glDeleteProgram(shaderProgram); shaderProgram = 0; }
}
