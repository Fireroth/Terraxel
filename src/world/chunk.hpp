#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <glad/glad.h>
#include "blockDB.hpp"
#include "../core/camera.hpp"
#include "world.hpp"
#include "structureDB.hpp"
#include "noise.hpp"

class World;

void clearPendingBlockPlacements();

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
    void render(const Camera& camera, GLint uModelLoc);
    void renderCross(const Camera& camera, GLint uModelLoc);
    void renderTranslucent(const Camera& camera, GLint uModelLoc);
    void placeStructure(const Structure& structure, int baseX, int baseY, int baseZ, bool forced = false);

    Block blocks[chunkWidth][chunkHeight][chunkDepth];
    int chunkX, chunkZ;
    int biomeIndex = 0;
    bool isModified = false;
    bool loadedFromSave = false;

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

    void addPlaneFace(std::vector<float>& vertices, std::vector<unsigned int>& indices, int x, int y, int z, int planeIndex, const BlockDB::BlockInfo* blockInfo, unsigned int& offset);
    void addCuboidFace(std::vector<float>& vertices, std::vector<unsigned int>& indices, int x, int y, int z, int face, size_t cuboidIndex, const BlockDB::BlockInfo* blockInfo, unsigned int& offset, bool useAO);

    bool isBlockVisible(int x, int y, int z, int face, bool fasterTrees, const BlockDB::BlockInfo* thisInfo) const;
    bool isOpaque(int x, int y, int z) const;
    float calculateVertexAO(int x, int y, int z, int face, const glm::vec3& cornerPos, bool useAO, bool isLiquid) const;

    Chunk* neighborCache[3][3] = {{nullptr}};
};
