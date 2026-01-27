#pragma once

#include "core/math.h"
#include <cstdint>
#include <memory>

struct Chunk;
class World;
struct WorldLight;
struct Vertex;

namespace MetalRT {

struct RaytracerConfig {
  uint32_t samplesPerPixel = 1;
  uint32_t maxBounces = 3;
  bool enableDenoising = true;
  bool enableAccumulation = false;
  float exposure = 1.0f;
};

class MetalRaytracer {
public:
  MetalRaytracer();
  ~MetalRaytracer();
  
  bool initialize(int windowWidth, int windowHeight);
  void shutdown();
  
  void uploadWorldMeshes(const World& world);
  
  void renderFrame(const Vec3& cameraPos, const Mat4& viewProj, 
                   const WorldLight& light, float timeNow);
  
  uint32_t getOutputTexture() const;
  
  void setConfig(const RaytracerConfig& config);
  
  void resize(int windowWidth, int windowHeight);
  
  struct Stats {
    uint32_t raysPerSecond = 0;
    uint32_t trianglesInScene = 0;
    float averageRayTracingTime = 0.0f;
  };
  Stats getStats() const { return m_stats; }
  
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  RaytracerConfig m_config;
  Stats m_stats;
};

} // namespace MetalRT
