#pragma once

#include <string>
#include "../world/world.hpp"
#include "sky.hpp"

class Renderer {
public:
    GLint uModelLoc = -1, uViewLoc = -1, uProjLoc = -1, uAtlasLoc = -1, uCrosshairAspectLoc = -1;
    GLint uCrossModelLoc = -1, uCrossViewLoc = -1, uCrossProjLoc = -1, uCrossAtlasLoc = -1;
    GLint uTranslucentModelLoc = -1, uTranslucentViewLoc = -1, uTranslucentProjLoc = -1, uTranslucentAtlasLoc = -1;
    GLint uBorderModelLoc = -1, uBorderViewLoc = -1, uBorderProjLoc = -1;
    GLint uTranslucentTimeLoc = -1;
    GLint uOpaqueFogEnabledLoc = -1, uOpaqueFogDensityLoc = -1, uOpaqueFogStartLoc = -1, uOpaqueFogColorLoc = -1;
    GLint uCrossFogEnabledLoc = -1, uCrossFogDensityLoc = -1, uCrossFogStartLoc = -1, uCrossFogColorLoc = -1;
    GLint uTranslucentFogEnabledLoc = -1, uTranslucentFogDensityLoc = -1, uTranslucentFogStartLoc = -1, uTranslucentFogColorLoc = -1;
    GLint uOpaqueLightingEnabledLoc = -1, uCrossLightingEnabledLoc = -1, uTranslucentLightingEnabledLoc = -1;
    Renderer();
    ~Renderer();

    void init();
    void renderWorld(const class Camera& camera, float aspectRatio, float deltaTime, float currentFrame);
    void renderCrosshair(float aspectRatio);
    void renderSelectedBlockBorder(const class Camera& camera, float aspectRatio);

    World world;
    float currentFov = 70.0f;
    bool fogEnabled = true;
    bool vignetteEnabled = true;
    int cloudsMode = 2; // 0 = OFF, 1 = 2D, 2 = 3D
    int cloudRenderDistance = 50;
    bool lightingEnabled = true;
    float fogDensity = 0.30f;
    float fogStartDistance = 0.0f;
    glm::vec3 fogColor = glm::vec3(0.6f, 1.0f, 1.0f);
    GLuint textureAtlas = 0;
    GLuint uiAtlas = 0;
    GLuint textureAtlas2D = 0;
    Sky sky;

private:
    int lastMipmapOption = -1;
    int lastMipmapLevels = -1;
    float lastLodBias = -10.0f;
    GLuint shaderProgram = 0;
    GLuint crossShaderProgram = 0;
    GLuint translucentShaderProgram = 0;
    GLuint crosshairVAO = 0, crosshairVBO = 0, crosshairShaderProgram = 0;
    GLuint borderVAO = 0, borderVBO = 0, borderEBO = 0, borderShaderProgram = 0;

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
    GLint uPostProcessVignetteEnabledLoc = -1;

    // Sky & Cloud Shader
    GLuint skyShaderProgram = 0;
    GLint uSkyRenderModeLoc = -1;
    GLint uSkyProjLoc = -1;
    GLint uSkyViewLoc = -1;
    GLint uSkyInvProjLoc = -1;
    GLint uSkyInvViewLoc = -1;
    GLint uSkyCamPosLoc = -1;
    GLint uSkyColorLoc = -1;
    GLint uSkyHorizonColorLoc = -1;
    GLint uSkyCloudNoiseTexLoc = -1;
    GLint uSkyCloudColorLoc = -1;
    GLint uSkyCloudHeightLoc = -1;
    GLint uSkyCloudOriginFracLoc = -1;
    GLint uSkyCloudBaseUVLoc = -1;
    GLint uSkyCloudScaleLoc = -1;
    GLint uSkyCloudThresholdLoc = -1;
    GLint uSkyCloudPixelSizeLoc = -1;
    GLint uSkyCloudThicknessLoc = -1;
    GLint uSkyCloudOffsetLoc = -1;
    GLint uSkyMaxCloudDistLoc = -1;

    void updateFBO(int width, int height);

    static GLuint loadTexture2D(const std::string& path, bool generateMipmaps = false);
    void loadTextureAtlas(const std::string& path);
    void reloadTextureAtlases();
    void initCrosshair();
    void initBorderMesh();
    void initPostProcessQuad();
};
