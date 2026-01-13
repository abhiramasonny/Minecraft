#include "game/world.h"

#include "core/math.h"
#include "core/perlin.h"
#include "render/textures.h"

#include <cmath>
#include <cstdint>
#include <future>
#include <thread>

namespace {
struct Color {
  float r;
  float g;
  float b;
};

struct BlockTextures {
  unsigned int top;
  unsigned int side;
  unsigned int bottom;
  unsigned int front;
};

constexpr int kChunkSize = 16;
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;
constexpr int kChunksY = 12;
constexpr int kWorldY = kChunkSize * kChunksY;
constexpr float kBlockSize = 1.0f;
constexpr int kDefaultRenderDistance = 2;
constexpr int kVerticalRenderDistance = 1;
constexpr int kChunkBuildPerFrame = 1;
constexpr int kMeshBuildPerFrame = 2;

int blockIndex(int x, int y, int z) {
  return x + kChunkSize * (y + kChunkSize * z);
}

int floorDiv(int value, int divisor) {
  int div = value / divisor;
  int mod = value % divisor;
  if (mod != 0 && ((mod < 0) != (divisor < 0))) {
    --div;
  }
  return div;
}

int floorMod(int value, int divisor) {
  int mod = value % divisor;
  if (mod < 0) {
    mod += std::abs(divisor);
  }
  return mod;
}

ChunkCoord chunkCoord(int cx, int cy, int cz) {
  return {cx, cy, cz};
}

const Chunk* findChunkInternal(const World& world, int cx, int cy, int cz) {
  auto it = world.chunks.find(chunkCoord(cx, cy, cz));
  if (it == world.chunks.end()) {
    return nullptr;
  }
  return &it->second;
}

Chunk* findChunkInternal(World& world, int cx, int cy, int cz) {
  auto it = world.chunks.find(chunkCoord(cx, cy, cz));
  if (it == world.chunks.end()) {
    return nullptr;
  }
  return &it->second;
}

BlockType blockAtInternal(const World& world, int wx, int wy, int wz) {
  if (wy < 0 || wy >= kWorldY) {
    return BlockAir;
  }

  int cx = floorDiv(wx, kChunkSize);
  int cy = floorDiv(wy, kChunkSize);
  int cz = floorDiv(wz, kChunkSize);
  const Chunk* chunk = findChunkInternal(world, cx, cy, cz);
  if (!chunk) {
    return BlockAir;
  }

  int lx = floorMod(wx, kChunkSize);
  int ly = floorMod(wy, kChunkSize);
  int lz = floorMod(wz, kChunkSize);
  return static_cast<BlockType>(chunk->blocks[blockIndex(lx, ly, lz)]);
}

void setBlockInternal(Chunk& chunk, int wx, int wy, int wz, BlockType type) {
  int lx = floorMod(wx, kChunkSize);
  int ly = floorMod(wy, kChunkSize);
  int lz = floorMod(wz, kChunkSize);
  chunk.blocks[blockIndex(lx, ly, lz)] = static_cast<uint8_t>(type);
}

Vertex makeVertex(Vec3 pos, float u, float v, Color color) {
  return {pos.x, pos.y, pos.z, u, v, color.r, color.g, color.b};
}

void addQuad(std::vector<Quad>& quads, unsigned int textureId, Color color, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
  Quad quad;
  quad.textureId = textureId;
  quad.v[0] = makeVertex(a, 0.0f, 0.0f, color);
  quad.v[1] = makeVertex(b, 1.0f, 0.0f, color);
  quad.v[2] = makeVertex(c, 1.0f, 1.0f, color);
  quad.v[3] = makeVertex(d, 0.0f, 1.0f, color);
  quads.push_back(quad);
}

BlockTextures texturesFor(BlockType type, const TextureAssets& textures) {
  switch (type) {
    case BlockGrass:
      return {textures.grassTop.id, textures.grassSide.id, textures.dirt.id, textures.grassSide.id};
    case BlockDirt:
      return {textures.dirt.id, textures.dirt.id, textures.dirt.id, textures.dirt.id};
    case BlockStone:
      return {textures.stone.id, textures.stone.id, textures.stone.id, textures.stone.id};
    case BlockWood:
      return {textures.woodTop.id, textures.woodSide.id, textures.woodTop.id, textures.woodSide.id};
    case BlockLeaves:
      return {textures.leaves.id, textures.leaves.id, textures.leaves.id, textures.leaves.id};
    case BlockPlanks:
      return {textures.planks.id, textures.planks.id, textures.planks.id, textures.planks.id};
    case BlockCraftingTable:
      return {textures.craftingTop.id, textures.craftingSide.id, textures.planks.id, textures.craftingFront.id};
    case BlockCobblestone:
      return {textures.cobblestone.id, textures.cobblestone.id, textures.cobblestone.id, textures.cobblestone.id};
    case BlockGravel:
      return {textures.gravel.id, textures.gravel.id, textures.gravel.id, textures.gravel.id};
    case BlockSand:
      return {textures.sand.id, textures.sand.id, textures.sand.id, textures.sand.id};
    case BlockSandstone:
      return {textures.sandstoneTop.id, textures.sandstoneSide.id, textures.sandstoneBottom.id,
              textures.sandstoneSide.id};
    case BlockStoneBricks:
      return {textures.stoneBricks.id, textures.stoneBricks.id, textures.stoneBricks.id, textures.stoneBricks.id};
    case BlockBricks:
      return {textures.bricks.id, textures.bricks.id, textures.bricks.id, textures.bricks.id};
    case BlockGlass:
      return {textures.glass.id, textures.glass.id, textures.glass.id, textures.glass.id};
    case BlockFurnace:
      return {textures.furnaceTop.id, textures.furnaceSide.id, textures.furnaceTop.id, textures.furnaceFront.id};
    case BlockStick:
      return {textures.stick.id, textures.stick.id, textures.stick.id, textures.stick.id};
    default:
      return {0u, 0u, 0u, 0u};
  }
}

Color shadeColor(float shade) {
  shade = clamp01(shade);
  return {shade, shade, shade};
}

Vec3 lerpVec3(Vec3 a, Vec3 b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

Color tintColor(Color base, Vec3 tint) {
  return {base.r * tint.x, base.g * tint.y, base.b * tint.z};
}

float hash01(int x, int z, uint32_t seed) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
  h ^= seed * 1442695041u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float hash01(int x, int y, int z, uint32_t seed) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u
    + static_cast<uint32_t>(y) * 668265263u
    + static_cast<uint32_t>(z) * 2246822519u;
  h ^= seed * 3266489917u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float fade(float t) {
  return t * t * (3.0f - 2.0f * t);
}

float valueNoise3D(float x, float y, float z, uint32_t seed) {
  int x0 = static_cast<int>(std::floor(x));
  int y0 = static_cast<int>(std::floor(y));
  int z0 = static_cast<int>(std::floor(z));
  int x1 = x0 + 1;
  int y1 = y0 + 1;
  int z1 = z0 + 1;

  float fx = x - static_cast<float>(x0);
  float fy = y - static_cast<float>(y0);
  float fz = z - static_cast<float>(z0);

  float u = fade(fx);
  float v = fade(fy);
  float w = fade(fz);

  float c000 = hash01(x0, y0, z0, seed);
  float c100 = hash01(x1, y0, z0, seed);
  float c010 = hash01(x0, y1, z0, seed);
  float c110 = hash01(x1, y1, z0, seed);
  float c001 = hash01(x0, y0, z1, seed);
  float c101 = hash01(x1, y0, z1, seed);
  float c011 = hash01(x0, y1, z1, seed);
  float c111 = hash01(x1, y1, z1, seed);

  float x00 = lerp(c000, c100, u);
  float x10 = lerp(c010, c110, u);
  float x01 = lerp(c001, c101, u);
  float x11 = lerp(c011, c111, u);
  float y0v = lerp(x00, x10, v);
  float y1v = lerp(x01, x11, v);
  return lerp(y0v, y1v, w);
}

float fbmNoise3D(float x, float y, float z, int octaves, float lacunarity, float gain, uint32_t seed) {
  float sum = 0.0f;
  float amp = 0.5f;
  float freq = 1.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * valueNoise3D(x * freq, y * freq, z * freq, seed + static_cast<uint32_t>(i) * 1013u);
    freq *= lacunarity;
    amp *= gain;
  }
  return sum;
}

float terrainHeightAt(int x, int z, uint32_t seed, float& biome) {
  float sx = static_cast<float>(x);
  float sz = static_cast<float>(z);
  float seedOffset = static_cast<float>((seed % 10000u) + 1u) * 0.001f;
  float baseNoise = fbmPerlin((sx + seedOffset) * 0.008f, (sz + seedOffset) * 0.008f, 5, 2.0f, 0.5f);
  float detailNoise = fbmPerlin((sx - seedOffset) * 0.03f, (sz - seedOffset) * 0.03f, 3, 2.0f, 0.5f);
  float ridge = 1.0f - std::fabs(baseNoise * 2.0f - 1.0f);
  biome = fbmPerlin((sx + seedOffset) * 0.0035f, (sz + seedOffset) * 0.0035f, 2, 2.0f, 0.5f);

  float height = 20.0f + baseNoise * 50.0f + ridge * 18.0f + detailNoise * 10.0f;
  return height;
}

Vec3 biomeTintAt(int x, int z, uint32_t seed) {
  float biome = 0.0f;
  terrainHeightAt(x, z, seed, biome);
  Vec3 dry = {0.8f, 0.82f, 0.4f};
  Vec3 lush = {0.2f, 0.85f, 0.25f};
  return lerpVec3(dry, lush, biome);
}

bool shouldCarveCave(int x, int y, int z, int top, uint32_t seed) {
  if (y <= 10 || y >= top - 24) {
    return false;
  }
  if (hash01(x / 24, z / 24, seed) > 0.04f) {
    return false;
  }
  float depth = 1.0f - static_cast<float>(y) / static_cast<float>(kWorldY - 1);
  float caveNoise = fbmNoise3D(static_cast<float>(x) * 0.12f, static_cast<float>(y) * 0.12f,
                               static_cast<float>(z) * 0.12f, 2, 2.8f, 0.38f, seed);
  float threshold = 0.88f - depth * 0.03f;
  return caveNoise > threshold && caveNoise < threshold + 0.02f;
}

bool blockInsideChunk(const Chunk& chunk, int wx, int wy, int wz) {
  int cx = floorDiv(wx, kChunkSize);
  int cy = floorDiv(wy, kChunkSize);
  int cz = floorDiv(wz, kChunkSize);
  return cx == chunk.cx && cy == chunk.cy && cz == chunk.cz;
}

void setBlockInChunk(Chunk& chunk, int wx, int wy, int wz, BlockType type) {
  if (wy < 0 || wy >= kWorldY) {
    return;
  }
  if (!blockInsideChunk(chunk, wx, wy, wz)) {
    return;
  }
  int lx = floorMod(wx, kChunkSize);
  int ly = floorMod(wy, kChunkSize);
  int lz = floorMod(wz, kChunkSize);
  int index = blockIndex(lx, ly, lz);
  BlockType current = static_cast<BlockType>(chunk.blocks[index]);
  if (type == BlockLeaves && current != BlockAir) {
    return;
  }
  if (type == BlockWood && current != BlockAir && current != BlockLeaves) {
    return;
  }
  chunk.blocks[index] = static_cast<uint8_t>(type);
}

void placeTreeInChunk(Chunk& chunk, int x, int z, int baseY, int height) {
  if (baseY + height + 3 >= kWorldY) {
    return;
  }

  for (int y = 1; y <= height; ++y) {
    setBlockInChunk(chunk, x, baseY + y, z, BlockWood);
  }

  int leafStart = baseY + height;
  int leafEnd = baseY + height + 2;
  for (int y = leafStart; y <= leafEnd; ++y) {
    int radius = (y == leafEnd) ? 1 : 2;
    for (int dx = -radius; dx <= radius; ++dx) {
      for (int dz = -radius; dz <= radius; ++dz) {
        if (dx * dx + dz * dz > radius * radius + 1) {
          continue;
        }
        setBlockInChunk(chunk, x + dx, y, z + dz, BlockLeaves);
      }
    }
  }
}

bool shouldPlaceTree(int x, int z, int top, float biome, uint32_t seed) {
  float forestNoise = fbmPerlin(static_cast<float>(x) * 0.006f, static_cast<float>(z) * 0.006f, 3, 2.0f, 0.5f);
  float localNoise = fbmPerlin(static_cast<float>(x) * 0.035f, static_cast<float>(z) * 0.035f, 2, 2.0f, 0.5f);
  float scatter = hash01(x, z, seed);
  bool forest = forestNoise > 0.58f && localNoise > 0.4f && scatter > 0.3f;
  bool lone = forestNoise > 0.45f && localNoise > 0.6f && scatter > 0.82f;
  return top > 4 && top < kWorldY - 6 && biome > 0.25f && (forest || lone);
}

int treeHeightAt(int x, int z, uint32_t seed, bool dense) {
  int height = 3 + static_cast<int>(hash01(x + 17, z + 31, seed) * 4.0f);
  if (dense) {
    height += 1;
  }
  return height;
}

void generateChunk(Chunk& chunk, uint32_t seed) {
  chunk.blocks.assign(kChunkVolume, static_cast<uint8_t>(BlockAir));
  int baseX = chunk.cx * kChunkSize;
  int baseY = chunk.cy * kChunkSize;
  int baseZ = chunk.cz * kChunkSize;

  for (int lx = 0; lx < kChunkSize; ++lx) {
    int wx = baseX + lx;
    for (int lz = 0; lz < kChunkSize; ++lz) {
      int wz = baseZ + lz;
      float biome = 0.0f;
      int top = static_cast<int>(terrainHeightAt(wx, wz, seed, biome));
      if (top < 1) {
        top = 1;
      }
      if (top > kWorldY - 2) {
        top = kWorldY - 2;
      }

      for (int ly = 0; ly < kChunkSize; ++ly) {
        int wy = baseY + ly;
        if (wy > top || wy < 0 || wy >= kWorldY) {
          continue;
        }
        if (shouldCarveCave(wx, wy, wz, top, seed)) {
          continue;
        }
        BlockType type = BlockStone;
        if (wy == top) {
          type = BlockGrass;
        } else if (wy >= top - 3) {
          type = BlockDirt;
        }
        chunk.blocks[blockIndex(lx, ly, lz)] = static_cast<uint8_t>(type);
      }
    }
  }

  int padding = 3;
  int minX = baseX - padding;
  int maxX = baseX + kChunkSize + padding;
  int minZ = baseZ - padding;
  int maxZ = baseZ + kChunkSize + padding;

  for (int x = minX; x <= maxX; ++x) {
    for (int z = minZ; z <= maxZ; ++z) {
      float biome = 0.0f;
      int top = static_cast<int>(terrainHeightAt(x, z, seed, biome));
      if (top < 1 || top >= kWorldY - 2) {
        continue;
      }
      float forestNoise = fbmPerlin(static_cast<float>(x) * 0.006f, static_cast<float>(z) * 0.006f, 3, 2.0f, 0.5f);
      float density = (forestNoise - 0.4f) * 1.4f;
      bool dense = density > 0.3f;
      if (!shouldPlaceTree(x, z, top, biome, seed)) {
        continue;
      }
      int height = treeHeightAt(x, z, seed, dense);
      placeTreeInChunk(chunk, x, z, top, height);
    }
  }
}

void buildChunkMesh(const World& world, const TextureAssets& textures, Chunk& chunk) {
  chunk.quads.clear();

  for (int lx = 0; lx < kChunkSize; ++lx) {
    for (int ly = 0; ly < kChunkSize; ++ly) {
      for (int lz = 0; lz < kChunkSize; ++lz) {
        int wx = chunk.cx * kChunkSize + lx;
        int wy = chunk.cy * kChunkSize + ly;
        int wz = chunk.cz * kChunkSize + lz;

        BlockType type = blockAtInternal(world, wx, wy, wz);
        if (type == BlockAir) {
          continue;
        }

        float x0 = static_cast<float>(wx) * kBlockSize;
        float x1 = x0 + kBlockSize;
        float y0 = wy * kBlockSize;
        float y1 = y0 + kBlockSize;
        float z0 = static_cast<float>(wz) * kBlockSize;
        float z1 = z0 + kBlockSize;

        float baseShade = 0.65f + 0.35f * (static_cast<float>(wy) / (kWorldY - 1));
        Color topColor = shadeColor(baseShade);
        Color sideColor = shadeColor(baseShade * 0.92f);
        Color bottomColor = shadeColor(baseShade * 0.75f);
        BlockTextures blockTextures = texturesFor(type, textures);
        Vec3 biomeTint = biomeTintAt(wx, wz, world.seed);

        if (type == BlockGrass) {
          topColor = tintColor(shadeColor(baseShade * 1.05f), biomeTint);
          sideColor = shadeColor(baseShade * 0.9f);
          bottomColor = shadeColor(baseShade * 0.7f);
        } else if (type == BlockLeaves) {
          Vec3 leafTint = {biomeTint.x * 0.85f, biomeTint.y * 0.9f, biomeTint.z * 0.85f};
          topColor = tintColor(shadeColor(baseShade * 1.0f), leafTint);
          sideColor = tintColor(shadeColor(baseShade * 0.95f), leafTint);
          bottomColor = tintColor(shadeColor(baseShade * 0.9f), leafTint);
        }

        if (blockAtInternal(world, wx, wy + 1, wz) == BlockAir) {
          addQuad(chunk.quads, blockTextures.top, topColor,
                  {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});
        }
        if (blockAtInternal(world, wx, wy - 1, wz) == BlockAir) {
          addQuad(chunk.quads, blockTextures.bottom, bottomColor,
                  {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0});
        }
        if (blockAtInternal(world, wx + 1, wy, wz) == BlockAir) {
          addQuad(chunk.quads, blockTextures.side, sideColor,
                  {x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0});
        }
        if (blockAtInternal(world, wx - 1, wy, wz) == BlockAir) {
          addQuad(chunk.quads, blockTextures.side, sideColor,
                  {x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1});
        }
        if (blockAtInternal(world, wx, wy, wz + 1) == BlockAir) {
          addQuad(chunk.quads, blockTextures.side, sideColor,
                  {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1});
        }
        if (blockAtInternal(world, wx, wy, wz - 1) == BlockAir) {
          addQuad(chunk.quads, blockTextures.front, sideColor,
                  {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0});
        }
      }
    }
  }
}

int worldToBlock(float value) {
  return static_cast<int>(std::floor(value / kBlockSize));
}
} // namespace

