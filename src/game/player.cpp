#include "game/player.h"

namespace {
constexpr float kMoveSpeed = 5.0f;
constexpr float kSprintMultiplier = 1.7f;
constexpr float kSpeedBoostMultiplier = 10.0f;
constexpr float kGravity = -18.0f;
constexpr float kJumpVelocity = 7.35f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kPlayerRadius = 0.35f;
constexpr float kCollisionEpsilon = 0.001f;
constexpr float kResolveStep = 0.1f;
constexpr float kResolveMax = 2.5f;
constexpr float kResolveMaxDown = 0.6f;
constexpr double kDoubleTapWindow = 0.25;

const Vec3 kWorldUp = {0.0f, 1.0f, 0.0f};
}

float playerRadius() {
  return kPlayerRadius;
}

float playerHeight() {
  return kPlayerHeight;
}

static void updateCameraVectors(Player& player) {
  if (player.pitch > 89.9f) {
    player.pitch = 89.9f;
  }
  if (player.pitch < -89.9f) {
    player.pitch = -89.9f;
  }
  
  Vec3 front;
  front.x = std::cos(radians(player.yaw)) * std::cos(radians(player.pitch));
  front.y = std::sin(radians(player.pitch));
  front.z = std::sin(radians(player.yaw)) * std::cos(radians(player.pitch));
  player.front = normalize(front);
  
  Vec3 right = cross(player.front, kWorldUp);
  float rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
  
  if (rightLen > 0.0001f) {
    player.right = {right.x / rightLen, right.y / rightLen, right.z / rightLen};
  } else {
    player.right = {1.0f, 0.0f, 0.0f};
  }
  
  player.up = normalize(cross(player.right, player.front));
}

void initPlayer(Player& player) {
  player.position = {0.0f, 20.0f, 30.0f};
  player.front = {0.0f, 0.0f, -1.0f};
  player.up = kWorldUp;
  player.right = {1.0f, 0.0f, 0.0f};
  player.yaw = -90.0f;
  player.pitch = -15.0f;

  player.verticalVelocity = 0.0f;
  player.flyMode = false;
  player.grounded = false;
  player.prevJumpDown = false;
  player.prevForwardDown = false;
  player.lastJumpTap = -10.0;
  player.lastForwardTap = -10.0;
  player.speedBoostEnabled = false;

  player.firstMouse = true;
  player.lastMouseX = 0.0f;
  player.lastMouseY = 0.0f;

  updateCameraVectors(player);
}

void handlePlayerMouse(Player& player, double xpos, double ypos) {
  if (player.firstMouse) {
    player.lastMouseX = xpos;
    player.lastMouseY = ypos;
    player.firstMouse = false;
  }

  float xoffset = static_cast<float>(xpos - player.lastMouseX);
  float yoffset = static_cast<float>(player.lastMouseY - ypos);
  player.lastMouseX = xpos;
  player.lastMouseY = ypos;

  float sensitivity = 0.1f;
  xoffset *= sensitivity;
  yoffset *= sensitivity;

  player.yaw += xoffset;
  player.pitch += yoffset;

  if (player.pitch > 89.0f) player.pitch = 89.0f;
  if (player.pitch < -89.0f) player.pitch = -89.0f;

  updateCameraVectors(player);
}

static bool isSolidBlock(BlockType type) {
  return type != BlockAir && type != BlockStick && type != BlockWater && type != BlockTorch;
}

static bool collidesAt(const World& world, const Vec3& position) {
  float minX = position.x - kPlayerRadius + kCollisionEpsilon;
  float maxX = position.x + kPlayerRadius - kCollisionEpsilon;
  float minY = position.y - kPlayerHeight + kCollisionEpsilon;
  float maxY = position.y - kCollisionEpsilon;
  float minZ = position.z - kPlayerRadius + kCollisionEpsilon;
  float maxZ = position.z + kPlayerRadius - kCollisionEpsilon;

  int minXi = static_cast<int>(std::floor(minX));
  int maxXi = static_cast<int>(std::floor(maxX));
  int minYi = static_cast<int>(std::floor(minY));
  int maxYi = static_cast<int>(std::floor(maxY));
  int minZi = static_cast<int>(std::floor(minZ));
  int maxZi = static_cast<int>(std::floor(maxZ));

  for (int y = minYi; y <= maxYi; ++y) {
    for (int x = minXi; x <= maxXi; ++x) {
      for (int z = minZi; z <= maxZi; ++z) {
        if (isSolidBlock(blockAt(world, x, y, z))) {
          return true;
        }
      }
    }
  }
  return false;
}

static bool resolvePenetration(Player& player, const World& world) {
  if (!collidesAt(world, player.position)) {
    return false;
  }

  Vec3 base = player.position;
  for (float offset = 0.0f; offset <= kResolveMax; offset += kResolveStep) {
    Vec3 test = base;
    test.y += offset;
    if (!collidesAt(world, test)) {
      player.position = test;
      return true;
    }
  }

  for (float offset = kResolveStep; offset <= kResolveMaxDown; offset += kResolveStep) {
    Vec3 test = base;
    test.y -= offset;
    if (!collidesAt(world, test)) {
      player.position = test;
      return true;
    }
  }

  const Vec3 directions[] = {
    {1.0f, 0.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -1.0f},
    {0.7071f, 0.0f, 0.7071f},
    {-0.7071f, 0.0f, 0.7071f},
    {0.7071f, 0.0f, -0.7071f},
    {-0.7071f, 0.0f, -0.7071f},
  };

  for (float radius = kResolveStep; radius <= kResolveMax; radius += kResolveStep) {
    for (const Vec3& dir : directions) {
      Vec3 planar = {dir.x * radius, 0.0f, dir.z * radius};
      for (float yOffset = 0.0f; yOffset <= kResolveMax; yOffset += kResolveStep) {
        Vec3 test = {base.x + planar.x, base.y + yOffset, base.z + planar.z};
        if (!collidesAt(world, test)) {
          player.position = test;
          return true;
        }
        if (yOffset > 0.0f && yOffset <= kResolveMaxDown) {
          test.y = base.y - yOffset;
          if (!collidesAt(world, test)) {
            player.position = test;
            return true;
          }
        }
      }
    }
  }

  return false;
}

