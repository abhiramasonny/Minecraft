#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>

#include "game/inventory.h"
#include "core/math.h"
#include "game/player.h"
#include "render/renderer.h"
#include "app/save.h"
#include "render/textures.h"
#include "game/ui.h"
#include "game/world.h"

struct AppState {
  Player player;
  World world;
  TextureAssets textures;
  Inventory inventory;
  UiState ui;
  bool prevBreakDown;
  bool prevPlaceDown;
  bool showChunkBounds;
  bool showWireframe;
  bool showCoords;
  bool prevBoundsDown;
  bool prevWireframeDown;
  bool prevCoordsDown;
};

static constexpr float kBlockReach = 6.0f;
static constexpr float kDayLength = 120.0f;
static constexpr float kFovDegrees = 60.0f;
static constexpr float kFarPlane = 600.0f;
static constexpr float kChunkSize = 16.0f;

static WorldLight updateSun(double timeNow) {
  float cycle = static_cast<float>(std::fmod(timeNow, kDayLength) / kDayLength);
  float angle = cycle * 2.0f * 3.1415926f;
  float sunX = std::cos(angle);
  float sunY = std::sin(angle);
  float sunZ = 0.2f;

  float dayFactor = std::max(0.0f, sunY);
  float ambient = 0.15f + dayFactor * 0.35f;
  float diffuse = 0.2f + dayFactor * 0.8f;

  float lightDir[4] = {-sunX, -sunY, -sunZ, 0.0f};
  float ambientColor[4] = {ambient, ambient, ambient + 0.05f, 1.0f};
  float diffuseColor[4] = {diffuse, diffuse * 0.95f, diffuse * 0.9f, 1.0f};

  glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
  glLightfv(GL_LIGHT0, GL_AMBIENT, ambientColor);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseColor);

  float skyR = 0.08f + dayFactor * 0.4f;
  float skyG = 0.1f + dayFactor * 0.45f;
  float skyB = 0.16f + dayFactor * 0.55f;
  glClearColor(skyR, skyG, skyB, 1.0f);

  WorldLight light = {};
  light.direction = normalize({-sunX, -sunY, -sunZ});
  light.ambient = {ambientColor[0], ambientColor[1], ambientColor[2]};
  light.diffuse = {diffuseColor[0], diffuseColor[1], diffuseColor[2]};
  return light;
}

static void toggleFlag(bool down, bool& prevDown, bool& flag) {
  if (down && !prevDown) {
    flag = !flag;
  }
  prevDown = down;
}

static void updateWindowTitle(GLFWwindow* window, const AppState& app) {
  if (!app.showCoords) {
    glfwSetWindowTitle(window, "Renderer");
    return;
  }

  int bx = static_cast<int>(std::floor(app.player.position.x));
  int by = static_cast<int>(std::floor(app.player.position.y));
  int bz = static_cast<int>(std::floor(app.player.position.z));
  int cx = static_cast<int>(std::floor(app.player.position.x / kChunkSize));
  int cy = static_cast<int>(std::floor(app.player.position.y / kChunkSize));
  int cz = static_cast<int>(std::floor(app.player.position.z / kChunkSize));

  char title[128];
  std::snprintf(title, sizeof(title),
                "Renderer | Pos %.1f %.1f %.1f | Block %d %d %d | Chunk %d %d %d",
                app.player.position.x, app.player.position.y, app.player.position.z,
                bx, by, bz, cx, cy, cz);
  glfwSetWindowTitle(window, title);
}

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
  (void)window;
  glViewport(0, 0, width, height);
}

static void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
  AppState* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (!app) {
    return;
  }

  updateUiMouse(app->ui, xpos, ypos);
  if (app->ui.inventoryOpen) {
    return;
  }

  handlePlayerMouse(app->player, xpos, ypos);
}

static void initApp(AppState& app) {
  initPlayer(app.player);
  initWorld(app.world);
  initInventory(app.inventory);
  initUi(app.ui);
  app.prevBreakDown = false;
  app.prevPlaceDown = false;
  app.showChunkBounds = false;
  app.showWireframe = false;
  app.showCoords = false;
  app.prevBoundsDown = false;
  app.prevWireframeDown = false;
  app.prevCoordsDown = false;
}