void initWorld(World& world) {
  world.seed = 1337u;
  world.renderDistance = kDefaultRenderDistance;
  world.chunks.clear();
  world.queuedChunks.clear();
  world.buildTasks.clear();
  world.visibleChunks.clear();
  world.pendingChunks.clear();
}

const Chunk* findChunk(const World& world, int cx, int cy, int cz) {
  return findChunkInternal(world, cx, cy, cz);
}

Chunk& getOrCreateChunk(World& world, const TextureAssets& textures, int cx, int cy, int cz, bool& created) {
  ChunkCoord key = chunkCoord(cx, cy, cz);
  auto it = world.chunks.find(key);
  if (it != world.chunks.end()) {
    created = false;
    return it->second;
  }

  Chunk chunk = {};
  chunk.cx = cx;
  chunk.cy = cy;
  chunk.cz = cz;
  chunk.blocks.assign(kChunkVolume, static_cast<uint8_t>(BlockAir));
  generateChunk(chunk, world.seed);
  buildChunkMesh(world, textures, chunk);
  auto result = world.chunks.emplace(key, std::move(chunk));
  created = true;
  return result.first->second;
}

Chunk buildChunkData(const ChunkCoord& coord, uint32_t seed) {
  Chunk chunk = {};
  chunk.cx = coord.cx;
  chunk.cy = coord.cy;
  chunk.cz = coord.cz;
  chunk.blocks.assign(kChunkVolume, static_cast<uint8_t>(BlockAir));
  generateChunk(chunk, seed);
  return chunk;
}

