#include <glm/gtc/matrix_access.hpp>
#include <cmath>
#include <deque>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmannJSON/json.hpp>
#include "world.hpp"
#include "../core/options.hpp"
#include "../core/saveManager.hpp"
#include "../core/logger.hpp"

static std::deque<std::pair<int, int>> chunkLoadQueue;
namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string getChunkSavePath(int chunkX, int chunkZ) {
    const WorldInfo* activeWorld = SaveManager::getActiveWorld();
    if (!activeWorld) {
        return "";
    }
    return activeWorld->savePath + "/chunks/" + std::to_string(chunkX) + "_" + std::to_string(chunkZ) + ".json";
}

World::World() {}

World::~World() {
    for (auto& [coord, chunk] : chunks) {
        saveChunkIfModified(chunk);
        delete chunk;
    }
    chunks.clear();
}

void World::reset() {
    for (auto& [coord, chunk] : chunks) {
        saveChunkIfModified(chunk);
        delete chunk;
    }
    chunks.clear();
    clearPendingBlockPlacements();
    lastPlayerChunkX = INT32_MIN;
    lastPlayerChunkZ = INT32_MIN;
}


bool World::loadChunkFromSave(Chunk* chunk) {
    if (!chunk) {
        return false;
    }

    const std::string path = getChunkSavePath(chunk->chunkX, chunk->chunkZ);
    if (path.empty() || !fs::exists(path)) {
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("World: Failed to open chunk save file: ", path);
        LOG_WARN("World: The chunk ", chunk->chunkX, ", ", chunk->chunkZ, " will be regenerated");
        return false;
    }

    try {
        json j;
        file >> j;
        const size_t expected = static_cast<size_t>(Chunk::chunkWidth) * Chunk::chunkHeight * Chunk::chunkDepth;
        std::vector<uint16_t> decodedBlocks;
        decodedBlocks.reserve(expected);

        const std::string encoding = j.value("encoding", "");
        if (encoding != "rlev1") {
            LOG_WARN("World: Chunk save uses unsupported encoding (", encoding, ")");
            LOG_WARN("World: The chunk ", chunk->chunkX, ", ", chunk->chunkZ, " will be regenerated");
            return false;
        }

        const auto& runs = j.at("runs");
        if (!runs.is_array()) {
            LOG_WARN("World: Chunk save is corrupted (invalid runs)");
            LOG_WARN("World: The chunk ", chunk->chunkX, ", ", chunk->chunkZ, " will be regenerated");
            return false;
        }

        for (const auto& run : runs) {
            if (!run.is_array() || run.size() != 2) {
                LOG_WARN("World: Chunk save is corrupted");
                LOG_WARN("World: The chunk ", chunk->chunkX, ", ", chunk->chunkZ, " will be regenerated");
                return false;
            }
            const size_t count = run[0].get<size_t>();
            const uint16_t type = run[1].get<uint16_t>();
            decodedBlocks.insert(decodedBlocks.end(), count, type);
        }

        if (decodedBlocks.size() != expected) {
            if (decodedBlocks.size() > expected) {
                LOG_WARN("World: Chunk save contains excessive data");
            } else if (decodedBlocks.size() < expected) {
                LOG_WARN("World: Chunk save contains insufficient data");
            }
            LOG_WARN("World: The chunk ", chunk->chunkX, ", ", chunk->chunkZ, " will be regenerated");
            return false;
        }

        size_t index = 0;
        for (int y = 0; y < Chunk::chunkHeight; y++) {
            for (int x = 0; x < Chunk::chunkWidth; x++) {
                for (int z = 0; z < Chunk::chunkDepth; z++) {
                    chunk->blocks[x][y][z].type = decodedBlocks[index++];
                }
            }
        }

        chunk->loadedFromSave = true;
        chunk->isModified = false;
        return true;
    } catch (std::exception& e) {
        LOG_ERROR("World: Error loading chunk from save: ", e.what());
        return false;
    }
}

void World::saveChunkIfModified(Chunk* chunk) {
    if (!chunk || !chunk->isModified) {
        return;
    }

    const std::string path = getChunkSavePath(chunk->chunkX, chunk->chunkZ);
    if (path.empty()) {
        return;
    }

    fs::create_directories(fs::path(path).parent_path());

    json j;
    j["chunkX"] = chunk->chunkX;
    j["chunkZ"] = chunk->chunkZ;
    j["encoding"] = "rlev1";
    j["runs"] = json::array();

    bool hasActiveRun = false;
    uint16_t currentType = 0;
    size_t runLength = 0;
    auto flushRun = [&]() {
        if (!hasActiveRun) {
            return;
        }
        j["runs"].push_back(json::array({runLength, currentType}));
    };

    for (int y = 0; y < Chunk::chunkHeight; y++) {
        for (int x = 0; x < Chunk::chunkWidth; x++) {
            for (int z = 0; z < Chunk::chunkDepth; z++) {
                const uint16_t type = chunk->blocks[x][y][z].type;
                if (!hasActiveRun) {
                    hasActiveRun = true;
                    currentType = type;
                    runLength = 1;
                } else if (type == currentType) {
                    runLength++;
                } else {
                    flushRun();
                    currentType = type;
                    runLength = 1;
                }
            }
        }
    }
    flushRun();

    std::ofstream file(path);
    if (!file.is_open()) {
        return;
    }

    file << j.dump();
    chunk->isModified = false;
}

