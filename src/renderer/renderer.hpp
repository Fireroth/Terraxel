#pragma once

#include <string>
#include "../world/world.hpp"

class Renderer {
public:
    GLint uModelLoc, uViewLoc, uProjLoc, uAtlasLoc, uCrosshairAspectLoc;
    GLint uCrossModelLoc, uCrossViewLoc, uCrossProjLoc, uCrossAtlasLoc;
    GLint uTranslucentModelLoc, uTranslucentViewLoc, uTranslucentProjLoc, uTranslucentAtlasLoc;
    GLint uBorderModelLoc, uBorderViewLoc, uBorderProjLoc;
    GLint uTranslucentTimeLoc, uCrossTimeLoc, uTimeLoc;
    Renderer();
    ~Renderer();

    void init();
    void renderWorld(const class Camera& camera, float aspectRatio, float deltaTime, float currentFrame);
    void renderCrosshair(float aspectRatio);
    void renderSelectedBlockBorder(const class Camera& camera, float aspectRatio);

    World world;
    float currentFov;
    bool fogEnabled;
    float fogDensity;
    float fogStartDistance;
    glm::vec3 fogColor;
    GLuint textureAtlas;
    GLuint uiAtlas;
    GLuint textureAtlas2D;

private:
    int lastMipmapOption = -1;
    int lastMipmapLevels = -1;
    float lastLodBias = -10.0f;
    GLuint shaderProgram;
    GLuint crossShaderProgram;
    GLuint translucentShaderProgram;
    GLuint crosshairVAO, crosshairVBO, crosshairShaderProgram;
    GLuint borderVAO, borderVBO, borderShaderProgram;

    // Post-processing FBO & Shaders
    GLuint fbo = 0;
    GLuint fboColorTex = 0;
    GLuint fboDepthTex = 0;
    int fboWidth = 0;
    int fboHeight = 0;
    GLuint quadVAO = 0;
    GLuint quadVBO = 0;
    GLuint postProcessShaderProgram = 0;
    GLint uPostProcessTextureLoc = -1;
    GLint uPostProcessEffectTypeLoc = -1;
    GLint uPostProcessTimeLoc = -1;
    GLint uPostProcessDepthTextureLoc = -1;
    GLint uPostProcessInvProjLoc = -1;
    GLint uPostProcessFogEnabledLoc = -1;
    GLint uPostProcessNormalFogStartLoc = -1;

    void updateFBO(int width, int height);

    GLuint createShader(const char* source, GLenum shaderType);
    GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource);
    void loadTextureAtlas(const std::string& path);
    void loadTextureAtlas2D(const std::string& path);
    void loadTextureUIAtlas(const std::string& path);
    void reloadTextureAtlases();
    void initCrosshair();
    void initBorderMesh();
    void initPostProcessQuad();
};
