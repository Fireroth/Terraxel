#version 330 core

in vec3 TexCoord;
in vec3 WorldPos;
out vec4 FragColor;

uniform sampler2DArray atlas;

uniform bool fogEnabled;
uniform float fogDensity;
uniform float fogStartDistance;
uniform vec3 fogColor;

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

    vec3 finalColor = applyFog(texColor.rgb);

    FragColor = vec4(finalColor, texColor.a);
}