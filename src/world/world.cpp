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

World::World() {
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads < 1) numThreads = 1;
    else if (numThreads > 1) numThreads -= 1;
    LOG_INFO("World: Initializing ThreadPool with ", numThreads, " worker threads.");
    threadPool = std::make_unique<ThreadPool>(numThreads);
}

World::~World() {
    LOG_INFO("World: Shutting down ThreadPool...");
    threadPool.reset();

    for (auto& [coord, chunk] : chunks) {
        saveChunkIfModified(chunk);
        delete chunk;
    }
    chunks.clear();

    for (Chunk* chunk : pendingDeletion) {
        delete chunk;
    }
    pendingDeletion.clear();
}

void World::reset() {
    LOG_INFO("World: Resetting world and ThreadPool...");
    if (threadPool) {
        threadPool->shutdown();
    }
    threadPool.reset();

    for (auto& [coord, chunk] : chunks) {
        saveChunkIfModified(chunk);
        delete chunk;
    }
    chunks.clear();

    {
        std::lock_guard<std::mutex> lock(deletionMutex);
        for (Chunk* chunk : pendingDeletion) {
            delete chunk;
        }
        pendingDeletion.clear();
    }

    {
        std::lock_guard<std::mutex> lock(uploadMutex);
        chunksToUpload.clear();
    }

    {
        std::lock_guard<std::mutex> lock(loadingMutex);
        loadingChunks.clear();
    }

    chunkLoadQueue.clear();

    clearPendingBlockPlacements();
    lastPlayerChunkX = INT32_MIN;
    lastPlayerChunkZ = INT32_MIN;
    lastRadius = -1;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads < 1) numThreads = 1;
    else if (numThreads > 1) numThreads -= 1;
    LOG_INFO("World: Reinitializing ThreadPool with ", numThreads, " worker threads.");
    threadPool = std::make_unique<ThreadPool>(numThreads);
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
    std::vector<Chunk*> activeChunks;
    {
        std::shared_lock<std::shared_mutex> lock(chunksMutex);
        activeChunks.reserve(chunks.size());
        for (auto& [coord, chunk] : chunks) {
            activeChunks.push_back(chunk);
        }
    }
    for (Chunk* chunk : activeChunks) {
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
    std::vector<Chunk*> activeChunks;
    std::vector<std::pair<int, int>> toCreate;
    {
        std::shared_lock<std::shared_mutex> lock(chunksMutex);
        for (int x = -radius; x <= radius; x++) {
            for (int z = -radius; z <= radius; z++) {
                std::pair<int, int> pos = {originX + x, originZ + z};
                if (chunks.find(pos) == chunks.end()) {
                    toCreate.push_back(pos);
                }
            }
        }
    }

    for (const auto& pos : toCreate) {
        Chunk* newChunk = new Chunk(pos.first, pos.second, this);
        {
            std::unique_lock<std::shared_mutex> lock(chunksMutex);
            chunks[pos] = newChunk;
        }
        newChunk->applyPendingBlockPlacements();
    }

    {
        std::shared_lock<std::shared_mutex> lock(chunksMutex);
        activeChunks.reserve(chunks.size());
        for (auto& [coord, chunk] : chunks) {
            activeChunks.push_back(chunk);
        }
    }

    // Build meshes
    for (Chunk* chunk : activeChunks) {
        chunk->buildMesh();
    }
}

