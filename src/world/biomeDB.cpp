#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <nlohmannJSON/json.hpp>
#include "biomeDB.hpp"
#include "../core/logger.hpp"
#include "../core/saveManager.hpp"

std::vector<BiomeData> BiomeDB::biomes;
std::unordered_map<std::string, int> BiomeDB::biomeNameToIndex;

static std::string toLowerStr(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return str;
}

void BiomeNoise::initNoise(int worldSeed) {
    noise.SetSeed(worldSeed + seedOffset);
    noise.SetFrequency(frequency);

    std::string lowerType = toLowerStr(type);
    if (lowerType == "opensimplex2") noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    else if (lowerType == "opensimplex2s") noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    else if (lowerType == "cellular") noise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    else if (lowerType == "perlin") noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    else if (lowerType == "valuecubic") noise.SetNoiseType(FastNoiseLite::NoiseType_ValueCubic);
    else if (lowerType == "value") noise.SetNoiseType(FastNoiseLite::NoiseType_Value);
    else noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);

    std::string lowerFractal = toLowerStr(fractalType);
    if (lowerFractal == "fbm") noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    else if (lowerFractal == "ridged") noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    else if (lowerFractal == "pingpong") noise.SetFractalType(FastNoiseLite::FractalType_PingPong);
    else if (lowerFractal == "domainwarpprogressive") noise.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive);
    else if (lowerFractal == "domainwarpindependent") noise.SetFractalType(FastNoiseLite::FractalType_DomainWarpIndependent);
    else noise.SetFractalType(FastNoiseLite::FractalType_None);

    noise.SetFractalOctaves(octaves);
    noise.SetFractalLacunarity(lacunarity);
    noise.SetFractalGain(gain);
    noise.SetFractalWeightedStrength(weightedStrength);
    noise.SetFractalPingPongStrength(pingPongStrength);

    std::string lowerCellDist = toLowerStr(cellularDistanceFunction);
    if (lowerCellDist == "euclidean") noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
    else if (lowerCellDist == "euclideansq") noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_EuclideanSq);
    else if (lowerCellDist == "manhattan") noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Manhattan);
    else if (lowerCellDist == "hybrid") noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Hybrid);

    std::string lowerCellRet = toLowerStr(cellularReturnType);
    if (lowerCellRet == "cellvalue") noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);
    else if (lowerCellRet == "distance") noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
    else if (lowerCellRet == "distance2") noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2);
    else if (lowerCellRet == "distance2add") noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2Add);
    else if (lowerCellRet == "distance2sub") noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2Sub);
    else if (lowerCellRet == "distance2mul") noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2Mul);
    else if (lowerCellRet == "distance2div") noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2Div);

    noise.SetCellularJitter(cellularJitter);

    std::string lowerWarp = toLowerStr(domainWarpType);
    if (lowerWarp == "opensimplex2") noise.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
    else if (lowerWarp == "opensimplex2reduced") noise.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2Reduced);
    else if (lowerWarp == "basicgrid") noise.SetDomainWarpType(FastNoiseLite::DomainWarpType_BasicGrid);

    noise.SetDomainWarpAmp(domainWarpAmp);
}

float BiomeNoise::evaluate(double worldX, double worldZ) const {
    double x = worldX;
    double z = worldZ;

    std::string lowerWarp = toLowerStr(domainWarpType);
    if (lowerWarp != "none" && !lowerWarp.empty()) {
        noise.DomainWarp(x, z);
    }

    float val = noise.GetNoise(x, z);
    if (normalize) {
        val = val * 0.5f + 0.5f;
    }
    if (power != 1.0f) {
        val = std::pow(std::max(0.0f, val), power);
    }
    return val * weight + offset;
}

