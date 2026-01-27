#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>
#include <cctype>
#include <sys/stat.h>
#include <string>
#include <ctime>
#include <algorithm>

#include "game/inventory.h"
#include "core/math.h"
#include "game/player.h"
#include "render/renderer.h"
#include "app/save.h"
#include "render/textures.h"
#include "game/ui.h"
#include "game/world.h"
#include "game/entity.h"

enum class GameMode {
  Launcher,
  Playing,
  Paused,
  Settings,
};

struct AppState {
  Player player;
  World world;
  EntityManager entityManager;
  TextureAssets textures;
  Inventory inventory;
  UiState ui;
  bool dayNightPaused;
  double dayNightTime;
  bool prevDayNightToggleDown;
  bool prevBreakDown;
  bool prevPlaceDown;
  bool prevSpawnCowDown;
  bool showChunkBounds;
  bool showWireframe;
  bool showCoords;
  bool prevBoundsDown;
  bool prevWireframeDown;
  bool prevCoordsDown;
  bool prevEscDown;
  bool prevMenuMouseDown;
  float fovDegrees;
  float currentFPS;
  double fpsUpdateTimer;
  int frameCount;
  int renderDistanceSetting;
  bool saveExists;
  GameMode mode;
  GameMode settingsReturn;
  uint32_t seedSetting;
};

static constexpr float kBlockReach = 6.0f;
static constexpr float kDayLength = 1200.0f;
static constexpr float kFarPlane = 600.0f;
static constexpr float kChunkSize = 16.0f;
static constexpr int kDefaultRenderDistance = 16;
static constexpr int kMinRenderDistance = 8;
static constexpr int kMaxRenderDistance = 32;
static constexpr float kMinFovDegrees = 45.0f;
static constexpr float kMaxFovDegrees = 90.0f;

struct Rect {
  float x;
  float y;
  float w;
  float h;
};

static double wrapDayTime(double t) {
  t = std::fmod(t, static_cast<double>(kDayLength));
  if (t < 0.0) {
    t += static_cast<double>(kDayLength);
  }
  return t;
}

static WorldLight updateSun(double timeNow) {
  float cycle = static_cast<float>(std::fmod(timeNow, kDayLength) / kDayLength);
  float angle = cycle * 2.0f * 3.1415926f;
  float sunX = std::cos(angle);
  float sunY = std::sin(angle);
  float sunZ = 0.2f;

  float dayFactor = std::max(0.0f, sunY);
  float ambient = 0.08f + dayFactor * 0.42f;
  float diffuse = 0.15f + dayFactor * 0.85f;

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

  char title[256];
  std::snprintf(title, sizeof(title),
                "Renderer | FPS: %.0f | Pos %.1f %.1f %.1f | Block %d %d %d | Chunk %d %d %d",
                app.currentFPS,
                app.player.position.x, app.player.position.y, app.player.position.z,
                bx, by, bz, cx, cy, cz);
  glfwSetWindowTitle(window, title);
}

