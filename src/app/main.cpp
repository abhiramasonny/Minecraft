#include <GLFW/glfw3.h>

#include <cstdio>

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
};

static constexpr float kBlockReach = 6.0f;

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
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glClearColor(0.08f, 0.09f, 0.12f, 1.0f);

  loadTextures(app.textures);
  bool loaded = loadGame("save/world.bin", app.world, app.player, app.inventory, app.textures);
  if (!loaded) {
    float surface = surfaceHeightAt(app.world, app.player.position.x, app.player.position.z);
    app.player.position.y = surface + 2.0f;
  }
  updateWorldChunks(app.world, app.textures, app.player.position);
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

    updateHotbarSelection(app, window);

    updateWorldChunks(app.world, app.textures, app.player.position);

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
    Mat4 proj = perspective(60.0f, aspect, 0.1f, 600.0f);
    Mat4 view = lookAt(app.player.position, app.player.position + app.player.front, app.player.up);

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    drawWorld(app.world);
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
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
