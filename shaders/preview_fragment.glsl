#version 330 core
in vec2 vUV;
in float vFaceID;

uniform sampler2D atlas;

float faceShade() {
    int face = int(vFaceID + 0.5);
    if (face == 4) return 1.0;
    if (face == 1) return 0.90;
    if (face == 2) return 0.75;
    return 0.85;
}

out vec4 FragColor;
void main() {
    vec4 color = texture(atlas, vUV);
    if (color.a < 0.1) discard;
    FragColor = vec4(color.rgb * faceShade(), color.a);
}