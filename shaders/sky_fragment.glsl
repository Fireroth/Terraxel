#version 330 core

out vec4 FragColor;

in vec2 TexCoord;
in vec2 CloudWorldXZ;
in vec2 RelPosXZ;
in float vBrightness;
in float vDistXZ;

uniform int renderMode; // 0 = sky background, 1 = 2D clouds, 2 = 3D clouds
uniform mat4 invProjection;
uniform mat4 invView;

uniform vec3 skyColor;
uniform vec3 horizonColor;

uniform sampler2D cloudNoiseTexture;
uniform vec3 cloudColor;
uniform float cloudScale;
uniform float cloudThreshold;
uniform float cloudPixelSize;
uniform float maxCloudDist;
uniform vec2 cloudBaseUV;

void main() {
    if (renderMode == 0) {
        vec4 ndc = vec4(TexCoord * 2.0 - 1.0, 1.0, 1.0);
        vec4 viewPos = invProjection * ndc;
        vec3 dir = normalize(mat3(invView) * viewPos.xyz);
        float h = clamp(dir.y * 2.5, 0.0, 1.0);
        FragColor = vec4(mix(horizonColor, skyColor, h), 1.0);
    } else if (renderMode == 1) {
        // 2D clouds
        float maxDist2D = (maxCloudDist > 0.0) ? maxCloudDist : 2000.0;
        float distXZ = length(RelPosXZ);
        if (distXZ >= maxDist2D) {
            discard;
        }
        vec2 localCloud = CloudWorldXZ;
        if (cloudPixelSize > 0.0) {
            localCloud = floor(localCloud / cloudPixelSize) * cloudPixelSize;
        }
        vec2 cloudUV = cloudBaseUV + localCloud * cloudScale;
        float n = texture(cloudNoiseTexture, cloudUV).r;

        if (n < cloudThreshold) {
            discard;
        }

        float edgeFade = clamp((maxDist2D - distXZ) / (maxDist2D * 0.25), 0.0, 1.0);
        FragColor = vec4(cloudColor, edgeFade);
    } else {
        // 3D clouds
        float maxDist = (maxCloudDist > 0.0) ? maxCloudDist : 420.0;
        if (vDistXZ >= maxDist) {
            discard;
        }

        float fadeRange = maxDist * 0.25;
        float edgeFade = clamp((maxDist - vDistXZ) / fadeRange, 0.0, 1.0);
        FragColor = vec4(cloudColor * vBrightness, edgeFade);
    }
}
