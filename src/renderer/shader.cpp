#include <fstream>
#include <sstream>
#include "shader.hpp"
#include "../core/logger.hpp"

std::string loadShaderSource(const char* filepath) {
    LOG_DEBUG("Shader: Loading shader from source: ", filepath);
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("Shader: Failed to open shader file: ", filepath);
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint createShader(const char* source, GLenum shaderType) {
    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOG_ERROR("Shader: Shader Compilation Error: ", infoLog);
    }

    return shader;
}

GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource) {
    GLuint vertexShader = createShader(vertexSource, GL_VERTEX_SHADER);
    GLuint fragmentShader = createShader(fragmentSource, GL_FRAGMENT_SHADER);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        LOG_ERROR("Shader: Program Linking Error: ", infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}