static bool pointInRect(double x, double y, const Rect& rect) {
  return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

static void drawRect(const Rect& rect, float r, float g, float b, float a) {
  glColor4f(r, g, b, a);
  glBegin(GL_QUADS);
  glVertex2f(rect.x, rect.y);
  glVertex2f(rect.x + rect.w, rect.y);
  glVertex2f(rect.x + rect.w, rect.y + rect.h);
  glVertex2f(rect.x, rect.y + rect.h);
  glEnd();
}

static const char** glyphFor(char c) {
  static const char* kBlank[7] = {"00000","00000","00000","00000","00000","00000","00000"};
  static const char* kA[7] = {"01110","10001","10001","11111","10001","10001","10001"};
  static const char* kB[7] = {"11110","10001","10001","11110","10001","10001","11110"};
  static const char* kC[7] = {"01111","10000","10000","10000","10000","10000","01111"};
  static const char* kD[7] = {"11110","10001","10001","10001","10001","10001","11110"};
  static const char* kE[7] = {"11111","10000","10000","11110","10000","10000","11111"};
  static const char* kF[7] = {"11111","10000","10000","11110","10000","10000","10000"};
  static const char* kG[7] = {"01111","10000","10000","10011","10001","10001","01111"};
  static const char* kH[7] = {"10001","10001","10001","11111","10001","10001","10001"};
  static const char* kI[7] = {"11111","00100","00100","00100","00100","00100","11111"};
  static const char* kJ[7] = {"00111","00010","00010","00010","10010","10010","01100"};
  static const char* kK[7] = {"10001","10010","10100","11000","10100","10010","10001"};
  static const char* kL[7] = {"10000","10000","10000","10000","10000","10000","11111"};
  static const char* kM[7] = {"10001","11011","10101","10001","10001","10001","10001"};
  static const char* kN[7] = {"10001","11001","10101","10011","10001","10001","10001"};
  static const char* kO[7] = {"01110","10001","10001","10001","10001","10001","01110"};
  static const char* kP[7] = {"11110","10001","10001","11110","10000","10000","10000"};
  static const char* kQ[7] = {"01110","10001","10001","10001","10101","10010","01101"};
  static const char* kR[7] = {"11110","10001","10001","11110","10100","10010","10001"};
  static const char* kS[7] = {"01111","10000","10000","01110","00001","00001","11110"};
  static const char* kT[7] = {"11111","00100","00100","00100","00100","00100","00100"};
  static const char* kU[7] = {"10001","10001","10001","10001","10001","10001","01110"};
  static const char* kV[7] = {"10001","10001","10001","10001","10001","01010","00100"};
  static const char* kW[7] = {"10001","10001","10001","10001","10101","11011","10001"};
  static const char* kX[7] = {"10001","10001","01010","00100","01010","10001","10001"};
  static const char* kY[7] = {"10001","10001","01010","00100","00100","00100","00100"};
  static const char* kZ[7] = {"11111","00001","00010","00100","01000","10000","11111"};
  static const char* k0[7] = {"01110","10001","10011","10101","11001","10001","01110"};
  static const char* k1[7] = {"00100","01100","00100","00100","00100","00100","01110"};
  static const char* k2[7] = {"01110","10001","00001","00010","00100","01000","11111"};
  static const char* k3[7] = {"11110","00001","00001","01110","00001","00001","11110"};
  static const char* k4[7] = {"00010","00110","01010","10010","11111","00010","00010"};
  static const char* k5[7] = {"11111","10000","10000","11110","00001","00001","11110"};
  static const char* k6[7] = {"01110","10000","10000","11110","10001","10001","01110"};
  static const char* k7[7] = {"11111","00001","00010","00100","01000","01000","01000"};
  static const char* k8[7] = {"01110","10001","10001","01110","10001","10001","01110"};
  static const char* k9[7] = {"01110","10001","10001","01111","00001","00001","01110"};
  static const char* kDash[7] = {"00000","00000","00000","11111","00000","00000","00000"};
  static const char* kAmp[7] = {"01100","10010","10100","01000","10101","10010","01101"};

  switch (c) {
    case 'A': return kA;
    case 'B': return kB;
    case 'C': return kC;
    case 'D': return kD;
    case 'E': return kE;
    case 'F': return kF;
    case 'G': return kG;
    case 'H': return kH;
    case 'I': return kI;
    case 'J': return kJ;
    case 'K': return kK;
    case 'L': return kL;
    case 'M': return kM;
    case 'N': return kN;
    case 'O': return kO;
    case 'P': return kP;
    case 'Q': return kQ;
    case 'R': return kR;
    case 'S': return kS;
    case 'T': return kT;
    case 'U': return kU;
    case 'V': return kV;
    case 'W': return kW;
    case 'X': return kX;
    case 'Y': return kY;
    case 'Z': return kZ;
    case '0': return k0;
    case '1': return k1;
    case '2': return k2;
    case '3': return k3;
    case '4': return k4;
    case '5': return k5;
    case '6': return k6;
    case '7': return k7;
    case '8': return k8;
    case '9': return k9;
    case '-': return kDash;
    case '&': return kAmp;
    default: return kBlank;
  }
}

static float textWidth(const std::string& text, float scale) {
  float width = 0.0f;
  for (char c : text) {
    width += 6.0f * scale;
  }
  if (!text.empty()) {
    width -= 1.0f * scale;
  }
  return width;
}

static void drawText(float x, float y, float scale, const std::string& text, float r, float g, float b, float a) {
  float cursor = x;
  glColor4f(r, g, b, a);
  for (char raw : text) {
    char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
    const char** glyph = glyphFor(c);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (glyph[row][col] == '1') {
          Rect cell = {cursor + col * scale, y + row * scale, scale, scale};
          drawRect(cell, r, g, b, a);
        }
      }
    }
    cursor += 6.0f * scale;
  }
}

