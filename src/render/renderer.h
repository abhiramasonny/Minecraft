#pragma once

#include "game/world.h"
#include "render/metal_raytracer.h"
#include <memory>

struct WorldLight {
  Vec3 direction;
  Vec3 ambient;
  Vec3 diffuse;
};

void initRenderer();
void shutdownRenderer();
void setWorldLight(const WorldLight& light);
void uploadVisibleChunkMeshes(World& world);
void drawWorld(const World& world, double timeNow);
void drawBlockOutline(int wx, int wy, int wz);
void drawChunkBounds(const World& world);

void enableRaytracing(bool enabled);
bool isRaytracingEnabled();
void setRaytracingConfig(const MetalRT::RaytracerConfig& config);
MetalRT::MetalRaytracer* getRaytracer();
