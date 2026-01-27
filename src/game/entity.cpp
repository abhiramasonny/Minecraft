#include "game/entity.h"
#include "game/world.h"
#include "game/player.h"
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
constexpr float kCowAvoidRange = 4.0f;
constexpr float kCowCuriousRange = 10.0f;
constexpr float kCowStopDistance = 1.2f;
constexpr float kCowTurnSpeed = 3.2f; //radians/sec
constexpr float kCowTexWidth = 64.0f;
constexpr float kCowTexHeight = 32.0f;
float gCowTexWidth = kCowTexWidth;
float gCowTexHeight = kCowTexHeight;

struct TexRect {
  float u0;
  float v0; //bottom
  float u1;
  float v1; //top
};

struct BoxUv {
  TexRect right;
  TexRect left;
  TexRect front;
  TexRect back;
  TexRect top;
  TexRect bottom;
};

unsigned int createSolidTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
  unsigned char data[4] = {r, g, b, a};
  unsigned int id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);
  return id;
}

float randomFloat(float min, float max) {
  return min + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * (max - min);
}

float normalizeAngle(float a) {
  while (a > 3.14159265359f) a -= 6.28318530718f;
  while (a < -3.14159265359f) a += 6.28318530718f;
  return a;
}

float approachAngle(float current, float target, float maxDelta) {
  float delta = normalizeAngle(target - current);
  if (delta > maxDelta) delta = maxDelta;
  if (delta < -maxDelta) delta = -maxDelta;
  return normalizeAngle(current + delta);
}

Vec3 randomWalkTarget(Vec3 origin, float radius) {
  float angle = randomFloat(0.0f, 6.28318530718f);
  float dist = randomFloat(radius * 0.3f, radius);
  return {origin.x + std::cos(angle) * dist, origin.y, origin.z + std::sin(angle) * dist};
}

TexRect texRect(float x0, float y0, float x1, float y1) {
  float u0 = x0 / gCowTexWidth;
  float u1 = x1 / gCowTexWidth;
  float vTop = 1.0f - (y0 / gCowTexHeight);
  float vBottom = 1.0f - (y1 / gCowTexHeight);
  return {u0, vBottom, u1, vTop};
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
  glTexCoord2f(uv.u0, uv.v0); glVertex3f(bl.x, bl.y, bl.z);
  glTexCoord2f(uv.u1, uv.v0); glVertex3f(br.x, br.y, br.z);
  glTexCoord2f(uv.u1, uv.v1); glVertex3f(tr.x, tr.y, tr.z);
  glTexCoord2f(uv.u0, uv.v1); glVertex3f(tl.x, tl.y, tl.z);
}

