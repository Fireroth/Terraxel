#version 330 core

in vec3 TexCoord;
in vec3 WorldPos;
in vec2 Light;
out vec4 FragColor;

uniform sampler2DArray atlas;

uniform bool fogEnabled;
uniform float fogDensity;
uniform float fogStartDistance;
uniform vec3 fogColor;
uniform bool lightingEnabled;

vec3 applyFog(vec3 color) {
    if (!fogEnabled) return color;
    float distance = length(WorldPos);
    float adjustedDistance = max(0.0, distance - fogStartDistance);
    float fogFactor = exp(-fogDensity * adjustedDistance);
    return mix(fogColor, color, fogFactor);
}

void main() {
    vec4 texColor = texture(atlas, TexCoord);

    if (texColor.a < 0.3)
        discard;

    vec3 light = vec3(1.0);
    if (lightingEnabled) {
        float skyNorm = clamp(Light.x / 15.0, 0.0, 1.0);
        float blockNorm = clamp(Light.y / 15.0, 0.0, 1.0);

        float skyFactor = pow(skyNorm, 1.4);
        float blockFactor = pow(blockNorm, 1.4);

        vec3 skyColor = vec3(1.0, 1.0, 1.0) * skyFactor;
        vec3 blockColor = vec3(1.05, 0.85, 0.60) * blockFactor;
        vec3 totalLight = max(skyColor, blockColor);
        light = max(vec3(0.06), totalLight);
    }

    vec3 finalColor = texColor.rgb * light;
    finalColor = applyFog(finalColor);

    FragColor = vec4(finalColor, texColor.a);
}