static void tryBreakBlock(AppState& app) {
  int hitX = 0;
  int hitY = 0;
  int hitZ = 0;
  int faceX = 0;
  int faceY = 0;
  int faceZ = 0;

  if (!raycastBlock(app.world, app.player.position, app.player.front, kBlockReach,
                    hitX, hitY, hitZ, faceX, faceY, faceZ)) {
    return;
  }

  BlockType hitType = blockAt(app.world, hitX, hitY, hitZ);
  if (hitType == BlockAir) {
    return;
  }

  if (setBlockAt(app.world, hitX, hitY, hitZ, BlockAir)) {
    addToInventory(app.inventory, hitType, 1);
    rebuildChunksAround(app.world, app.textures, hitX, hitY, hitZ);
  }
}

static void tryPlaceBlock(AppState& app) {
  BlockType placeType = selectedBlock(app.inventory);
  if (!isPlaceableBlock(placeType)) {
    return;
  }

  int hitX = 0;
  int hitY = 0;
  int hitZ = 0;
  int faceX = 0;
  int faceY = 0;
  int faceZ = 0;

  if (!raycastBlock(app.world, app.player.position, app.player.front, kBlockReach,
                    hitX, hitY, hitZ, faceX, faceY, faceZ)) {
    return;
  }

  if (faceX == 0 && faceY == 0 && faceZ == 0) {
    return;
  }

  int placeX = hitX + faceX;
  int placeY = hitY + faceY;
  int placeZ = hitZ + faceZ;

  if (blockAt(app.world, placeX, placeY, placeZ) != BlockAir) {
    return;
  }

  if (playerIntersectsBlock(app.player, placeX, placeY, placeZ)) {
    return;
  }

  if (!consumeSelected(app.inventory, 1)) {
    return;
  }

  if (setBlockAt(app.world, placeX, placeY, placeZ, placeType)) {
    rebuildChunksAround(app.world, app.textures, placeX, placeY, placeZ);
  } else {
    addToInventory(app.inventory, placeType, 1);
  }
}

static void handleInventoryToggle(GLFWwindow* window, AppState& app, bool toggled) {
  if (!toggled) {
    return;
  }

  if (app.ui.inventoryOpen) {
    app.ui.openedFromTable = false;
    setCraftingSize(app.inventory, 2);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
  } else {
    app.ui.openedFromTable = false;
    setCraftingSize(app.inventory, 2);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) {
      glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
  }

  app.player.firstMouse = true;
}

static void openInventoryFromTable(GLFWwindow* window, AppState& app) {
  if (app.ui.inventoryOpen) {
    return;
  }
  app.ui.inventoryOpen = true;
  app.ui.openedFromTable = true;
  setCraftingSize(app.inventory, 3);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
  app.player.firstMouse = true;
}

static bool tryOpenCraftingTable(GLFWwindow* window, AppState& app) {
  int hitX = 0;
  int hitY = 0;
  int hitZ = 0;
  int faceX = 0;
  int faceY = 0;
  int faceZ = 0;

  if (!raycastBlock(app.world, app.player.position, app.player.front, kBlockReach,
                    hitX, hitY, hitZ, faceX, faceY, faceZ)) {
    return false;
  }

  if (blockAt(app.world, hitX, hitY, hitZ) != BlockCraftingTable) {
    return false;
  }

  openInventoryFromTable(window, app);
  return true;
}

static void updateHotbarSelection(AppState& app, GLFWwindow* window) {
  for (int i = 0; i < kHotbarSlots; ++i) {
    int key = GLFW_KEY_1 + i;
    if (glfwGetKey(window, key) == GLFW_PRESS) {
      setSelectedSlot(app.inventory, i);
    }
  }
}

