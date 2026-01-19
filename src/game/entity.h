#pragma once

#include "core/math.h"
#include "game/world.h"
#include <vector>
#include <memory>

enum EntityType {
  EntityCow = 0,
};

struct Entity {
  EntityType type;
  Vec3 position;
  Vec3 velocity;
  float yaw;
  float pitch;
  float width;
  float height;
  bool onGround;
  float walkTimer;
  float idleTimer;
  
  Vec3 targetPosition;
  float stateTimer;
};

struct EntityManager {
  std::vector<Entity> entities;
  unsigned int cowTexture;
};

void initEntityManager(EntityManager& manager);
void spawnCow(EntityManager& manager, Vec3 position);
void updateEntities(EntityManager& manager, const World& world, const Vec3& playerPos, float deltaTime);
void renderEntities(const EntityManager& manager, Vec3 cameraPos);
void loadEntityTextures(EntityManager& manager);