static bool drawButton(const Rect& rect, const std::string& label, bool hovered, bool enabled) {
  float bg = hovered ? 0.28f : 0.2f;
  float alpha = enabled ? 0.92f : 0.45f;
  drawRect(rect, bg, bg, bg, alpha);
  Rect inner = {rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f};
  drawRect(inner, 0.12f, 0.12f, 0.12f, alpha);
  float scale = rect.h * 0.18f;
  float labelW = textWidth(label, scale);
  float textX = rect.x + (rect.w - labelW) * 0.5f;
  float textY = rect.y + (rect.h - 7.0f * scale) * 0.5f;
  drawText(textX, textY, scale, label, 0.95f, 0.95f, 0.95f, enabled ? 0.95f : 0.35f);
  return hovered && enabled;
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
  if (app->ui.inventoryOpen || app->mode != GameMode::Playing) {
    return;
  }

  handlePlayerMouse(app->player, xpos, ypos);
}

static void initApp(AppState& app) {
  initPlayer(app.player);
  initWorld(app.world);
  initInventory(app.inventory);
  initUi(app.ui);
  initEntityManager(app.entityManager);
  app.dayNightPaused = true;
  app.dayNightTime = static_cast<double>(kDayLength) * 0.25;
  app.prevDayNightToggleDown = false;
  app.prevBreakDown = false;
  app.prevPlaceDown = false;
  app.prevSpawnCowDown = false;
  app.showChunkBounds = false;
  app.showWireframe = false;
  app.showCoords = false;
  app.currentFPS = 0.0f;
  app.fpsUpdateTimer = 0.0;
  app.frameCount = 0;
  app.prevBoundsDown = false;
  app.prevWireframeDown = false;
  app.prevCoordsDown = false;
  app.prevEscDown = false;
  app.prevMenuMouseDown = false;
  app.fovDegrees = 60.0f;
  app.renderDistanceSetting = kDefaultRenderDistance;
  app.saveExists = false;
  app.mode = GameMode::Launcher;
  app.settingsReturn = GameMode::Launcher;
  app.seedSetting = static_cast<uint32_t>(std::time(nullptr));
}

static void updateSaveExists(AppState& app) {
  struct stat buffer;
  app.saveExists = (stat("save/world.bin", &buffer) == 0);
}

static void applyVideoSettings(AppState& app) {
  app.world.renderDistance = std::max(kMinRenderDistance, std::min(kMaxRenderDistance, app.renderDistanceSetting));
}

static void spawnCowsNearPlayer(AppState& app, int count) {
  Vec3 base = app.player.position;
  for (int i = 0; i < count; ++i) {
    float angle = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 6.2831853f;
    float dist = 2.5f + (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 4.0f;
    float x = base.x + std::cos(angle) * dist;
    float z = base.z + std::sin(angle) * dist;
    float y = surfaceHeightAt(app.world, x, z);
    spawnCow(app.entityManager, {x, y, z});
  }
}

static void spawnStarterEntities(AppState& app) {
  app.entityManager.entities.clear();

  Vec3 base = app.player.position;

  float maxDist = static_cast<float>(app.world.renderDistance * 16) * 0.85f;
  float minDist = 14.0f;
  float minSeparation = 10.0f;
  int cowCount = 18;
  int attempts = 0;
  while (static_cast<int>(app.entityManager.entities.size()) < cowCount && attempts < cowCount * 40) {
    attempts++;
    float angle = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 6.2831853f;
    float t = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));
    float dist = minDist + t * (maxDist - minDist);
    float x = base.x + std::cos(angle) * dist;
    float z = base.z + std::sin(angle) * dist;
    float y = surfaceHeightAt(app.world, x, z);

    bool tooClose = false;
    for (const Entity& e : app.entityManager.entities) {
      float dx = e.position.x - x;
      float dz = e.position.z - z;
      if (dx * dx + dz * dz < minSeparation * minSeparation) {
        tooClose = true;
        break;
      }
    }
    if (tooClose) {
      continue;
    }
    spawnCow(app.entityManager, {x, y, z});
  }
}