void World::saveAllModifiedChunks() {
    for (auto& [coord, chunk] : chunks) {
        saveChunkIfModified(chunk);
    }
}

glm::dvec3 World::findSpawnPosition() {
    generateChunks(2);

    int spawnX = 0;
    int spawnZ = 0;
    int spawnY = 70; // fallback

    for (int attempt = 0; attempt < 64; attempt++) {
        int chunkX = static_cast<int>(std::floor((float)spawnX / Chunk::chunkWidth));
        int chunkZ = static_cast<int>(std::floor((float)spawnZ / Chunk::chunkDepth));
        Chunk* chunk = getChunk(chunkX, chunkZ);

        if (!chunk) break;

        int localX = ((spawnX % Chunk::chunkWidth) + Chunk::chunkWidth) % Chunk::chunkWidth;
        int localZ = ((spawnZ % Chunk::chunkDepth) + Chunk::chunkDepth) % Chunk::chunkDepth;

        // Scan from top down for first non-air block
        int topY = -1;
        for (int y = Chunk::chunkHeight - 1; y >= 0; y--) {
            uint16_t type = chunk->blocks[localX][y][localZ].type;
            if (type != 0) {
                topY = y;
                break;
            }
        }

        if (topY < 0) break;

        uint16_t topType = chunk->blocks[localX][topY][localZ].type;
        const auto* blockInfo = BlockDB::getBlockInfo(topType);

        if (blockInfo && blockInfo->liquid) {
            spawnX++;
            spawnZ++;
            continue;
        }

        spawnY = topY + 1;
        break;
    }

    return glm::dvec3(spawnX + 0.5, spawnY + 1.6, spawnZ + 0.5);
}

void World::generateChunks(int radius) {
    generateChunks(radius, 0, 0);
}

void World::generateChunks(int radius, int originX, int originZ) {
    // Create chunks
    for (int x = -radius; x <= radius; x++) {
        for (int z = -radius; z <= radius; z++) {
            std::pair<int, int> pos = {originX + x, originZ + z};
            if (chunks.find(pos) == chunks.end()) {
                chunks[pos] = new Chunk(pos.first, pos.second, this);
            }
        }
    }

    // Build meshes
    for (auto& [coord, chunk] : chunks) {
        chunk->buildMesh();
    }
}

void World::updateChunksAroundPlayer(const glm::dvec3& playerPos, int radius, bool force) {
    int playerChunkX = static_cast<int>(std::floor(playerPos.x / Chunk::chunkWidth));
    int playerChunkZ = static_cast<int>(std::floor(playerPos.z / Chunk::chunkDepth));

    // Only update if player moved to a new chunk
    if (playerChunkX != lastPlayerChunkX || playerChunkZ != lastPlayerChunkZ || force) {
        lastPlayerChunkX = playerChunkX;
        lastPlayerChunkZ = playerChunkZ;

        // Unload chunks outside radius
        for (auto iterator = chunks.begin(); iterator != chunks.end();) {
            int chunkOffsetX = iterator->first.first - playerChunkX;
            int chunkOffsetZ = iterator->first.second - playerChunkZ;
            if (std::abs(chunkOffsetX) > radius || std::abs(chunkOffsetZ) > radius) {
                saveChunkIfModified(iterator->second);
                delete iterator->second;
                iterator = chunks.erase(iterator);
            } else {
                iterator++;
            }
        }

        chunkLoadQueue.clear();
        std::vector<std::pair<int, int>> positions;
        for (int x = -radius; x <= radius; x++) {
            for (int z = -radius; z <= radius; z++) {
                int chunkX = playerChunkX + x;
                int chunkZ = playerChunkZ + z;
                std::pair<int, int> pos = {chunkX, chunkZ};
                if (chunks.find(pos) == chunks.end()) {
                    positions.push_back(pos);
                }
            }
        }
        std::sort(positions.begin(), positions.end(),
            [playerChunkX, playerChunkZ](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                int distanceA = (a.first - playerChunkX) * (a.first - playerChunkX) + (a.second - playerChunkZ) * (a.second - playerChunkZ);
                int distanceB = (b.first - playerChunkX) * (b.first - playerChunkX) + (b.second - playerChunkZ) * (b.second - playerChunkZ);
                return distanceA < distanceB;
            }
        );
        for (const auto& pos : positions) {
            chunkLoadQueue.push_back(pos);
        }
    }

    int chunksToLoadPerFrame = getOptionInt("chunks_to_load_per_frame", 1);
    for (int i = 0; i < chunksToLoadPerFrame && !chunkLoadQueue.empty(); i++) {
        auto pos = chunkLoadQueue.front();
        chunkLoadQueue.pop_front();
        Chunk* newChunk = new Chunk(pos.first, pos.second, this);
        chunks[pos] = newChunk;
        newChunk->buildMesh();
        for (int dx = -1; dx <= 1; dx++) {
            for (int dz = -1; dz <= 1; dz++) {
                if (dx == 0 && dz == 0) continue;
                auto neighbor = getChunk(pos.first + dx, pos.second + dz);
                if (neighbor) neighbor->buildMesh();
            }
        }
    }
}

