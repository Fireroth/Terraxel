#pragma once

#include <string>
#include "../world/world.hpp"

class Renderer {
public:
    GLint uModelLoc, uViewLoc, uProjLoc, uAtlasLoc, uCrosshairAspectLoc, uFogDensityLoc, uFogStartLoc, uFogColorLoc, uCamPosLoc;
    GLint uCrossModelLoc, uCrossViewLoc, uCrossProjLoc, uCrossAtlasLoc;
    GLint uLiquidModelLoc, uLiquidViewLoc, uLiquidProjLoc, uLiquidAtlasLoc;
    GLint uBorderModelLoc, uBorderViewLoc, uBorderProjLoc;
    GLint uLiquidTimeLoc, uCrossTimeLoc, uTimeLoc;
    GLint uLiquidFogDensityLoc, uLiquidFogStartLoc, uLiquidFogColorLoc, uLiquidCamPosLoc;
    GLint uCrossFogDensityLoc, uCrossFogStartLoc, uCrossFogColorLoc, uCrossCamPosLoc;
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

private:
    GLuint shaderProgram;
    GLuint crossShaderProgram;
    GLuint liquidShaderProgram;
    GLuint crosshairVAO, crosshairVBO, crosshairShaderProgram;
    GLuint borderVAO, borderVBO, borderShaderProgram;
    GLuint createShader(const char* source, GLenum shaderType);
    GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource);
    void loadTextureAtlas(const std::string& path);
    void loadTextureUIAtlas(const std::string& path);
    void initCrosshair();
    void initBorderMesh();
};
