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
uniform float timeOfDay;

uniform sampler2D cloudNoiseTexture;
uniform vec3 cloudColor;
uniform float cloudScale;
uniform float cloudThreshold;
uniform float cloudPixelSize;
uniform float maxCloudDist;
uniform vec2 cloudBaseUV;

// Dither to break up color banding
float dither(vec2 screenPos) {
    // https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
    return fract(52.9829189 * fract(dot(screenPos, vec2(0.06711056, 0.00583715))));
}

// Helper for procedural stars
float hash21(vec2 p) {
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

void main() {
    if (renderMode == 0) {
        vec4 ndc = vec4(TexCoord * 2.0 - 1.0, 1.0, 1.0);
        vec4 viewPos = invProjection * ndc;
        vec3 dir = normalize(mat3(invView) * viewPos.xyz);
        float h = clamp(dir.y * 2.5, 0.0, 1.0);
        vec3 color = mix(horizonColor, skyColor, h);
        float angle = (timeOfDay / 24000.0) * 6.28318530718;
        vec3 sunDir = normalize(vec3(-cos(angle), sin(angle), 0.0));
        vec3 moonDir = -sunDir;

        vec3 right = vec3(0.0, 0.0, 1.0);
        vec3 upSun = normalize(cross(sunDir, right));
        vec3 upMoon = normalize(cross(moonDir, right));

        float nightFactor = clamp(-sunDir.y * 2.5, 0.0, 1.0);
        if (nightFactor > 0.0 && dir.y > 0.02) {
            // Rotate celestial sphere
            float cosA = cos(angle);
            float sinA = sin(angle);
            vec3 skyRotDir = vec3(dir.x * cosA + dir.y * sinA, -dir.x * sinA + dir.y * cosA, dir.z);

            // Cube projection
            vec3 absD = abs(skyRotDir);
            vec2 starUV;
            if (absD.x >= absD.y && absD.x >= absD.z) {
                starUV = skyRotDir.yz / absD.x;
            } else if (absD.y >= absD.x && absD.y >= absD.z) {
                starUV = skyRotDir.xz / absD.y;
            } else {
                starUV = skyRotDir.xy / absD.z;
            }

            // Stars
            vec2 starGrid = starUV * 80.0;
            vec2 cellId = floor(starGrid);
            vec2 cellUV = fract(starGrid) - 0.5;

            float rnd = hash21(cellId);
            if (rnd > 0.96) {
                vec2 starOffset = (vec2(hash21(cellId + 1.3), hash21(cellId + 3.7)) - 0.5) * 0.4;
                vec2 localSquare = abs(cellUV - starOffset);
                float halfStarSize = 0.16 + 0.10 * hash21(cellId + 7.1);

                if (localSquare.x <= halfStarSize && localSquare.y <= halfStarSize) {
                    float starEdge = max(localSquare.x, localSquare.y);
                    float starAlpha = 1.0 - smoothstep(halfStarSize * 0.82, halfStarSize, starEdge);
                    float shade = mix(0.2, 1.0, hash21(cellId + 8.9));
                    float horizonFade = smoothstep(0.02, 0.18, dir.y);
                    vec3 starColor = vec3(shade);
                    color += starColor * starAlpha * nightFactor * horizonFade;
                }
            }
        }

        // Sun
        float sunDot = dot(dir, sunDir);
        if (sunDot > 0.0) {
            float sunGlow = pow(sunDot, 16.0) * 0.35 + pow(sunDot, 64.0) * 0.45;
            color += vec3(1.0, 0.85, 0.55) * sunGlow;

            vec2 sunCoord = vec2(dot(dir, right), dot(dir, upSun)) / sunDot;
            float sunHalfSize = 0.075;
            if (abs(sunCoord.x) <= sunHalfSize && abs(sunCoord.y) <= sunHalfSize) {
                float edge = max(abs(sunCoord.x), abs(sunCoord.y));
                float alpha = 1.0 - smoothstep(sunHalfSize * 0.94, sunHalfSize, edge);
                vec3 sunColor = vec3(1.0, 0.96, 0.75) * 1.6;
                color = mix(color, sunColor, alpha);
            }
        }

        // Moon
        float moonDot = dot(dir, moonDir);
        if (moonDot > 0.0) {
            float moonGlow = pow(moonDot, 32.0) * 0.20 + pow(moonDot, 128.0) * 0.30;
            color += vec3(0.65, 0.78, 1.0) * moonGlow;

            vec2 moonCoord = vec2(dot(dir, right), dot(dir, upMoon)) / moonDot;
            float moonHalfSize = 0.055;
            if (abs(moonCoord.x) <= moonHalfSize && abs(moonCoord.y) <= moonHalfSize) {
                float edge = max(abs(moonCoord.x), abs(moonCoord.y));
                float alpha = 1.0 - smoothstep(moonHalfSize * 0.94, moonHalfSize, edge);
                vec3 moonColor = vec3(0.92, 0.95, 1.0) * 1.3;
                color = mix(color, moonColor, alpha);
            }
        }

        float dit = (dither(gl_FragCoord.xy) - 0.5) / 255.0;
        color += dit;

        FragColor = vec4(color, 1.0);
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