Frustum World::extractFrustumPlanes(const glm::mat4& projView) {
    Frustum frustum;
    frustum.planes[0] = glm::row(projView, 3) + glm::row(projView, 0); // Left
    frustum.planes[1] = glm::row(projView, 3) - glm::row(projView, 0); // Right
    frustum.planes[2] = glm::row(projView, 3) + glm::row(projView, 1); // Bottom
    frustum.planes[3] = glm::row(projView, 3) - glm::row(projView, 1); // Top
    frustum.planes[4] = glm::row(projView, 3) + glm::row(projView, 2); // Near
    frustum.planes[5] = glm::row(projView, 3) - glm::row(projView, 2); // Far
    return frustum;
}

bool World::isChunkInFrustum(int chunkX, int chunkZ, const Frustum& frustum, const glm::dvec3& cameraPos) {
    double minX = static_cast<double>(chunkX * Chunk::chunkWidth) - cameraPos.x;
    double maxX = minX + static_cast<double>(Chunk::chunkWidth);
    double minY = 0.0 - cameraPos.y;
    double maxY = static_cast<double>(Chunk::chunkHeight) - cameraPos.y;
    double minZ = static_cast<double>(chunkZ * Chunk::chunkDepth) - cameraPos.z;
    double maxZ = minZ + static_cast<double>(Chunk::chunkDepth);

    for (int i = 0; i < 6; i++) {
        const glm::vec4& plane = frustum.planes[i];
        int out = 0;
        out += (glm::dot(plane, glm::vec4(minX, minY, minZ, 1.0f)) < 0.0f) ? 1 : 0;
        out += (glm::dot(plane, glm::vec4(maxX, minY, minZ, 1.0f)) < 0.0f) ? 1 : 0;
        out += (glm::dot(plane, glm::vec4(minX, maxY, minZ, 1.0f)) < 0.0f) ? 1 : 0;
        out += (glm::dot(plane, glm::vec4(maxX, maxY, minZ, 1.0f)) < 0.0f) ? 1 : 0;
        out += (glm::dot(plane, glm::vec4(minX, minY, maxZ, 1.0f)) < 0.0f) ? 1 : 0;
        out += (glm::dot(plane, glm::vec4(maxX, minY, maxZ, 1.0f)) < 0.0f) ? 1 : 0;
        out += (glm::dot(plane, glm::vec4(minX, maxY, maxZ, 1.0f)) < 0.0f) ? 1 : 0;
        out += (glm::dot(plane, glm::vec4(maxX, maxY, maxZ, 1.0f)) < 0.0f) ? 1 : 0;
        if (out == 8) return false;
    }
    return true;
}

void World::render(const Camera& camera, GLint uModelLoc, const Frustum& frustum) {
    glm::dvec3 camPos = camera.getPositionDouble();
    for (auto& [coord, chunk] : chunks) {
        if (isChunkInFrustum(coord.first, coord.second, frustum, camPos))
            chunk->render(camera, uModelLoc);
    }
}

void World::renderCross(const Camera& camera, GLint uCrossModelLoc, const Frustum& frustum) {
    glm::dvec3 camPos = camera.getPositionDouble();
    for (auto& [coord, chunk] : chunks) {
        if (isChunkInFrustum(coord.first, coord.second, frustum, camPos))
            chunk->renderCross(camera, uCrossModelLoc);
    }
}

void World::renderTranslucent(const Camera& camera, GLint uModelLoc, const Frustum& frustum) {
    std::vector<std::pair<float, Chunk*>> visible;
    visible.reserve(chunks.size());

    glm::dvec3 camPos = camera.getPositionDouble();
    for (auto& [coord, chunk] : chunks) {
        if (!isChunkInFrustum(coord.first, coord.second, frustum, camPos))
            continue;

        float cx = (coord.first * Chunk::chunkWidth) + (Chunk::chunkWidth * 0.5f);
        float cz = (coord.second * Chunk::chunkDepth) + (Chunk::chunkDepth * 0.5f);
        float dx = static_cast<float>(camPos.x - cx);
        float dy = static_cast<float>(camPos.y);
        float dz = static_cast<float>(camPos.z - cz);
        float dist2 = dx*dx + dy*dy + dz*dz;
        visible.emplace_back(dist2, chunk);
    }

    std::sort(visible.begin(), visible.end(), [](const auto& A, const auto& B) {
        return A.first > B.first;
    });

    for (auto& p : visible) {
        p.second->renderTranslucent(camera, uModelLoc);
    }
}

Chunk* World::getChunk(int x, int z) const {
    auto iterator = chunks.find({x, z});
    if (iterator != chunks.end())
        return iterator->second;
    return nullptr;
}
