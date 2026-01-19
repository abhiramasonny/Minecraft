#include "game/entity.h"
#include "game/world.h"
#include "core/math.h"
#include "third_party/stb_image.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>

namespace {
constexpr float kGravity = 20.0f;
constexpr float kEntityDrag = 0.91f;
constexpr float kCowWalkSpeed = 1.2f;
constexpr float kCowIdleMin = 2.0f;
constexpr float kCowIdleMax = 5.0f;
constexpr float kCowWalkMin = 3.0f;
constexpr float kCowWalkMax = 6.0f;
constexpr float kCowFollowRange = 18.0f;
constexpr float kCowStopDistance = 2.0f;
constexpr float kCowTexWidth = 64.0f;
constexpr float kCowTexHeight = 32.0f;
float gCowTexWidth = kCowTexWidth;
float gCowTexHeight = kCowTexHeight;

struct TexRect {
  float u0;
  float v0;
  float u1;
  float v1;
};

struct BoxUv {
  TexRect right;
  TexRect left;
  TexRect front;
  TexRect back;
  TexRect top;
  TexRect bottom;
};

float randomFloat(float min, float max) {
  return min + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * (max - min);
}

Vec3 randomWalkTarget(Vec3 origin, float radius) {
  float angle = randomFloat(0.0f, 6.28318530718f);
  float dist = randomFloat(radius * 0.3f, radius);
  return {origin.x + std::cos(angle) * dist, origin.y, origin.z + std::sin(angle) * dist};
}

TexRect texRect(float x0, float y0, float x1, float y1) {
  return {x0 / gCowTexWidth, y0 / gCowTexHeight, x1 / gCowTexWidth, y1 / gCowTexHeight};
}

BoxUv makeBoxUv(float u, float v, float w, float h, float d) {
  return {
    texRect(u + d + w, v + d, u + d + w + d, v + d + h),         // right
    texRect(u, v + d, u + d, v + d + h),                         // left
    texRect(u + d, v + d, u + d + w, v + d + h),                 // front
    texRect(u + d + w + d, v + d, u + d + w + d + w, v + d + h), // back
    texRect(u + d, v, u + d + w, v + d),                         // top
    texRect(u + d + w, v, u + d + w + w, v + d)                  // bottom
  };
}

void emitQuad(const TexRect& uv, Vec3 bl, Vec3 br, Vec3 tr, Vec3 tl) {
  glTexCoord2f(uv.u0, uv.v1); glVertex3f(bl.x, bl.y, bl.z);
  glTexCoord2f(uv.u1, uv.v1); glVertex3f(br.x, br.y, br.z);
  glTexCoord2f(uv.u1, uv.v0); glVertex3f(tr.x, tr.y, tr.z);
  glTexCoord2f(uv.u0, uv.v0); glVertex3f(tl.x, tl.y, tl.z);
}

void drawTexturedBox(const Vec3& min, const Vec3& max, const BoxUv& uv) {
  glBegin(GL_QUADS);
  // Front (+Z)
  emitQuad(uv.front,
           {min.x, min.y, max.z},
           {max.x, min.y, max.z},
           {max.x, max.y, max.z},
           {min.x, max.y, max.z});
  // Back (-Z)
  emitQuad(uv.back,
           {max.x, min.y, min.z},
           {min.x, min.y, min.z},
           {min.x, max.y, min.z},
           {max.x, max.y, min.z});
  // Right (+X)
  emitQuad(uv.right,
           {max.x, min.y, max.z},
           {max.x, min.y, min.z},
           {max.x, max.y, min.z},
           {max.x, max.y, max.z});
  //Left (-X)
  emitQuad(uv.left,
           {min.x, min.y, min.z},
           {min.x, min.y, max.z},
           {min.x, max.y, max.z},
           {min.x, max.y, min.z});
  //Top (+Y)
  emitQuad(uv.top,
           {min.x, max.y, max.z},
           {max.x, max.y, max.z},
           {max.x, max.y, min.z},
           {min.x, max.y, min.z});
  // Bottom (-Y)
  emitQuad(uv.bottom,
           {min.x, min.y, min.z},
           {max.x, min.y, min.z},
           {max.x, min.y, max.z},
           {min.x, min.y, max.z});
  glEnd();
}

void updateCowAI(Entity& cow, const World& world, const Vec3& playerPos, float deltaTime) {
  Vec3 toPlayer = {playerPos.x - cow.position.x, 0.0f, playerPos.z - cow.position.z};
  float playerDistSq = dot(toPlayer, toPlayer);
  bool followingPlayer = playerDistSq < kCowFollowRange * kCowFollowRange;

  cow.stateTimer -= deltaTime;

  if (followingPlayer) {
    cow.targetPosition = {playerPos.x, surfaceHeightAt(world, playerPos.x, playerPos.z), playerPos.z};
    if (cow.stateTimer <= 0.0f) {
      cow.stateTimer = randomFloat(kCowWalkMin, kCowWalkMax);
    }
  } else if (cow.stateTimer <= 0.0f) {
    float velMagSq = dot(cow.velocity, cow.velocity);
    if (velMagSq < 0.01f) {// 0.1 * 0.1 = 0.01
      cow.targetPosition = randomWalkTarget(cow.position, 8.0f);
      cow.targetPosition.y = surfaceHeightAt(world, cow.targetPosition.x, cow.targetPosition.z);
      cow.stateTimer = randomFloat(kCowWalkMin, kCowWalkMax);
      Vec3 toTarget = {cow.targetPosition.x - cow.position.x, 0.0f, cow.targetPosition.z - cow.position.z};
      float distSq = dot(toTarget, toTarget);
      if (distSq > 0.01f) {
        Vec3 dir = normalize(toTarget);
        cow.velocity.x = dir.x * kCowWalkSpeed;
        cow.velocity.z = dir.z * kCowWalkSpeed;
        cow.yaw = std::atan2(dir.z, dir.x);
      }
    } else {
      cow.velocity = {0.0f, cow.velocity.y, 0.0f};
      cow.stateTimer = randomFloat(kCowIdleMin, kCowIdleMax);
    }
  }

  Vec3 toTarget = {cow.targetPosition.x - cow.position.x, 0.0f, cow.targetPosition.z - cow.position.z};
  float distSq = dot(toTarget, toTarget);

  float stopDistanceSq = followingPlayer ? kCowStopDistance * kCowStopDistance : 0.5f;
  if (distSq > stopDistanceSq && cow.stateTimer > 0.0f) {
    Vec3 dir = normalize(toTarget);
    cow.velocity.x = dir.x * kCowWalkSpeed;
    cow.velocity.z = dir.z * kCowWalkSpeed;
    cow.yaw = std::atan2(dir.z, dir.x);
    cow.walkTimer += deltaTime * 4.0f;
  } else {
    cow.velocity.x *= kEntityDrag;
    cow.velocity.z *= kEntityDrag;
    if (distSq <= stopDistanceSq) {
      cow.velocity.x = 0.0f;
      cow.velocity.z = 0.0f;
      cow.stateTimer = randomFloat(kCowIdleMin, kCowIdleMax);
    }
  }
}

void updateEntityPhysics(Entity& entity, const World& world, float deltaTime) {
  if (!entity.onGround) {
    entity.velocity.y -= kGravity * deltaTime;
  }
  
  entity.position.x += entity.velocity.x * deltaTime;
  entity.position.y += entity.velocity.y * deltaTime;
  entity.position.z += entity.velocity.z * deltaTime;
  
  bool valid = false;
  float ground = groundHeightAt(world, entity.position.x, entity.position.y, entity.position.z, valid);
  if (valid && entity.position.y <= ground) {
    entity.position.y = ground;
    entity.velocity.y = 0.0f;
    entity.onGround = true;
  } else {
    entity.onGround = false;
  }
}

void drawCow(const Entity& cow, unsigned int texture) {
  glPushMatrix();
  glTranslatef(cow.position.x, cow.position.y, cow.position.z);
  glRotatef(cow.yaw * 180.0f / 3.14159265359f - 90.0f, 0.0f, 1.0f, 0.0f);
  glFrontFace(GL_CCW);
  
  glBindTexture(GL_TEXTURE_2D, texture);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  
  float bodyW = 0.45f;
  float bodyH = 0.6f;
  float bodyL = 0.9f;
  float bodyY = 0.65f;
  BoxUv bodyUv = makeBoxUv(18.0f, 4.0f, 12.0f, 18.0f, 10.0f);
  drawTexturedBox({-bodyW, bodyY, -bodyL}, {bodyW, bodyY + bodyH, bodyL}, bodyUv);
  
  float headW = 0.4f;
  float headH = 0.4f;
  float headD = 0.3f;
  float headY = bodyY + bodyH * 0.6f;
  float headZ = bodyL + headD * 0.5f;
  BoxUv headUv = makeBoxUv(0.0f, 0.0f, 8.0f, 8.0f, 6.0f);
  drawTexturedBox({-headW * 0.5f, headY, headZ - headD},
                  {headW * 0.5f, headY + headH, headZ}, headUv);
  
  float legW = 0.15f;
  float legH = bodyY - 0.05f;
  float legOffset = bodyW * 0.5f;
  
  float legPositions[4][2] = {
    {-legOffset, bodyL * 0.5f},
    {legOffset, bodyL * 0.5f},
    {-legOffset, -bodyL * 0.5f},
    {legOffset, -bodyL * 0.5f}
  };
  
  for (int i = 0; i < 4; ++i) {
    float bobble = 0.0f;
    float velMagSq = dot(cow.velocity, cow.velocity);
    if (velMagSq > 0.01f) {  //0.1 * 0.1 = 0.01
      bobble = std::sin(cow.walkTimer + i * 3.14159f) * 0.1f;
    }
    
    glPushMatrix();
    glTranslatef(legPositions[i][0], bobble, legPositions[i][1]);

    BoxUv legUv = makeBoxUv(0.0f, 16.0f, 4.0f, 12.0f, 4.0f);
    drawTexturedBox({-legW, 0.0f, -legW}, {legW, legH, legW}, legUv);
    
    glPopMatrix();
  }
  
  glFrontFace(GL_CW);
  glPopMatrix();
}

} // namespace