float BiomeData::evaluateTerrainNoise(double worldX, double worldZ) const {
    if (noises.empty()) return 0.0f;

    float accum = 0.0f;
    for (size_t i = 0; i < noises.size(); ++i) {
        const auto& n = noises[i];
        float val = n.evaluate(worldX, worldZ);
        if (i == 0) {
            if (n.operation == BiomeNoiseOp::Subtract) {
                accum = -val;
            } else {
                accum = val;
            }
        } else {
            switch (n.operation) {
                case BiomeNoiseOp::Add:
                    accum += val;
                    break;
                case BiomeNoiseOp::Subtract:
                    accum -= val;
                    break;
                case BiomeNoiseOp::Multiply:
                    accum *= val;
                    break;
                case BiomeNoiseOp::Divide:
                    if (std::abs(val) > 1e-6f) {
                        accum /= val;
                    }
                    break;
                case BiomeNoiseOp::Min:
                    accum = std::min(accum, val);
                    break;
                case BiomeNoiseOp::Max:
                    accum = std::max(accum, val);
                    break;
                case BiomeNoiseOp::Set:
                    accum = val;
                    break;
            }
        }
    }
    return accum;
}

float BiomeModifier::apply(float height) const {
    switch (type) {
        case BiomeModifierType::Deepen:
            if (height < belowY) {
                height = height - ((belowY - height) * factor);
            }
            break;
        case BiomeModifierType::Clamp:
            if (hasBelowY && height < belowY) height = belowY;
            if (hasAboveY && height > aboveY) height = aboveY;
            break;
        case BiomeModifierType::Add:
            height += value;
            break;
        case BiomeModifierType::Multiply:
            height *= factor;
            break;
        case BiomeModifierType::Power:
            if (height > 0.0f) height = std::pow(height, factor);
            break;
    }
    return height;
}

float BiomeData::applyModifiers(float height) const {
    for (const auto& mod : modifiers) {
        height = mod.apply(height);
    }
    return height;
}