static void startNewWorld(AppState& app) {
  initWorld(app.world);
  initPlayer(app.player);
  initInventory(app.inventory);
  initEntityManager(app.entityManager);
  app.world.seed = app.seedSetting;
  app.renderDistanceSetting = app.world.renderDistance;
  applyVideoSettings(app);
  float surface = surfaceHeightAt(app.world, app.player.position.x, app.player.position.z);
  app.player.position.y = surface + 2.0f;
  spawnStarterEntities(app);
}

static bool loadExistingWorld(AppState& app) {
  bool loaded = loadGame("save/world.bin", app.world, app.player, app.inventory, app.textures);
  initEntityManager(app.entityManager);
  app.renderDistanceSetting = app.world.renderDistance;
  app.seedSetting = app.world.seed;
  applyVideoSettings(app);
  spawnStarterEntities(app);
  return loaded;
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

static void beginUiPass(int width, int height) {
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_TEXTURE_2D);
}

static void endUiPass() {
  glEnable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glEnable(GL_LIGHTING);
  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

static Rect centeredButton(float centerX, float startY, float width, float height, int index, float gap) {
  float y = startY + index * (height + gap);
  return {centerX - width * 0.5f, y, width, height};
}

static bool menuButton(const AppState& app, const Rect& rect, const std::string& label,
                       bool enabled, bool mouseClick) {
  bool hovered = pointInRect(app.ui.mouseX, app.ui.mouseY, rect);
  bool pressed = drawButton(rect, label, hovered, enabled) && mouseClick;
  return pressed;
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
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);

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
  loadEntityTextures(app.entityManager);
  updateSaveExists(app);

  double lastFrame = glfwGetTime();

  while (!glfwWindowShouldClose(window)) {
    double currentFrame = glfwGetTime();
    float deltaTime = static_cast<float>(currentFrame - lastFrame);
    lastFrame = currentFrame;

    if (!app.dayNightPaused) {
      app.dayNightTime = wrapDayTime(app.dayNightTime + static_cast<double>(deltaTime));
    }

    app.frameCount++;
    app.fpsUpdateTimer += deltaTime;
    if (app.fpsUpdateTimer >= 0.5) {
      app.currentFPS = static_cast<float>(app.frameCount) / static_cast<float>(app.fpsUpdateTimer);
      app.frameCount = 0;
      app.fpsUpdateTimer = 0.0;
    }

    bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (escDown && !app.prevEscDown) {
      if (app.mode == GameMode::Playing) {
        if (app.ui.inventoryOpen) {
          app.ui.inventoryOpen = false;
          app.ui.openedFromTable = false;
          setCraftingSize(app.inventory, 2);
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
          }
          app.player.firstMouse = true;
        } else {
          app.mode = GameMode::Paused;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
      } else if (app.mode == GameMode::Paused) {
        app.mode = GameMode::Playing;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
          glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        app.player.firstMouse = true;
      } else if (app.mode == GameMode::Settings) {
        app.mode = app.settingsReturn;
      }
    }
    app.prevEscDown = escDown;

    bool toggleDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    bool toggled = app.mode == GameMode::Playing ? toggleInventory(app.ui, toggleDown) : false;
    handleInventoryToggle(window, app, toggled);

    if (app.mode == GameMode::Playing) {
      toggleFlag(glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS, app.prevBoundsDown, app.showChunkBounds);
      toggleFlag(glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS, app.prevWireframeDown, app.showWireframe);
      toggleFlag(glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS, app.prevCoordsDown, app.showCoords);

      toggleFlag(glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS,
                 app.prevDayNightToggleDown, app.dayNightPaused);

      bool spawnDown = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
      if (spawnDown && !app.prevSpawnCowDown && !app.ui.inventoryOpen) {
        spawnCowsNearPlayer(app, 2);
      }
      app.prevSpawnCowDown = spawnDown;

      if (app.dayNightPaused && !app.ui.inventoryOpen) {
        bool scrubLeft = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS;
        bool scrubRight = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
        if (scrubLeft || scrubRight) {
          constexpr double kScrubSpeed = 220.0; //daytime per second
          double dir = scrubRight ? 1.0 : -1.0;
          app.dayNightTime = wrapDayTime(app.dayNightTime + dir * kScrubSpeed * static_cast<double>(deltaTime));
        }
      }
    }

    if (app.mode == GameMode::Playing) {
      updateHotbarSelection(app, window);
    }

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

    if (app.mode == GameMode::Launcher) {
      updateSaveExists(app);
    }

    if (app.ui.inventoryOpen) {
      setCraftingSize(app.inventory, app.ui.openedFromTable ? 3 : 2);
    }

    handleInventoryClick(app.ui, app.inventory, width, height, mouseDown, rightDown, shiftDown);

    bool breakDown = mouseDown;
    bool placeDown = rightDown;
    if (app.mode == GameMode::Playing && !app.ui.inventoryOpen) {
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

    if (app.mode == GameMode::Playing && !app.ui.inventoryOpen) {
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
    } else if (app.mode == GameMode::Playing) {
      app.player.prevJumpDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    }

    if (app.mode == GameMode::Playing) {
      updatePlayerPhysics(app.player, app.world, deltaTime);
      updateEntities(app.entityManager, app.world, app.player.position, deltaTime);
      resolvePlayerEntityCollisions(app.player, app.entityManager);
    }

    float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (app.mode == GameMode::Playing) {
      updateWorldChunks(app.world, app.textures, app.player.position,
                        app.player.front, app.player.up, app.player.right,
                        app.fovDegrees, aspect, currentFrame);
    }

    if (app.mode == GameMode::Playing) {
      updateWindowTitle(window, app);
    } else {
      glfwSetWindowTitle(window, "Renderer");
    }

    Mat4 proj = perspective(app.fovDegrees, aspect, 0.1f, kFarPlane);
    Mat4 view = lookAt(app.player.position, app.player.position + app.player.front, app.player.up);

    glViewport(0, 0, width, height);
    WorldLight worldLight = updateSun(app.dayNightTime);
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

    if (app.mode != GameMode::Launcher) {
      uploadVisibleChunkMeshes(app.world);
      drawWorld(app.world, currentFrame);
      renderEntities(app.entityManager, app.player.position);
    }

    if (wireframe) {
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      glEnable(GL_CULL_FACE);
    }

    if (app.showChunkBounds) {
      drawChunkBounds(app.world);
    }
    if (app.mode == GameMode::Playing && !app.ui.inventoryOpen) {
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
    if (app.mode != GameMode::Launcher) {
      drawUi(app.inventory, app.textures, app.ui, width, height);
    }

    if (app.mode != GameMode::Playing) {
      bool mouseClick = mouseDown && !app.prevMenuMouseDown;
      app.prevMenuMouseDown = mouseDown;
      beginUiPass(width, height);
      drawRect({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)},
               0.0f, 0.0f, 0.0f, 0.55f);

      float centerX = width * 0.5f;
      float buttonW = std::min(460.0f, width * 0.6f);
      float buttonH = 56.0f;
      float gap = 14.0f;
      float startY = height * 0.35f;

      if (app.mode == GameMode::Launcher) {
        drawText(centerX - textWidth("Renderer", 6.0f) * 0.5f, height * 0.18f, 6.0f, "Renderer",
                 0.95f, 0.95f, 0.95f, 0.95f);
        float seedRowY = height * 0.27f;
        float labelScale = 3.2f;
        float valueScale = 3.0f;
        float labelX = width * 0.22f;
        float valueX = width * 0.62f;
        float buttonSize = 44.0f;
        float valueGap = 12.0f;

        drawText(labelX, seedRowY, labelScale, "SEED", 0.9f, 0.9f, 0.9f, 0.9f);
        std::string seedValue = std::to_string(app.seedSetting);
        float seedW = textWidth(seedValue, valueScale);
        drawText(valueX, seedRowY, valueScale, seedValue, 0.9f, 0.9f, 0.9f, 0.9f);
        Rect minusSeed = {valueX - buttonSize - valueGap, seedRowY - 6.0f, buttonSize, buttonSize};
        Rect plusSeed = {valueX + seedW + valueGap, seedRowY - 6.0f, buttonSize, buttonSize};
        Rect randSeed = {valueX + seedW + valueGap + buttonSize + 10.0f, seedRowY - 6.0f, buttonSize * 2.1f, buttonSize};
        bool minusS = menuButton(app, minusSeed, "-", true, mouseClick);
        bool plusS = menuButton(app, plusSeed, "+", true, mouseClick);
        bool randS = menuButton(app, randSeed, "RANDOM", true, mouseClick);
        if (minusS) {
          app.seedSetting -= 1u;
        } else if (plusS) {
          app.seedSetting += 1u;
        } else if (randS) {
          app.seedSetting = static_cast<uint32_t>(std::time(nullptr)) ^ (app.seedSetting * 1664525u + 1013904223u);
        }

        bool newWorld = menuButton(app, centeredButton(centerX, startY, buttonW, buttonH, 0, gap),
                                   "NEW WORLD", true, mouseClick);
        bool loadWorld = menuButton(app, centeredButton(centerX, startY, buttonW, buttonH, 1, gap),
                                    "LOAD WORLD", app.saveExists, mouseClick);
        bool settings = menuButton(app, centeredButton(centerX, startY, buttonW, buttonH, 2, gap),
                                   "SETTINGS", true, mouseClick);
        bool quit = menuButton(app, centeredButton(centerX, startY, buttonW, buttonH, 3, gap),
                               "QUIT", true, mouseClick);
        if (newWorld) {
          startNewWorld(app);
          app.ui.inventoryOpen = false;
          updateWorldChunks(app.world, app.textures, app.player.position,
                            app.player.front, app.player.up, app.player.right,
                            app.fovDegrees, aspect, currentFrame);
          rebuildWorldMeshes(app.world, app.textures);
          app.mode = GameMode::Playing;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
          }
          app.player.firstMouse = true;
        } else if (loadWorld) {
          if (!loadExistingWorld(app)) {
            startNewWorld(app);
          }
          app.ui.inventoryOpen = false;
          updateWorldChunks(app.world, app.textures, app.player.position,
                            app.player.front, app.player.up, app.player.right,
                            app.fovDegrees, aspect, currentFrame);
          rebuildWorldMeshes(app.world, app.textures);
          app.mode = GameMode::Playing;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
          }
          app.player.firstMouse = true;
        } else if (settings) {
          app.settingsReturn = GameMode::Launcher;
          app.mode = GameMode::Settings;
        } else if (quit) {
          glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
      } else if (app.mode == GameMode::Paused) {
        drawText(centerX - textWidth("PAUSED", 5.0f) * 0.5f, height * 0.18f, 5.0f, "PAUSED",
                 0.95f, 0.95f, 0.95f, 0.95f);
        bool resume = menuButton(app, centeredButton(centerX, startY, buttonW, buttonH, 0, gap),
                                 "RESUME", true, mouseClick);
        bool saveQuit = menuButton(app, centeredButton(centerX, startY, buttonW, buttonH, 1, gap),
                                   "SAVE & QUIT", true, mouseClick);
        bool settings = menuButton(app, centeredButton(centerX, startY, buttonW, buttonH, 2, gap),
                                   "SETTINGS", true, mouseClick);
        bool launcher = menuButton(app, centeredButton(centerX, startY, buttonW, buttonH, 3, gap),
                                   "LAUNCHER", true, mouseClick);
        if (resume) {
          app.mode = GameMode::Playing;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
          }
          app.player.firstMouse = true;
        } else if (saveQuit) {
          saveGame("save/world.bin", app.world, app.player, app.inventory);
          updateSaveExists(app);
          app.mode = GameMode::Launcher;
          app.ui.inventoryOpen = false;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        } else if (settings) {
          app.settingsReturn = GameMode::Paused;
          app.mode = GameMode::Settings;
        } else if (launcher) {
          app.mode = GameMode::Launcher;
          app.ui.inventoryOpen = false;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
      } else if (app.mode == GameMode::Settings) {
        drawText(centerX - textWidth("VIDEO SETTINGS", 4.5f) * 0.5f, height * 0.16f, 4.5f,
                 "VIDEO SETTINGS", 0.95f, 0.95f, 0.95f, 0.95f);

        float rowY = height * 0.34f;
        float labelScale = 3.2f;
        float valueScale = 3.0f;
        float labelX = width * 0.22f;
        float valueX = width * 0.62f;
        float buttonSize = 44.0f;
        float valueGap = 12.0f;

        drawText(labelX, rowY, labelScale, "RENDER DISTANCE", 0.9f, 0.9f, 0.9f, 0.9f);
        std::string renderValue = std::to_string(app.renderDistanceSetting);
        float valueW = textWidth(renderValue, valueScale);
        drawText(valueX, rowY, valueScale, renderValue, 0.9f, 0.9f, 0.9f, 0.9f);
        Rect minusRect = {valueX - buttonSize - valueGap, rowY - 6.0f, buttonSize, buttonSize};
        Rect plusRect = {valueX + valueW + valueGap, rowY - 6.0f, buttonSize, buttonSize};
        bool minus = menuButton(app, minusRect, "-", app.renderDistanceSetting > kMinRenderDistance, mouseClick);
        bool plus = menuButton(app, plusRect, "+", app.renderDistanceSetting < kMaxRenderDistance, mouseClick);
        if (minus) {
          app.renderDistanceSetting -= 1;
        } else if (plus) {
          app.renderDistanceSetting += 1;
        }
        applyVideoSettings(app);

        float rowY2 = rowY + 80.0f;
        drawText(labelX, rowY2, labelScale, "FOV", 0.9f, 0.9f, 0.9f, 0.9f);
        std::string fovValue = std::to_string(static_cast<int>(app.fovDegrees));
        float fovW = textWidth(fovValue, valueScale);
        drawText(valueX, rowY2, valueScale, fovValue, 0.9f, 0.9f, 0.9f, 0.9f);
        Rect minusFov = {valueX - buttonSize - valueGap, rowY2 - 6.0f, buttonSize, buttonSize};
        Rect plusFov = {valueX + fovW + valueGap, rowY2 - 6.0f, buttonSize, buttonSize};
        bool minusF = menuButton(app, minusFov, "-", app.fovDegrees > kMinFovDegrees, mouseClick);
        bool plusF = menuButton(app, plusFov, "+", app.fovDegrees < kMaxFovDegrees, mouseClick);
        if (minusF) {
          app.fovDegrees = std::max(kMinFovDegrees, app.fovDegrees - 2.0f);
        } else if (plusF) {
          app.fovDegrees = std::min(kMaxFovDegrees, app.fovDegrees + 2.0f);
        }

        float rowY3 = rowY2 + 80.0f;
        drawText(labelX, rowY3, labelScale, "DAY/NIGHT CYCLE", 0.9f, 0.9f, 0.9f, 0.9f);
        std::string cycleValue = app.dayNightPaused ? "FROZEN" : "RUNNING";
        float cycleW = textWidth(cycleValue, valueScale);
        drawText(valueX, rowY3, valueScale, cycleValue, 0.9f, 0.9f, 0.9f, 0.9f);
        Rect toggleCycle = {valueX + cycleW + valueGap, rowY3 - 6.0f, buttonSize * 2.0f, buttonSize};
        if (menuButton(app, toggleCycle, "TOGGLE", true, mouseClick)) {
          app.dayNightPaused = !app.dayNightPaused;
        }

        float rowY4 = rowY3 + 80.0f;
        drawText(labelX, rowY4, labelScale, "TIME OF DAY", 0.9f, 0.9f, 0.9f, 0.9f);
        int hour = static_cast<int>((wrapDayTime(app.dayNightTime) / static_cast<double>(kDayLength)) * 24.0);
        hour = std::max(0, std::min(23, hour));
        std::string hourValue = std::to_string(hour);
        float hourW = textWidth(hourValue, valueScale);
        drawText(valueX, rowY4, valueScale, hourValue, 0.9f, 0.9f, 0.9f, 0.9f);
        Rect minusHour = {valueX - buttonSize - valueGap, rowY4 - 6.0f, buttonSize, buttonSize};
        Rect plusHour = {valueX + hourW + valueGap, rowY4 - 6.0f, buttonSize, buttonSize};
        bool decHour = menuButton(app, minusHour, "-", true, mouseClick);
        bool incHour = menuButton(app, plusHour, "+", true, mouseClick);
        if (decHour) {
          app.dayNightTime = wrapDayTime(app.dayNightTime - static_cast<double>(kDayLength) / 24.0);
        } else if (incHour) {
          app.dayNightTime = wrapDayTime(app.dayNightTime + static_cast<double>(kDayLength) / 24.0);
        }

        Rect backRect = centeredButton(centerX, height * 0.80f, buttonW, buttonH, 0, gap);
        bool back = menuButton(app, backRect, "BACK", true, mouseClick);
        if (back) {
          applyVideoSettings(app);
          app.mode = app.settingsReturn;
        }
      }

      endUiPass();
    }

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  saveGame("save/world.bin", app.world, app.player, app.inventory);
  shutdownRenderer();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
