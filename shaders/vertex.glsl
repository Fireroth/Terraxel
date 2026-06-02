#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aTexCoord;
layout (location = 2) in float aFaceID;
layout (location = 3) in float aAO;

out vec3 TexCoord;
out float FaceID;
out vec3 WorldPos;
out float AO;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPosition = model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPosition;
    TexCoord = aTexCoord;
    FaceID = aFaceID;
    WorldPos = worldPosition.xyz;
    AO = aAO;
}