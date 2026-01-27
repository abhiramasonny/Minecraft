#pragma once

#include <cstdint>
#include <vector>
#include "core/math.h"

struct Chunk;
struct World;

class LightingSystem {
 public:
  static void Initialize();

  static void ComputeChunkLighting(const World& world, Chunk& chunk, bool fullPropagation = false);

  static void UpdateLightAroundBlock(const World& world, int wx, int wy, int wz);

  static uint8_t GetLightAt(const World& world, int wx, int wy, int wz);

 private:
  static uint8_t RaytraceDirectLight(const World& world, int wx, int wy, int wz, 
                                     int& stepsToSky);

  static uint8_t ComputeScatteredLight(const World& world, const Chunk& chunk, 
                                      int lx, int ly, int lz);

  static bool IsBlockSolid(const World& world, int wx, int wy, int wz);

  static uint8_t GetEmissionAt(const Chunk& chunk, int lx, int ly, int lz);

  static constexpr int kMaxLightLevel = 15;
  static constexpr int kWorldY = 192;
  static constexpr int kChunkSize = 16;
  static constexpr float kLightDecayPerBlock = 1.0f;
  static constexpr int kMaxRayDistance = 32;
};