void drawTexturedBox(const Vec3& min, const Vec3& max, const BoxUv& uv) {
  glBegin(GL_QUADS);
  // Front (+Z)
  glNormal3f(0.0f, 0.0f, 1.0f);
  emitQuad(uv.front,
           {min.x, min.y, max.z},
           {max.x, min.y, max.z},
           {max.x, max.y, max.z},
           {min.x, max.y, max.z});
  // Back (-Z)
  glNormal3f(0.0f, 0.0f, -1.0f);
  emitQuad(uv.back,
           {max.x, min.y, min.z},
           {min.x, min.y, min.z},
           {min.x, max.y, min.z},
           {max.x, max.y, min.z});
  // Right (+X)
  glNormal3f(1.0f, 0.0f, 0.0f);
  emitQuad(uv.right,
           {max.x, min.y, max.z},
           {max.x, min.y, min.z},
           {max.x, max.y, min.z},
           {max.x, max.y, max.z});
  //Left (-X)
  glNormal3f(-1.0f, 0.0f, 0.0f);
  emitQuad(uv.left,
           {min.x, min.y, min.z},
           {min.x, min.y, max.z},
           {min.x, max.y, max.z},
           {min.x, max.y, min.z});
  //Top (+Y)
  glNormal3f(0.0f, 1.0f, 0.0f);
  emitQuad(uv.top,
           {min.x, max.y, max.z},
           {max.x, max.y, max.z},
           {max.x, max.y, min.z},
           {min.x, max.y, min.z});
  // Bottom (-Y)
  glNormal3f(0.0f, -1.0f, 0.0f);
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

  cow.stateTimer -= deltaTime;

  if (cow.stateTimer <= 0.0f) {
    cow.targetPosition = randomWalkTarget(cow.position, 10.0f);
    cow.targetPosition.y = surfaceHeightAt(world, cow.targetPosition.x, cow.targetPosition.z);
    cow.stateTimer = randomFloat(kCowWalkMin, kCowWalkMax);

    if (playerDistSq < kCowCuriousRange * kCowCuriousRange) {
      float curiosity = randomFloat(0.0f, 1.0f);
      if (curiosity < 0.2f) {
        cow.targetPosition = {playerPos.x, surfaceHeightAt(world, playerPos.x, playerPos.z), playerPos.z};
      }
    }
  }

  if (playerDistSq < kCowAvoidRange * kCowAvoidRange) {
    Vec3 away = {-toPlayer.x, 0.0f, -toPlayer.z};
    if (dot(away, away) > 1.0e-6f) {
      away = normalize(away);
      cow.targetPosition = {cow.position.x + away.x * 6.0f,
                            cow.position.y,
                            cow.position.z + away.z * 6.0f};
      cow.targetPosition.y = surfaceHeightAt(world, cow.targetPosition.x, cow.targetPosition.z);
      cow.stateTimer = randomFloat(1.2f, 2.5f);
    }
  }

  Vec3 toTarget = {cow.targetPosition.x - cow.position.x, 0.0f, cow.targetPosition.z - cow.position.z};
  float distSq = dot(toTarget, toTarget);

  if (distSq > 1.0e-6f) {
    float desiredYaw = std::atan2(toTarget.z, toTarget.x);
    cow.yaw = approachAngle(cow.yaw, desiredYaw, kCowTurnSpeed * deltaTime);
  }

  float stopDistanceSq = kCowStopDistance * kCowStopDistance;
  if (distSq > stopDistanceSq) {
    float speed = kCowWalkSpeed;
    cow.velocity.x = -std::cos(cow.yaw) * speed;
    cow.velocity.z = -std::sin(cow.yaw) * speed;
    cow.walkTimer += deltaTime * 4.0f;
  } else {
    cow.velocity.x *= kEntityDrag;
    cow.velocity.z *= kEntityDrag;
    if (distSq <= stopDistanceSq) {
      cow.velocity.x = 0.0f;
      cow.velocity.z = 0.0f;
      cow.stateTimer = std::max(cow.stateTimer, randomFloat(kCowIdleMin, kCowIdleMax));
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
  if (!valid) {
    ground = surfaceHeightAt(world, entity.position.x, entity.position.z);
    valid = true;
  }
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
  drawTexturedBox({-headW * 0.5f, headY, headZ - headD},
                  {headW * 0.5f, headY + headH, headZ}, makeBoxUv(0.0f, 0.0f, 8.0f, 8.0f, 6.0f));
  
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

    drawTexturedBox({-legW, 0.0f, -legW}, {legW, legH, legW}, makeBoxUv(0.0f, 16.0f, 4.0f, 12.0f, 4.0f));
    
    glPopMatrix();
  }
  
  glFrontFace(GL_CW);
  glPopMatrix();
}

void drawHitbox(const Entity& entity) {
  float halfX = entity.width * 0.5f;
  float halfZ = entity.length * 0.5f;
  float x0 = entity.position.x - halfX;
  float y0 = entity.position.y;
  float z0 = entity.position.z - halfZ;
  float x1 = entity.position.x + halfX;
  float y1 = entity.position.y + entity.height;
  float z1 = entity.position.z + halfZ;

  glBegin(GL_LINES);
  glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
  glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1);
  glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
  glVertex3f(x0, y0, z1); glVertex3f(x0, y0, z0);
  glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
  glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1);
  glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
  glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);
  glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0);
  glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
  glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1);
  glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
  glEnd();
}

} // namespace

void initEntityManager(EntityManager& manager) {
  manager.entities.clear();
}

