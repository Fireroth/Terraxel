#version 330 core

in vec3 TexCoord;
in float FaceID;
in vec3 WorldPos;
in float AO;
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

    float brightness = 1.0;

    int faceIndex = int(FaceID + 0.5);
    
    switch(faceIndex) {
        case 0: brightness = 0.90; break; // Front
        case 1: brightness = 0.90; break; // Back
        case 2: brightness = 0.75; break; // Left
        case 3: brightness = 0.75; break; // Right
        case 4: brightness = 1.03; break; // Top
        case 5: brightness = 0.60; break; // Bottom
    }

    float aoFactor = 0.45 + 0.55 * (AO / 3.0);
    brightness *= aoFactor;

    vec3 finalColor = texColor.rgb * brightness;
    finalColor = applyFog(finalColor);

    FragColor = vec4(finalColor, texColor.a);
}