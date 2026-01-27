#include "game/lighting.h"
#include "game/world.h"

#include <algorithm>
#include <cmath>

extern bool isLightBlocking(BlockType type);

namespace {
constexpr int kChunkSize = 16;
constexpr int kChunksY = 12;
constexpr int kWorldY = kChunkSize * kChunksY;
constexpr int kMaxLightLevel = 15;
constexpr int kMaxRayDistance = 32;

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

bool isLightBlocking_local(BlockType type) {
  return type != BlockAir && type != BlockStick && type != BlockWater && type != BlockTorch;
}

const Chunk* GetChunkAtWorld(const World& world, int wx, int wy, int wz) {
  int cx = floorDiv(wx, kChunkSize);
  int cy = floorDiv(wy, kChunkSize);
  int cz = floorDiv(wz, kChunkSize);
  
  ChunkCoord key{cx, cy, cz};
  auto it = world.chunks.find(key);
  return it != world.chunks.end() ? &it->second : nullptr;
}

int RaytraceToSky(const World& world, int wx, int wy, int wz) {
  int stepsToSky = 0;
  
  for (int step = 0; step < kMaxRayDistance; ++step) {
    int checkY = wy + step;
    if (checkY >= kWorldY) {
      stepsToSky = step;
      break;
    }
    
    int cx = floorDiv(wx, kChunkSize);
    int cy = floorDiv(checkY, kChunkSize);
    int cz = floorDiv(wz, kChunkSize);
    
    ChunkCoord key{cx, cy, cz};
    auto it = world.chunks.find(key);
    
    if (it == world.chunks.end() || it->second.blocks.empty()) {
      stepsToSky = step;
      continue;
    }
    
    int lx = floorMod(wx, kChunkSize);
    int ly = floorMod(checkY, kChunkSize);
    int lz = floorMod(wz, kChunkSize);
    
    BlockType type = static_cast<BlockType>(it->second.blocks[blockIndex(lx, ly, lz)]);
    if (isLightBlocking_local(type)) {
      return -1;
    }
    stepsToSky = step;
  }
  
  return stepsToSky;
}

uint8_t GetEmissionLevel(BlockType type) {
  switch (type) {
    case BlockTorch:
      return kMaxLightLevel;
    default:
      return 0;
  }
}

}
void LightingSystem::Initialize() {
  //cur no initialization needed
}

uint8_t LightingSystem::RaytraceDirectLight(const World& world, int wx, int wy, int wz,
                                           int& stepsToSky) {
  stepsToSky = RaytraceToSky(world, wx, wy, wz);
  
  if (stepsToSky < 0) {
    return 2;
  }
  
  uint8_t directLight = kMaxLightLevel;
  
  if (stepsToSky > 0) {
    directLight = std::max(static_cast<uint8_t>(13), 
                          static_cast<uint8_t>(directLight - stepsToSky / 8));
  }
  
  return directLight;
}

uint8_t LightingSystem::ComputeScatteredLight(const World& world, const Chunk& chunk,
                                             int lx, int ly, int lz) {
  int wx = chunk.cx * kChunkSize + lx;
  int wy = chunk.cy * kChunkSize + ly;
  int wz = chunk.cz * kChunkSize + lz;
  
  uint8_t maxNeighborLight = 0;
  
  const int directions[6][3] = {
    {1, 0, 0}, {-1, 0, 0},
    {0, 1, 0}, {0, -1, 0},
    {0, 0, 1}, {0, 0, -1}
  };
  
  for (const auto& dir : directions) {
    int nx = wx + dir[0];
    int ny = wy + dir[1];
    int nz = wz + dir[2];
    
    const Chunk* neighborChunk = GetChunkAtWorld(world, nx, ny, nz);
    if (!neighborChunk || neighborChunk->light.empty()) {
      continue;
    }
    
    int nlx = floorMod(nx, kChunkSize);
    int nly = floorMod(ny, kChunkSize);
    int nlz = floorMod(nz, kChunkSize);
    
    uint8_t neighborLight = neighborChunk->light[blockIndex(nlx, nly, nlz)];
    if (neighborLight > 0) {
      neighborLight = std::max(0, static_cast<int>(neighborLight) - 1);
      maxNeighborLight = std::max(maxNeighborLight, neighborLight);
    }
  }
  
  return maxNeighborLight;
}

bool LightingSystem::IsBlockSolid(const World& world, int wx, int wy, int wz) {
  const Chunk* chunk = GetChunkAtWorld(world, wx, wy, wz);
  if (!chunk || chunk->blocks.empty()) {
    return false;
  }
  
  int lx = floorMod(wx, kChunkSize);
  int ly = floorMod(wy, kChunkSize);
  int lz = floorMod(wz, kChunkSize);
  
  BlockType type = static_cast<BlockType>(chunk->blocks[blockIndex(lx, ly, lz)]);
  return isLightBlocking_local(type);
}

uint8_t LightingSystem::GetEmissionAt(const Chunk& chunk, int lx, int ly, int lz) {
  BlockType type = static_cast<BlockType>(chunk.blocks[blockIndex(lx, ly, lz)]);
  return GetEmissionLevel(type);
}

void LightingSystem::ComputeChunkLighting(const World& world, Chunk& chunk,
                                        bool fullPropagation) {
  chunk.light.assign(kChunkSize * kChunkSize * kChunkSize, 0);

  for (int i = 0; i < static_cast<int>(chunk.light.size()); ++i) { //hardcoded
    chunk.light[i] = kMaxLightLevel;
  }
}

void LightingSystem::UpdateLightAroundBlock(const World& world, int wx, int wy, int wz) {
}

uint8_t LightingSystem::GetLightAt(const World& world, int wx, int wy, int wz) {
  const Chunk* chunk = GetChunkAtWorld(world, wx, wy, wz);
  if (!chunk || chunk->light.empty()) {
    return 0;
  }
  
  int lx = floorMod(wx, kChunkSize);
  int ly = floorMod(wy, kChunkSize);
  int lz = floorMod(wz, kChunkSize);
  
  return chunk->light[blockIndex(lx, ly, lz)];
}
