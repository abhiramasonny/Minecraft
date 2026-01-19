#pragma once

#include "game/world.h"

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