bool isFutureReady(const std::future<Chunk>& future) {
  return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void updateWorldChunks(World& world, const TextureAssets& textures, Vec3 playerPosition) {
  int meshBuilt = 0;
  for (auto it = world.buildTasks.begin(); it != world.buildTasks.end(); ) {
    if (!isFutureReady(it->future)) {
      ++it;
      continue;
    }
    if (meshBuilt >= kMeshBuildPerFrame) {
      break;
    }
    Chunk chunk = it->future.get();
    world.queuedChunks.erase(it->coord);
    ChunkCoord key = it->coord;
    auto existing = world.chunks.find(key);
    if (existing == world.chunks.end()) {
      auto result = world.chunks.emplace(key, std::move(chunk));
      buildChunkMesh(world, textures, result.first->second);
      rebuildChunksAround(world, textures, result.first->second.cx * kChunkSize,
                          result.first->second.cy * kChunkSize,
                          result.first->second.cz * kChunkSize);
      meshBuilt++;
    }
    it = world.buildTasks.erase(it);
  }

  int wx = worldToBlock(playerPosition.x);
  int wz = worldToBlock(playerPosition.z);
  int wy = worldToBlock(playerPosition.y);
  int pcx = floorDiv(wx, kChunkSize);
  int pcy = floorDiv(wy, kChunkSize);
  int pcz = floorDiv(wz, kChunkSize);
  int minCy = pcy - kVerticalRenderDistance;
  int maxCy = pcy + kVerticalRenderDistance;
  if (minCy < 0) minCy = 0;
  if (maxCy > kChunksY - 1) maxCy = kChunksY - 1;

  world.visibleChunks.clear();
  world.pendingChunks.clear();
  int render = world.renderDistance;
  for (int radius = 0; radius <= render; ++radius) {
    for (int dz = -radius; dz <= radius; ++dz) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (std::abs(dx) != radius && std::abs(dz) != radius) {
          continue;
        }
        if (dx * dx + dz * dz > render * render) {
          continue;
        }
        int cx = pcx + dx;
        int cz = pcz + dz;
        for (int cy = minCy; cy <= maxCy; ++cy) {
          const Chunk* chunk = findChunkInternal(world, cx, cy, cz);
          if (chunk) {
            world.visibleChunks.push_back(chunkCoord(chunk->cx, chunk->cy, chunk->cz));
          } else {
            world.pendingChunks.push_back(chunkCoord(cx, cy, cz));
          }
        }
      }
    }
  }

  unsigned int workerCount = std::thread::hardware_concurrency();
  if (workerCount < 2) {
    workerCount = 2;
  }
  int maxInFlight = static_cast<int>(workerCount) * 3;
  int available = maxInFlight - static_cast<int>(world.buildTasks.size());
  int budget = std::max(kChunkBuildPerFrame, static_cast<int>(workerCount));
  int toCreate = std::min(available, budget);

  int spawned = 0;
  for (const ChunkCoord& coord : world.pendingChunks) {
    if (spawned >= toCreate) {
      break;
    }
    if (world.queuedChunks.find(coord) != world.queuedChunks.end()) {
      continue;
    }
    if (findChunkInternal(world, coord.cx, coord.cy, coord.cz)) {
      continue;
    }
    world.queuedChunks.insert(coord);
    world.buildTasks.push_back({coord, std::async(std::launch::async, buildChunkData, coord, world.seed)});
    spawned++;
  }

}

