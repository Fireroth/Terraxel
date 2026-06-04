#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include "renderer.hpp"
#include "shader.hpp"
#include "../core/camera.hpp"
#include "../core/options.hpp"
#include "../core/input.hpp"
#include "imguiOverlay.hpp"
#include "../world/block_interaction.hpp"
#include "../core/logger.hpp"


Renderer::Renderer()
    : shaderProgram(0), textureAtlas(0), textureAtlas2D(0),
      crosshairVAO(0), crosshairVBO(0), borderVAO(0), borderVBO(0), borderShaderProgram(0),
      fbo(0), fboColorTex(0), fboDepthTex(0), fboWidth(0), fboHeight(0),
      quadVAO(0), quadVBO(0), postProcessShaderProgram(0),
      uPostProcessTextureLoc(-1), uPostProcessEffectTypeLoc(-1), uPostProcessTimeLoc(-1),
      uPostProcessDepthTextureLoc(-1), uPostProcessInvProjLoc(-1),
      uPostProcessFogEnabledLoc(-1), uPostProcessNormalFogStartLoc(-1),
      uOpaqueFogEnabledLoc(-1), uOpaqueFogDensityLoc(-1), uOpaqueFogStartLoc(-1), uOpaqueFogColorLoc(-1),
      uCrossFogEnabledLoc(-1), uCrossFogDensityLoc(-1), uCrossFogStartLoc(-1), uCrossFogColorLoc(-1),
      uTranslucentFogEnabledLoc(-1), uTranslucentFogDensityLoc(-1), uTranslucentFogStartLoc(-1), uTranslucentFogColorLoc(-1) {}

Renderer::~Renderer() {
    LOG_INFO("Renderer: Cleaning up...");
    glDeleteTextures(1, &textureAtlas);
    glDeleteTextures(1, &uiAtlas);
    glDeleteTextures(1, &textureAtlas2D);
    glDeleteProgram(shaderProgram);

    glDeleteVertexArrays(1, &crosshairVAO);
    glDeleteBuffers(1, &crosshairVBO);
    glDeleteProgram(crosshairShaderProgram);

    glDeleteVertexArrays(1, &borderVAO);
    glDeleteBuffers(1, &borderVBO);
    glDeleteProgram(borderShaderProgram);

    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &fboColorTex);
        glDeleteTextures(1, &fboDepthTex);
    }
    if (quadVAO != 0) {
        glDeleteVertexArrays(1, &quadVAO);
        glDeleteBuffers(1, &quadVBO);
    }
    if (postProcessShaderProgram != 0) {
        glDeleteProgram(postProcessShaderProgram);
    }
}