void initEntityManager(EntityManager& manager) {
  manager.entities.clear();
  manager.cowTexture = 0;
}

void spawnCow(EntityManager& manager, Vec3 position) {
  Entity cow = {};
  cow.type = EntityCow;
  cow.position = position;
  cow.velocity = {0.0f, 0.0f, 0.0f};
  cow.yaw = randomFloat(0.0f, 6.28318530718f);
  cow.pitch = 0.0f;
  cow.width = 0.9f;
  cow.height = 1.4f;
  cow.onGround = false;
  cow.walkTimer = 0.0f;
  cow.idleTimer = 0.0f;
  cow.targetPosition = position;
  cow.stateTimer = randomFloat(kCowIdleMin, kCowIdleMax);
  
  manager.entities.push_back(cow);
}

void updateEntities(EntityManager& manager, const World& world, const Vec3& playerPos, float deltaTime) {
  for (Entity& entity : manager.entities) {
    if (entity.type == EntityCow) {
      updateCowAI(entity, world, playerPos, deltaTime);
    }
    updateEntityPhysics(entity, world, deltaTime);
  }
}

void renderEntities(const EntityManager& manager, Vec3 cameraPos) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  for (const Entity& entity : manager.entities) {
    float dx = entity.position.x - cameraPos.x;
    float dz = entity.position.z - cameraPos.z;
    float distSq = dx * dx + dz * dz;
    if (distSq > 100.0f * 100.0f) {
      continue;
    }
    
    if (entity.type == EntityCow) {
      drawCow(entity, manager.cowTexture);
    }
  }
  
  glDisable(GL_BLEND);
}

void loadEntityTextures(EntityManager& manager) {
  int width, height, channels;
  const char* path = "textures/entity/cow/cow_v2.png";
  unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
  if (!data) {
    path = "textures/entity/cow/cow.png";
    data = stbi_load(path, &width, &height, &channels, 4);
  }
  
  if (data) {
    gCowTexWidth = static_cast<float>(width);
    gCowTexHeight = static_cast<float>(height);
    glGenTextures(1, &manager.cowTexture);
    glBindTexture(GL_TEXTURE_2D, manager.cowTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
  }
}