void movePlayer(Player& player, const World& world, const PlayerInput& input, float deltaTime) {
  resolvePenetration(player, world);

  if (input.forward && !player.prevForwardDown) {
    if (input.timeNow - player.lastForwardTap <= kDoubleTapWindow) {
      player.speedBoostEnabled = !player.speedBoostEnabled;
    }
    player.lastForwardTap = input.timeNow;
  }
  player.prevForwardDown = input.forward;

  float speedMul = (input.sprint ? kSprintMultiplier : 1.0f);
  if (player.speedBoostEnabled) {
    speedMul *= kSpeedBoostMultiplier;
  }
  float speed = kMoveSpeed * speedMul;
  float velocity = speed * deltaTime;

  Vec3 flatFront = normalize({player.front.x, 0.0f, player.front.z});
  Vec3 flatRight = normalize(cross(flatFront, kWorldUp));
  Vec3 move = {0.0f, 0.0f, 0.0f};

  if (input.forward) {
    move = move + flatFront;
  }
  if (input.back) {
    move = move - flatFront;
  }
  if (input.left) {
    move = move - flatRight;
  }
  if (input.right) {
    move = move + flatRight;
  }

  if (dot(move, move) > 1.0e-8f) {
    move = normalize(move) * velocity;
    Vec3 next = player.position;
    next.x += move.x;
    if (!collidesAt(world, next)) {
      player.position.x = next.x;
    }

    next = player.position;
    next.z += move.z;
    if (!collidesAt(world, next)) {
      player.position.z = next.z;
    }
  }

  if (input.jumpDown && !player.prevJumpDown) {
    if (input.timeNow - player.lastJumpTap <= kDoubleTapWindow) {
      player.flyMode = !player.flyMode;
      player.verticalVelocity = 0.0f;
      player.grounded = false;
    }

    player.lastJumpTap = input.timeNow;

    if (!player.flyMode && player.grounded) {
      player.verticalVelocity = kJumpVelocity;
      player.grounded = false;
    }
  }
  player.prevJumpDown = input.jumpDown;

  if (player.flyMode) {
    float flyDelta = 0.0f;
    if (input.jumpDown) {
      flyDelta += velocity;
    }
    if (input.flyDown) {
      flyDelta -= velocity;
    }
    if (flyDelta != 0.0f) {
      Vec3 next = player.position;
      next.y += flyDelta;
      if (!collidesAt(world, next)) {
        player.position.y = next.y;
      }
    }
  }
}

void updatePlayerPhysics(Player& player, const World& world, float deltaTime) {
  resolvePenetration(player, world);
  if (player.flyMode) {
    player.verticalVelocity = 0.0f;
    return;
  }

  player.verticalVelocity += kGravity * deltaTime;
  float deltaY = player.verticalVelocity * deltaTime;
  player.grounded = false;
  if (deltaY != 0.0f) {
    float remaining = std::abs(deltaY);
    float direction = deltaY > 0.0f ? 1.0f : -1.0f;
    float step = 0.05f;
    while (remaining > 0.0f) {
      float move = std::min(step, remaining) * direction;
      Vec3 next = player.position;
      next.y += move;
      if (!collidesAt(world, next)) {
        player.position.y = next.y;
        remaining -= std::abs(move);
        continue;
      }

      if (direction < 0.0f) {
        player.verticalVelocity = 0.0f;
        player.grounded = true;
      } else {
        player.verticalVelocity = 0.0f;
      }
      break;
    }
  }

  resolvePenetration(player, world);
}

void syncPlayerCamera(Player& player) {
  updateCameraVectors(player);
}

bool playerIntersectsBlock(const Player& player, int wx, int wy, int wz) {
  float minX = player.position.x - kPlayerRadius + kCollisionEpsilon;
  float maxX = player.position.x + kPlayerRadius - kCollisionEpsilon;
  float minY = player.position.y - kPlayerHeight + kCollisionEpsilon;
  float maxY = player.position.y - kCollisionEpsilon;
  float minZ = player.position.z - kPlayerRadius + kCollisionEpsilon;
  float maxZ = player.position.z + kPlayerRadius - kCollisionEpsilon;

  float bx0 = static_cast<float>(wx);
  float bx1 = bx0 + 1.0f;
  float by0 = static_cast<float>(wy);
  float by1 = by0 + 1.0f;
  float bz0 = static_cast<float>(wz);
  float bz1 = bz0 + 1.0f;

  bool overlapX = maxX > bx0 && minX < bx1;
  bool overlapY = maxY > by0 && minY < by1;
  bool overlapZ = maxZ > bz0 && minZ < bz1;
  return overlapX && overlapY && overlapZ;
}
