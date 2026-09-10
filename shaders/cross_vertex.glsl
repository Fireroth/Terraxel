#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aTexCoord;
layout (location = 2) in vec2 aLight;

out vec3 TexCoord;
out vec3 WorldPos;
out vec2 Light;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPosition = model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPosition;
    TexCoord = aTexCoord;
    WorldPos = worldPosition.xyz;
    Light = aLight;
}