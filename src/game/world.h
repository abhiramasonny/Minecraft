#pragma once

#include <cstdint>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/math.h"

struct TextureAssets;

enum BlockType : uint8_t {
  BlockAir = 0,
  BlockGrass = 1,
  BlockDirt = 2,
  BlockStone = 3,
  BlockWood = 4,
  BlockLeaves = 5,
  BlockPlanks = 6,
  BlockCraftingTable = 7,
  BlockCobblestone = 8,
  BlockGravel = 9,
  BlockSand = 10,
  BlockSandstone = 11,
  BlockStoneBricks = 12,
  BlockBricks = 13,
  BlockGlass = 14,
  BlockFurnace = 15,
  BlockStick = 16,
  BlockWater = 17,
  BlockTorch = 18,
};

struct Vertex {
  int16_t x;
  int16_t y;
  int16_t z;
  int16_t u;
  int16_t v;
  int8_t nx;
  int8_t ny;
  int8_t nz;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct MeshBatch {
  unsigned int textureId;
  int indexOffset;
  int indexCount;
  bool transparent;
  bool animated;
};

struct Chunk {
  int cx;
  int cy;
  int cz;
  std::vector<uint8_t> blocks;
  std::vector<uint8_t> light;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<MeshBatch> batches;
  unsigned int vbo;
  unsigned int ibo;
  bool meshDirty;
  int lodStep;
  int maxHeight;
  uint8_t faceOpenMask;
  uint64_t contentHash;
  uint64_t lastMeshHash;
  bool isEmpty;
  bool isSolid;
  double lastAccessTime;
};

struct ChunkCoord {
  int cx;
  int cy;
  int cz;

  bool operator==(const ChunkCoord& other) const {
    return cx == other.cx && cy == other.cy && cz == other.cz;
  }
};

struct ChunkCoordHash {
  std::size_t operator()(const ChunkCoord& coord) const {
    std::size_t h1 = std::hash<int>{}(coord.cx);
    std::size_t h2 = std::hash<int>{}(coord.cy);
    std::size_t h3 = std::hash<int>{}(coord.cz);
    std::size_t h = h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    h ^= h3 + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct MeshBuildResult {
  ChunkCoord coord;
  int lodStep;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<MeshBatch> batches;
};

struct MeshBuildTask {
  ChunkCoord coord;
  std::future<MeshBuildResult> future;
};

struct ChunkBuildTask {
  ChunkCoord coord;
  std::future<Chunk> future;
};

struct World {
  uint32_t seed;
  int renderDistance;
  std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks;
  std::unordered_set<ChunkCoord, ChunkCoordHash> queuedChunks;
  std::unordered_set<ChunkCoord, ChunkCoordHash> queuedMeshes;
  std::vector<ChunkBuildTask> buildTasks;
  std::vector<MeshBuildTask> meshTasks;
  std::vector<ChunkCoord> visibleChunks;
  std::vector<ChunkCoord> pendingChunks;
  bool enableOcclusionCulling;
  std::unordered_map<int64_t, int> heightmapCache;
  size_t loadedChunkCount;
  size_t maxLoadedChunks;
};

void initWorld(World& world);
void updateWorldChunks(World& world, const TextureAssets& textures, Vec3 playerPosition,
                       Vec3 cameraFront, Vec3 cameraUp, Vec3 cameraRight,
                       float fovDegrees, float aspect, double timeNow);
void rebuildWorldMeshes(World& world, const TextureAssets& textures);
float groundHeightAt(const World& world, float x, float y, float z, bool& valid);
BlockType blockAt(const World& world, int wx, int wy, int wz);
bool setBlockAt(World& world, int wx, int wy, int wz, BlockType type);
void rebuildChunksAround(World& world, const TextureAssets& textures, int wx, int wy, int wz);
bool raycastBlock(const World& world, Vec3 origin, Vec3 direction, float maxDistance,
                  int& hitX, int& hitY, int& hitZ, int& faceX, int& faceY, int& faceZ);
bool hasBlockNear(const World& world, Vec3 position, BlockType type, int radius);
bool isPlaceableBlock(BlockType type);
const Chunk* findChunk(const World& world, int cx, int cy, int cz);
Chunk* findChunkMutable(World& world, int cx, int cy, int cz);
float surfaceHeightAt(const World& world, float x, float z);
