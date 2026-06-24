#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
uniform sampler2D screenTexture;
uniform sampler2D depthTexture;
uniform int effectType; // 9 = water, 10 = lava, 69 = warm water
uniform float time;
uniform mat4 invProjection;
uniform bool fogEnabled;
uniform float normalFogStartDistance;

vec4 blur(sampler2D tex, vec2 coords, float radius) {
    vec2 texelSize = 1.0 / vec2(textureSize(tex, 0));
    vec4 sum = vec4(0.0);
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize * radius;
            sum += texture(tex, coords + offset);
        }
    }
    return sum / 9.0;
}

vec3 applyFog(vec3 color, vec2 uv, vec3 fColor, float fDensity, float fStart) {
    float depth = texture(depthTexture, uv).r;
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = invProjection * ndc;
    viewPos /= viewPos.w;
    float distance = length(viewPos.xyz);
    float adjustedDistance = max(0.0, distance - fStart);
    float fogFactor = exp(-fDensity * adjustedDistance);
    return mix(fColor, color, fogFactor);
}

float viewDepth(vec2 uv) {
    float d = texture(depthTexture, uv).r;
    vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 vp  = invProjection * ndc;
    vp /= vp.w;
    return length(vp.xyz);
}

vec3 chromaticAberration(sampler2D tex, vec2 uv, float amount) {
    vec2 dir = (uv - 0.5) * amount;
    float r = texture(tex, uv + dir).r;
    float g = texture(tex, uv).g;
    float b = texture(tex, uv - dir).b;
    return vec3(r, g, b);
}

void main() {
    vec2 uv = TexCoords;
    vec3 activeFogColor;
    float activeFogDensity;
    float activeFogStartDistance;

    //---------------------------------------------------------------------------------
    if (effectType == 9) {
        activeFogColor         = vec3(0.145, 0.231, 0.80);
        activeFogDensity       = 0.07;
        activeFogStartDistance = 0.0;

        // Chromatic aberration and blur
        float dist = viewDepth(uv);
        float caAmount = 0.004 + smoothstep(0.0, 12.0, dist) * 0.006;
        vec3 aberrated = chromaticAberration(screenTexture, uv, caAmount);
        vec4 blurred = blur(screenTexture, uv, 2.0);
        vec3 scene = mix(blurred.rgb, aberrated, 0.6);

        // Depth-based color absorption
        float absorption = clamp(dist * 0.035, 0.0, 0.7);
        scene.r *= exp(-absorption * 2.8);
        scene.g *= exp(-absorption * 0.9);

        // Depth fog
        vec3 fogged = applyFog(scene, uv, activeFogColor, activeFogDensity, activeFogStartDistance);

        // Base water tint
        vec3 finalColor = mix(fogged, activeFogColor, 0.30);

        // Vignette
        vec2 d = abs(TexCoords - 0.5) * 2.0;
        float vignette = 1.0 - dot(d, d) * 0.2;
        vignette = clamp(vignette, 0.0, 1.0);
        finalColor = mix(activeFogColor * 0.5, finalColor, vignette);

        FragColor = vec4(finalColor, 1.0);
    }
    //---------------------------------------------------------------------------------
    else if (effectType == 10) {
        activeFogColor         = vec3(0.85, 0.18, 0.0);
        activeFogDensity       = 0.25;
        activeFogStartDistance = 0.0;

        // Blur and fog
        vec4 color = blur(screenTexture, uv, 2.0);
        vec3 fogged = applyFog(color.rgb, uv, activeFogColor, activeFogDensity, activeFogStartDistance);

        // Pulse effect
        vec3 finalColor = mix(fogged, activeFogColor, 0.65);
        float pulse = sin(time * 2.5) * 0.04 + 0.96;
        finalColor *= pulse;

        // Vignette
        vec2 d = abs(TexCoords - 0.5) * 2.0;
        float vignette = 1.0 - dot(d, d) * 0.4;
        vignette = clamp(vignette, 0.0, 1.0);
        finalColor = mix(activeFogColor * 0.3, finalColor, vignette);

        FragColor = vec4(finalColor, 1.0);
    }
    //---------------------------------------------------------------------------------
    else if (effectType == 69) {
        activeFogColor         = vec3(0.145, 0.525, 0.80);
        activeFogDensity       = 0.07;
        activeFogStartDistance = 0.0;

        // Chromatic aberration and blur
        float dist = viewDepth(uv);
        float caAmount = 0.004 + smoothstep(0.0, 12.0, dist) * 0.006;
        vec3 aberrated = chromaticAberration(screenTexture, uv, caAmount);
        vec4 blurred = blur(screenTexture, uv, 2.0);
        vec3 scene = mix(blurred.rgb, aberrated, 0.6);

        // Depth-based color absorption
        float absorption = clamp(dist * 0.035, 0.0, 0.7);
        scene.r *= exp(-absorption * 2.8);
        scene.g *= exp(-absorption * 0.9);

        // Depth fog
        vec3 fogged = applyFog(scene, uv, activeFogColor, activeFogDensity, activeFogStartDistance);

        // Base water tint
        vec3 finalColor = mix(fogged, activeFogColor, 0.30);

        // Vignette
        vec2 d = abs(TexCoords - 0.5) * 2.0;
        float vignette = 1.0 - dot(d, d) * 0.2;
        vignette = clamp(vignette, 0.0, 1.0);
        finalColor = mix(activeFogColor * 0.5, finalColor, vignette);

        FragColor = vec4(finalColor, 1.0);
    }
    //---------------------------------------------------------------------------------
    else {
        vec4 color = texture(screenTexture, uv);
        FragColor = color;
    }
}