void Renderer::init() {
    LOG_INFO("Renderer: Initializing...");
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);

    LOG_INFO("Renderer: Loading and compiling shaders");
    std::string vertexSource = loadShaderSource("shaders/vertex.glsl");
    std::string fragmentSource = loadShaderSource("shaders/fragment.glsl");
    shaderProgram = createShaderProgram(vertexSource.c_str(), fragmentSource.c_str());

    std::string crossVertexSource = loadShaderSource("shaders/cross_vertex.glsl");
    std::string crossFragmentSource = loadShaderSource("shaders/cross_fragment.glsl");
    crossShaderProgram = createShaderProgram(crossVertexSource.c_str(), crossFragmentSource.c_str());

    std::string translucentVertexSource = loadShaderSource("shaders/translucent_vertex.glsl");
    std::string translucentFragmentSource = loadShaderSource("shaders/translucent_fragment.glsl");
    translucentShaderProgram = createShaderProgram(translucentVertexSource.c_str(), translucentFragmentSource.c_str());
    
    std::string crosshairVertexSource = loadShaderSource("shaders/crosshair_vertex.glsl");
    std::string crosshairFragmentSource = loadShaderSource("shaders/crosshair_fragment.glsl");
    crosshairShaderProgram = createShaderProgram(crosshairVertexSource.c_str(), crosshairFragmentSource.c_str());

    std::string borderVertexSource = loadShaderSource("shaders/border_vertex.glsl");
    std::string borderFragmentSource = loadShaderSource("shaders/border_fragment.glsl");
    borderShaderProgram = createShaderProgram(borderVertexSource.c_str(), borderFragmentSource.c_str());

    std::string postProcessVertexSource = loadShaderSource("shaders/postprocess_vertex.glsl");
    std::string postProcessFragmentSource = loadShaderSource("shaders/postprocess_fragment.glsl");
    postProcessShaderProgram = createShaderProgram(postProcessVertexSource.c_str(), postProcessFragmentSource.c_str());


    uCrosshairAspectLoc = glGetUniformLocation(crosshairShaderProgram, "aspectRatio");

    uCrossModelLoc = glGetUniformLocation(crossShaderProgram, "model");
    uCrossViewLoc = glGetUniformLocation(crossShaderProgram, "view");
    uCrossProjLoc = glGetUniformLocation(crossShaderProgram, "projection");
    uCrossAtlasLoc = glGetUniformLocation(crossShaderProgram, "atlas");

    uTranslucentModelLoc = glGetUniformLocation(translucentShaderProgram, "model");
    uTranslucentViewLoc = glGetUniformLocation(translucentShaderProgram, "view");
    uTranslucentProjLoc = glGetUniformLocation(translucentShaderProgram, "projection");
    uTranslucentAtlasLoc = glGetUniformLocation(translucentShaderProgram, "atlas");
    uTranslucentTimeLoc = glGetUniformLocation(translucentShaderProgram, "time");

    uBorderModelLoc = glGetUniformLocation(borderShaderProgram, "model");
    uBorderViewLoc = glGetUniformLocation(borderShaderProgram, "view");
    uBorderProjLoc = glGetUniformLocation(borderShaderProgram, "projection");

    uModelLoc = glGetUniformLocation(shaderProgram, "model");
    uViewLoc = glGetUniformLocation(shaderProgram, "view");
    uProjLoc = glGetUniformLocation(shaderProgram, "projection");
    uAtlasLoc = glGetUniformLocation(shaderProgram, "atlas");

    uOpaqueFogEnabledLoc = glGetUniformLocation(shaderProgram, "fogEnabled");
    uOpaqueFogDensityLoc = glGetUniformLocation(shaderProgram, "fogDensity");
    uOpaqueFogStartLoc = glGetUniformLocation(shaderProgram, "fogStartDistance");
    uOpaqueFogColorLoc = glGetUniformLocation(shaderProgram, "fogColor");

    uCrossFogEnabledLoc = glGetUniformLocation(crossShaderProgram, "fogEnabled");
    uCrossFogDensityLoc = glGetUniformLocation(crossShaderProgram, "fogDensity");
    uCrossFogStartLoc = glGetUniformLocation(crossShaderProgram, "fogStartDistance");
    uCrossFogColorLoc = glGetUniformLocation(crossShaderProgram, "fogColor");

    uTranslucentFogEnabledLoc = glGetUniformLocation(translucentShaderProgram, "fogEnabled");
    uTranslucentFogDensityLoc = glGetUniformLocation(translucentShaderProgram, "fogDensity");
    uTranslucentFogStartLoc = glGetUniformLocation(translucentShaderProgram, "fogStartDistance");
    uTranslucentFogColorLoc = glGetUniformLocation(translucentShaderProgram, "fogColor");

    uPostProcessTextureLoc = glGetUniformLocation(postProcessShaderProgram, "screenTexture");
    uPostProcessEffectTypeLoc = glGetUniformLocation(postProcessShaderProgram, "effectType");
    uPostProcessTimeLoc = glGetUniformLocation(postProcessShaderProgram, "time");
    uPostProcessDepthTextureLoc = glGetUniformLocation(postProcessShaderProgram, "depthTexture");
    uPostProcessInvProjLoc = glGetUniformLocation(postProcessShaderProgram, "invProjection");
    uPostProcessFogEnabledLoc = glGetUniformLocation(postProcessShaderProgram, "fogEnabled");
    uPostProcessNormalFogStartLoc = glGetUniformLocation(postProcessShaderProgram, "normalFogStartDistance");

    LOG_INFO("Renderer: Loading texture assets");
    loadTextureAtlas("textures/blocks.png");
    loadTextureAtlas2D("textures/blocks.png");
    loadTextureUIAtlas("textures/ui.png");

    LOG_INFO("Renderer: Finishing initialization");
    initCrosshair();
    initBorderMesh();
    initPostProcessQuad();

    currentFov = getOptionFloat("fov", 70.0f);

    fogEnabled = getOptionInt("fog", 1);
    fogDensity = 0.30f;
    fogStartDistance = ((getOptionFloat("render_distance", 7) + 1) * 16) - 20;
    fogColor = glm::vec3(0.6f, 1.0f, 1.0f);
}

