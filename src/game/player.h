#pragma once

#include "core/math.h"
#include "game/world.h"

struct Player {
  Vec3 position;
  Vec3 front;
  Vec3 up;
  Vec3 right;
  float yaw;
  float pitch;
  float verticalVelocity;
  bool flyMode;
  bool grounded;
  bool prevJumpDown;
  bool prevForwardDown;
  double lastJumpTap;
  double lastForwardTap;
  bool speedBoostEnabled;
  bool firstMouse;
  double lastMouseX;
  double lastMouseY;
};

struct PlayerInput {
  bool forward;
  bool back;
  bool left;
  bool right;
  bool jumpDown;
  bool flyDown;
  bool sprint;
  double timeNow;
};

void initPlayer(Player& player);
void handlePlayerMouse(Player& player, double xpos, double ypos);
void movePlayer(Player& player, const World& world, const PlayerInput& input, float deltaTime);
void updatePlayerPhysics(Player& player, const World& world, float deltaTime);
void syncPlayerCamera(Player& player);
bool playerIntersectsBlock(const Player& player, int wx, int wy, int wz);

float playerRadius();
float playerHeight();