void spawnCow(EntityManager& manager, Vec3 position) {
  Entity cow = {};
  cow.type = EntityCow;
  cow.position = position;
  cow.velocity = {0.0f, 0.0f, 0.0f};
  cow.yaw = randomFloat(0.0f, 6.28318530718f);
  cow.pitch = 0.0f;
  cow.width = 0.95f;
  cow.length = 1.8f;
  cow.height = 1.35f;
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
  if (manager.entities.empty()) {
    return;
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  for (const Entity& entity : manager.entities) {
    float dx = entity.position.x - cameraPos.x;
    float dz = entity.position.z - cameraPos.z;
    float distSq = dx * dx + dz * dz;
    if (distSq > 100.0f * 100.0f) {
      continue;
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glLineWidth(2.0f);
    glColor4f(1.0f, 0.25f, 0.25f, 0.85f);
    drawHitbox(entity);
    
    if (entity.type == EntityCow && manager.cowTexture != 0) {
      glEnable(GL_TEXTURE_2D);
      glDisable(GL_CULL_FACE);
      glDisable(GL_LIGHTING);
      glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
      glDisable(GL_BLEND);
      drawCow(entity, manager.cowTexture);
      glEnable(GL_BLEND);
      glDisable(GL_TEXTURE_2D);
    }
  }
  
  glDisable(GL_BLEND);
  glLineWidth(1.0f);
  glEnable(GL_CULL_FACE);
  glEnable(GL_LIGHTING);
  glEnable(GL_TEXTURE_2D);
}

void loadEntityTextures(EntityManager& manager) {
  stbi_set_flip_vertically_on_load(true);

  int width, height, channels;
  const char* path = "textures/entity/cow/cow.png";
  unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
  if (!data) {
    path = "textures/entity/cow/cow_v2.png";
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
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
  } else if (manager.cowTexture == 0) {
    manager.cowTexture = createSolidTexture(255, 0, 255, 255);
  }
}

static bool rangesOverlap(float a0, float a1, float b0, float b1) {
  return a1 > b0 && a0 < b1;
}

void resolvePlayerEntityCollisions(Player& player, const EntityManager& manager) {
  if (manager.entities.empty()) {
    return;
  }

  float pr = playerRadius();
  float ph = playerHeight();
  constexpr float eps = 0.001f;

  float pMinY = player.position.y - ph + eps;
  float pMaxY = player.position.y - eps;

  for (int iter = 0; iter < 4; ++iter) {
    bool anyResolved = false;

    for (const Entity& e : manager.entities) {
      float eMinY = e.position.y;
      float eMaxY = e.position.y + e.height;
      if (!rangesOverlap(pMinY, pMaxY, eMinY, eMaxY)) {
        continue;
      }

      float eHalfX = e.width * 0.5f;
      float eHalfZ = e.length * 0.5f;

      float pMinX = player.position.x - pr + eps;
      float pMaxX = player.position.x + pr - eps;
      float pMinZ = player.position.z - pr + eps;
      float pMaxZ = player.position.z + pr - eps;

      float eMinX = e.position.x - eHalfX;
      float eMaxX = e.position.x + eHalfX;
      float eMinZ = e.position.z - eHalfZ;
      float eMaxZ = e.position.z + eHalfZ;

      if (!rangesOverlap(pMinX, pMaxX, eMinX, eMaxX) || !rangesOverlap(pMinZ, pMaxZ, eMinZ, eMaxZ)) {
        continue;
      }

      float overlapX = std::min(pMaxX, eMaxX) - std::max(pMinX, eMinX);
      float overlapZ = std::min(pMaxZ, eMaxZ) - std::max(pMinZ, eMinZ);
      if (overlapX <= 0.0f || overlapZ <= 0.0f) {
        continue;
      }

      if (overlapX < overlapZ) {
        float dir = (player.position.x < e.position.x) ? -1.0f : 1.0f;
        player.position.x += dir * (overlapX + eps);
      } else {
        float dir = (player.position.z < e.position.z) ? -1.0f : 1.0f;
        player.position.z += dir * (overlapZ + eps);
      }

      anyResolved = true;
      pMinY = player.position.y - ph + eps;
      pMaxY = player.position.y - eps;
    }

    if (!anyResolved) {
      break;
    }
  }
}