void BiomeDB::init() {
    LOG_INFO("BiomeDB: Initializing...");
    biomes.clear();
    biomeNameToIndex.clear();

    int currentSeed = SaveManager::getActiveSeed();

    namespace fs = std::filesystem;
    fs::path biomesDir = fs::current_path() / "biomes";
    bool hasBiomesDir = fs::exists(biomesDir) && fs::is_directory(biomesDir);

    if (!hasBiomesDir) {
        LOG_ERROR("BiomeDB::init: could not find 'biomes' directory at ", biomesDir.string());
    } else {
        std::vector<fs::path> files;
        for (auto& entry : fs::directory_iterator(biomesDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        for (auto& filePath : files) {
            std::ifstream in(filePath);
            if (!in.is_open()) {
                LOG_WARN("BiomeDB::init: failed to open ", filePath.string());
                continue;
            }

            try {
                nlohmann::json j;
                in >> j;

                BiomeData biome;
                biome.name = j.value("name", "Unknown");
                biome.id = j.value("id", "unknown");
                biome.waterBlock = j.value("waterBlock", 9);
                biome.waterLevel = j.value("waterLevel", 37);

                // Parse terrain params
                if (j.contains("terrain") && j["terrain"].is_object()) {
                    auto& t = j["terrain"];
                    biome.terrain.baseHeight = t.value("baseHeight", 30.0f);
                } else {
                    LOG_WARN("BiomeDB: biome '", biome.id, "' has no terrain block, using defaults");
                }

                // Parse noises block
                const nlohmann::json* noisesArr = nullptr;
                if (j.contains("noises") && j["noises"].is_array()) {
                    noisesArr = &j["noises"];
                } else if (j.contains("terrain") && j["terrain"].is_object() && j["terrain"].contains("noises") && j["terrain"]["noises"].is_array()) {
                    noisesArr = &j["terrain"]["noises"];
                }

                if (noisesArr) {
                    for (auto& noiseJson : *noisesArr) {
                        BiomeNoise n;
                        n.type = noiseJson.value("type", noiseJson.value("noiseType", std::string("OpenSimplex2")));
                        n.frequency = noiseJson.value("frequency", 0.01f);
                        n.seedOffset = noiseJson.value("seedOffset", 0);
                        n.weight = noiseJson.value("weight", noiseJson.value("scale", 1.0f));
                        n.offset = noiseJson.value("offset", 0.0f);
                        n.power = noiseJson.value("power", 1.0f);
                        n.normalize = noiseJson.value("normalize", true);

                        std::string opStr = toLowerStr(noiseJson.value("operation", noiseJson.value("op", std::string("add"))));
                        if (opStr == "add") n.operation = BiomeNoiseOp::Add;
                        else if (opStr == "subtract") n.operation = BiomeNoiseOp::Subtract;
                        else if (opStr == "multiply" || opStr == "mul") n.operation = BiomeNoiseOp::Multiply;
                        else if (opStr == "divide" || opStr == "div") n.operation = BiomeNoiseOp::Divide;
                        else if (opStr == "min") n.operation = BiomeNoiseOp::Min;
                        else if (opStr == "max") n.operation = BiomeNoiseOp::Max;
                        else if (opStr == "set" || opStr == "first") n.operation = BiomeNoiseOp::Set;
                        else n.operation = BiomeNoiseOp::Add;

                        if (noiseJson.contains("fractal") && noiseJson["fractal"].is_object()) {
                            auto& f = noiseJson["fractal"];
                            n.fractalType = f.value("type", std::string("None"));
                            n.octaves = f.value("octaves", 3);
                            n.lacunarity = f.value("lacunarity", 2.0f);
                            n.gain = f.value("gain", 0.5f);
                            n.weightedStrength = f.value("weightedStrength", 0.0f);
                            n.pingPongStrength = f.value("pingPongStrength", 2.0f);
                        } else {
                            n.fractalType = noiseJson.value("fractalType", std::string("None"));
                            n.octaves = noiseJson.value("octaves", noiseJson.value("fractalOctaves", 3));
                            n.lacunarity = noiseJson.value("lacunarity", noiseJson.value("fractalLacunarity", 2.0f));
                            n.gain = noiseJson.value("gain", noiseJson.value("fractalGain", 0.5f));
                            n.weightedStrength = noiseJson.value("weightedStrength", noiseJson.value("fractalWeightedStrength", 0.0f));
                            n.pingPongStrength = noiseJson.value("pingPongStrength", noiseJson.value("fractalPingPongStrength", 2.0f));
                        }

                        if (noiseJson.contains("cellular") && noiseJson["cellular"].is_object()) {
                            auto& c = noiseJson["cellular"];
                            n.cellularDistanceFunction = c.value("distanceFunction", std::string("EuclideanSq"));
                            n.cellularReturnType = c.value("returnType", std::string("Distance"));
                            n.cellularJitter = c.value("jitter", 1.0f);
                        } else {
                            n.cellularDistanceFunction = noiseJson.value("cellularDistanceFunction", std::string("EuclideanSq"));
                            n.cellularReturnType = noiseJson.value("cellularReturnType", std::string("Distance"));
                            n.cellularJitter = noiseJson.value("cellularJitter", 1.0f);
                        }

                        n.domainWarpType = noiseJson.value("domainWarpType", std::string("None"));
                        n.domainWarpAmp = noiseJson.value("domainWarpAmp", 1.0f);

                        n.initNoise(currentSeed);
                        biome.noises.push_back(n);
                    }
                    LOG_DEBUG("BiomeDB: biome '", biome.id, "' parsed ", biome.noises.size(), " noises");
                }

                // Parse modifiers block
                const nlohmann::json* modifiersArr = nullptr;
                if (j.contains("modifiers") && j["modifiers"].is_array()) {
                    modifiersArr = &j["modifiers"];
                } else if (j.contains("terrain") && j["terrain"].is_object() && j["terrain"].contains("modifiers") && j["terrain"]["modifiers"].is_array()) {
                    modifiersArr = &j["terrain"]["modifiers"];
                }

                if (modifiersArr) {
                    for (auto& modJson : *modifiersArr) {
                        BiomeModifier mod;
                        std::string modTypeStr = toLowerStr(modJson.value("type", std::string("deepen")));
                        if (modTypeStr == "deepen") mod.type = BiomeModifierType::Deepen;
                        else if (modTypeStr == "clamp" || modTypeStr == "flatten") mod.type = BiomeModifierType::Clamp;
                        else if (modTypeStr == "add" || modTypeStr == "offset") mod.type = BiomeModifierType::Add;
                        else if (modTypeStr == "multiply" || modTypeStr == "scale") mod.type = BiomeModifierType::Multiply;
                        else if (modTypeStr == "power" || modTypeStr == "pow") {
                            mod.type = BiomeModifierType::Power;
                            mod.factor = modJson.value("exponent", modJson.value("power", modJson.value("factor", 1.0f)));
                        }
                        else mod.type = BiomeModifierType::Deepen;

                        if (modJson.contains("belowY")) {
                            mod.belowY = modJson["belowY"].get<float>();
                            mod.hasBelowY = true;
                        } else if (modJson.contains("minY")) {
                            mod.belowY = modJson["minY"].get<float>();
                            mod.hasBelowY = true;
                        }

                        if (modJson.contains("aboveY")) {
                            mod.aboveY = modJson["aboveY"].get<float>();
                            mod.hasAboveY = true;
                        } else if (modJson.contains("maxY")) {
                            mod.aboveY = modJson["maxY"].get<float>();
                            mod.hasAboveY = true;
                        }

                        mod.factor = modJson.value("factor", modJson.value("scale", 0.0f));
                        mod.value = modJson.value("value", modJson.value("offset", 0.0f));

                        biome.modifiers.push_back(mod);
                    }
                    LOG_DEBUG("BiomeDB: biome '", biome.id, "' parsed ", biome.modifiers.size(), " modifiers");
                }

                // Parse layers
                if (j.contains("layers") && j["layers"].is_array()) {
                    for (auto& layerJson : j["layers"]) {
                        BiomeLayer layer;
                        layer.position = layerJson.value("position", std::string("fill"));
                        layer.block = layerJson.value("block", 3);
                        layer.depth = layerJson.value("depth", 1);

                        if (layerJson.contains("overrides") && layerJson["overrides"].is_array()) {
                            for (auto& ovJson : layerJson["overrides"]) {
                                BiomeLayerOverride ov;
                                if (ovJson.contains("minY")) {
                                    ov.minY = ovJson["minY"].get<int>();
                                } else if (ovJson.contains("belowY")) {
                                    ov.minY = ovJson["belowY"].get<int>();
                                }

                                if (ovJson.contains("maxY")) {
                                    ov.maxY = ovJson["maxY"].get<int>();
                                } else if (ovJson.contains("aboveY")) {
                                    ov.maxY = ovJson["aboveY"].get<int>();
                                }

                                if (ovJson.contains("block")) {
                                    ov.block = ovJson["block"].get<int>();
                                }
                                if (ovJson.contains("depth")) {
                                    ov.depth = ovJson["depth"].get<int>();
                                }
                                layer.overrides.push_back(ov);
                            }
                        } else {
                            // Backward compatibility for legacy aboveY / belowY / fallbackBlock
                            int aboveY = layerJson.value("aboveY", -1);
                            int belowY = layerJson.value("belowY", -1);
                            int fallbackBlock = layerJson.value("fallbackBlock", -1);

                            if (fallbackBlock >= 0) {
                                if (aboveY >= 0) {
                                    BiomeLayerOverride ov;
                                    ov.maxY = aboveY - 1;
                                    ov.block = fallbackBlock;
                                    layer.overrides.push_back(ov);
                                }
                                if (belowY >= 0) {
                                    BiomeLayerOverride ov;
                                    ov.minY = belowY + 1;
                                    ov.block = fallbackBlock;
                                    layer.overrides.push_back(ov);
                                }
                            }
                        }

                        biome.layers.push_back(layer);
                        LOG_TRACE("BiomeDB: biome '", biome.id, "' parsed layer: pos=", layer.position, " block=", layer.block, " depth=", layer.depth, " overrides=", layer.overrides.size());
                    }
                    LOG_DEBUG("BiomeDB: biome '", biome.id, "' parsed ", biome.layers.size(), " layers");
                } else {
                    LOG_WARN("BiomeDB: biome '", biome.id, "' has no layers block, using defaults");
                }

                // Parse features
                if (j.contains("features") && j["features"].is_array()) {
                    for (auto& featureJson : j["features"]) {
                        BiomeFeature feature;
                        feature.type = featureJson.value("type", std::string("block"));
                        feature.structure = featureJson.value("structure", std::string(""));
                        feature.block = featureJson.value("block", 0);
                        feature.threshold = featureJson.value("threshold", 0.99f);
                        if (featureJson.contains("xOffset")) {
                            feature.xOffset = featureJson["xOffset"].get<int>();
                        }
                        if (featureJson.contains("zOffset")) {
                            feature.zOffset = featureJson["zOffset"].get<int>();
                        }
                        if (featureJson.contains("yOffset")) {
                            feature.yOffset = featureJson["yOffset"].get<int>();
                        }
                        feature.allowedBlock = featureJson.value("allowedBlock", 1);
                        feature.seedOffset = featureJson.value("seedOffset", 0);
                        feature.minCount = featureJson.value("minCount", 1);
                        feature.maxCount = featureJson.value("maxCount", 8);
                        feature.yMin = featureJson.value("yMin", 0);
                        feature.yMax = featureJson.value("yMax", 256);
                        feature.spread = featureJson.value("spread", 1.0f);
                        biome.features.push_back(feature);
                        LOG_TRACE("BiomeDB: biome '", biome.id, "' parsed feature: type=", feature.type,
                                  feature.type == "structure" ? ", structure=" + feature.structure : ", block=" + std::to_string(feature.block));
                    }
                    LOG_DEBUG("BiomeDB: biome '", biome.id, "' parsed ", biome.features.size(), " features");
                }

                int index = static_cast<int>(biomes.size());
                biomeNameToIndex[biome.id] = index;
                biomes.push_back(biome);

                LOG_DEBUG("BiomeDB: loaded biome '", biome.name, "' (id: ", biome.id, ")");

            } catch (std::exception& e) {
                LOG_ERROR("BiomeDB: JSON parse error in ", filePath.string(), ": ", e.what());
                continue;
            }
        }
    }

    if (biomes.empty()) {
        LOG_WARN("BiomeDB: no biomes found, registering fallback biome.");
        BiomeData defaultBiome;
        defaultBiome.name = "Fallback Biome";
        defaultBiome.id = "default";
        defaultBiome.waterBlock = 65000;
        defaultBiome.waterLevel = 37;

        defaultBiome.terrain.baseHeight = 30.0f;

        BiomeNoise defaultNoise;
        defaultNoise.type = "OpenSimplex2";
        defaultNoise.frequency = 0.005f;
        defaultNoise.weight = 0.0f;
        defaultNoise.initNoise(currentSeed);
        defaultBiome.noises.push_back(defaultNoise);

        BiomeLayer layer1;
        layer1.block = 65000;
        layer1.depth = 1;
        layer1.position = "top";

        BiomeLayer layer2;
        layer2.block = 65000;
        layer2.depth = 1;
        layer2.position = "fill";

        defaultBiome.layers.push_back(layer1);
        defaultBiome.layers.push_back(layer2);

        int index = static_cast<int>(biomes.size());
        biomeNameToIndex[defaultBiome.id] = index;
        biomes.push_back(defaultBiome);
    }

    LOG_INFO("BiomeDB: loaded ", biomes.size(), " biomes");
}

void BiomeDB::setWorldSeed(int seed) {
    static int lastSeed = -99999999;
    if (seed == lastSeed) return;
    lastSeed = seed;

    for (auto& biome : biomes) {
        for (auto& n : biome.noises) {
            n.initNoise(seed);
        }
    }
}

const BiomeData* BiomeDB::getBiome(int index) {
    if (index < 0 || index >= static_cast<int>(biomes.size())) {
        if (!biomes.empty()) return &biomes[0];
        return nullptr;
    }
    return &biomes[index];
}

int BiomeDB::getBiomeCount() {
    return static_cast<int>(biomes.size());
}

const BiomeData* BiomeDB::getBiomeByName(const std::string& id) {
    auto it = biomeNameToIndex.find(id);
    if (it != biomeNameToIndex.end())
        return &biomes[it->second];
    return nullptr;
}
