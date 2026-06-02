#pragma once

#include <string>
#include <glad/glad.h>

std::string loadShaderSource(const char* filepath);
GLuint createShader(const char* source, GLenum shaderType);
GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource);
