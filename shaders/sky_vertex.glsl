#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in float aBrightness;

uniform int renderMode; // 0 = sky background, 1 = 2D clouds, 2 = 3D clouds
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
uniform float cloudHeight;
uniform vec2 cloudOriginFrac;
uniform vec3 cloudOffset;

out vec2 TexCoord;
out vec2 CloudWorldXZ;
out vec2 RelPosXZ;
out float vBrightness;
out float vDistXZ;

void main() {
    if (renderMode == 0) {
        TexCoord = (aPos.xy + 1.0) * 0.5;
        gl_Position = vec4(aPos.x, aPos.y, 1.0, 1.0);
    } else if (renderMode == 1) {
        CloudWorldXZ = aPos.xy + cloudOriginFrac;
        RelPosXZ = aPos.xy;
        float relY = cloudHeight - cameraPos.y;
        vec3 relPos = vec3(aPos.x, relY, aPos.y);
        gl_Position = projection * view * vec4(relPos, 1.0);
    } else {
        vec3 relPos = aPos + cloudOffset;
        gl_Position = projection * view * vec4(relPos, 1.0);
        vBrightness = aBrightness;
        vDistXZ = length(relPos.xz);
    }
}
