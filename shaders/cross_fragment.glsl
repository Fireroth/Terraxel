#version 330 core

in vec3 TexCoord;
in vec3 WorldPos;
out vec4 FragColor;

uniform sampler2DArray atlas;

void main() {
    vec4 texColor = texture(atlas, TexCoord);

    if (texColor.a < 0.3)
        discard;

    FragColor = vec4(texColor.rgb, texColor.a);
}