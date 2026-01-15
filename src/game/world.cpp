#include "game/world.h"

#include "core/math.h"
#include "core/perlin.h"
#include "render/textures.h"

#include <cmath>
#include <cstdint>
#include <future>
#include <thread>
#include <algorithm>

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
constexpr float kChunkHalfSize = kChunkSize * 0.5f * kBlockSize;
constexpr float kChunkRadius = kChunkHalfSize * 1.7320508f;
constexpr int kDefaultRenderDistance = 6;
constexpr int kVerticalRenderDistance = 3;
constexpr int kChunkBuildPerFrame = 1;
constexpr int kMeshBuildPerFrame = 2;
constexpr int kMaxLightLevel = 15;
constexpr int kLodDistanceMid = 3;
constexpr int kLodDistanceFar = 6;
constexpr int kFarLodRing = 2;

static_assert(sizeof(Vertex) == 16, "Packed vertex size mismatch.");

int blockIndex(int x, int y, int z) {
  return x + kChunkSize * (y + kChunkSize * z);
}

BlockType blockAtLocal(const Chunk& chunk, int lx, int ly, int lz) {
  return static_cast<BlockType>(chunk.blocks[blockIndex(lx, ly, lz)]);
}

BlockType blockAtNeighbor(const Chunk& chunk, const Chunk* neighbor,
                          int lx, int ly, int lz, int dx, int dy, int dz) {
  int nx = lx + dx;
  int ny = ly + dy;
  int nz = lz + dz;
  if (nx >= 0 && nx < kChunkSize && ny >= 0 && ny < kChunkSize && nz >= 0 && nz < kChunkSize) {
    return static_cast<BlockType>(chunk.blocks[blockIndex(nx, ny, nz)]);
  }
  if (!neighbor || neighbor->blocks.empty()) {
    return BlockAir;
  }
  if (nx < 0) nx = kChunkSize - 1;
  else if (nx >= kChunkSize) nx = 0;
  if (ny < 0) ny = kChunkSize - 1;
  else if (ny >= kChunkSize) ny = 0;
  if (nz < 0) nz = kChunkSize - 1;
  else if (nz >= kChunkSize) nz = 0;
  return static_cast<BlockType>(neighbor->blocks[blockIndex(nx, ny, nz)]);
}