void rebuildWorldMeshes(World& world, const TextureAssets& textures) {
  for (auto& entry : world.chunks) {
    buildChunkMesh(world, textures, entry.second);
  }
}

float groundHeightAt(const World& world, float x, float y, float z, bool& valid) {
  int ix = worldToBlock(x);
  int iz = worldToBlock(z);
  int startY = worldToBlock(y);
  if (startY >= kWorldY) {
    startY = kWorldY - 1;
  }
  if (startY < 0) {
    valid = false;
    return 0.0f;
  }

  for (int wy = startY; wy >= 0; --wy) {
    if (blockAtInternal(world, ix, wy, iz) != BlockAir) {
      valid = true;
      return (static_cast<float>(wy) + 1.0f) * kBlockSize;
    }
  }

  valid = false;
  return 0.0f;
}

BlockType blockAt(const World& world, int wx, int wy, int wz) {
  return blockAtInternal(world, wx, wy, wz);
}

bool setBlockAt(World& world, int wx, int wy, int wz, BlockType type) {
  if (wy < 0 || wy >= kWorldY) {
    return false;
  }

  BlockType current = blockAtInternal(world, wx, wy, wz);
  if (current == type) {
    return false;
  }

  int cx = floorDiv(wx, kChunkSize);
  int cy = floorDiv(wy, kChunkSize);
  int cz = floorDiv(wz, kChunkSize);
  Chunk* chunk = findChunkInternal(world, cx, cy, cz);
  if (!chunk) {
    return false;
  }
  setBlockInternal(*chunk, wx, wy, wz, type);
  return true;
}