void World::updateChunksAroundPlayer(const glm::dvec3& playerPos, int radius, bool force) {
    uploadPendingChunkMeshes();
    processPendingDeletions();

    int playerChunkX = static_cast<int>(std::floor(playerPos.x / Chunk::chunkWidth));
    int playerChunkZ = static_cast<int>(std::floor(playerPos.z / Chunk::chunkDepth));

    // Only update if player moved to a new chunk or radius changed
    if (playerChunkX != lastPlayerChunkX || playerChunkZ != lastPlayerChunkZ || radius != lastRadius || force) {
        lastPlayerChunkX = playerChunkX;
        lastPlayerChunkZ = playerChunkZ;
        lastRadius = radius;

        // Unload chunks outside radius
        {
            std::unique_lock<std::shared_mutex> lock(chunksMutex);
            for (auto iterator = chunks.begin(); iterator != chunks.end();) {
                int chunkOffsetX = iterator->first.first - playerChunkX;
                int chunkOffsetZ = iterator->first.second - playerChunkZ;
                if (std::abs(chunkOffsetX) > radius || std::abs(chunkOffsetZ) > radius) {
                    saveChunkIfModified(iterator->second);
                    Chunk* chunk = iterator->second;
                    iterator = chunks.erase(iterator);
                    if (chunk->refCount == 0) {
                        delete chunk;
                    } else {
                        std::lock_guard<std::mutex> delLock(deletionMutex);
                        pendingDeletion.push_back(chunk);
                    }
                } else {
                    iterator++;
                }
            }
        }

        chunkLoadQueue.clear();
        std::vector<std::pair<int, int>> positions;
        for (int x = -radius; x <= radius; x++) {
            for (int z = -radius; z <= radius; z++) {
                int chunkX = playerChunkX + x;
                int chunkZ = playerChunkZ + z;
                std::pair<int, int> pos = {chunkX, chunkZ};
                
                bool chunkExists = false;
                {
                    std::shared_lock<std::shared_mutex> lock(chunksMutex);
                    chunkExists = (chunks.find(pos) != chunks.end());
                }
                
                bool isLoading = false;
                {
                    std::lock_guard<std::mutex> lock(loadingMutex);
                    isLoading = (loadingChunks.find(pos) != loadingChunks.end());
                }

                if (!chunkExists && !isLoading) {
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
        loadChunkAsync(pos.first, pos.second);
    }
}

void World::loadChunkAsync(int x, int z) {
    {
        std::lock_guard<std::mutex> lock(loadingMutex);
        if (loadingChunks.find({x, z}) != loadingChunks.end()) {
            return;
        }
        loadingChunks.insert({x, z});
    }

    try {
        threadPool->enqueue([this, x, z]() {
            Chunk* newChunk = new Chunk(x, z, this);

            newChunk->refCount++;

            // Check if chunk is still within the active player chunk radius
            int playerX = lastPlayerChunkX;
            int playerZ = lastPlayerChunkZ;
            int radius = lastRadius;
            if (playerX != INT32_MIN && playerZ != INT32_MIN && radius != -1) {
                int chunkOffsetX = x - playerX;
                int chunkOffsetZ = z - playerZ;
                if (std::abs(chunkOffsetX) > radius || std::abs(chunkOffsetZ) > radius) {
                    delete newChunk;
                    {
                        std::lock_guard<std::mutex> lock(loadingMutex);
                        loadingChunks.erase({x, z});
                    }
                    return;
                }
            }

            {
                std::unique_lock<std::shared_mutex> lock(chunksMutex);
                chunks[{x, z}] = newChunk;
            }

            newChunk->applyPendingBlockPlacements();

            {
                std::lock_guard<std::mutex> lock(loadingMutex);
                loadingChunks.erase({x, z});
            }

            queueMeshComputation(x, z);
            queueMeshComputation(x + 1, z);
            queueMeshComputation(x - 1, z);
            queueMeshComputation(x, z + 1);
            queueMeshComputation(x, z - 1);

            newChunk->refCount--;
        });
    } catch (const std::runtime_error&) {
        std::lock_guard<std::mutex> lock(loadingMutex);
        loadingChunks.erase({x, z});
    }
}

void World::queueMeshComputation(int x, int z) {
    std::shared_lock<std::shared_mutex> lock(chunksMutex);
    auto it = chunks.find({x, z});
    if (it != chunks.end()) {
        Chunk* chunk = it->second;
        bool expected = false;
        if (chunk->isMeshing.compare_exchange_strong(expected, true)) {
            chunk->refCount++;
            try {
                threadPool->enqueue([this, chunk]() {
                    chunk->computeMesh();
                    chunk->isMeshing = false;
                    chunk->refCount--;
                });
            } catch (const std::runtime_error&) {
                chunk->isMeshing = false;
                chunk->refCount--;
            }
        }
    }
}

void World::queueChunkMeshUpload(Chunk* chunk) {
    std::lock_guard<std::mutex> lock(uploadMutex);
    if (std::find(chunksToUpload.begin(), chunksToUpload.end(), chunk) == chunksToUpload.end()) {
        chunk->refCount++;
        chunksToUpload.push_back(chunk);
    }
}

void World::uploadPendingChunkMeshes() {
    std::vector<Chunk*> toUpload;
    {
        std::lock_guard<std::mutex> lock(uploadMutex);
        toUpload = std::move(chunksToUpload);
        chunksToUpload.clear();
    }

    for (Chunk* chunk : toUpload) {
        chunk->uploadMesh();
        chunk->refCount--;
    }
}

void World::processPendingDeletions() {
    std::lock_guard<std::mutex> lock(deletionMutex);
    for (auto it = pendingDeletion.begin(); it != pendingDeletion.end(); ) {
        if ((*it)->refCount == 0) {
            delete *it;
            it = pendingDeletion.erase(it);
        } else {
            it++;
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
    std::vector<Chunk*> activeChunks;
    {
        std::shared_lock<std::shared_mutex> lock(chunksMutex);
        activeChunks.reserve(chunks.size());
        for (auto& [coord, chunk] : chunks) {
            activeChunks.push_back(chunk);
        }
    }

    glm::dvec3 camPos = camera.getPositionDouble();
    for (Chunk* chunk : activeChunks) {
        if (isChunkInFrustum(chunk->chunkX, chunk->chunkZ, frustum, camPos))
            chunk->render(camera, uModelLoc);
    }
}

void World::renderCross(const Camera& camera, GLint uCrossModelLoc, const Frustum& frustum) {
    std::vector<Chunk*> activeChunks;
    {
        std::shared_lock<std::shared_mutex> lock(chunksMutex);
        activeChunks.reserve(chunks.size());
        for (auto& [coord, chunk] : chunks) {
            activeChunks.push_back(chunk);
        }
    }

    glm::dvec3 camPos = camera.getPositionDouble();
    for (Chunk* chunk : activeChunks) {
        if (isChunkInFrustum(chunk->chunkX, chunk->chunkZ, frustum, camPos))
            chunk->renderCross(camera, uCrossModelLoc);
    }
}

void World::renderTranslucent(const Camera& camera, GLint uModelLoc, const Frustum& frustum) {
    std::vector<std::pair<float, Chunk*>> visible;
    {
        std::shared_lock<std::shared_mutex> lock(chunksMutex);
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
    }

    std::sort(visible.begin(), visible.end(), [](const auto& A, const auto& B) {
        return A.first > B.first;
    });

    for (auto& p : visible) {
        p.second->renderTranslucent(camera, uModelLoc);
    }
}

Chunk* World::getChunk(int x, int z) const {
    std::shared_lock<std::shared_mutex> lock(chunksMutex);
    auto iterator = chunks.find({x, z});
    if (iterator != chunks.end())
        return iterator->second;
    return nullptr;
}