int main() {
  if (!glfwInit()) {
    std::fprintf(stderr, "Failed to initialize GLFW\n");
    return 1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "Renderer", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return 1;
  }

  AppState app = {};
  initApp(app);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(window, &app);

  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
  glfwSetCursorPosCallback(window, mouseCallback);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported()) {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glEnable(GL_COLOR_MATERIAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CW);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  glShadeModel(GL_SMOOTH);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glClearColor(0.08f, 0.09f, 0.12f, 1.0f);

  initRenderer();

  loadTextures(app.textures);
  bool loaded = loadGame("save/world.bin", app.world, app.player, app.inventory, app.textures);
  if (!loaded) {
    float surface = surfaceHeightAt(app.world, app.player.position.x, app.player.position.z);
    app.player.position.y = surface + 2.0f;
  }
  int initWidth = 0;
  int initHeight = 0;
  glfwGetFramebufferSize(window, &initWidth, &initHeight);
  float initAspect = (initHeight > 0) ? static_cast<float>(initWidth) / static_cast<float>(initHeight) : 1.0f;
  updateWorldChunks(app.world, app.textures, app.player.position,
                    app.player.front, app.player.up, app.player.right,
                    kFovDegrees, initAspect);
  rebuildWorldMeshes(app.world, app.textures);

  double lastFrame = glfwGetTime();

  while (!glfwWindowShouldClose(window)) {
    double currentFrame = glfwGetTime();
    float deltaTime = static_cast<float>(currentFrame - lastFrame);
    lastFrame = currentFrame;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    bool toggleDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    bool toggled = toggleInventory(app.ui, toggleDown);
    handleInventoryToggle(window, app, toggled);

    toggleFlag(glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS, app.prevBoundsDown, app.showChunkBounds);
    toggleFlag(glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS, app.prevWireframeDown, app.showWireframe);
    toggleFlag(glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS, app.prevCoordsDown, app.showCoords);

    updateHotbarSelection(app, window);

    bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    bool shiftDown = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
      || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    int windowW = 0;
    int windowH = 0;
    glfwGetWindowSize(window, &windowW, &windowH);
    if (windowW > 0 && windowH > 0) {
      app.ui.scaleX = static_cast<float>(width) / static_cast<float>(windowW);
      app.ui.scaleY = static_cast<float>(height) / static_cast<float>(windowH);
    } else {
      app.ui.scaleX = 1.0f;
      app.ui.scaleY = 1.0f;
    }
    if (width == 0 || height == 0) {
      glfwPollEvents();
      continue;
    }

    if (app.ui.inventoryOpen) {
      setCraftingSize(app.inventory, app.ui.openedFromTable ? 3 : 2);
    }

    handleInventoryClick(app.ui, app.inventory, width, height, mouseDown, rightDown, shiftDown);

    bool breakDown = mouseDown;
    bool placeDown = rightDown;
    if (!app.ui.inventoryOpen) {
      if (breakDown && !app.prevBreakDown) {
        tryBreakBlock(app);
      }
      if (placeDown && !app.prevPlaceDown) {
        if (!tryOpenCraftingTable(window, app)) {
          tryPlaceBlock(app);
        }
      }
    }
    app.prevBreakDown = breakDown;
    app.prevPlaceDown = placeDown;

    if (!app.ui.inventoryOpen) {
      PlayerInput input = {};
      input.forward = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
      input.back = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
      input.left = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
      input.right = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
      input.jumpDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
      input.flyDown = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
      input.sprint = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS && input.forward;
      input.timeNow = currentFrame;

      movePlayer(app.player, app.world, input, deltaTime);
    } else {
      app.player.prevJumpDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    }

    updatePlayerPhysics(app.player, app.world, deltaTime);

    float aspect = static_cast<float>(width) / static_cast<float>(height);
    updateWorldChunks(app.world, app.textures, app.player.position,
                      app.player.front, app.player.up, app.player.right,
                      kFovDegrees, aspect);

    updateWindowTitle(window, app);

    Mat4 proj = perspective(kFovDegrees, aspect, 0.1f, kFarPlane);
    Mat4 view = lookAt(app.player.position, app.player.position + app.player.front, app.player.up);

    glViewport(0, 0, width, height);
    WorldLight worldLight = updateSun(currentFrame);
    setWorldLight(worldLight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    bool wireframe = app.showWireframe;
    if (wireframe) {
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      glDisable(GL_CULL_FACE);
    }

    uploadVisibleChunkMeshes(app.world);
    drawWorld(app.world);

    if (wireframe) {
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      glEnable(GL_CULL_FACE);
    }

    if (app.showChunkBounds) {
      drawChunkBounds(app.world);
    }
    if (!app.ui.inventoryOpen) {
      int hitX = 0;
      int hitY = 0;
      int hitZ = 0;
      int faceX = 0;
      int faceY = 0;
      int faceZ = 0;
      if (raycastBlock(app.world, app.player.position, app.player.front, kBlockReach,
                       hitX, hitY, hitZ, faceX, faceY, faceZ)) {
        if (blockAt(app.world, hitX, hitY, hitZ) != BlockAir) {
          drawBlockOutline(hitX, hitY, hitZ);
        }
      }
    }
    drawUi(app.inventory, app.textures, app.ui, width, height);

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  saveGame("save/world.bin", app.world, app.player, app.inventory);
  shutdownRenderer();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
