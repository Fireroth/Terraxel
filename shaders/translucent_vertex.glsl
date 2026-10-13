#version 330 core

layout (location = 0) in uvec2 aPackedData;

out vec3 TexCoord;
out float FaceID;
out vec3 WorldPos;
out float AO;
out vec2 Light;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;

float pi = 3.1415926535;

void main() {
    uint d0 = aPackedData.x;
    uint d1 = aPackedData.y;

    float posX = float(d0 & 0x1FFu) / 16.0;
    float posY = float((d0 >> 9u) & 0x3FFFu) / 16.0;
    float posZ = float((d0 >> 23u) & 0x1FFu) / 16.0;
    vec3 aPos = vec3(posX, posY, posZ);

    float u = float(d1 & 0x1Fu) / 16.0;
    float v = float((d1 >> 5u) & 0x1Fu) / 16.0;
    float layer = float((d1 >> 10u) & 0xFFu);

    float faceID = float((d1 >> 18u) & 0x7u);
    float ao = float((d1 >> 21u) & 0x3u);
    float skyLight = float((d1 >> 23u) & 0xFu);
    float blockLight = float((d1 >> 27u) & 0xFu);
    uint isTop = (d1 >> 31u) & 0x1u;

    vec3 animatedPos = aPos;
    if (isTop > 0u) {
        animatedPos.y -= 0.18;
        animatedPos.y += (sin(aPos.x * pi / 2.0 + time) + sin(aPos.z * pi / 2.0 + time * 1.5)) * 0.04;
    }

    vec4 worldPosition = model * vec4(animatedPos, 1.0);
    gl_Position = projection * view * worldPosition;
    TexCoord = vec3(u, v, layer);
    FaceID = faceID;
    WorldPos = worldPosition.xyz;
    AO = ao;
    Light = vec2(skyLight, blockLight);
}