uint8_t lightAtNeighbor(const Chunk& chunk, const Chunk* neighbor,
                        int lx, int ly, int lz, int dx, int dy, int dz) {
  int nx = lx + dx;
  int ny = ly + dy;
  int nz = lz + dz;
  if (nx >= 0 && nx < kChunkSize && ny >= 0 && ny < kChunkSize && nz >= 0 && nz < kChunkSize) {
    if (chunk.light.empty()) {
      return 0u;
    }
    return chunk.light[blockIndex(nx, ny, nz)];
  }
  if (!neighbor || neighbor->light.empty()) {
    return 0u;
  }
  if (nx < 0) nx = kChunkSize - 1;
  else if (nx >= kChunkSize) nx = 0;
  if (ny < 0) ny = kChunkSize - 1;
  else if (ny >= kChunkSize) ny = 0;
  if (nz < 0) nz = kChunkSize - 1;
  else if (nz >= kChunkSize) nz = 0;
  return neighbor->light[blockIndex(nx, ny, nz)];
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

bool isLightBlocking(BlockType type) {
  return type != BlockAir && type != BlockStick;
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

uint8_t lightAtInternal(const World& world, int wx, int wy, int wz) {
  if (wy < 0 || wy >= kWorldY) {
    return 0u;
  }
  int cx = floorDiv(wx, kChunkSize);
  int cy = floorDiv(wy, kChunkSize);
  int cz = floorDiv(wz, kChunkSize);
  const Chunk* chunk = findChunkInternal(world, cx, cy, cz);
  if (!chunk || chunk->light.empty()) {
    return 0u;
  }
  int lx = floorMod(wx, kChunkSize);
  int ly = floorMod(wy, kChunkSize);
  int lz = floorMod(wz, kChunkSize);
  return chunk->light[blockIndex(lx, ly, lz)];
}

void setBlockInternal(Chunk& chunk, int wx, int wy, int wz, BlockType type) {
  int lx = floorMod(wx, kChunkSize);
  int ly = floorMod(wy, kChunkSize);
  int lz = floorMod(wz, kChunkSize);
  chunk.blocks[blockIndex(lx, ly, lz)] = static_cast<uint8_t>(type);
}

int8_t normalToByte(float value) {
  value = std::max(-1.0f, std::min(1.0f, value));
  return static_cast<int8_t>(std::lround(value * 127.0f));
}

uint8_t colorToByte(float value);

int16_t coordToShort(float value) {
  return static_cast<int16_t>(std::lround(value));
}

Vertex makeVertex(Vec3 pos, float u, float v, Vec3 normal, Color color) {
  return {
    coordToShort(pos.x),
    coordToShort(pos.y),
    coordToShort(pos.z),
    coordToShort(u),
    coordToShort(v),
    normalToByte(normal.x),
    normalToByte(normal.y),
    normalToByte(normal.z),
    colorToByte(color.r),
    colorToByte(color.g),
    colorToByte(color.b),
  };
}

void addQuad(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, Vec3 normal, Color color,
             Vec3 a, Vec3 b, Vec3 c, Vec3 d, float uScale, float vScale) {
  uint32_t base = static_cast<uint32_t>(vertices.size());
  vertices.push_back(makeVertex(a, 0.0f, 0.0f, normal, color));
  vertices.push_back(makeVertex(b, uScale, 0.0f, normal, color));
  vertices.push_back(makeVertex(c, uScale, vScale, normal, color));
  vertices.push_back(makeVertex(d, 0.0f, vScale, normal, color));
  indices.push_back(base + 0u);
  indices.push_back(base + 1u);
  indices.push_back(base + 2u);
  indices.push_back(base + 0u);
  indices.push_back(base + 2u);
  indices.push_back(base + 3u);
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

Color scaleColor(Color base, float factor) {
  return {base.r * factor, base.g * factor, base.b * factor};
}

Vec3 biomeTintAt(int x, int z, uint32_t seed);

struct FaceKey {
  unsigned int textureId;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct FaceCell {
  bool visible;
  FaceKey key;
};

bool operator==(const FaceKey& a, const FaceKey& b) {
  return a.textureId == b.textureId && a.r == b.r && a.g == b.g && a.b == b.b;
}

uint8_t colorToByte(float value) {
  value = clamp01(value);
  return static_cast<uint8_t>(std::lround(value * 255.0f));
}

Color colorFromKey(const FaceKey& key) {
  float inv = 1.0f / 255.0f;
  return {key.r * inv, key.g * inv, key.b * inv};
}

enum class FaceDir {
  Top,
  Bottom,
  XPos,
  XNeg,
  ZPos,
  ZNeg,
};

FaceKey faceKeyFor(BlockType type, FaceDir face, int wx, int wz, uint32_t seed, uint8_t light,
                   const TextureAssets& textures) {
  float baseShade = 1.0f;
  Color topColor = shadeColor(baseShade);
  Color sideColor = shadeColor(baseShade * 0.95f);
  Color bottomColor = shadeColor(baseShade * 0.85f);
  Vec3 biomeTint = {1.0f, 1.0f, 1.0f};

  if (type == BlockGrass) {
    biomeTint = biomeTintAt(wx, wz, seed);
    topColor = tintColor(shadeColor(baseShade * 1.05f), biomeTint);
    sideColor = shadeColor(baseShade * 0.9f);
    bottomColor = shadeColor(baseShade * 0.7f);
  } else if (type == BlockLeaves) {
    biomeTint = biomeTintAt(wx, wz, seed);
    Vec3 leafTint = {biomeTint.x * 0.85f, biomeTint.y * 0.9f, biomeTint.z * 0.85f};
    topColor = tintColor(shadeColor(baseShade * 1.0f), leafTint);
    sideColor = tintColor(shadeColor(baseShade * 0.95f), leafTint);
    bottomColor = tintColor(shadeColor(baseShade * 0.9f), leafTint);
  }

  BlockTextures blockTextures = texturesFor(type, textures);
  Color faceColor = sideColor;
  unsigned int textureId = blockTextures.side;
  switch (face) {
    case FaceDir::Top:
      faceColor = topColor;
      textureId = blockTextures.top;
      break;
    case FaceDir::Bottom:
      faceColor = bottomColor;
      textureId = blockTextures.bottom;
      break;
    case FaceDir::XPos:
    case FaceDir::XNeg:
    case FaceDir::ZPos:
      faceColor = sideColor;
      textureId = blockTextures.side;
      break;
    case FaceDir::ZNeg:
      faceColor = sideColor;
      textureId = blockTextures.front;
      break;
  }

  float ambientMin = 0.3f;
  float invMax = 1.0f / static_cast<float>(kMaxLightLevel);
  float lightFactor = ambientMin + (1.0f - ambientMin) * (static_cast<float>(light) * invMax);
  Color lit = scaleColor(faceColor, lightFactor);
  return {textureId, colorToByte(lit.r), colorToByte(lit.g), colorToByte(lit.b)};
}

BlockType sampleBlockRegion(const Chunk& chunk, int lx, int ly, int lz, int step) {
  if (step == 1) {
    return blockAtLocal(chunk, lx, ly, lz);
  }
  for (int y = 0; y < step; ++y) {
    for (int z = 0; z < step; ++z) {
      for (int x = 0; x < step; ++x) {
        BlockType type = blockAtLocal(chunk, lx + x, ly + y, lz + z);
        if (type != BlockAir) {
          return type;
        }
      }
    }
  }
  return BlockAir;
}

bool regionIsFullySolid(const Chunk& chunk, int lx, int ly, int lz, int step) {
  for (int y = 0; y < step; ++y) {
    for (int z = 0; z < step; ++z) {
      for (int x = 0; x < step; ++x) {
        if (blockAtLocal(chunk, lx + x, ly + y, lz + z) == BlockAir) {
          return false;
        }
      }
    }
  }
  return true;
}

bool neighborRegionIsFullySolid(const Chunk& chunk, const Chunk* neighbor,
                                int lx, int ly, int lz, int step, int dx, int dy, int dz) {
  int nx = lx + dx * step;
  int ny = ly + dy * step;
  int nz = lz + dz * step;
  if (nx >= 0 && nx + step <= kChunkSize &&
      ny >= 0 && ny + step <= kChunkSize &&
      nz >= 0 && nz + step <= kChunkSize) {
    return regionIsFullySolid(chunk, nx, ny, nz, step);
  }
  if (!neighbor) {
    return false;
  }
  if (nx < 0) {
    nx = kChunkSize - step;
  } else if (nx + step > kChunkSize) {
    nx = 0;
  }
  if (ny < 0) {
    ny = kChunkSize - step;
  } else if (ny + step > kChunkSize) {
    ny = 0;
  }
  if (nz < 0) {
    nz = kChunkSize - step;
  } else if (nz + step > kChunkSize) {
    nz = 0;
  }
  return regionIsFullySolid(*neighbor, nx, ny, nz, step);
}

uint8_t lightAtMacroNeighbor(const Chunk& chunk, const Chunk* neighbor,
                             int lx, int ly, int lz, int step, int dx, int dy, int dz) {
  int sx = lx + (dx > 0 ? step - 1 : 0);
  int sy = ly + (dy > 0 ? step - 1 : 0);
  int sz = lz + (dz > 0 ? step - 1 : 0);
  return lightAtNeighbor(chunk, neighbor, sx, sy, sz, dx, dy, dz);
}

template <typename EmitFunc>
void greedyMask(int width, int height, std::vector<FaceCell>& mask, EmitFunc emit) {
  for (int v = 0; v < height; ++v) {
    for (int u = 0; u < width; ++u) {
      int index = v * width + u;
      if (!mask[index].visible) {
        continue;
      }
      FaceKey key = mask[index].key;
      int w = 1;
      while (u + w < width) {
        int idx = v * width + (u + w);
        if (!mask[idx].visible || !(mask[idx].key == key)) {
          break;
        }
        ++w;
      }
      int h = 1;
      bool done = false;
      while (v + h < height && !done) {
        for (int k = 0; k < w; ++k) {
          int idx = (v + h) * width + (u + k);
          if (!mask[idx].visible || !(mask[idx].key == key)) {
            done = true;
            break;
          }
        }
        if (!done) {
          ++h;
        }
      }
      emit(u, v, w, h, key);
      for (int dv = 0; dv < h; ++dv) {
        for (int du = 0; du < w; ++du) {
          mask[(v + dv) * width + (u + du)].visible = false;
        }
      }
    }
  }
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

void computeChunkLight(const World& world, Chunk& chunk) {
  chunk.light.assign(kChunkVolume, 0u);
  std::vector<int> queue;
  queue.reserve(kChunkVolume);

  int baseX = chunk.cx * kChunkSize;
  int baseY = chunk.cy * kChunkSize;
  int baseZ = chunk.cz * kChunkSize;
  int topY = baseY + kChunkSize - 1;

  for (int lx = 0; lx < kChunkSize; ++lx) {
    int wx = baseX + lx;
    for (int lz = 0; lz < kChunkSize; ++lz) {
      int wz = baseZ + lz;
      bool blocked = false;
      for (int wy = kWorldY - 1; wy > topY; --wy) {
        if (isLightBlocking(blockAtInternal(world, wx, wy, wz))) {
          blocked = true;
          break;
        }
      }

      for (int wy = topY; wy >= baseY; --wy) {
        BlockType type = blockAtInternal(world, wx, wy, wz);
        if (isLightBlocking(type)) {
          blocked = true;
          continue;
        }
        if (blocked) {
          continue;
        }
        int ly = wy - baseY;
        int index = blockIndex(lx, ly, lz);
        if (chunk.light[index] == 0u) {
          chunk.light[index] = static_cast<uint8_t>(kMaxLightLevel);
          queue.push_back(index);
        }
      }
    }
  }

  size_t head = 0;
  while (head < queue.size()) {
    int index = queue[head++];
    int lx = index % kChunkSize;
    int ly = (index / kChunkSize) % kChunkSize;
    int lz = index / (kChunkSize * kChunkSize);
    uint8_t light = chunk.light[index];
    if (light <= 1u) {
      continue;
    }
    int wx = baseX + lx;
    int wy = baseY + ly;
    int wz = baseZ + lz;
    const int offsets[6][3] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
      {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };
    for (const auto& offset : offsets) {
      int nx = wx + offset[0];
      int ny = wy + offset[1];
      int nz = wz + offset[2];
      if (!blockInsideChunk(chunk, nx, ny, nz)) {
        continue;
      }
      if (isLightBlocking(blockAtInternal(world, nx, ny, nz))) {
        continue;
      }
      int nlx = nx - baseX;
      int nly = ny - baseY;
      int nlz = nz - baseZ;
      int nindex = blockIndex(nlx, nly, nlz);
      uint8_t nextLight = static_cast<uint8_t>(light - 1u);
      if (chunk.light[nindex] + 1u <= nextLight) {
        chunk.light[nindex] = nextLight;
        queue.push_back(nindex);
      }
    }
  }
}

struct TempBatch {
  unsigned int textureId;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

TempBatch& getBatch(std::vector<TempBatch>& batches, unsigned int textureId) {
  for (auto& batch : batches) {
    if (batch.textureId == textureId) {
      return batch;
    }
  }
  batches.push_back(TempBatch{textureId, {}, {}});
  return batches.back();
}

void buildChunkMesh(const TextureAssets& textures, Chunk& chunk, int lodStep, uint32_t seed,
                    const Chunk* chunkXPos, const Chunk* chunkXNeg,
                    const Chunk* chunkYPos, const Chunk* chunkYNeg,
                    const Chunk* chunkZPos, const Chunk* chunkZNeg) {
  if (lodStep < 1 || (kChunkSize % lodStep) != 0) {
    lodStep = 1;
  }
  int size = kChunkSize / lodStep;

  chunk.vertices.clear();
  chunk.indices.clear();
  chunk.batches.clear();

  int baseX = chunk.cx * kChunkSize;
  int baseY = chunk.cy * kChunkSize;
  int baseZ = chunk.cz * kChunkSize;
  float stepSize = kBlockSize * static_cast<float>(lodStep);

  std::vector<TempBatch> tempBatches;
  tempBatches.reserve(8);

  std::vector<FaceCell> mask(size * size);

  for (int x = 0; x < size; ++x) {
    for (int y = 0; y < size; ++y) {
      for (int z = 0; z < size; ++z) {
        int lx = x * lodStep;
        int ly = y * lodStep;
        int lz = z * lodStep;
        int index = y * size + z;
        BlockType type = sampleBlockRegion(chunk, lx, ly, lz, lodStep);
        if (type == BlockAir ||
            neighborRegionIsFullySolid(chunk, chunkXPos, lx, ly, lz, lodStep, 1, 0, 0)) {
          mask[index].visible = false;
          continue;
        }
        uint8_t light = lightAtMacroNeighbor(chunk, chunkXPos, lx, ly, lz, lodStep, 1, 0, 0);
        int wx = baseX + lx;
        int wz = baseZ + lz;
        mask[index] = {true, faceKeyFor(type, FaceDir::XPos, wx, wz, seed, light, textures)};
      }
    }
    greedyMask(size, size, mask, [&](int u, int v, int w, int h, const FaceKey& key) {
      float xPlane = static_cast<float>(x * lodStep + lodStep) * kBlockSize;
      float y0 = static_cast<float>(v * lodStep) * kBlockSize;
      float y1 = y0 + stepSize * static_cast<float>(h);
      float z0 = static_cast<float>(u * lodStep) * kBlockSize;
      float z1 = z0 + stepSize * static_cast<float>(w);
      TempBatch& batch = getBatch(tempBatches, key.textureId);
      addQuad(batch.vertices, batch.indices, {1.0f, 0.0f, 0.0f}, colorFromKey(key),
              {xPlane, y0, z0}, {xPlane, y0, z1}, {xPlane, y1, z1}, {xPlane, y1, z0},
              static_cast<float>(w * lodStep), static_cast<float>(h * lodStep));
    });
  }

  for (int x = 0; x < size; ++x) {
    for (int y = 0; y < size; ++y) {
      for (int z = 0; z < size; ++z) {
        int lx = x * lodStep;
        int ly = y * lodStep;
        int lz = z * lodStep;
        int index = y * size + z;
        BlockType type = sampleBlockRegion(chunk, lx, ly, lz, lodStep);
        if (type == BlockAir ||
            neighborRegionIsFullySolid(chunk, chunkXNeg, lx, ly, lz, lodStep, -1, 0, 0)) {
          mask[index].visible = false;
          continue;
        }
        uint8_t light = lightAtMacroNeighbor(chunk, chunkXNeg, lx, ly, lz, lodStep, -1, 0, 0);
        int wx = baseX + lx;
        int wz = baseZ + lz;
        mask[index] = {true, faceKeyFor(type, FaceDir::XNeg, wx, wz, seed, light, textures)};
      }
    }
    greedyMask(size, size, mask, [&](int u, int v, int w, int h, const FaceKey& key) {
      float xPlane = static_cast<float>(x * lodStep) * kBlockSize;
      float y0 = static_cast<float>(v * lodStep) * kBlockSize;
      float y1 = y0 + stepSize * static_cast<float>(h);
      float z0 = static_cast<float>(u * lodStep) * kBlockSize;
      float z1 = z0 + stepSize * static_cast<float>(w);
      TempBatch& batch = getBatch(tempBatches, key.textureId);
      addQuad(batch.vertices, batch.indices, {-1.0f, 0.0f, 0.0f}, colorFromKey(key),
              {xPlane, y0, z1}, {xPlane, y0, z0}, {xPlane, y1, z0}, {xPlane, y1, z1},
              static_cast<float>(w * lodStep), static_cast<float>(h * lodStep));
    });
  }

  for (int y = 0; y < size; ++y) {
    for (int z = 0; z < size; ++z) {
      for (int x = 0; x < size; ++x) {
        int lx = x * lodStep;
        int ly = y * lodStep;
        int lz = z * lodStep;
        int index = z * size + x;
        BlockType type = sampleBlockRegion(chunk, lx, ly, lz, lodStep);
        if (type == BlockAir ||
            neighborRegionIsFullySolid(chunk, chunkYPos, lx, ly, lz, lodStep, 0, 1, 0)) {
          mask[index].visible = false;
          continue;
        }
        uint8_t light = lightAtMacroNeighbor(chunk, chunkYPos, lx, ly, lz, lodStep, 0, 1, 0);
        int wx = baseX + lx;
        int wz = baseZ + lz;
        mask[index] = {true, faceKeyFor(type, FaceDir::Top, wx, wz, seed, light, textures)};
      }
    }
    greedyMask(size, size, mask, [&](int u, int v, int w, int h, const FaceKey& key) {
      float yPlane = static_cast<float>(y * lodStep + lodStep) * kBlockSize;
      float x0 = static_cast<float>(u * lodStep) * kBlockSize;
      float x1 = x0 + stepSize * static_cast<float>(w);
      float z0 = static_cast<float>(v * lodStep) * kBlockSize;
      float z1 = z0 + stepSize * static_cast<float>(h);
      TempBatch& batch = getBatch(tempBatches, key.textureId);
      addQuad(batch.vertices, batch.indices, {0.0f, 1.0f, 0.0f}, colorFromKey(key),
              {x0, yPlane, z0}, {x1, yPlane, z0}, {x1, yPlane, z1}, {x0, yPlane, z1},
              static_cast<float>(w * lodStep), static_cast<float>(h * lodStep));
    });
  }

  for (int y = 0; y < size; ++y) {
    for (int z = 0; z < size; ++z) {
      for (int x = 0; x < size; ++x) {
        int lx = x * lodStep;
        int ly = y * lodStep;
        int lz = z * lodStep;
        int index = z * size + x;
        BlockType type = sampleBlockRegion(chunk, lx, ly, lz, lodStep);
        if (type == BlockAir ||
            neighborRegionIsFullySolid(chunk, chunkYNeg, lx, ly, lz, lodStep, 0, -1, 0)) {
          mask[index].visible = false;
          continue;
        }
        uint8_t light = lightAtMacroNeighbor(chunk, chunkYNeg, lx, ly, lz, lodStep, 0, -1, 0);
        int wx = baseX + lx;
        int wz = baseZ + lz;
        mask[index] = {true, faceKeyFor(type, FaceDir::Bottom, wx, wz, seed, light, textures)};
      }
    }
    greedyMask(size, size, mask, [&](int u, int v, int w, int h, const FaceKey& key) {
      float yPlane = static_cast<float>(y * lodStep) * kBlockSize;
      float x0 = static_cast<float>(u * lodStep) * kBlockSize;
      float x1 = x0 + stepSize * static_cast<float>(w);
      float z0 = static_cast<float>(v * lodStep) * kBlockSize;
      float z1 = z0 + stepSize * static_cast<float>(h);
      TempBatch& batch = getBatch(tempBatches, key.textureId);
      addQuad(batch.vertices, batch.indices, {0.0f, -1.0f, 0.0f}, colorFromKey(key),
              {x0, yPlane, z1}, {x1, yPlane, z1}, {x1, yPlane, z0}, {x0, yPlane, z0},
              static_cast<float>(w * lodStep), static_cast<float>(h * lodStep));
    });
  }

  for (int z = 0; z < size; ++z) {
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        int lx = x * lodStep;
        int ly = y * lodStep;
        int lz = z * lodStep;
        int index = y * size + x;
        BlockType type = sampleBlockRegion(chunk, lx, ly, lz, lodStep);
        if (type == BlockAir ||
            neighborRegionIsFullySolid(chunk, chunkZPos, lx, ly, lz, lodStep, 0, 0, 1)) {
          mask[index].visible = false;
          continue;
        }
        uint8_t light = lightAtMacroNeighbor(chunk, chunkZPos, lx, ly, lz, lodStep, 0, 0, 1);
        int wx = baseX + lx;
        int wz = baseZ + lz;
        mask[index] = {true, faceKeyFor(type, FaceDir::ZPos, wx, wz, seed, light, textures)};
      }
    }
    greedyMask(size, size, mask, [&](int u, int v, int w, int h, const FaceKey& key) {
      float zPlane = static_cast<float>(z * lodStep + lodStep) * kBlockSize;
      float x0 = static_cast<float>(u * lodStep) * kBlockSize;
      float x1 = x0 + stepSize * static_cast<float>(w);
      float y0 = static_cast<float>(v * lodStep) * kBlockSize;
      float y1 = y0 + stepSize * static_cast<float>(h);
      TempBatch& batch = getBatch(tempBatches, key.textureId);
      addQuad(batch.vertices, batch.indices, {0.0f, 0.0f, 1.0f}, colorFromKey(key),
              {x1, y0, zPlane}, {x0, y0, zPlane}, {x0, y1, zPlane}, {x1, y1, zPlane},
              static_cast<float>(w * lodStep), static_cast<float>(h * lodStep));
    });
  }

  for (int z = 0; z < size; ++z) {
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        int lx = x * lodStep;
        int ly = y * lodStep;
        int lz = z * lodStep;
        int index = y * size + x;
        BlockType type = sampleBlockRegion(chunk, lx, ly, lz, lodStep);
        if (type == BlockAir ||
            neighborRegionIsFullySolid(chunk, chunkZNeg, lx, ly, lz, lodStep, 0, 0, -1)) {
          mask[index].visible = false;
          continue;
        }
        uint8_t light = lightAtMacroNeighbor(chunk, chunkZNeg, lx, ly, lz, lodStep, 0, 0, -1);
        int wx = baseX + lx;
        int wz = baseZ + lz;
        mask[index] = {true, faceKeyFor(type, FaceDir::ZNeg, wx, wz, seed, light, textures)};
      }
    }
    greedyMask(size, size, mask, [&](int u, int v, int w, int h, const FaceKey& key) {
      float zPlane = static_cast<float>(z * lodStep) * kBlockSize;
      float x0 = static_cast<float>(u * lodStep) * kBlockSize;
      float x1 = x0 + stepSize * static_cast<float>(w);
      float y0 = static_cast<float>(v * lodStep) * kBlockSize;
      float y1 = y0 + stepSize * static_cast<float>(h);
      TempBatch& batch = getBatch(tempBatches, key.textureId);
      addQuad(batch.vertices, batch.indices, {0.0f, 0.0f, -1.0f}, colorFromKey(key),
              {x0, y0, zPlane}, {x1, y0, zPlane}, {x1, y1, zPlane}, {x0, y1, zPlane},
              static_cast<float>(w * lodStep), static_cast<float>(h * lodStep));
    });
  }

  size_t totalVerts = 0;
  size_t totalIndices = 0;
  for (const auto& batch : tempBatches) {
    totalVerts += batch.vertices.size();
    totalIndices += batch.indices.size();
  }
  chunk.vertices.reserve(totalVerts);
  chunk.indices.reserve(totalIndices);

  for (auto& batch : tempBatches) {
    if (batch.vertices.empty() || batch.indices.empty()) {
      continue;
    }
    MeshBatch out = {};
    out.textureId = batch.textureId;
    out.indexOffset = static_cast<int>(chunk.indices.size());
    out.indexCount = static_cast<int>(batch.indices.size());
    chunk.batches.push_back(out);

    uint32_t baseVertex = static_cast<uint32_t>(chunk.vertices.size());
    chunk.vertices.insert(chunk.vertices.end(), batch.vertices.begin(), batch.vertices.end());
    for (uint32_t index : batch.indices) {
      chunk.indices.push_back(baseVertex + index);
    }
  }

  chunk.meshDirty = true;
  chunk.lodStep = lodStep;
}

void buildChunkMeshWorld(const World& world, const TextureAssets& textures, Chunk& chunk, int lodStep) {
  const Chunk* chunkXPos = findChunkInternal(world, chunk.cx + 1, chunk.cy, chunk.cz);
  const Chunk* chunkXNeg = findChunkInternal(world, chunk.cx - 1, chunk.cy, chunk.cz);
  const Chunk* chunkYPos = (chunk.cy + 1 < kChunksY) ? findChunkInternal(world, chunk.cx, chunk.cy + 1, chunk.cz) : nullptr;
  const Chunk* chunkYNeg = (chunk.cy - 1 >= 0) ? findChunkInternal(world, chunk.cx, chunk.cy - 1, chunk.cz) : nullptr;
  const Chunk* chunkZPos = findChunkInternal(world, chunk.cx, chunk.cy, chunk.cz + 1);
  const Chunk* chunkZNeg = findChunkInternal(world, chunk.cx, chunk.cy, chunk.cz - 1);

  buildChunkMesh(textures, chunk, lodStep, world.seed,
                 chunkXPos, chunkXNeg, chunkYPos, chunkYNeg, chunkZPos, chunkZNeg);
}

struct MeshBuildInput {
  ChunkCoord coord;
  int lodStep;
  uint32_t seed;
  TextureAssets textures;
  Chunk chunk;
  bool hasXPos;
  bool hasXNeg;
  bool hasYPos;
  bool hasYNeg;
  bool hasZPos;
  bool hasZNeg;
  Chunk xPos;
  Chunk xNeg;
  Chunk yPos;
  Chunk yNeg;
  Chunk zPos;
  Chunk zNeg;
};

Chunk makeChunkCopy(const Chunk& source) {
  Chunk copy = {};
  copy.cx = source.cx;
  copy.cy = source.cy;
  copy.cz = source.cz;
  copy.blocks = source.blocks;
  copy.light = source.light;
  copy.lodStep = source.lodStep;
  return copy;
}

void copyNeighbor(const Chunk* source, bool& hasNeighbor, Chunk& dest) {
  if (!source || source->blocks.empty()) {
    hasNeighbor = false;
    dest.blocks.clear();
    dest.light.clear();
    return;
  }
  hasNeighbor = true;
  dest = makeChunkCopy(*source);
}

MeshBuildResult buildChunkMeshAsync(MeshBuildInput input) {
  Chunk temp = std::move(input.chunk);
  temp.vertices.clear();
  temp.indices.clear();
  temp.batches.clear();

  const Chunk* xPos = input.hasXPos ? &input.xPos : nullptr;
  const Chunk* xNeg = input.hasXNeg ? &input.xNeg : nullptr;
  const Chunk* yPos = input.hasYPos ? &input.yPos : nullptr;
  const Chunk* yNeg = input.hasYNeg ? &input.yNeg : nullptr;
  const Chunk* zPos = input.hasZPos ? &input.zPos : nullptr;
  const Chunk* zNeg = input.hasZNeg ? &input.zNeg : nullptr;

  buildChunkMesh(input.textures, temp, input.lodStep, input.seed,
                 xPos, xNeg, yPos, yNeg, zPos, zNeg);

  MeshBuildResult result = {};
  result.coord = input.coord;
  result.lodStep = temp.lodStep;
  result.vertices = std::move(temp.vertices);
  result.indices = std::move(temp.indices);
  result.batches = std::move(temp.batches);
  return result;
}

void enqueueMeshBuild(World& world, const TextureAssets& textures, const Chunk& chunk, int lodStep) {
  ChunkCoord coord = chunkCoord(chunk.cx, chunk.cy, chunk.cz);
  if (world.queuedMeshes.find(coord) != world.queuedMeshes.end()) {
    return;
  }

  MeshBuildInput input = {};
  input.coord = coord;
  input.lodStep = lodStep;
  input.seed = world.seed;
  input.textures = textures;
  input.chunk = makeChunkCopy(chunk);

  copyNeighbor(findChunkInternal(world, chunk.cx + 1, chunk.cy, chunk.cz), input.hasXPos, input.xPos);
  copyNeighbor(findChunkInternal(world, chunk.cx - 1, chunk.cy, chunk.cz), input.hasXNeg, input.xNeg);
  copyNeighbor((chunk.cy + 1 < kChunksY) ? findChunkInternal(world, chunk.cx, chunk.cy + 1, chunk.cz) : nullptr,
               input.hasYPos, input.yPos);
  copyNeighbor((chunk.cy - 1 >= 0) ? findChunkInternal(world, chunk.cx, chunk.cy - 1, chunk.cz) : nullptr,
               input.hasYNeg, input.yNeg);
  copyNeighbor(findChunkInternal(world, chunk.cx, chunk.cy, chunk.cz + 1), input.hasZPos, input.zPos);
  copyNeighbor(findChunkInternal(world, chunk.cx, chunk.cy, chunk.cz - 1), input.hasZNeg, input.zNeg);

  world.queuedMeshes.insert(coord);
  world.meshTasks.push_back({coord, std::async(std::launch::async, buildChunkMeshAsync, std::move(input))});
}

bool chunkInView(const ChunkCoord& coord, Vec3 cameraPos, Vec3 cameraFront,
                 Vec3 cameraUp, Vec3 cameraRight, float tanHalfFovX, float tanHalfFovY) {
  float centerX = static_cast<float>(coord.cx * kChunkSize) * kBlockSize + kChunkHalfSize;
  float centerY = static_cast<float>(coord.cy * kChunkSize) * kBlockSize + kChunkHalfSize;
  float centerZ = static_cast<float>(coord.cz * kChunkSize) * kBlockSize + kChunkHalfSize;
  Vec3 toCenter = {centerX - cameraPos.x, centerY - cameraPos.y, centerZ - cameraPos.z};
  float distSq = dot(toCenter, toCenter);
  if (distSq <= kChunkRadius * kChunkRadius) {
    return true;
  }

  float forward = dot(toCenter, cameraFront);
  if (forward <= 0.0f) {
    return false;
  }

  float right = dot(toCenter, cameraRight);
  float up = dot(toCenter, cameraUp);

  float maxRight = forward * tanHalfFovX + kChunkRadius;
  float maxUp = forward * tanHalfFovY + kChunkRadius;
  if (std::fabs(right) > maxRight) {
    return false;
  }
  if (std::fabs(up) > maxUp) {
    return false;
  }
  return true;
}

int lodStepForDistance(int distance) {
  if (distance <= kLodDistanceMid) {
    return 1;
  }
  if (distance <= kLodDistanceFar) {
    return 2;
  }
  return 4;
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
  world.queuedMeshes.clear();
  world.buildTasks.clear();
  world.meshTasks.clear();
  world.visibleChunks.clear();
  world.pendingChunks.clear();
}

const Chunk* findChunk(const World& world, int cx, int cy, int cz) {
  return findChunkInternal(world, cx, cy, cz);
}

Chunk* findChunkMutable(World& world, int cx, int cy, int cz) {
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
  chunk.vbo = 0;
  chunk.ibo = 0;
  chunk.meshDirty = false;
  chunk.lodStep = 1;
  chunk.blocks.assign(kChunkVolume, static_cast<uint8_t>(BlockAir));
  generateChunk(chunk, world.seed);
  computeChunkLight(world, chunk);
  auto result = world.chunks.emplace(key, std::move(chunk));
  enqueueMeshBuild(world, textures, result.first->second, result.first->second.lodStep);
  created = true;
  return result.first->second;
}

Chunk buildChunkData(const ChunkCoord& coord, uint32_t seed) {
  Chunk chunk = {};
  chunk.cx = coord.cx;
  chunk.cy = coord.cy;
  chunk.cz = coord.cz;
  chunk.vbo = 0;
  chunk.ibo = 0;
  chunk.meshDirty = false;
  chunk.lodStep = 1;
  chunk.blocks.assign(kChunkVolume, static_cast<uint8_t>(BlockAir));
  generateChunk(chunk, seed);
  return chunk;
}

template <typename T>
bool isFutureReady(const std::future<T>& future) {
  return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void updateWorldChunks(World& world, const TextureAssets& textures, Vec3 playerPosition,
                       Vec3 cameraFront, Vec3 cameraUp, Vec3 cameraRight,
                       float fovDegrees, float aspect) {
  int wx = worldToBlock(playerPosition.x);
  int wz = worldToBlock(playerPosition.z);
  int wy = worldToBlock(playerPosition.y);
  int pcx = floorDiv(wx, kChunkSize);
  int pcy = floorDiv(wy, kChunkSize);
  int pcz = floorDiv(wz, kChunkSize);

  int meshApplied = 0;
  for (auto it = world.meshTasks.begin(); it != world.meshTasks.end(); ) {
    if (!isFutureReady(it->future)) {
      ++it;
      continue;
    }
    if (meshApplied >= kMeshBuildPerFrame) {
      break;
    }
    MeshBuildResult result = it->future.get();
    world.queuedMeshes.erase(result.coord);
    Chunk* chunk = findChunkInternal(world, result.coord.cx, result.coord.cy, result.coord.cz);
    if (chunk && chunk->lodStep == result.lodStep) {
      chunk->vertices = std::move(result.vertices);
      chunk->indices = std::move(result.indices);
      chunk->batches = std::move(result.batches);
      chunk->meshDirty = true;
    }
    it = world.meshTasks.erase(it);
    meshApplied++;
  }

  for (auto it = world.buildTasks.begin(); it != world.buildTasks.end(); ) {
    if (!isFutureReady(it->future)) {
      ++it;
      continue;
    }
    Chunk chunk = it->future.get();
    world.queuedChunks.erase(it->coord);
    ChunkCoord key = it->coord;
    auto existing = world.chunks.find(key);
    if (existing == world.chunks.end()) {
      auto result = world.chunks.emplace(key, std::move(chunk));
      computeChunkLight(world, result.first->second);
      int dist = std::abs(result.first->second.cx - pcx);
      dist = std::max(dist, std::abs(result.first->second.cy - pcy));
      dist = std::max(dist, std::abs(result.first->second.cz - pcz));
      int lodStep = lodStepForDistance(dist);
      enqueueMeshBuild(world, textures, result.first->second, lodStep);
      rebuildChunksAround(world, textures, result.first->second.cx * kChunkSize,
                          result.first->second.cy * kChunkSize,
                          result.first->second.cz * kChunkSize);
    }
    it = world.buildTasks.erase(it);
  }
  int minCy = pcy - kVerticalRenderDistance;
  int maxCy = pcy + kVerticalRenderDistance;
  if (minCy < 0) minCy = 0;
  if (maxCy > kChunksY - 1) maxCy = kChunksY - 1;

  int render = world.renderDistance;
  int maxRadius = render + kFarLodRing;

  world.visibleChunks.clear();
  world.pendingChunks.clear();
  int maxChunks = (maxRadius * 2 + 1) * (maxRadius * 2 + 1) * (maxCy - minCy + 1);
  if (maxChunks < 0) {
    maxChunks = 0;
  }
  world.visibleChunks.reserve(static_cast<size_t>(maxChunks));
  world.pendingChunks.reserve(static_cast<size_t>(maxChunks));

  float tanHalfFovY = 0.0f;
  float tanHalfFovX = 0.0f;
  bool useFrustum = aspect > 0.0f && fovDegrees > 1.0f;
  if (useFrustum) {
    tanHalfFovY = std::tan(radians(fovDegrees) * 0.5f);
    tanHalfFovX = tanHalfFovY * aspect;
  }
  for (int radius = 0; radius <= maxRadius; ++radius) {
    for (int dz = -radius; dz <= radius; ++dz) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (std::abs(dx) != radius && std::abs(dz) != radius) {
          continue;
        }
        if (dx * dx + dz * dz > maxRadius * maxRadius) {
          continue;
        }
        int cx = pcx + dx;
        int cz = pcz + dz;
        for (int cy = minCy; cy <= maxCy; ++cy) {
          const Chunk* chunk = findChunkInternal(world, cx, cy, cz);
          if (chunk) {
            ChunkCoord coord = chunkCoord(chunk->cx, chunk->cy, chunk->cz);
            if (!useFrustum || chunkInView(coord, playerPosition, cameraFront, cameraUp, cameraRight,
                                           tanHalfFovX, tanHalfFovY)) {
              world.visibleChunks.push_back(coord);
            }
          } else {
            world.pendingChunks.push_back(chunkCoord(cx, cy, cz));
          }
        }
      }
    }
  }

  int meshScheduled = 0;
  for (const ChunkCoord& coord : world.visibleChunks) {
    if (meshScheduled >= kMeshBuildPerFrame) {
      break;
    }
    Chunk* chunk = findChunkInternal(world, coord.cx, coord.cy, coord.cz);
    if (!chunk) {
      continue;
    }
    int dist = std::abs(coord.cx - pcx);
    dist = std::max(dist, std::abs(coord.cy - pcy));
    dist = std::max(dist, std::abs(coord.cz - pcz));
    int lodStep = lodStepForDistance(dist);
    if (chunk->lodStep != lodStep) {
      chunk->lodStep = lodStep;
      enqueueMeshBuild(world, textures, *chunk, lodStep);
      meshScheduled++;
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
    computeChunkLight(world, entry.second);
    int lodStep = entry.second.lodStep > 0 ? entry.second.lodStep : 1;
    entry.second.lodStep = lodStep;
    enqueueMeshBuild(world, textures, entry.second, lodStep);
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
    computeChunkLight(world, *chunk);
    int lodStep = chunk->lodStep > 0 ? chunk->lodStep : 1;
    chunk->lodStep = lodStep;
    enqueueMeshBuild(world, textures, *chunk, lodStep);
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
