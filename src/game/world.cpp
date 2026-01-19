#include "game/world.h"

#include "core/math.h"
#include "core/perlin.h"
#include "render/textures.h"

#include <GLFW/glfw3.h>
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
constexpr int kDefaultRenderDistance = 16;
constexpr int kVerticalRenderDistance = 8;
constexpr int kChunkBuildPerFrame = 2;
constexpr int kMeshBuildPerFrame = 4;
constexpr int kMaxLightLevel = 15;
constexpr int kLodDistanceMid = 6;
constexpr int kLodDistanceFar = 12;
constexpr int kFarLodRing = 5;
constexpr float kLakeChance = 0.035f;

static_assert(sizeof(Vertex) == 16, "Packed vertex size mismatch.");

uint64_t splitmix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

uint64_t blockHashContribution(uint32_t index, uint8_t value) {
  uint64_t x = (static_cast<uint64_t>(index) + 1ULL) * 0x9e3779b97f4a7c15ULL;
  x ^= static_cast<uint64_t>(value) * 0xbf58476d1ce4e5b9ULL;
  return splitmix64(x);
}

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
  return type != BlockAir && type != BlockStick && type != BlockWater && type != BlockTorch;
}

bool isOccludingBlock(BlockType type) {
  return type != BlockAir && type != BlockStick && type != BlockWater && type != BlockTorch;
}

bool isTransparentBlock(BlockType type) {
  return type == BlockWater || type == BlockTorch;
}

constexpr uint8_t kFaceXPos = 1u << 0;
constexpr uint8_t kFaceXNeg = 1u << 1;
constexpr uint8_t kFaceYPos = 1u << 2;
constexpr uint8_t kFaceYNeg = 1u << 3;
constexpr uint8_t kFaceZPos = 1u << 4;
constexpr uint8_t kFaceZNeg = 1u << 5;

uint8_t computeFaceOpenMask(const Chunk& chunk) {
  if (chunk.blocks.empty()) {
    return 0u;
  }
  uint8_t mask = 0u;
  //X+
  for (int ly = 0; ly < kChunkSize && !(mask & kFaceXPos); ++ly) {
    for (int lz = 0; lz < kChunkSize; ++lz) {
      if (!isLightBlocking(blockAtLocal(chunk, kChunkSize - 1, ly, lz))) {
        mask |= kFaceXPos;
        break;
      }
    }
  }
  //X-
  for (int ly = 0; ly < kChunkSize && !(mask & kFaceXNeg); ++ly) {
    for (int lz = 0; lz < kChunkSize; ++lz) {
      if (!isLightBlocking(blockAtLocal(chunk, 0, ly, lz))) {
        mask |= kFaceXNeg;
        break;
      }
    }
  }
  //Z+
  for (int ly = 0; ly < kChunkSize && !(mask & kFaceZPos); ++ly) {
    for (int lx = 0; lx < kChunkSize; ++lx) {
      if (!isLightBlocking(blockAtLocal(chunk, lx, ly, kChunkSize - 1))) {
        mask |= kFaceZPos;
        break;
      }
    }
  }
  //Z-
  for (int ly = 0; ly < kChunkSize && !(mask & kFaceZNeg); ++ly) {
    for (int lx = 0; lx < kChunkSize; ++lx) {
      if (!isLightBlocking(blockAtLocal(chunk, lx, ly, 0))) {
        mask |= kFaceZNeg;
        break;
      }
    }
  }
  //Y+
  for (int lz = 0; lz < kChunkSize && !(mask & kFaceYPos); ++lz) {
    for (int lx = 0; lx < kChunkSize; ++lx) {
      if (!isLightBlocking(blockAtLocal(chunk, lx, kChunkSize - 1, lz))) {
        mask |= kFaceYPos;
        break;
      }
    }
  }
  // Y-
  for (int lz = 0; lz < kChunkSize && !(mask & kFaceYNeg); ++lz) {
    for (int lx = 0; lx < kChunkSize; ++lx) {
      if (!isLightBlocking(blockAtLocal(chunk, lx, 0, lz))) {
        mask |= kFaceYNeg;
        break;
      }
    }
  }
  return mask;
}

void recomputeChunkMetadata(Chunk& chunk) {
  uint64_t hash = 0;
  int airCount = 0;
  int solidCount = 0;
  int maxHeight = 0;

  for (int lx = 0; lx < kChunkSize; ++lx) {
    for (int lz = 0; lz < kChunkSize; ++lz) {
      for (int ly = kChunkSize - 1; ly >= 0; --ly) {
        BlockType type = blockAtLocal(chunk, lx, ly, lz);
        hash ^= blockHashContribution(static_cast<uint32_t>(blockIndex(lx, ly, lz)), static_cast<uint8_t>(type));
        if (type == BlockAir) {
          airCount++;
        } else {
          solidCount++;
          int worldY = chunk.cy * kChunkSize + ly;
          if (type != BlockWater && worldY > maxHeight) {
            maxHeight = worldY;
          }
        }
      }
    }
  }

  chunk.contentHash = hash;
  chunk.isEmpty = (airCount == kChunkVolume);
  chunk.isSolid = (solidCount == kChunkVolume);
  chunk.maxHeight = maxHeight;
  chunk.faceOpenMask = computeFaceOpenMask(chunk);
}

