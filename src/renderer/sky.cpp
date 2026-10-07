#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <nlohmannJSON/json.hpp>
#include "sky.hpp"
#include "../core/logger.hpp"

namespace fs = std::filesystem;

static BiomeNoise parseNoiseJson(const nlohmann::json& noiseJson) {
    BiomeNoise n;
    n.type = noiseJson.value("type", noiseJson.value("noiseType", std::string("OpenSimplex2")));
    n.frequency = noiseJson.value("frequency", 0.012f);
    n.seedOffset = noiseJson.value("seedOffset", 0);
    n.weight = noiseJson.value("weight", noiseJson.value("scale", 1.0f));
    n.offset = noiseJson.value("offset", 0.0f);
    n.power = noiseJson.value("power", 1.0f);
    n.normalize = noiseJson.value("normalize", true);

    std::string opStr = noiseJson.value("operation", noiseJson.value("op", std::string("add")));
    std::transform(opStr.begin(), opStr.end(), opStr.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (opStr == "subtract") n.operation = BiomeNoiseOp::Subtract;
    else if (opStr == "multiply" || opStr == "mul") n.operation = BiomeNoiseOp::Multiply;
    else if (opStr == "divide" || opStr == "div") n.operation = BiomeNoiseOp::Divide;
    else if (opStr == "min") n.operation = BiomeNoiseOp::Min;
    else if (opStr == "max") n.operation = BiomeNoiseOp::Max;
    else if (opStr == "set") n.operation = BiomeNoiseOp::Set;
    else n.operation = BiomeNoiseOp::Add;

    if (noiseJson.contains("fractal") && noiseJson["fractal"].is_object()) {
        auto& f = noiseJson["fractal"];
        n.fractalType = f.value("type", std::string("None"));
        n.octaves = f.value("octaves", 3);
        n.lacunarity = f.value("lacunarity", 2.0f);
        n.gain = f.value("gain", 0.5f);
    } else {
        n.fractalType = noiseJson.value("fractalType", std::string("None"));
        n.octaves = noiseJson.value("octaves", 3);
        n.lacunarity = noiseJson.value("lacunarity", 2.0f);
        n.gain = noiseJson.value("gain", 0.5f);
    }
    return n;
}

void Sky::init(int seed) {
    currentSeed = seed;

    fs::path skyPath = fs::current_path() / "sky.json";
    if (!fs::exists(skyPath)) {
        skyPath = fs::current_path() / "biomes" / "sky.json";
    }

    if (fs::exists(skyPath)) {
        loadJson(skyPath.string());
    } else {
        LOG_INFO("Sky: 'sky.json' not found at ", skyPath.string(), ", using defaults");
        noises.clear();
        BiomeNoise defaultNoise;
        defaultNoise.type = "OpenSimplex2";
        defaultNoise.frequency = 0.012f;
        noises.push_back(defaultNoise);
    }

    for (auto& n : noises) {
        n.initNoise(currentSeed);
    }

    generateNoiseTexture();
    initMesh();
    LOG_INFO("Sky: Initialized successfully with seed ", currentSeed);
}

void Sky::loadJson(const std::string& path) {
    try {
        std::ifstream in(path);
        if (!in.is_open()) return;

        nlohmann::json j;
        in >> j;

        name = j.value("name", name);
        if (j.contains("skyColor") && j["skyColor"].is_array() && j["skyColor"].size() >= 3)
            skyColor = glm::vec3(j["skyColor"][0], j["skyColor"][1], j["skyColor"][2]);
        if (j.contains("horizonColor") && j["horizonColor"].is_array() && j["horizonColor"].size() >= 3)
            horizonColor = glm::vec3(j["horizonColor"][0], j["horizonColor"][1], j["horizonColor"][2]);
        if (j.contains("fogColor") && j["fogColor"].is_array() && j["fogColor"].size() >= 3)
            fogColor = glm::vec3(j["fogColor"][0], j["fogColor"][1], j["fogColor"][2]);
        else
            fogColor = horizonColor;

        if (j.contains("clouds") && j["clouds"].is_object()) {
            auto& c = j["clouds"];
            cloudHeight = c.value("height", cloudHeight);
            cloudSpeed = c.value("speed", cloudSpeed);
            cloudScale = c.value("scale", cloudScale);
            if (c.contains("worldSize") && c["worldSize"].is_number() && c["worldSize"].get<float>() > 0.0f) {
                cloudScale = 1.0f / c["worldSize"].get<float>();
            }
            cloudThreshold = c.value("threshold", cloudThreshold);
            cloudPixelSize = c.value("pixelSize", cloudPixelSize);
            cloudThickness = c.value("thickness", cloudThickness);
            cloudTextureSize = c.value("textureSize", cloudTextureSize);
            if (cloudTextureSize < 16) cloudTextureSize = 16;
            if (cloudTextureSize > 4096) cloudTextureSize = 4096;
            if (c.contains("color") && c["color"].is_array() && c["color"].size() >= 3)
                cloudColor = glm::vec3(c["color"][0], c["color"][1], c["color"][2]);

            if (c.contains("noises") && c["noises"].is_array()) {
                noises.clear();
                for (auto& nJson : c["noises"]) {
                    noises.push_back(parseNoiseJson(nJson));
                }
            }
        }
        LOG_INFO("Sky: Loaded configuration from ", path, " (", noises.size(), " noise layers)");
    } catch (const std::exception& e) {
        LOG_ERROR("Sky: Error parsing JSON: ", e.what());
    }
}

void Sky::updateSeed(int seed) {
    if (seed == currentSeed && cloudTexture != 0) return;
    currentSeed = seed;
    for (auto& n : noises) {
        n.initNoise(currentSeed);
    }
    generateNoiseTexture();
}

void Sky::generateNoiseTexture() {
    if (cloudTexture != 0) {
        glDeleteTextures(1, &cloudTexture);
        cloudTexture = 0;
    }

    const int N = cloudTextureSize;
    std::vector<uint8_t> data(N * N);

    auto evalNoise = [this](double x, double z) {
        if (noises.empty()) return 0.5f;
        float accum = 0.0f;
        for (size_t i = 0; i < noises.size(); ++i) {
            float val = noises[i].evaluate(x, z);
            if (i == 0) {
                accum = (noises[i].operation == BiomeNoiseOp::Subtract) ? -val : val;
            } else {
                switch (noises[i].operation) {
                    case BiomeNoiseOp::Subtract: accum -= val; break;
                    case BiomeNoiseOp::Multiply: accum *= val; break;
                    case BiomeNoiseOp::Divide: if (std::abs(val) > 1e-6f) accum /= val; break;
                    case BiomeNoiseOp::Min: accum = std::min(accum, val); break;
                    case BiomeNoiseOp::Max: accum = std::max(accum, val); break;
                    case BiomeNoiseOp::Set: accum = val; break;
                    default: accum += val; break;
                }
            }
        }
        return accum;
    };

    for (int z = 0; z < N; ++z) {
        float v = static_cast<float>(z) / N;
        float wz = v * v * (3.0f - 2.0f * v);
        for (int x = 0; x < N; ++x) {
            float u = static_cast<float>(x) / N;
            float wx = u * u * (3.0f - 2.0f * u);

            float n00 = evalNoise(x, z);
            float n10 = evalNoise(x - N, z);
            float n01 = evalNoise(x, z - N);
            float n11 = evalNoise(x - N, z - N);

            float top = n00 * (1.0f - wx) + n10 * wx;
            float bot = n01 * (1.0f - wx) + n11 * wx;
            float val = top * (1.0f - wz) + bot * wz;

            val = std::clamp(val, 0.0f, 1.0f);
            data[z * N + x] = static_cast<uint8_t>(val * 255.0f);
        }
    }

    glGenTextures(1, &cloudTexture);
    glBindTexture(GL_TEXTURE_2D, cloudTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, N, N, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    cloudNoiseData = std::move(data);
    lastCenterCX = -999999;
    lastCenterCZ = -999999;
}

bool Sky::isCloudCell(int cx, int cz) const {
    if (cloudNoiseData.empty() || cloudTextureSize <= 0) return false;
    double pSize = (cloudPixelSize > 0.1f) ? cloudPixelSize : 10.0;
    double scale = (cloudScale > 0.0f) ? cloudScale : 0.0005;
    double u = static_cast<double>(cx) * pSize * scale;
    double v = static_cast<double>(cz) * pSize * scale;
    u = u - std::floor(u);
    v = v - std::floor(v);
    int tx = static_cast<int>(u * cloudTextureSize) % cloudTextureSize;
    int tz = static_cast<int>(v * cloudTextureSize) % cloudTextureSize;
    if (tx < 0) tx += cloudTextureSize;
    if (tz < 0) tz += cloudTextureSize;
    float val = cloudNoiseData[tz * cloudTextureSize + tx] / 255.0f;
    return val >= cloudThreshold;
}

void Sky::compute2DCloudOrigin(const glm::dvec3& camPos, double time, glm::vec2& outOriginFrac, glm::vec2& outBaseUV) const {
    const double scale = (cloudScale > 0.0f) ? cloudScale : 0.0005;
    const double driftSpeed = (cloudScale > 0.0f) ? (cloudSpeed / static_cast<double>(cloudScale)) : 0.0;
    const double cell = (cloudPixelSize > 0.0f) ? cloudPixelSize : 1.0;
    const double ox = camPos.x - time * driftSpeed;
    const double oz = camPos.z - time * driftSpeed * 0.5;
    const double cellX = std::floor(ox / cell);
    const double cellZ = std::floor(oz / cell);

    outOriginFrac = glm::vec2(static_cast<float>(ox - cellX * cell), static_cast<float>(oz - cellZ * cell));

    double u = cellX * cell * scale;
    double v = cellZ * cell * scale;
    outBaseUV = glm::vec2(static_cast<float>(u - std::floor(u)), static_cast<float>(v - std::floor(v)));
}

void Sky::update3DMesh(const glm::dvec3& camPos, double time) {
    if (cloudNoiseData.empty()) {
        generateNoiseTexture();
    }

    const double pSize = (cloudPixelSize > 0.1f) ? cloudPixelSize : 10.0;
    const float thickness = (cloudThickness > 0.1f) ? cloudThickness : 4.0f;
    const double driftSpeed = (cloudScale > 0.0f) ? (cloudSpeed / static_cast<double>(cloudScale)) : 0.0;
    const double driftX = time * driftSpeed;
    const double driftZ = time * driftSpeed * 0.5;
    const double playerGridX = camPos.x - driftX;
    const double playerGridZ = camPos.z - driftZ;
    int centerCX = static_cast<int>(std::floor(playerGridX / pSize));
    int centerCZ = static_cast<int>(std::floor(playerGridZ / pSize));

    int R = static_cast<int>(std::ceil(std::max(static_cast<double>(cloudRenderDistance), 16.0) / pSize)) + 1;
    R = std::clamp(R, 2, 256);

    cloud3DOffset = glm::vec3(
        static_cast<float>(static_cast<double>(centerCX) * pSize + driftX - camPos.x),
        static_cast<float>(static_cast<double>(cloudHeight) - camPos.y),
        static_cast<float>(static_cast<double>(centerCZ) * pSize + driftZ - camPos.z)
    );

    if (cloud3DVAO != 0 && centerCX == lastCenterCX && centerCZ == lastCenterCZ && R == lastCloudRadius) {
        return;
    }

    lastCenterCX = centerCX;
    lastCenterCZ = centerCZ;
    lastCloudRadius = R;

    int W = 2 * (R + 1) + 1;
    std::vector<bool> solid(W * W, false);
    for (int j = 0; j < W; ++j) {
        int cz = centerCZ - (R + 1) + j;
        for (int i = 0; i < W; ++i) {
            int cx = centerCX - (R + 1) + i;
            solid[j * W + i] = isCloudCell(cx, cz);
        }
    }

    std::vector<CloudVertex> verts;
    verts.reserve(W * W * 12);

    for (int dz = -R; dz <= R; ++dz) {
        int j = dz + (R + 1);
        for (int dx = -R; dx <= R; ++dx) {
            int i = dx + (R + 1);
            if (!solid[j * W + i]) continue;

            bool solidNorth = solid[(j - 1) * W + i];
            bool solidSouth = solid[(j + 1) * W + i];
            bool solidWest  = solid[j * W + (i - 1)];
            bool solidEast  = solid[j * W + (i + 1)];

            float x0 = static_cast<float>(dx * pSize);
            float x1 = static_cast<float>((dx + 1) * pSize);
            float y0 = 0.0f;
            float y1 = thickness;
            float z0 = static_cast<float>(dz * pSize);
            float z1 = static_cast<float>((dz + 1) * pSize);

            // Top face (+Y)
            verts.push_back({x0, y1, z0, 1.0f});
            verts.push_back({x0, y1, z1, 1.0f});
            verts.push_back({x1, y1, z1, 1.0f});
            verts.push_back({x0, y1, z0, 1.0f});
            verts.push_back({x1, y1, z1, 1.0f});
            verts.push_back({x1, y1, z0, 1.0f});

            // Bottom face (-Y)
            verts.push_back({x0, y0, z0, 0.7f});
            verts.push_back({x1, y0, z1, 0.7f});
            verts.push_back({x0, y0, z1, 0.7f});
            verts.push_back({x0, y0, z0, 0.7f});
            verts.push_back({x1, y0, z0, 0.7f});
            verts.push_back({x1, y0, z1, 0.7f});

            // North face (-Z)
            if (!solidNorth) {
                verts.push_back({x0, y0, z0, 0.8f});
                verts.push_back({x0, y1, z0, 0.8f});
                verts.push_back({x1, y1, z0, 0.8f});
                verts.push_back({x0, y0, z0, 0.8f});
                verts.push_back({x1, y1, z0, 0.8f});
                verts.push_back({x1, y0, z0, 0.8f});
            }

            // South face (+Z)
            if (!solidSouth) {
                verts.push_back({x0, y0, z1, 0.8f});
                verts.push_back({x1, y1, z1, 0.8f});
                verts.push_back({x0, y1, z1, 0.8f});
                verts.push_back({x0, y0, z1, 0.8f});
                verts.push_back({x1, y0, z1, 0.8f});
                verts.push_back({x1, y1, z1, 0.8f});
            }

            // West face (-X)
            if (!solidWest) {
                verts.push_back({x0, y0, z0, 0.75f});
                verts.push_back({x0, y1, z1, 0.75f});
                verts.push_back({x0, y1, z0, 0.75f});
                verts.push_back({x0, y0, z0, 0.75f});
                verts.push_back({x0, y0, z1, 0.75f});
                verts.push_back({x0, y1, z1, 0.75f});
            }

            // East face (+X)
            if (!solidEast) {
                verts.push_back({x1, y0, z0, 0.75f});
                verts.push_back({x1, y1, z0, 0.75f});
                verts.push_back({x1, y1, z1, 0.75f});
                verts.push_back({x1, y0, z0, 0.75f});
                verts.push_back({x1, y1, z1, 0.75f});
                verts.push_back({x1, y0, z1, 0.75f});
            }
        }
    }

    if (cloud3DVAO == 0) {
        glGenVertexArrays(1, &cloud3DVAO);
        glGenBuffers(1, &cloud3DVBO);
        glBindVertexArray(cloud3DVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cloud3DVBO);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CloudVertex), (void*)offsetof(CloudVertex, x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(CloudVertex), (void*)offsetof(CloudVertex, brightness));
        glBindVertexArray(0);
    }

    cloud3DVertexCount = static_cast<int>(verts.size());
    glBindBuffer(GL_ARRAY_BUFFER, cloud3DVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(CloudVertex), verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Sky::initMesh() {
    if (cloudVAO != 0) return;

    std::vector<float> vertices;
    const int GRID = 16;
    const float SIZE = 4200.0f;
    const float STEP = SIZE / GRID;
    const float HALF = SIZE * 0.5f;

    for (int z = 0; z < GRID; ++z) {
        for (int x = 0; x < GRID; ++x) {
            float x0 = -HALF + x * STEP;
            float x1 = x0 + STEP;
            float z0 = -HALF + z * STEP;
            float z1 = z0 + STEP;

            vertices.push_back(x0); vertices.push_back(z0);
            vertices.push_back(x1); vertices.push_back(z0);
            vertices.push_back(x1); vertices.push_back(z1);

            vertices.push_back(x0); vertices.push_back(z0);
            vertices.push_back(x1); vertices.push_back(z1);
            vertices.push_back(x0); vertices.push_back(z1);
        }
    }

    cloudVertexCount = static_cast<int>(vertices.size() / 2);

    glGenVertexArrays(1, &cloudVAO);
    glGenBuffers(1, &cloudVBO);
    glBindVertexArray(cloudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cloudVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Sky::cleanup() {
    if (cloudTexture != 0) {
        glDeleteTextures(1, &cloudTexture);
        cloudTexture = 0;
    }
    if (cloudVAO != 0) {
        glDeleteVertexArrays(1, &cloudVAO);
        cloudVAO = 0;
    }
    if (cloudVBO != 0) {
        glDeleteBuffers(1, &cloudVBO);
        cloudVBO = 0;
    }
    cloudVertexCount = 0;

    if (cloud3DVAO != 0) {
        glDeleteVertexArrays(1, &cloud3DVAO);
        cloud3DVAO = 0;
    }
    if (cloud3DVBO != 0) {
        glDeleteBuffers(1, &cloud3DVBO);
        cloud3DVBO = 0;
    }
    cloud3DVertexCount = 0;
    lastCenterCX = -999999;
    lastCenterCZ = -999999;
}
