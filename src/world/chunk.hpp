#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <set>
#include <glad/glad.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include "blockDB.hpp"
#include "../core/camera.hpp"
#include "structureDB.hpp"
#include "noise.hpp"

class World;

void clearPendingBlockPlacements();

struct ChunkMeshData {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    std::vector<float> crossVertices;
    std::vector<unsigned int> crossIndices;
    std::vector<float> translucentVertices;
    std::vector<unsigned int> translucentIndices;
    std::vector<glm::vec3> translucentFaceCentroids;
    bool hasData = false;
};

class Chunk {
public:
    static const int chunkWidth = 16;
    static const int chunkHeight = 256;
    static const int chunkDepth = 16;

    ChunkNoises noises;

    struct Block {
        uint16_t type;
    };

    Chunk(int x, int z, World* worldPtr);
    ~Chunk();

    void buildMesh();
    void computeMesh();
    void uploadMesh();
    void render(const Camera& camera, GLint uModelLoc);
    void renderCross(const Camera& camera, GLint uModelLoc);
    void renderTranslucent(const Camera& camera, GLint uModelLoc);
    void placeStructure(const Structure& structure, int baseX, int baseY, int baseZ, bool forced = false);
    void applyPendingBlockPlacements();

    Block blocks[chunkWidth][chunkHeight][chunkDepth];
    uint8_t light[chunkWidth][chunkHeight][chunkDepth] = {{{0}}};
    int chunkX, chunkZ;
    int biomeIndex = 0;
    bool isModified = false;
    bool loadedFromSave = false;

    inline uint8_t getSkyLight(int x, int y, int z) const {
        if (x < 0 || x >= chunkWidth || y < 0 || y >= chunkHeight || z < 0 || z >= chunkDepth) return 0;
        return (light[x][y][z] >> 4) & 0x0F;
    }

    inline void setSkyLight(int x, int y, int z, uint8_t val) {
        if (x >= 0 && x < chunkWidth && y >= 0 && y < chunkHeight && z >= 0 && z < chunkDepth) {
            light[x][y][z] = (light[x][y][z] & 0x0F) | ((val & 0x0F) << 4);
        }
    }

    inline uint8_t getBlockLight(int x, int y, int z) const {
        if (x < 0 || x >= chunkWidth || y < 0 || y >= chunkHeight || z < 0 || z >= chunkDepth) return 0;
        return light[x][y][z] & 0x0F;
    }

    inline void setBlockLight(int x, int y, int z, uint8_t val) {
        if (x >= 0 && x < chunkWidth && y >= 0 && y < chunkHeight && z >= 0 && z < chunkDepth) {
            light[x][y][z] = (light[x][y][z] & 0xF0) | (val & 0x0F);
        }
    }

    inline void clearLight() {
        std::memset(light, 0, sizeof(light));
    }

    std::atomic<int> refCount{0};
    std::atomic<bool> isMeshing{false};
    std::atomic<bool> dirtyMesh{false};
    std::atomic<bool> isLightCalculated{false};
    ChunkMeshData pendingMeshData;
    std::mutex meshMutex;
    std::set<Chunk*> modifiedNeighborChunks;

private:
    World* world;

    GLuint VAO, VBO, EBO;
    GLuint crossVAO, crossVBO, crossEBO;
    GLuint translucentVAO, translucentVBO, translucentEBO;
    GLsizei indexCount;
    GLsizei crossIndexCount;
    GLsizei translucentIndexCount;

    std::vector<glm::vec3> translucentFaceCentroids;
    std::vector<unsigned int> translucentIndexDataCPU;

    bool translucentNeedsSort;
    glm::vec3 lastSortCamPosLocal;

    void addPlaneFace(std::vector<float>& vertices, std::vector<unsigned int>& indices, int x, int y, int z, int planeIndex, const BlockDB::BlockInfo* blockInfo, unsigned int& offset, bool useLighting);
    void addCuboidFace(std::vector<float>& vertices, std::vector<unsigned int>& indices, int x, int y, int z, int face, size_t cuboidIndex, const BlockDB::BlockInfo* blockInfo, unsigned int& offset, bool useAO, bool useLighting);

    bool isBlockVisible(int x, int y, int z, int face, bool fasterTrees, const BlockDB::BlockInfo* thisInfo) const;
    bool isOpaque(int x, int y, int z) const;
    bool isLiquidBlock(int nx, int ny, int nz) const;
    float calculateVertexAO(int x, int y, int z, int face, const glm::vec3& cornerPos, bool useAO, bool isLiquid) const;
    glm::vec2 calculateVertexLight(int x, int y, int z, int face, const glm::vec3& cornerPos, bool isLiquid) const;
    glm::vec2 getVoxelLight(int nx, int ny, int nz) const;

    Chunk* neighborCache[3][3] = {{nullptr}};
};