void Renderer::initCrosshair() {
    float crosshairVertices[] = {
        -0.025f,  0.0f,
         0.025f,  0.0f,
         0.0f,  -0.025f,
         0.0f,   0.025f
    };

    glGenVertexArrays(1, &crosshairVAO);
    glGenBuffers(1, &crosshairVBO);

    glBindVertexArray(crosshairVAO);

    glBindBuffer(GL_ARRAY_BUFFER, crosshairVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVertices), crosshairVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Renderer::initBorderMesh() {
    float borderVertices[] = {
        // Bottom
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        // Top
        0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 1.0f
    };

    unsigned int borderIndices[] = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7
    };

    glGenVertexArrays(1, &borderVAO);
    glGenBuffers(1, &borderVBO);
    GLuint borderEBO;
    glGenBuffers(1, &borderEBO);

    glBindVertexArray(borderVAO);

    glBindBuffer(GL_ARRAY_BUFFER, borderVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(borderVertices), borderVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, borderEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(borderIndices), borderIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Renderer::initPostProcessQuad() {
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);

    glBindVertexArray(quadVAO);

    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void Renderer::renderWorld(const Camera& camera, float aspectRatio, float deltaTime, float currentFrame) {
    glEnable(GL_DEPTH_TEST);

    GLFWwindow* contextWindow = glfwGetCurrentContext();
    int width = 0, height = 0;
    if (contextWindow) {
        glfwGetFramebufferSize(contextWindow, &width, &height);
    }
    if (width <= 0) width = 1280;
    if (height <= 0) height = 720;
    updateFBO(width, height);

    // Check camera eye block for liquid effects
    uint16_t eyeBlock = camera.getEyeBlock(&world);
    int effectType = 0;
    const BlockDB::BlockInfo* eyeBlockInfo = BlockDB::getBlockInfo(eyeBlock);
    if (eyeBlockInfo && eyeBlockInfo->liquid) {
        effectType = eyeBlock;
    }

    fogStartDistance = ((getOptionFloat("render_distance", 7) + 1) * 16) - 20;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glPolygonMode(GL_FRONT_AND_BACK, getWireframeEnabled() ? GL_LINE : GL_FILL);
    glClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Check if mipmap options changed
    int currentMipmapOption = getOptionInt("use_mipmaps", 1);
    int currentMipmapLevels = getOptionInt("mipmap_levels", 4);
    float currentLodBias = getOptionFloat("lod_bias", -0.2f);
    if (currentMipmapOption != lastMipmapOption || currentMipmapLevels != lastMipmapLevels || currentLodBias != lastLodBias) {
        lastMipmapOption = currentMipmapOption;
        lastMipmapLevels = currentMipmapLevels;
        lastLodBias = currentLodBias;
        reloadTextureAtlases();
    }
    
    int renderDist = getOptionInt("render_distance", 7) + 1; // +1 to account for invisible "mesh helper" chunk
    world.updateChunksAroundPlayer(camera.getPositionDouble(), renderDist);

    GLFWwindow* getCurrentGLFWwindow();
    GLFWwindow* window = getCurrentGLFWwindow();
    float baseFov = getOptionFloat("fov", 70.0f);
    
    float getSpeedMultiplier(GLFWwindow* window);
    bool sprintState = window && getSpeedMultiplier(window) > 2.0f;
    float sprintFov = baseFov + 10.0f;

    bool zoomState = window && getZoomState(window);
    float zoomFov = baseFov * 0.1f;

    float targetFov = baseFov;
    if (zoomState) {
        targetFov = zoomFov;
    } else if (sprintState) {
        targetFov = sprintFov;
    }

    float fovLerpSpeed = 20.0f;
    float lerpFactor = 1.0f - expf(-fovLerpSpeed * deltaTime);
    currentFov = currentFov + (targetFov - currentFov) * lerpFactor;

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(currentFov), aspectRatio, 0.1f, 5000.0f);
    
    Frustum frustum = World::extractFrustumPlanes(projection * view);

    glm::vec3 camPos = camera.getPosition();

    // -------------------------------- Render main --------------------------------

    glUseProgram(shaderProgram);
    glEnable(GL_CULL_FACE);
    
    glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, &projection[0][0]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureAtlas);
    glUniform1i(uAtlasLoc, 0);

    if (uOpaqueFogEnabledLoc != -1) {
        glUniform1i(uOpaqueFogEnabledLoc, fogEnabled ? 1 : 0);
    }
    if (uOpaqueFogDensityLoc != -1) {
        glUniform1f(uOpaqueFogDensityLoc, fogDensity);
    }
    if (uOpaqueFogStartLoc != -1) {
        glUniform1f(uOpaqueFogStartLoc, fogStartDistance);
    }
    if (uOpaqueFogColorLoc != -1) {
        glUniform3fv(uOpaqueFogColorLoc, 1, &fogColor[0]);
    }

    world.render(camera, uModelLoc, frustum);

    // -------------------------------- Render cross --------------------------------

    glUseProgram(crossShaderProgram);
    glDisable(GL_CULL_FACE);

    glUniformMatrix4fv(uCrossViewLoc, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(uCrossProjLoc, 1, GL_FALSE, &projection[0][0]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureAtlas);
    glUniform1i(uCrossAtlasLoc, 0);
    
    if (uCrossFogEnabledLoc != -1) {
        glUniform1i(uCrossFogEnabledLoc, fogEnabled ? 1 : 0);
    }
    if (uCrossFogDensityLoc != -1) {
        glUniform1f(uCrossFogDensityLoc, fogDensity);
    }
    if (uCrossFogStartLoc != -1) {
        glUniform1f(uCrossFogStartLoc, fogStartDistance);
    }
    if (uCrossFogColorLoc != -1) {
        glUniform3fv(uCrossFogColorLoc, 1, &fogColor[0]);
    }

    world.renderCross(camera, uCrossModelLoc, frustum);

    // -------------------------------- Render translucent & liquid --------------------------------

    glEnable(GL_BLEND);
    glEnable(GL_CULL_FACE);

    glUseProgram(translucentShaderProgram);

    glUniformMatrix4fv(uTranslucentViewLoc, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(uTranslucentProjLoc, 1, GL_FALSE, &projection[0][0]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureAtlas);
    glUniform1i(uTranslucentAtlasLoc, 0);
    glUniform1f(uTranslucentTimeLoc, currentFrame);

    if (uTranslucentFogEnabledLoc != -1) {
        glUniform1i(uTranslucentFogEnabledLoc, fogEnabled ? 1 : 0);
    }
    if (uTranslucentFogDensityLoc != -1) {
        glUniform1f(uTranslucentFogDensityLoc, fogDensity);
    }
    if (uTranslucentFogStartLoc != -1) {
        glUniform1f(uTranslucentFogStartLoc, fogStartDistance);
    }
    if (uTranslucentFogColorLoc != -1) {
        glUniform3fv(uTranslucentFogColorLoc, 1, &fogColor[0]);
    }

    world.renderTranslucent(camera, uTranslucentModelLoc, frustum);

    // -------------------- Render selected block border --------------------
    glEnable(GL_DEPTH_TEST);
    renderSelectedBlockBorder(camera, aspectRatio);

    glDisable(GL_BLEND);

    // -------------------- Render post-processed quad --------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glDisable(GL_DEPTH_TEST);
    glUseProgram(postProcessShaderProgram);
    glUniform1i(uPostProcessEffectTypeLoc, effectType);
    glUniform1f(uPostProcessTimeLoc, currentFrame);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fboColorTex);
    glUniform1i(uPostProcessTextureLoc, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fboDepthTex);
    glUniform1i(uPostProcessDepthTextureLoc, 1);

    if (uPostProcessInvProjLoc != -1) {
        glm::mat4 invProj = glm::inverse(projection);
        glUniformMatrix4fv(uPostProcessInvProjLoc, 1, GL_FALSE, glm::value_ptr(invProj));
    }
    if (uPostProcessFogEnabledLoc != -1) {
        glUniform1i(uPostProcessFogEnabledLoc, fogEnabled ? 1 : 0);
    }
    if (uPostProcessNormalFogStartLoc != -1) {
        glUniform1f(uPostProcessNormalFogStartLoc, fogStartDistance);
    }

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderCrosshair(float aspectRatio) {
    if (!hotbarOpen) return;
    glDisable(GL_DEPTH_TEST);
    glUseProgram(crosshairShaderProgram);
    
    if (uCrosshairAspectLoc != -1) {
        glUniform1f(uCrosshairAspectLoc, aspectRatio);
    }
    glBindVertexArray(crosshairVAO);
    glDrawArrays(GL_LINES, 0, 4);
    glBindVertexArray(0);
}


void Renderer::renderSelectedBlockBorder(const Camera& camera, float aspectRatio) {
    if (!hotbarOpen) return;
    RaycastResult hit = raycast(&world, camera.getPositionDouble(), camera.getFront(), 6.0f);
    if (!hit.hit || !hit.hitChunk) return;

    glm::ivec3 worldPos = glm::ivec3(
        hit.hitChunk->chunkX * Chunk::chunkWidth + hit.hitBlockPos.x,
        hit.hitBlockPos.y,
        hit.hitChunk->chunkZ * Chunk::chunkDepth + hit.hitBlockPos.z
    );

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = glm::perspective(
        glm::radians(currentFov),
        aspectRatio,
        0.1f,
        5000.0f
    );

    glUseProgram(borderShaderProgram);

    glDisable(GL_CULL_FACE);
    glLineWidth(4.0f);

    // Avoid z-fighting
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-2.0f, -4.0f);

    glDepthMask(GL_FALSE);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(glm::dvec3(worldPos) - camera.getPositionDouble()));
    model = glm::scale(model, glm::vec3(1.001f));

    glUniformMatrix4fv(uBorderModelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(uBorderViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uBorderProjLoc, 1, GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(borderVAO);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_LINE);

    glLineWidth(2.0f);
    glEnable(GL_CULL_FACE);
    glUseProgram(0);
}

void Renderer::updateFBO(int width, int height) {
    if (fboWidth == width && fboHeight == height && fbo != 0) {
        return;
    }

    fboWidth = width;
    fboHeight = height;

    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &fboColorTex);
        glDeleteTextures(1, &fboDepthTex);
        fbo = 0;
        fboColorTex = 0;
        fboDepthTex = 0;
    }

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &fboColorTex);
    glBindTexture(GL_TEXTURE_2D, fboColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColorTex, 0);

    glGenTextures(1, &fboDepthTex);
    glBindTexture(GL_TEXTURE_2D, fboDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fboDepthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("Renderer::updateFBO: Framebuffer is not complete!");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint Renderer::createShader(const char *source, GLenum shaderType) {
    return ::createShader(source, shaderType);
}

GLuint Renderer::createShaderProgram(const char *vertexSource, const char *fragmentSource) {
    return ::createShaderProgram(vertexSource, fragmentSource);
}

void Renderer::loadTextureAtlas(const std::string& path) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        LOG_ERROR("Renderer: Failed to load texture atlas: ", path);
        return;
    }

    auto bleedTransparent = [&](unsigned char* pixels, int w, int h) {
        std::vector<unsigned char> copy(w * h * 4);
        memcpy(copy.data(), pixels, w * h * 4);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int i = (y * w + x) * 4;
                unsigned char a = copy[i + 3];
                if (a == 0) {
                    int rsum = 0, gsum = 0, bsum = 0, count = 0;
                    for (int oy = -1; oy <= 1; ++oy) {
                        int ny = y + oy;
                        if (ny < 0 || ny >= h) continue;
                        for (int ox = -1; ox <= 1; ++ox) {
                            int nx = x + ox;
                            if (nx < 0 || nx >= w) continue;
                            int ni = (ny * w + nx) * 4;
                            if (copy[ni + 3] > 0) {
                                rsum += copy[ni + 0];
                                gsum += copy[ni + 1];
                                bsum += copy[ni + 2];
                                ++count;
                            }
                        }
                    }
                    if (count > 0) {
                        pixels[i + 0] = static_cast<unsigned char>(rsum / count);
                        pixels[i + 1] = static_cast<unsigned char>(gsum / count);
                        pixels[i + 2] = static_cast<unsigned char>(bsum / count);
                        // leave alpha at 0
                    }
                }
            }
        }
    };

    int tile_w = width / 16;
    int tile_h = height / 16;

    glGenTextures(1, &textureAtlas);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureAtlas);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, tile_w, tile_h, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    std::vector<unsigned char> tileData(tile_w * tile_h * 4);
    for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 16; ++col) {
            for (int y = 0; y < tile_h; ++y) {
                int srcY = row * tile_h + y;
                int srcX = col * tile_w;
                memcpy(&tileData[y * tile_w * 4], &data[(srcY * width + srcX) * 4], tile_w * 4);
            }
            bleedTransparent(tileData.data(), tile_w, tile_h);
            int layer = row * 16 + col;
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, tile_w, tile_h, 1, GL_RGBA, GL_UNSIGNED_BYTE, tileData.data());
        }
    }

    int maxMipLevel = getOptionInt("mipmap_levels", 4);
    float lodBias = getOptionFloat("lod_bias", -0.2f);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, maxMipLevel);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_LOD_BIAS, lodBias);
    
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    int mipmapOption = getOptionInt("use_mipmaps", 1);
    if (mipmapOption) {
        if (mipmapOption == 1) {
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); // Nearest
        } else if (mipmapOption == 2) {
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST); // Bilinear
        } else if (mipmapOption == 3) {
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // Trilinear
        }
    } else {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
}

void Renderer::loadTextureAtlas2D(const std::string& path) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        LOG_ERROR("Renderer: Failed to load texture atlas 2D: ", path);
        return;
    }

    glGenTextures(1, &textureAtlas2D);
    glBindTexture(GL_TEXTURE_2D, textureAtlas2D);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
}

void Renderer::loadTextureUIAtlas(const std::string& path) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        LOG_ERROR("Renderer: Failed to load texture UI atlas: ", path);
        return;
    }

    glGenTextures(1, &uiAtlas);
    glBindTexture(GL_TEXTURE_2D, uiAtlas);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
}

void Renderer::reloadTextureAtlases() {
    if (textureAtlas != 0) {
        glDeleteTextures(1, &textureAtlas);
        textureAtlas = 0;
    }
    if (textureAtlas2D != 0) {
        glDeleteTextures(1, &textureAtlas2D);
        textureAtlas2D = 0;
    }

    loadTextureAtlas("textures/blocks.png");
    loadTextureAtlas2D("textures/blocks.png");
    LOG_INFO("Renderer: Texture atlases reloaded");
}