void rebuildChunksAround(World& world, const TextureAssets& textures, int wx, int wy, int wz) {
  int cx = floorDiv(wx, kChunkSize);
  int cy = floorDiv(wy, kChunkSize);
  int cz = floorDiv(wz, kChunkSize);

  static const int kNeighborOffsets[7][3] = {
    {0, 0, 0},
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
  };

  for (const auto& offset : kNeighborOffsets) {
    int nx = cx + offset[0];
    int ny = cy + offset[1];
    int nz = cz + offset[2];
    if (ny < 0 || ny >= kChunksY) {
      continue;
    }
    Chunk* chunk = findChunkInternal(world, nx, ny, nz);
    if (!chunk) {
      continue;
    }
    buildChunkMesh(world, textures, *chunk);
  }
}

static float intBound(float s, float ds) {
  if (ds > 0.0f) {
    return (std::floor(s + 1.0f) - s) / ds;
  }
  if (ds < 0.0f) {
    return (s - std::floor(s)) / -ds;
  }
  return 1e30f;
}

bool raycastBlock(const World& world, Vec3 origin, Vec3 direction, float maxDistance,
                  int& hitX, int& hitY, int& hitZ, int& faceX, int& faceY, int& faceZ) {
  Vec3 dir = normalize(direction);
  if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f) {
    return false;
  }

  float invBlock = 1.0f / kBlockSize;
  float x = origin.x * invBlock;
  float y = origin.y * invBlock;
  float z = origin.z * invBlock;

  int ix = static_cast<int>(std::floor(x));
  int iy = static_cast<int>(std::floor(y));
  int iz = static_cast<int>(std::floor(z));

  float dx = dir.x * invBlock;
  float dy = dir.y * invBlock;
  float dz = dir.z * invBlock;

  int stepX = dx > 0.0f ? 1 : (dx < 0.0f ? -1 : 0);
  int stepY = dy > 0.0f ? 1 : (dy < 0.0f ? -1 : 0);
  int stepZ = dz > 0.0f ? 1 : (dz < 0.0f ? -1 : 0);

  float tMaxX = intBound(x, dx);
  float tMaxY = intBound(y, dy);
  float tMaxZ = intBound(z, dz);

  float tDeltaX = stepX == 0 ? 1e30f : std::fabs(1.0f / dx);
  float tDeltaY = stepY == 0 ? 1e30f : std::fabs(1.0f / dy);
  float tDeltaZ = stepZ == 0 ? 1e30f : std::fabs(1.0f / dz);

  float t = 0.0f;
  float maxT = maxDistance * invBlock;

  faceX = 0;
  faceY = 0;
  faceZ = 0;

  while (t <= maxT) {
    if (blockAtInternal(world, ix, iy, iz) != BlockAir) {
      hitX = ix;
      hitY = iy;
      hitZ = iz;
      return true;
    }

    if (tMaxX < tMaxY) {
      if (tMaxX < tMaxZ) {
        ix += stepX;
        t = tMaxX;
        tMaxX += tDeltaX;
        faceX = -stepX;
        faceY = 0;
        faceZ = 0;
      } else {
        iz += stepZ;
        t = tMaxZ;
        tMaxZ += tDeltaZ;
        faceX = 0;
        faceY = 0;
        faceZ = -stepZ;
      }
    } else {
      if (tMaxY < tMaxZ) {
        iy += stepY;
        t = tMaxY;
        tMaxY += tDeltaY;
        faceX = 0;
        faceY = -stepY;
        faceZ = 0;
      } else {
        iz += stepZ;
        t = tMaxZ;
        tMaxZ += tDeltaZ;
        faceX = 0;
        faceY = 0;
        faceZ = -stepZ;
      }
    }
  }

  return false;
}

bool hasBlockNear(const World& world, Vec3 position, BlockType type, int radius) {
  int wx = worldToBlock(position.x);
  int wy = worldToBlock(position.y);
  int wz = worldToBlock(position.z);

  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (blockAtInternal(world, wx + dx, wy + dy, wz + dz) == type) {
          return true;
        }
      }
    }
  }

  return false;
}

bool isPlaceableBlock(BlockType type) {
  return type != BlockAir && type != BlockStick;
}

float surfaceHeightAt(const World& world, float x, float z) {
  int ix = worldToBlock(x);
  int iz = worldToBlock(z);
  float biome = 0.0f;
  int top = static_cast<int>(terrainHeightAt(ix, iz, world.seed, biome));
  if (top < 0) {
    top = 0;
  }
  if (top >= kWorldY) {
    top = kWorldY - 1;
  }
  return (static_cast<float>(top) + 1.0f) * kBlockSize;
}