bool isFaceOccluding(BlockType current, BlockType neighbor) {
  if (neighbor == BlockAir || neighbor == BlockStick) {
    return false;
  }
  if (neighbor == BlockWater) {
    return current == BlockWater;
  }
  if (neighbor == BlockTorch) {
    return current == BlockTorch;
  }
  return true;
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

void addQuadSmooth(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                   Vec3 normal, Color baseColor[4],
                   Vec3 a, Vec3 b, Vec3 c, Vec3 d, float uScale, float vScale) {
  uint32_t base = static_cast<uint32_t>(vertices.size());
  vertices.push_back(makeVertex(a, 0.0f, 0.0f, normal, baseColor[0]));
  vertices.push_back(makeVertex(b, uScale, 0.0f, normal, baseColor[1]));
  vertices.push_back(makeVertex(c, uScale, vScale, normal, baseColor[2]));
  vertices.push_back(makeVertex(d, 0.0f, vScale, normal, baseColor[3]));
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
    case BlockWater:
      return {textures.water.id, textures.water.id, textures.water.id, textures.water.id};
    case BlockTorch:
      return {textures.torch.id, textures.torch.id, textures.torch.id, textures.torch.id};
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
void setBlockInChunk(Chunk& chunk, int wx, int wy, int wz, BlockType type);

struct FaceKey {
  unsigned int textureId;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  bool transparent;
  bool animated;
};

struct FaceCell {
  bool visible;
  FaceKey key;
};

bool operator==(const FaceKey& a, const FaceKey& b) {
  return a.textureId == b.textureId && a.r == b.r && a.g == b.g && a.b == b.b
    && a.transparent == b.transparent && a.animated == b.animated;
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

  float ambientMin = 0.12f;
  float invMax = 1.0f / static_cast<float>(kMaxLightLevel);
  float lightFactor = ambientMin + (1.0f - ambientMin) * (static_cast<float>(light) * invMax);
  Color lit = scaleColor(faceColor, lightFactor);
  return {textureId, colorToByte(lit.r), colorToByte(lit.g), colorToByte(lit.b),
          isTransparentBlock(type), type == BlockWater};
}

float smoothLightAO(const Chunk& chunk, int lx, int ly, int lz,
                    const Chunk* neighbors[6],
                    int dx, int dy, int dz, uint8_t& avgLight) {
  const Chunk* chunkXPos = neighbors[0];
  const Chunk* chunkXNeg = neighbors[1];
  const Chunk* chunkYPos = neighbors[2];
  const Chunk* chunkYNeg = neighbors[3];
  const Chunk* chunkZPos = neighbors[4];
  const Chunk* chunkZNeg = neighbors[5];
  
  uint8_t s[3];
  bool solid[3];
  
  const Chunk* nx = (dx > 0) ? chunkXPos : (dx < 0) ? chunkXNeg : nullptr;
  const Chunk* ny = (dy > 0) ? chunkYPos : (dy < 0) ? chunkYNeg : nullptr;
  const Chunk* nz = (dz > 0) ? chunkZPos : (dz < 0) ? chunkZNeg : nullptr;
  
  s[0] = lightAtNeighbor(chunk, nx, lx, ly, lz, dx, 0, 0);
  s[1] = lightAtNeighbor(chunk, ny, lx, ly, lz, 0, dy, 0);
  s[2] = lightAtNeighbor(chunk, nz, lx, ly, lz, 0, 0, dz);
  
  solid[0] = isOccludingBlock(blockAtNeighbor(chunk, nx, lx, ly, lz, dx, 0, 0));
  solid[1] = isOccludingBlock(blockAtNeighbor(chunk, ny, lx, ly, lz, 0, dy, 0));
  solid[2] = isOccludingBlock(blockAtNeighbor(chunk, nz, lx, ly, lz, 0, 0, dz));
  
  int solidCount = (solid[0] ? 1 : 0) + (solid[1] ? 1 : 0) + (solid[2] ? 1 : 0);
  
  float ao = 1.0f - (static_cast<float>(solidCount) * 0.2f);
  
  int lightSum = 0;
  int lightCount = 0;
  for (int i = 0; i < 3; ++i) {
    if (!solid[i]) {
      lightSum += s[i];
      lightCount++;
    }
  }
  
  avgLight = lightCount > 0 ? static_cast<uint8_t>(lightSum / lightCount) : 0;
  return ao;
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

bool regionIsFullySolid(const Chunk& chunk, int lx, int ly, int lz, int step, BlockType current) {
  for (int y = 0; y < step; ++y) {
    for (int z = 0; z < step; ++z) {
      for (int x = 0; x < step; ++x) {
        BlockType neighbor = blockAtLocal(chunk, lx + x, ly + y, lz + z);
        if (!isFaceOccluding(current, neighbor)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool neighborRegionIsFullySolid(const Chunk& chunk, const Chunk* neighbor,
                                int lx, int ly, int lz, int step, int dx, int dy, int dz, BlockType current) {
  int nx = lx + dx * step;
  int ny = ly + dy * step;
  int nz = lz + dz * step;
  if (nx >= 0 && nx + step <= kChunkSize &&
      ny >= 0 && ny + step <= kChunkSize &&
      nz >= 0 && nz + step <= kChunkSize) {
    return regionIsFullySolid(chunk, nx, ny, nz, step, current);
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
  return regionIsFullySolid(*neighbor, nx, ny, nz, step, current);
}

uint8_t lightAtMacroNeighbor(const Chunk& chunk, const Chunk* neighbor,
                             int lx, int ly, int lz, int step, int dx, int dy, int dz) {
  int sx = lx + (dx > 0 ? step - 1 : 0);
  int sy = ly + (dy > 0 ? step - 1 : 0);
  int sz = lz + (dz > 0 ? step - 1 : 0);
  return lightAtNeighbor(chunk, neighbor, sx, sy, sz, dx, dy, dz);
}

struct VertexLight {
  uint8_t light;
  float ao;
};

VertexLight smoothLightAtVertex(const Chunk& chunk, const Chunk* neighbors[6],
                                 int lx, int ly, int lz,
                                 int du, int dv, int dw,
                                 int axis) {
  const Chunk* chunkXPos = neighbors[0];
  const Chunk* chunkXNeg = neighbors[1];
  const Chunk* chunkYPos = neighbors[2];
  const Chunk* chunkYNeg = neighbors[3];
  const Chunk* chunkZPos = neighbors[4];
  const Chunk* chunkZNeg = neighbors[5];
  
  uint8_t light[4];
  bool solid[4];
  
  if (axis == 0) {
    light[0] = lightAtNeighbor(chunk, dv > 0 ? chunkYPos : chunkYNeg, lx, ly, lz, 0, dv, dw);
    light[1] = lightAtNeighbor(chunk, dw > 0 ? chunkZPos : chunkZNeg, lx, ly, lz, 0, 0, dw);
    light[2] = lightAtNeighbor(chunk, dv > 0 ? chunkYPos : chunkYNeg, lx, ly, lz, 0, dv, 0);
    light[3] = lightAtNeighbor(chunk, (dv > 0 && dw > 0) ? (chunkYPos ? chunkYPos : chunkZPos) : 
                                      (dv < 0 && dw < 0) ? (chunkYNeg ? chunkYNeg : chunkZNeg) :
                                      (dv > 0) ? chunkYPos : (dw > 0) ? chunkZPos : chunkZNeg,
                              lx, ly, lz, 0, dv, dw);
    
    solid[0] = isOccludingBlock(blockAtNeighbor(chunk, dv > 0 ? chunkYPos : chunkYNeg, lx, ly, lz, 0, dv, dw));
    solid[1] = isOccludingBlock(blockAtNeighbor(chunk, dw > 0 ? chunkZPos : chunkZNeg, lx, ly, lz, 0, 0, dw));
    solid[2] = isOccludingBlock(blockAtNeighbor(chunk, dv > 0 ? chunkYPos : chunkYNeg, lx, ly, lz, 0, dv, 0));
    solid[3] = isOccludingBlock(blockAtNeighbor(chunk, (dv > 0 && dw > 0) ? (chunkYPos ? chunkYPos : chunkZPos) :
                                                        (dv < 0 && dw < 0) ? (chunkYNeg ? chunkYNeg : chunkZNeg) :
                                                        (dv > 0) ? chunkYPos : (dw > 0) ? chunkZPos : chunkZNeg,
                                               lx, ly, lz, 0, dv, dw));
  } else if (axis == 1) {
    light[0] = lightAtNeighbor(chunk, du > 0 ? chunkXPos : chunkXNeg, lx, ly, lz, du, 0, dw);
    light[1] = lightAtNeighbor(chunk, dw > 0 ? chunkZPos : chunkZNeg, lx, ly, lz, 0, 0, dw);
    light[2] = lightAtNeighbor(chunk, du > 0 ? chunkXPos : chunkXNeg, lx, ly, lz, du, 0, 0);
    light[3] = lightAtNeighbor(chunk, (du > 0 && dw > 0) ? (chunkXPos ? chunkXPos : chunkZPos) :
                                      (du < 0 && dw < 0) ? (chunkXNeg ? chunkXNeg : chunkZNeg) :
                                      (du > 0) ? chunkXPos : (dw > 0) ? chunkZPos : chunkZNeg,
                              lx, ly, lz, du, 0, dw);
    
    solid[0] = isOccludingBlock(blockAtNeighbor(chunk, du > 0 ? chunkXPos : chunkXNeg, lx, ly, lz, du, 0, dw));
    solid[1] = isOccludingBlock(blockAtNeighbor(chunk, dw > 0 ? chunkZPos : chunkZNeg, lx, ly, lz, 0, 0, dw));
    solid[2] = isOccludingBlock(blockAtNeighbor(chunk, du > 0 ? chunkXPos : chunkXNeg, lx, ly, lz, du, 0, 0));
    solid[3] = isOccludingBlock(blockAtNeighbor(chunk, (du > 0 && dw > 0) ? (chunkXPos ? chunkXPos : chunkZPos) :
                                                        (du < 0 && dw < 0) ? (chunkXNeg ? chunkXNeg : chunkZNeg) :
                                                        (du > 0) ? chunkXPos : (dw > 0) ? chunkZPos : chunkZNeg,
                                               lx, ly, lz, du, 0, dw));
  } else {
    light[0] = lightAtNeighbor(chunk, du > 0 ? chunkXPos : chunkXNeg, lx, ly, lz, du, dv, 0);
    light[1] = lightAtNeighbor(chunk, dv > 0 ? chunkYPos : chunkYNeg, lx, ly, lz, 0, dv, 0);
    light[2] = lightAtNeighbor(chunk, du > 0 ? chunkXPos : chunkXNeg, lx, ly, lz, du, 0, 0);
    light[3] = lightAtNeighbor(chunk, (du > 0 && dv > 0) ? (chunkXPos ? chunkXPos : chunkYPos) :
                                      (du < 0 && dv < 0) ? (chunkXNeg ? chunkXNeg : chunkYNeg) :
                                      (du > 0) ? chunkXPos : (dv > 0) ? chunkYPos : chunkYNeg,
                              lx, ly, lz, du, dv, 0);
    
    solid[0] = isOccludingBlock(blockAtNeighbor(chunk, du > 0 ? chunkXPos : chunkXNeg, lx, ly, lz, du, dv, 0));
    solid[1] = isOccludingBlock(blockAtNeighbor(chunk, dv > 0 ? chunkYPos : chunkYNeg, lx, ly, lz, 0, dv, 0));
    solid[2] = isOccludingBlock(blockAtNeighbor(chunk, du > 0 ? chunkXPos : chunkXNeg, lx, ly, lz, du, 0, 0));
    solid[3] = isOccludingBlock(blockAtNeighbor(chunk, (du > 0 && dv > 0) ? (chunkXPos ? chunkXPos : chunkYPos) :
                                                        (du < 0 && dv < 0) ? (chunkXNeg ? chunkXNeg : chunkYNeg) :
                                                        (du > 0) ? chunkXPos : (dv > 0) ? chunkYPos : chunkYNeg,
                                               lx, ly, lz, du, dv, 0));
  }
  
  int aoValue;
  if (solid[0] && solid[1]) {
    aoValue = 0;
  } else {
    int solidCount = (solid[0] ? 1 : 0) + (solid[1] ? 1 : 0) + (solid[2] ? 1 : 0);
    aoValue = 3 - solidCount;
  }
  float ao = aoValue / 3.0f;
  
  int lightSum = 0;
  int lightCount = 0;
  for (int i = 0; i < 4; ++i) {
    if (!solid[i]) {
      lightSum += light[i];
      lightCount++;
    }
  }
  
  uint8_t avgLight = lightCount > 0 ? static_cast<uint8_t>(lightSum / lightCount) : 0;
  
  return {avgLight, ao};
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

struct CaveSegment {
  Vec3 start;
  Vec3 end;
  float radius;
  bool entrance;
};

constexpr float kCaveChunkRate = 0.2f;

float distanceSqToSegment(Vec3 p, Vec3 a, Vec3 b) {
  Vec3 ab = b - a;
  float abLenSq = dot(ab, ab);
  if (abLenSq <= 0.0001f) {
    Vec3 ap = p - a;
    return dot(ap, ap);
  }
  float t = dot(p - a, ab) / abLenSq;
  t = std::max(0.0f, std::min(1.0f, t));
  Vec3 proj = a + ab * t;
  Vec3 diff = p - proj;
  return dot(diff, diff);
}

float caveRand(int cx, int cz, int salt, uint32_t seed) {
  int hx = cx * 31 + salt * 101;
  int hz = cz * 17 + salt * 57;
  return hash01(hx, hz, seed + 0x9e3779b9u);
}

bool caveRootAt(int cx, int cz, uint32_t seed) {
  return caveRand(cx, cz, 1, seed) < kCaveChunkRate;
}

Vec3 caveDirection(int cx, int cz, int salt, uint32_t seed, float verticalRange) {
  float yaw = caveRand(cx, cz, salt, seed) * 6.2831853f;
  float pitch = (caveRand(cx, cz, salt + 11, seed) - 0.5f) * verticalRange;
  float cosPitch = std::cos(pitch);
  return {std::cos(yaw) * cosPitch, std::sin(pitch), std::sin(yaw) * cosPitch};
}

void appendCaveSystemSegments(int cx, int cz, uint32_t seed, std::vector<CaveSegment>& segments) {
  float offsetX = caveRand(cx, cz, 2, seed) * static_cast<float>(kChunkSize - 6) + 3.0f;
  float offsetZ = caveRand(cx, cz, 3, seed) * static_cast<float>(kChunkSize - 6) + 3.0f;
  int baseX = cx * kChunkSize + static_cast<int>(offsetX);
  int baseZ = cz * kChunkSize + static_cast<int>(offsetZ);
  float biome = 0.0f;
  int top = static_cast<int>(terrainHeightAt(baseX, baseZ, seed, biome));
  if (top < 4 || top >= kWorldY - 2) {
    return;
  }
  bool hillside = caveRand(cx, cz, 4, seed) > 0.55f;
  int lowestNeighbor = top;
  int slopeDirX = 1;
  int slopeDirZ = 0;
  if (hillside) {
    float tempBiome = 0.0f;
    int hXPos = static_cast<int>(terrainHeightAt(baseX + 3, baseZ, seed, tempBiome));
    int hXNeg = static_cast<int>(terrainHeightAt(baseX - 3, baseZ, seed, tempBiome));
    int hZPos = static_cast<int>(terrainHeightAt(baseX, baseZ + 3, seed, tempBiome));
    int hZNeg = static_cast<int>(terrainHeightAt(baseX, baseZ - 3, seed, tempBiome));
    lowestNeighbor = hXPos;
    slopeDirX = 1;
    slopeDirZ = 0;
    if (hXNeg < lowestNeighbor) {
      lowestNeighbor = hXNeg;
      slopeDirX = -1;
      slopeDirZ = 0;
    }
    if (hZPos < lowestNeighbor) {
      lowestNeighbor = hZPos;
      slopeDirX = 0;
      slopeDirZ = 1;
    }
    if (hZNeg < lowestNeighbor) {
      lowestNeighbor = hZNeg;
      slopeDirX = 0;
      slopeDirZ = -1;
    }
  }
  int inset = hillside ? (2 + static_cast<int>(caveRand(cx, cz, 5, seed) * 3.0f))
                       : static_cast<int>(caveRand(cx, cz, 6, seed) * 2.0f);
  int entranceY = top - inset;
  if (hillside) {
    entranceY = std::min(entranceY, lowestNeighbor + 1);
  }
  entranceY = std::max(2, std::min(kWorldY - 3, entranceY));
  Vec3 entrance = {static_cast<float>(baseX) + 0.5f,
                   static_cast<float>(entranceY) + 0.5f,
                   static_cast<float>(baseZ) + 0.5f};
  Vec3 entryDir = caveDirection(cx, cz, 7, seed, 0.8f);
  entryDir.y = -std::abs(entryDir.y) - 0.2f;
  entryDir = normalize(entryDir);
  float entryDepth = 3.0f + caveRand(cx, cz, 8, seed) * 4.0f;
  Vec3 entryPoint = entrance + entryDir * entryDepth;
  float entranceRadius = 2.3f + caveRand(cx, cz, 9, seed) * 0.9f;
  segments.push_back({entrance, entryPoint, entranceRadius, true});
  if (hillside) {
    Vec3 mouthDir = {static_cast<float>(slopeDirX), 0.0f, static_cast<float>(slopeDirZ)};
    float mouthLength = 3.0f + caveRand(cx, cz, 12, seed) * 4.0f;
    Vec3 mouthEnd = entrance + mouthDir * mouthLength;
    segments.push_back({entrance, mouthEnd, entranceRadius, true});
  }

  std::vector<CaveSegment> spine;
  spine.reserve(6);
  Vec3 start = entryPoint;
  int mainCount = 3 + static_cast<int>(caveRand(cx, cz, 10, seed) * 3.0f);
  for (int i = 0; i < mainCount; ++i) {
    Vec3 dir = caveDirection(cx, cz, 20 + i * 3, seed, 0.5f);
    dir.y *= 0.6f;
    float length = 20.0f + caveRand(cx, cz, 30 + i * 3, seed) * 50.0f;
    float radius = 2.4f + caveRand(cx, cz, 31 + i * 3, seed) * 1.8f;
    Vec3 end = start + dir * length;
    CaveSegment segment = {start, end, radius, false};
    segments.push_back(segment);
    spine.push_back(segment);
    start = end;
  }

  int branchCount = 1 + static_cast<int>(caveRand(cx, cz, 60, seed) * 3.0f);
  for (int b = 0; b < branchCount; ++b) {
    int pick = static_cast<int>(caveRand(cx, cz, 70 + b, seed) * static_cast<float>(spine.size()));
    if (pick < 0) pick = 0;
    if (pick >= static_cast<int>(spine.size())) pick = static_cast<int>(spine.size()) - 1;
    Vec3 branchVec = spine[pick].end - spine[pick].start;
    float t = 0.3f + caveRand(cx, cz, 80 + b, seed) * 0.4f;
    Vec3 branchStart = spine[pick].start + branchVec * t;
    int branchSegments = 1 + static_cast<int>(caveRand(cx, cz, 90 + b, seed) * 2.0f);
    Vec3 branchCursor = branchStart;
    for (int s = 0; s < branchSegments; ++s) {
      Vec3 dir = caveDirection(cx, cz, 100 + b * 7 + s * 3, seed, 0.9f);
      dir.y *= 0.6f;
      float length = 12.0f + caveRand(cx, cz, 110 + b * 7 + s * 3, seed) * 28.0f;
      float radius = 1.7f + caveRand(cx, cz, 120 + b * 7 + s * 3, seed) * 1.4f;
      Vec3 end = branchCursor + dir * length;
      segments.push_back({branchCursor, end, radius, false});
      branchCursor = end;
    }
  }
}

void buildCaveSegmentsForChunk(const Chunk& chunk, uint32_t seed, std::vector<CaveSegment>& segments) {
  int range = 3;
  segments.reserve(48);
  for (int cx = chunk.cx - range; cx <= chunk.cx + range; ++cx) {
    for (int cz = chunk.cz - range; cz <= chunk.cz + range; ++cz) {
      if (!caveRootAt(cx, cz, seed)) {
        continue;
      }
      appendCaveSystemSegments(cx, cz, seed, segments);
    }
  }
}

bool shouldCarveCave(const std::vector<CaveSegment>& segments, int x, int y, int z, int top, uint32_t seed) {
  if (segments.empty() || y <= 1 || y >= kWorldY - 1) {
    return false;
  }
  if (y > top) {
    return false;
  }
  Vec3 p = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f};
  float noise = fbmNoise3D(static_cast<float>(x) * 0.17f, static_cast<float>(y) * 0.17f,
                           static_cast<float>(z) * 0.17f, 2, 2.2f, 0.5f, seed + 3187u);
  float radiusScale = 0.85f + noise * 0.3f;
  for (const CaveSegment& segment : segments) {
    if (!segment.entrance && y > top - 4) {
      continue;
    }
    float radius = segment.radius * radiusScale;
    float distSq = distanceSqToSegment(p, segment.start, segment.end);
    if (distSq <= radius * radius) {
      return true;
    }
  }
  return false;
}

struct LakeParams {
  bool enabled;
  int centerX;
  int centerZ;
  int centerY;
  float radius;
  int maxDepth;
  int seaLevel;
};

float lakeRand(int cx, int cz, int salt, uint32_t seed) {
  int hx = cx * 29 + salt * 131;
  int hz = cz * 43 + salt * 97;
  return hash01(hx, hz, seed + 0x7f4a7c15u);
}

LakeParams lakeForChunk(int cx, int cz, uint32_t seed) {
  LakeParams lake = {};
  if (lakeRand(cx, cz, 1, seed) > kLakeChance) {
    lake.enabled = false;
    return lake;
  }
  float centerLX = 5.0f + lakeRand(cx, cz, 2, seed) * 5.0f;
  float centerLZ = 5.0f + lakeRand(cx, cz, 3, seed) * 5.0f;
  float radius = 3.5f + lakeRand(cx, cz, 4, seed) * 1.6f;
  int maxDepth = 2 + static_cast<int>(lakeRand(cx, cz, 5, seed) * 3.0f);
  lake.enabled = true;
  lake.centerX = cx * kChunkSize + static_cast<int>(centerLX);
  lake.centerZ = cz * kChunkSize + static_cast<int>(centerLZ);
  lake.radius = radius;
  lake.maxDepth = maxDepth;

  float biome = 0.0f;
  int centerTop = static_cast<int>(terrainHeightAt(lake.centerX, lake.centerZ, seed, biome));
  lake.centerY = std::max(2, std::min(kWorldY - 3, centerTop));

  int rimMin = kWorldY - 1;
  constexpr int kRimSamples = 8;
  for (int i = 0; i < kRimSamples; ++i) {
    float angle = static_cast<float>(i) / static_cast<float>(kRimSamples) * 6.2831853f;
    int rx = lake.centerX + static_cast<int>(std::cos(angle) * (lake.radius + 1.0f));
    int rz = lake.centerZ + static_cast<int>(std::sin(angle) * (lake.radius + 1.0f));
    float rimBiome = 0.0f;
    int rimTop = static_cast<int>(terrainHeightAt(rx, rz, seed, rimBiome));
    rimMin = std::min(rimMin, rimTop);
  }
  int seaLevel = std::min(rimMin - 1, lake.centerY - 1);
  seaLevel = std::max(2, std::min(kWorldY - 3, seaLevel));
  int minFloor = lake.centerY - lake.maxDepth;
  if (seaLevel <= minFloor + 1) {
    lake.enabled = false;
    return lake;
  }
  lake.seaLevel = seaLevel;
  return lake;
}

bool lakeContains(const LakeParams& lake, int wx, int wz, uint32_t seed) {
  if (!lake.enabled) {
    return false;
  }
  float dx = static_cast<float>(wx - lake.centerX);
  float dz = static_cast<float>(wz - lake.centerZ);
  float distSq = dx * dx + dz * dz;
  float wobble = (hash01(wx, wz, seed + 9187u) - 0.5f) * 1.0f;
  float radius = std::max(1.5f, lake.radius + wobble);
  return distSq <= radius * radius;
}

void carveLakeInChunk(Chunk& chunk, const LakeParams& lake, uint32_t seed) {
  if (!lake.enabled) {
    return;
  }
  int baseX = chunk.cx * kChunkSize;
  int baseZ = chunk.cz * kChunkSize;
  for (int lx = 0; lx < kChunkSize; ++lx) {
    int wx = baseX + lx;
    for (int lz = 0; lz < kChunkSize; ++lz) {
      int wz = baseZ + lz;
      if (!lakeContains(lake, wx, wz, seed)) {
        continue;
      }
      float dx = static_cast<float>(wx - lake.centerX);
      float dz = static_cast<float>(wz - lake.centerZ);
      float dist = std::sqrt(dx * dx + dz * dz);
      float t = std::min(1.0f, dist / lake.radius);
      float depthShape = 1.0f - (t * t);
      int depthJitter = static_cast<int>(hash01(wx, wz, seed + 1223u) * 2.0f);
      int floorY = lake.centerY - static_cast<int>(depthShape * static_cast<float>(lake.maxDepth)) - depthJitter;
      floorY = std::max(1, floorY);
      int topY = lake.seaLevel;
      for (int wy = floorY; wy <= topY; ++wy) {
        setBlockInChunk(chunk, wx, wy, wz, BlockWater);
      }
      float biome = 0.0f;
      int top = static_cast<int>(terrainHeightAt(wx, wz, seed, biome));
      if (top < 1) {
        top = 1;
      }
      for (int wy = lake.seaLevel + 1; wy <= top; ++wy) {
        setBlockInChunk(chunk, wx, wy, wz, BlockAir);
      }
    }
  }
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
  bool forest = forestNoise > 0.66f && localNoise > 0.55f && scatter > 0.45f;
  bool lone = forestNoise > 0.5f && localNoise > 0.7f && scatter > 0.88f;
  return top > 4 && top < kWorldY - 6 && biome > 0.25f && (forest || lone);
}

int treeHeightAt(int x, int z, uint32_t seed, bool dense) {
  int height = 3 + static_cast<int>(hash01(x + 17, z + 31, seed) * 4.0f);
  if (dense) {
    height += 1;
  }
  return height;
}

float treeScoreAt(int x, int z, uint32_t seed) {
  return hash01(x, z, seed + 9127u);
}

bool isTreeSpacingClear(int x, int z, int top, float biome, uint32_t seed) {
  constexpr int kTreeSpacing = 6;
  float score = treeScoreAt(x, z, seed);
  int radiusSq = kTreeSpacing * kTreeSpacing;
  for (int dz = -kTreeSpacing; dz <= kTreeSpacing; ++dz) {
    for (int dx = -kTreeSpacing; dx <= kTreeSpacing; ++dx) {
      if (dx == 0 && dz == 0) {
        continue;
      }
      if (dx * dx + dz * dz > radiusSq) {
        continue;
      }
      int nx = x + dx;
      int nz = z + dz;
      float neighborScore = treeScoreAt(nx, nz, seed);
      if (neighborScore < score - 1e-5f) {
        continue;
      }
      float neighborBiome = 0.0f;
      int neighborTop = static_cast<int>(terrainHeightAt(nx, nz, seed, neighborBiome));
      if (!shouldPlaceTree(nx, nz, neighborTop, neighborBiome, seed)) {
        continue;
      }
      if (neighborScore > score + 1e-5f) {
        return false;
      }
      if (neighborScore >= score && (dx < 0 || (dx == 0 && dz < 0))) {
        return false;
      }
    }
  }
  return true;
}

void removeInteriorBlocks(Chunk& chunk);

void generateChunk(Chunk& chunk, uint32_t seed) {
  chunk.blocks.assign(kChunkVolume, static_cast<uint8_t>(BlockAir));
  int baseX = chunk.cx * kChunkSize;
  int baseY = chunk.cy * kChunkSize;
  int baseZ = chunk.cz * kChunkSize;
  std::vector<CaveSegment> caveSegments;
  buildCaveSegmentsForChunk(chunk, seed, caveSegments);
  LakeParams lake = lakeForChunk(chunk.cx, chunk.cz, seed);

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
        if (shouldCarveCave(caveSegments, wx, wy, wz, top, seed)) {
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

  carveLakeInChunk(chunk, lake, seed);

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
      if (lakeContains(lake, x, z, seed)) {
        continue;
      }
      float forestNoise = fbmPerlin(static_cast<float>(x) * 0.006f, static_cast<float>(z) * 0.006f, 3, 2.0f, 0.5f);
      float density = (forestNoise - 0.5f) * 1.1f;
      bool dense = density > 0.35f;
      if (!shouldPlaceTree(x, z, top, biome, seed)) {
        continue;
      }
      if (!isTreeSpacingClear(x, z, top, biome, seed)) {
        continue;
      }
      int height = treeHeightAt(x, z, seed, dense);
      placeTreeInChunk(chunk, x, z, top, height);
    }
  }

  removeInteriorBlocks(chunk);
}

void removeInteriorBlocks(Chunk& chunk) {
  if (chunk.blocks.empty()) {
    return;
  }

  for (int lx = 0; lx < kChunkSize; ++lx) {
    for (int ly = 0; ly < kChunkSize; ++ly) {
      for (int lz = 0; lz < kChunkSize; ++lz) {
        BlockType type = blockAtLocal(chunk, lx, ly, lz);
        if (type == BlockAir) {
          continue;
        }

        bool hasAirNeighbor = false;
        const int neighbors[6][3] = {
          {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
        };

        for (const auto& n : neighbors) {
          int nlx = lx + n[0];
          int nly = ly + n[1];
          int nlz = lz + n[2];

          if (nlx < 0 || nlx >= kChunkSize || nly < 0 || nly >= kChunkSize || nlz < 0 || nlz >= kChunkSize) {
            hasAirNeighbor = true;
            break;
          }

          BlockType neighbor = blockAtLocal(chunk, nlx, nly, nlz);
          if (neighbor == BlockAir) {
            hasAirNeighbor = true;
            break;
          }
        }

        if (!hasAirNeighbor) {
          chunk.blocks[blockIndex(lx, ly, lz)] = static_cast<uint8_t>(BlockAir);
        }
      }
    }
  }
}

void computeChunkLight(const World& world, Chunk& chunk, bool fullPropagation) {
  chunk.light.assign(kChunkVolume, 0u);

  int baseY = chunk.cy * kChunkSize;
  int topY = baseY + kChunkSize - 1;

  for (int lx = 0; lx < kChunkSize; ++lx) {
    for (int lz = 0; lz < kChunkSize; ++lz) {
      int highestSolid = -1;
      for (int ly = kChunkSize - 1; ly >= 0; --ly) {
        BlockType type = blockAtLocal(chunk, lx, ly, lz);
        if (isLightBlocking(type)) {
          highestSolid = ly;
          break;
        }
      }

      for (int ly = 0; ly < kChunkSize; ++ly) {
        int index = blockIndex(lx, ly, lz);
        BlockType type = blockAtLocal(chunk, lx, ly, lz);
        
        if (type == BlockTorch) {
          chunk.light[index] = static_cast<uint8_t>(kMaxLightLevel);
          continue;
        }
        if (type == BlockAir) {
          int wy = baseY + ly;
          if (ly > highestSolid) {
            chunk.light[index] = static_cast<uint8_t>(kMaxLightLevel);
          } else {
            chunk.light[index] = 2;
          }
        } else {
          int wy = baseY + ly;
          if (ly == highestSolid) {
            chunk.light[index] = static_cast<uint8_t>(kMaxLightLevel);
          } else {
            int depthFromTop = (kWorldY - 1) - wy;
            int light = kMaxLightLevel - (depthFromTop / 6);
            if (light < 3) light = 3;
            chunk.light[index] = static_cast<uint8_t>(light);
          }
        }
      }
    }
  }

  int propagationPasses = fullPropagation ? 3 : 1;
  for (int pass = 0; pass < propagationPasses; ++pass) {
    for (int ly = kChunkSize - 1; ly >= 0; --ly) {
      for (int lz = 0; lz < kChunkSize; ++lz) {
        for (int lx = 0; lx < kChunkSize; ++lx) {
          int index = blockIndex(lx, ly, lz);
          BlockType type = blockAtLocal(chunk, lx, ly, lz);
          
          if (isLightBlocking(type)) {
            continue;
          }
          
          uint8_t currentLight = chunk.light[index];
          uint8_t maxNeighborLight = currentLight;
          
          const int neighbors[6][3] = {
            {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, -1, 0}
          };
          
          for (const auto& n : neighbors) {
            int nlx = lx + n[0];
            int nly = ly + n[1];
            int nlz = lz + n[2];
            
            if (nlx < 0 || nlx >= kChunkSize || 
                nly < 0 || nly >= kChunkSize || 
                nlz < 0 || nlz >= kChunkSize) {
              continue;
            }
            
            BlockType neighborType = blockAtLocal(chunk, nlx, nly, nlz);
            if (isLightBlocking(neighborType)) {
              continue;
            }
            
            uint8_t neighborLight = chunk.light[blockIndex(nlx, nly, nlz)];
            if (neighborLight > maxNeighborLight) {
              maxNeighborLight = neighborLight;
            }
          }
          
          if (maxNeighborLight > currentLight + 1) {
            uint8_t newLight = maxNeighborLight - 1;
            if (newLight > currentLight) {
              chunk.light[index] = newLight;
            }
          }
        }
      }
    }
  }
}

struct TempBatch {
  unsigned int textureId;
  bool transparent;
  bool animated;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

TempBatch& getBatch(std::vector<TempBatch>& batches, unsigned int textureId, bool transparent, bool animated) {
  for (auto& batch : batches) {
    if (batch.textureId == textureId && batch.transparent == transparent && batch.animated == animated) {
      return batch;
    }
  }
  batches.push_back(TempBatch{textureId, transparent, animated, {}, {}});
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

  int estimatedFaces = (size * size * size) * 3;
  chunk.vertices.reserve(estimatedFaces * 4);
  chunk.indices.reserve(estimatedFaces * 6);

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
            neighborRegionIsFullySolid(chunk, chunkXPos, lx, ly, lz, lodStep, 1, 0, 0, type)) {
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
      TempBatch& batch = getBatch(tempBatches, key.textureId, key.transparent, key.animated);
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
            neighborRegionIsFullySolid(chunk, chunkXNeg, lx, ly, lz, lodStep, -1, 0, 0, type)) {
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
      TempBatch& batch = getBatch(tempBatches, key.textureId, key.transparent, key.animated);
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
            neighborRegionIsFullySolid(chunk, chunkYPos, lx, ly, lz, lodStep, 0, 1, 0, type)) {
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
      TempBatch& batch = getBatch(tempBatches, key.textureId, key.transparent, key.animated);
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
            neighborRegionIsFullySolid(chunk, chunkYNeg, lx, ly, lz, lodStep, 0, -1, 0, type)) {
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
      TempBatch& batch = getBatch(tempBatches, key.textureId, key.transparent, key.animated);
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
            neighborRegionIsFullySolid(chunk, chunkZPos, lx, ly, lz, lodStep, 0, 0, 1, type)) {
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
      TempBatch& batch = getBatch(tempBatches, key.textureId, key.transparent, key.animated);
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
            neighborRegionIsFullySolid(chunk, chunkZNeg, lx, ly, lz, lodStep, 0, 0, -1, type)) {
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
      TempBatch& batch = getBatch(tempBatches, key.textureId, key.transparent, key.animated);
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
  if (chunk.vertices.capacity() < totalVerts) {
    chunk.vertices.reserve(totalVerts);
  }
  if (chunk.indices.capacity() < totalIndices) {
    chunk.indices.reserve(totalIndices);
  }

  std::sort(tempBatches.begin(), tempBatches.end(),
            [](const TempBatch& a, const TempBatch& b) {
              if (a.transparent != b.transparent) {
                return a.transparent < b.transparent;
              }
              if (a.animated != b.animated) {
                return a.animated < b.animated;
              }
              return a.textureId < b.textureId;
            });

  for (auto& batch : tempBatches) {
    if (batch.vertices.empty() || batch.indices.empty()) {
      continue;
    }
    MeshBatch out = {};
    out.textureId = batch.textureId;
    out.indexOffset = static_cast<int>(chunk.indices.size());
    out.indexCount = static_cast<int>(batch.indices.size());
    out.transparent = batch.transparent;
    out.animated = batch.animated;
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

void enqueueMeshBuild(World& world, const TextureAssets& textures, Chunk& chunk, int lodStep) {
  ChunkCoord coord = chunkCoord(chunk.cx, chunk.cy, chunk.cz);
  if (world.queuedMeshes.find(coord) != world.queuedMeshes.end()) {
    return;
  }
  
  uint64_t meshKey = (chunk.contentHash << 3) | (static_cast<uint64_t>(lodStep) & 0x7);
  bool recomputedLighting = false;
  if (chunk.light.size() != kChunkVolume) {
    computeChunkLight(world, chunk, false);
    chunk.lastMeshHash = 0;
    recomputedLighting = true;
  }

  if (!recomputedLighting && chunk.lastMeshHash == meshKey && !chunk.vertices.empty()) {
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
  world.enableOcclusionCulling = false;
  world.loadedChunkCount = 0;
  world.maxLoadedChunks = 20000;
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
  recomputeChunkMetadata(chunk);
  computeChunkLight(world, chunk, false);
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
  chunk.maxHeight = 0;
  chunk.faceOpenMask = 0;
  chunk.contentHash = 0;
  chunk.lastMeshHash = 0;
  chunk.isEmpty = true;
  chunk.isSolid = false;
  chunk.lastAccessTime = 0.0;
  chunk.blocks.assign(kChunkVolume, static_cast<uint8_t>(BlockAir));
  generateChunk(chunk, seed);
  recomputeChunkMetadata(chunk);
  
  return chunk;
}

template <typename T>
bool isFutureReady(const std::future<T>& future) {
  return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

int getColumnMaxHeight(const World& world, int cx, int cz) {
  int64_t key = (static_cast<int64_t>(cx) << 32) | static_cast<int64_t>(cz);
  auto it = world.heightmapCache.find(key);
  if (it != world.heightmapCache.end()) {
    return it->second;
  }
  
  int maxHeight = 0;
  for (int cy = 0; cy < kChunksY; ++cy) {
    const Chunk* chunk = findChunkInternal(world, cx, cy, cz);
    if (chunk && chunk->maxHeight > maxHeight) {
      maxHeight = chunk->maxHeight;
    }
  }
  return maxHeight;
}

void updateHeightmapCache(World& world, int cx, int cz) {
  int maxHeight = 0;
  for (int cy = 0; cy < kChunksY; ++cy) {
    const Chunk* chunk = findChunkInternal(world, cx, cy, cz);
    if (chunk && chunk->maxHeight > maxHeight) {
      maxHeight = chunk->maxHeight;
    }
  }
  int64_t key = (static_cast<int64_t>(cx) << 32) | static_cast<int64_t>(cz);
  world.heightmapCache[key] = maxHeight;
}

bool isChunkOccluded(const World& world, Vec3 playerPos, int targetCx, int targetCy, int targetCz) {
  int playerChunkX = floorDiv(worldToBlock(playerPos.x), kChunkSize);
  int playerChunkZ = floorDiv(worldToBlock(playerPos.z), kChunkSize);
  int playerY = worldToBlock(playerPos.y);
  
  int dx = targetCx - playerChunkX;
  int dz = targetCz - playerChunkZ;
  int distSq = dx * dx + dz * dz;
  if (distSq <= 4) {
    return false;
  }
  
  int targetHeight = getColumnMaxHeight(world, targetCx, targetCz);
  int targetChunkMinY = targetCy * kChunkSize;
  int targetChunkMaxY = (targetCy + 1) * kChunkSize - 1;
  
  if (targetChunkMinY > targetHeight) {
    return false;
  }
  
  float centerX = static_cast<float>(targetCx * kChunkSize) + kChunkHalfSize * kBlockSize;
  float centerZ = static_cast<float>(targetCz * kChunkSize) + kChunkHalfSize * kBlockSize;
  float centerY = static_cast<float>((targetChunkMinY + targetChunkMaxY) / 2) * kBlockSize;
  
  Vec3 toTarget = {centerX - playerPos.x, centerY - playerPos.y, centerZ - playerPos.z};
  float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
  
  if (distance < 0.1f) {
    return false;
  }

  toTarget.x /= distance;
  toTarget.y /= distance;
  toTarget.z /= distance;
  int steps = static_cast<int>(distance / (kChunkSize * kBlockSize)) + 1;
  if (steps > 20) steps = 20;
  
  for (int i = 1; i < steps; ++i) {
    float t = (distance * static_cast<float>(i)) / static_cast<float>(steps);
    float sampleX = playerPos.x + toTarget.x * t;
    float sampleZ = playerPos.z + toTarget.z * t;
    float sampleY = playerPos.y + toTarget.y * t;
    
    int sampleCx = floorDiv(worldToBlock(sampleX), kChunkSize);
    int sampleCz = floorDiv(worldToBlock(sampleZ), kChunkSize);
    int sampleWy = worldToBlock(sampleY);

    int sampleCy = floorDiv(sampleWy, kChunkSize);
    const Chunk* sampleChunk = findChunkInternal(world, sampleCx, sampleCy, sampleCz);
    if (sampleChunk && !sampleChunk->isEmpty && !sampleChunk->blocks.empty()) {
      int dxToTarget = targetCx - sampleCx;
      int dyToTarget = targetCy - sampleChunk->cy;
      int dzToTarget = targetCz - sampleCz;
      int adx = std::abs(dxToTarget);
      int ady = std::abs(dyToTarget);
      int adz = std::abs(dzToTarget);
      uint8_t faceMask = 0u;
      if (adx >= ady && adx >= adz) {
        faceMask = dxToTarget > 0 ? kFaceXPos : kFaceXNeg;
      } else if (ady >= adx && ady >= adz) {
        faceMask = dyToTarget > 0 ? kFaceYPos : kFaceYNeg;
      } else {
        faceMask = dzToTarget > 0 ? kFaceZPos : kFaceZNeg;
      }

      if ((sampleChunk->faceOpenMask & faceMask) == 0u) {
        return true;
      }
    }
    
    int terrainHeight = getColumnMaxHeight(world, sampleCx, sampleCz);
    
    if (terrainHeight > sampleWy + kChunkSize && terrainHeight > playerY) {
      if (terrainHeight >= targetChunkMaxY) return true;
    }
  }
  
  return false;
}

bool isChunkInFrustum(Vec3 chunkCenter, Vec3 cameraPos, Vec3 cameraFront, Vec3 cameraRight, Vec3 cameraUp,
                      float tanHalfFovX, float tanHalfFovY) {
  Vec3 toChunk = {chunkCenter.x - cameraPos.x, chunkCenter.y - cameraPos.y, chunkCenter.z - cameraPos.z};
  float distance = std::sqrt(toChunk.x * toChunk.x + toChunk.y * toChunk.y + toChunk.z * toChunk.z);
  
  if (distance < 0.1f) {
    return true;
  }
  
  toChunk.x /= distance;
  toChunk.y /= distance;
  toChunk.z /= distance;
  
  float forward = dot(toChunk, cameraFront);
  if (forward < -0.2f) {
    return false;
  }
  
  float right = dot(toChunk, cameraRight);
  float maxRight = tanHalfFovX * forward + kChunkRadius / distance;
  if (std::abs(right) > maxRight) {
    return false;
  }
  
  float up = dot(toChunk, cameraUp);
  float maxUp = tanHalfFovY * forward + kChunkRadius / distance;
  if (std::abs(up) > maxUp) {
    return false;
  }
  
  return true;
}

void unloadDistantChunks(World& world, int pcx, int pcy, int pcz) {
  if (world.chunks.size() < world.maxLoadedChunks) {
    return;
  }
  std::vector<ChunkCoord> toUnload;
  int unloadRadius = world.renderDistance + kFarLodRing + 2;
  
  for (const auto& entry : world.chunks) {
    const Chunk& chunk = entry.second;
    int dist = std::abs(chunk.cx - pcx);
    dist = std::max(dist, std::abs(chunk.cy - pcy));
    dist = std::max(dist, std::abs(chunk.cz - pcz));
    
    if (dist > unloadRadius) {
      toUnload.push_back(entry.first);
    }
  }

  for (const ChunkCoord& coord : toUnload) {
    auto it = world.chunks.find(coord);
    if (it != world.chunks.end()) {
      if (it->second.vbo != 0) {
        glDeleteBuffers(1, &it->second.vbo);
      }
      if (it->second.ibo != 0) {
        glDeleteBuffers(1, &it->second.ibo);
      }
      world.chunks.erase(it);
    }
  }
  
  world.loadedChunkCount = world.chunks.size();
}

void updateWorldChunks(World& world, const TextureAssets& textures, Vec3 playerPosition,
                       Vec3 cameraFront, Vec3 cameraUp, Vec3 cameraRight,
                       float fovDegrees, float aspect, double timeNow) {
  int wx = worldToBlock(playerPosition.x);
  int wz = worldToBlock(playerPosition.z);
  int wy = worldToBlock(playerPosition.y);
  int pcx = floorDiv(wx, kChunkSize);
  int pcy = floorDiv(wy, kChunkSize);
  int pcz = floorDiv(wz, kChunkSize);

  unloadDistantChunks(world, pcx, pcy, pcz);

  int meshApplied = 0;
  int maxMeshPerFrame = kMeshBuildPerFrame * 3;
  int nearbyPassLimit = maxMeshPerFrame / 2;
  int nearbyProcessed = 0;
  
  for (auto it = world.meshTasks.begin(); it != world.meshTasks.end() && nearbyProcessed < nearbyPassLimit; ) {
    if (!isFutureReady(it->future)) {
      ++it;
      continue;
    }
    int dist = std::abs(it->coord.cx - pcx) + std::abs(it->coord.cy - pcy) + std::abs(it->coord.cz - pcz);
    if (dist > 2) {
      ++it;
      continue;
    }
    
    MeshBuildResult result = it->future.get();
    world.queuedMeshes.erase(result.coord);
    Chunk* chunk = findChunkInternal(world, result.coord.cx, result.coord.cy, result.coord.cz);
    if (chunk && chunk->lodStep == result.lodStep && chunk->blocks.size() > 0) {
      chunk->vertices = std::move(result.vertices);
      chunk->indices = std::move(result.indices);
      chunk->batches = std::move(result.batches);
      chunk->meshDirty = true;
      chunk->lastMeshHash = (chunk->contentHash << 3) | (static_cast<uint64_t>(result.lodStep) & 0x7);
    }
    it = world.meshTasks.erase(it);
    nearbyProcessed++;
    meshApplied++;
  }
  
  for (auto it = world.meshTasks.begin(); it != world.meshTasks.end() && meshApplied < maxMeshPerFrame; ) {
    if (!isFutureReady(it->future)) {
      ++it;
      continue;
    }
    
    MeshBuildResult result = it->future.get();
    world.queuedMeshes.erase(result.coord);
    Chunk* chunk = findChunkInternal(world, result.coord.cx, result.coord.cy, result.coord.cz);
    if (chunk && chunk->lodStep == result.lodStep && chunk->blocks.size() > 0) {
      chunk->vertices = std::move(result.vertices);
      chunk->indices = std::move(result.indices);
      chunk->batches = std::move(result.batches);
      chunk->meshDirty = true;
      chunk->lastMeshHash = (chunk->contentHash << 3) | (static_cast<uint64_t>(result.lodStep) & 0x7);
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
      World tempWorld;
      tempWorld.chunks[key] = chunk;
      computeChunkLight(tempWorld, tempWorld.chunks[key], false);
      chunk = std::move(tempWorld.chunks[key]);
      
      auto result = world.chunks.emplace(key, std::move(chunk));
      
      updateHeightmapCache(world, result.first->second.cx, result.first->second.cz);
      
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
        int distSq = dx * dx + dz * dz;
        if (distSq > maxRadius * maxRadius) {
          continue;
        }
        int cx = pcx + dx;
        int cz = pcz + dz;
        for (int cy = minCy; cy <= maxCy; ++cy) {
          ChunkCoord coord = chunkCoord(cx, cy, cz);
          
          float centerX = static_cast<float>(cx * kChunkSize) * kBlockSize + kChunkHalfSize;
          float centerY = static_cast<float>(cy * kChunkSize) * kBlockSize + kChunkHalfSize;
          float centerZ = static_cast<float>(cz * kChunkSize) * kBlockSize + kChunkHalfSize;
          Vec3 chunkCenter = {centerX, centerY, centerZ};
          
          if (useFrustum && radius > 1) {
            if (!isChunkInFrustum(chunkCenter, playerPosition, cameraFront, cameraRight, cameraUp,
                                 tanHalfFovX, tanHalfFovY)) {
              continue;
            }
          }
          
          bool occluded = false;
          if (world.enableOcclusionCulling && radius > 3) {
            occluded = isChunkOccluded(world, playerPosition, cx, cy, cz);
          }
          
          if (!occluded) {
            Chunk* chunk = findChunkInternal(world, cx, cy, cz);
            if (chunk) {
              chunk->lastAccessTime = timeNow;
              world.visibleChunks.push_back(coord);
            } else {
              world.pendingChunks.push_back(coord);
            }
          }
        }
      }
    }
  }

  int meshScheduled = 0;
  int maxMeshSchedule = kMeshBuildPerFrame * 2;
  for (const ChunkCoord& coord : world.visibleChunks) {
    if (meshScheduled >= maxMeshSchedule) {
      break;
    }
    Chunk* chunk = findChunkInternal(world, coord.cx, coord.cy, coord.cz);
    if (!chunk || chunk->blocks.empty()) {
      continue;
    }
    int dist = std::abs(coord.cx - pcx);
    dist = std::max(dist, std::abs(coord.cy - pcy));
    dist = std::max(dist, std::abs(coord.cz - pcz));
    int lodStep = lodStepForDistance(dist);
    bool needsLight = chunk->light.size() != kChunkVolume;
    bool lodChanged = (chunk->lodStep != lodStep);
    bool missingMesh = chunk->vertices.empty() || chunk->indices.empty();

    if (needsLight || lodChanged || missingMesh) {
      chunk->lodStep = lodStep;
      enqueueMeshBuild(world, textures, *chunk, lodStep);
      meshScheduled++;
    }
  }

  unsigned int workerCount = std::thread::hardware_concurrency();
  if (workerCount == 0) {
    workerCount = 2;
  }
  workerCount = std::min<unsigned int>(workerCount, 4);
  int maxInFlight = static_cast<int>(workerCount) * 2;
  int available = maxInFlight - static_cast<int>(world.buildTasks.size());
  int budget = std::max(kChunkBuildPerFrame * 2, static_cast<int>(workerCount) * 2);
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
    recomputeChunkMetadata(entry.second);
    computeChunkLight(world, entry.second, false);
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
    BlockType type = blockAtInternal(world, ix, wy, iz);
    if (type != BlockAir && type != BlockWater) {
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
  int lx = floorMod(wx, kChunkSize);
  int ly = floorMod(wy, kChunkSize);
  int lz = floorMod(wz, kChunkSize);
  int idx = blockIndex(lx, ly, lz);
  uint8_t oldValue = chunk->blocks[idx];
  uint8_t newValue = static_cast<uint8_t>(type);
  setBlockInternal(*chunk, wx, wy, wz, type);
  
  computeChunkLight(world, *chunk, true);
  recomputeChunkMetadata(*chunk);

  if (lx == 0 || lx == kChunkSize - 1 || 
      ly == 0 || ly == kChunkSize - 1 ||
      lz == 0 || lz == kChunkSize - 1) {
    static const int kNeighborOffsets[26][3] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
      {1, 1, 0}, {1, -1, 0}, {-1, 1, 0}, {-1, -1, 0},
      {1, 0, 1}, {1, 0, -1}, {-1, 0, 1}, {-1, 0, -1},
      {0, 1, 1}, {0, 1, -1}, {0, -1, 1}, {0, -1, -1},
      {1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {1, -1, -1},
      {-1, 1, 1}, {-1, 1, -1}, {-1, -1, 1}, {-1, -1, -1},
    };
    
    for (const auto& offset : kNeighborOffsets) {
      int nx = cx + offset[0];
      int ny = cy + offset[1];
      int nz = cz + offset[2];
      if (ny >= 0 && ny < kChunksY) {
        Chunk* neighbor = findChunkInternal(world, nx, ny, nz);
        if (neighbor && !neighbor->blocks.empty()) {
          computeChunkLight(world, *neighbor, true);
        }
      }
    }
  }
  
  if (newValue != static_cast<uint8_t>(BlockAir)) {
    chunk->isEmpty = false;
  }
  chunk->contentHash ^= blockHashContribution(static_cast<uint32_t>(idx), oldValue);
  chunk->contentHash ^= blockHashContribution(static_cast<uint32_t>(idx), newValue);
  
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
    if (!chunk || chunk->blocks.empty()) {
      continue;
    }
    ChunkCoord coord = chunkCoord(nx, ny, nz);
    if (world.queuedMeshes.find(coord) != world.queuedMeshes.end()) {
      continue;
    }
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
  return type != BlockAir && type != BlockStick && type != BlockWater;
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
