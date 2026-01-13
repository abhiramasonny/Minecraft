#include <GLFW/glfw3.h>

#include "stb_image.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Mat4 {
  float m[16];
};

struct Color {
  float r;
  float g;
  float b;
};

struct Vertex {
  float x;
  float y;
  float z;
  float u;
  float v;
  float r;
  float g;
  float b;
};

struct Quad {
  Vertex v[4];
  unsigned int textureId;
};

struct Texture {
  unsigned int id;
  int width;
  int height;
};

struct TextureAssets {
  Texture grassTop;
  Texture grassSide;
  Texture dirt;
  Texture stone;
};

struct Chunk {
  int cx;
  int cy;
  int cz;
  std::vector<uint8_t> blocks;
  std::vector<Quad> quads;
};

struct World {
  float originX;
  float originZ;
  std::vector<int> columnHeights;
  std::vector<Chunk> chunks;
};

struct Camera {
  Vec3 position;
  Vec3 front;
  Vec3 up;
  Vec3 right;
  float yaw;
  float pitch;
};

struct PlayerController {
  float verticalVelocity;
  bool flyMode;
  bool grounded;
  bool prevJumpDown;
  double lastJumpTap;
};

struct MouseState {
  bool first;
  double lastX;
  double lastY;
};

struct AppState {
  Camera camera;
  PlayerController controller;
  MouseState mouse;
  World world;
  TextureAssets textures;
};

enum BlockType : uint8_t {
  BlockAir = 0,
  BlockGrass = 1,
  BlockDirt = 2,
  BlockStone = 3,
};

struct BlockTextures {
  unsigned int top;
  unsigned int side;
  unsigned int bottom;
};

static Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }

static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static Vec3 cross(Vec3 a, Vec3 b) {
  return {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}

static Vec3 normalize(Vec3 v) {
  float len = std::sqrt(dot(v, v));
  if (len <= 0.00001f) {
    return {0.0f, 0.0f, 0.0f};
  }
  return {v.x / len, v.y / len, v.z / len};
}

static float radians(float degrees) {
  return degrees * 3.1415926535f / 180.0f;
}

static float clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static Color shadeColor(float shade) {
  shade = clamp01(shade);
  return {shade, shade, shade};
}

static Mat4 perspective(float fovDegrees, float aspect, float nearZ, float farZ) {
  float f = 1.0f / std::tan(radians(fovDegrees) * 0.5f);
  Mat4 out = {};
  out.m[0] = f / aspect;
  out.m[5] = f;
  out.m[10] = (farZ + nearZ) / (nearZ - farZ);
  out.m[11] = -1.0f;
  out.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
  return out;
}

static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
  Vec3 f = normalize(center - eye);
  Vec3 r = normalize(cross(f, up));
  Vec3 u = cross(r, f);

  Mat4 out = {};
  out.m[0] = r.x;
  out.m[1] = u.x;
  out.m[2] = -f.x;
  out.m[3] = 0.0f;

  out.m[4] = r.y;
  out.m[5] = u.y;
  out.m[6] = -f.y;
  out.m[7] = 0.0f;

  out.m[8] = r.z;
  out.m[9] = u.z;
  out.m[10] = -f.z;
  out.m[11] = 0.0f;

  out.m[12] = -dot(r, eye);
  out.m[13] = -dot(u, eye);
  out.m[14] = dot(f, eye);
  out.m[15] = 1.0f;

  return out;
}

static constexpr int kChunkSize = 16;
static constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;
static constexpr int kChunksX = 10;
static constexpr int kChunksY = 2;
static constexpr int kChunksZ = 10;
static constexpr int kWorldX = kChunkSize * kChunksX;
static constexpr int kWorldY = kChunkSize * kChunksY;
static constexpr int kWorldZ = kChunkSize * kChunksZ;
static constexpr float kBlockSize = 1.0f;
static constexpr float kMoveSpeed = 5.0f;
static constexpr float kGravity = -18.0f;
static constexpr float kJumpVelocity = 5.5f;
static constexpr float kPlayerHeight = 1.7f;
static constexpr double kDoubleTapWindow = 0.25;

static const Vec3 worldUp = {0.0f, 1.0f, 0.0f};

static int chunkIndex(int cx, int cy, int cz) {
  return cx + kChunksX * (cy + kChunksY * cz);
}

static int blockIndex(int x, int y, int z) {
  return x + kChunkSize * (y + kChunkSize * z);
}

static int columnIndex(int x, int z) {
  return x + kWorldX * z;
}

static BlockType blockAt(const World& world, int wx, int wy, int wz) {
  if (wx < 0 || wx >= kWorldX || wy < 0 || wy >= kWorldY || wz < 0 || wz >= kWorldZ) {
    return BlockAir;
  }

  int cx = wx / kChunkSize;
  int cy = wy / kChunkSize;
  int cz = wz / kChunkSize;
  int lx = wx - cx * kChunkSize;
  int ly = wy - cy * kChunkSize;
  int lz = wz - cz * kChunkSize;

  const Chunk& chunk = world.chunks[chunkIndex(cx, cy, cz)];
  return static_cast<BlockType>(chunk.blocks[blockIndex(lx, ly, lz)]);
}

static void setBlock(World& world, int wx, int wy, int wz, BlockType type) {
  if (wx < 0 || wx >= kWorldX || wy < 0 || wy >= kWorldY || wz < 0 || wz >= kWorldZ) {
    return;
  }

  int cx = wx / kChunkSize;
  int cy = wy / kChunkSize;
  int cz = wz / kChunkSize;
  int lx = wx - cx * kChunkSize;
  int ly = wy - cy * kChunkSize;
  int lz = wz - cz * kChunkSize;

  Chunk& chunk = world.chunks[chunkIndex(cx, cy, cz)];
  chunk.blocks[blockIndex(lx, ly, lz)] = static_cast<uint8_t>(type);
}

static Vertex makeVertex(Vec3 pos, float u, float v, Color color) {
  return {pos.x, pos.y, pos.z, u, v, color.r, color.g, color.b};
}

static void addQuad(std::vector<Quad>& quads, unsigned int textureId, Color color, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
  Quad quad;
  quad.textureId = textureId;
  quad.v[0] = makeVertex(a, 0.0f, 0.0f, color);
  quad.v[1] = makeVertex(b, 1.0f, 0.0f, color);
  quad.v[2] = makeVertex(c, 1.0f, 1.0f, color);
  quad.v[3] = makeVertex(d, 0.0f, 1.0f, color);
  quads.push_back(quad);
}

static BlockTextures texturesFor(BlockType type, const TextureAssets& textures) {
  switch (type) {
    case BlockGrass:
      return {textures.grassTop.id, textures.grassSide.id, textures.dirt.id};
    case BlockDirt:
      return {textures.dirt.id, textures.dirt.id, textures.dirt.id};
    case BlockStone:
      return {textures.stone.id, textures.stone.id, textures.stone.id};
    default:
      return {0u, 0u, 0u};
  }
}

static Texture createSolidTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
  Texture texture = {};
  unsigned char data[4] = {r, g, b, a};

  glGenTextures(1, &texture.id);
  glBindTexture(GL_TEXTURE_2D, texture.id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);

  texture.width = 1;
  texture.height = 1;
  return texture;
}

static Texture loadTexture(const char* path, unsigned char fallbackR, unsigned char fallbackG, unsigned char fallbackB) {
  Texture texture = {};
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char* data = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
  if (!data) {
    std::fprintf(stderr, "Failed to load texture: %s\n", path);
    return createSolidTexture(fallbackR, fallbackG, fallbackB, 255);
  }

  glGenTextures(1, &texture.id);
  glBindTexture(GL_TEXTURE_2D, texture.id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(data);

  texture.width = width;
  texture.height = height;
  return texture;
}

static void loadTextures(TextureAssets& textures) {
  stbi_set_flip_vertically_on_load(true);

  textures.grassTop = loadTexture("textures/blocks/grass_top.png", 100, 200, 100);
  textures.grassSide = loadTexture("textures/blocks/grass_side_carried.png", 120, 90, 60);
  textures.dirt = loadTexture("textures/blocks/dirt.png", 120, 90, 60);
  textures.stone = loadTexture("textures/blocks/stone.png", 140, 140, 140);
}

static void generateTerrain(World& world) {
  world.columnHeights.assign(kWorldX * kWorldZ, -1);

  for (int x = 0; x < kWorldX; ++x) {
    for (int z = 0; z < kWorldZ; ++z) {
      float height =
        6.0f + std::sin(static_cast<float>(x) * 0.2f) * 3.0f +
        std::cos(static_cast<float>(z) * 0.2f) * 3.0f +
        std::sin(static_cast<float>(x + z) * 0.1f) * 2.0f;
      int top = static_cast<int>(height);
      if (top < 1) top = 1;
      if (top > kWorldY - 2) top = kWorldY - 2;

      world.columnHeights[columnIndex(x, z)] = top;

      for (int y = 0; y <= top; ++y) {
        BlockType type = BlockStone;
        if (y == top) {
          type = BlockGrass;
        } else if (y >= top - 3) {
          type = BlockDirt;
        }
        setBlock(world, x, y, z, type);
      }
    }
  }
}

static void buildChunkMesh(const World& world, const TextureAssets& textures, Chunk& chunk) {
  chunk.quads.clear();

  for (int lx = 0; lx < kChunkSize; ++lx) {
    for (int ly = 0; ly < kChunkSize; ++ly) {
      for (int lz = 0; lz < kChunkSize; ++lz) {
        int wx = chunk.cx * kChunkSize + lx;
        int wy = chunk.cy * kChunkSize + ly;
        int wz = chunk.cz * kChunkSize + lz;

        BlockType type = blockAt(world, wx, wy, wz);
        if (type == BlockAir) {
          continue;
        }

        float x0 = world.originX + wx * kBlockSize;
        float x1 = x0 + kBlockSize;
        float y0 = wy * kBlockSize;
        float y1 = y0 + kBlockSize;
        float z0 = world.originZ + wz * kBlockSize;
        float z1 = z0 + kBlockSize;

        float baseShade = 0.7f + 0.3f * (static_cast<float>(wy) / (kWorldY - 1));
        Color topColor = shadeColor(baseShade * 1.05f);
        Color sideColor = shadeColor(baseShade * 0.9f);
        Color bottomColor = shadeColor(baseShade * 0.7f);
        BlockTextures blockTextures = texturesFor(type, textures);

        if (blockAt(world, wx, wy + 1, wz) == BlockAir) {
          addQuad(chunk.quads, blockTextures.top, topColor,
                  {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});
        }
        if (blockAt(world, wx, wy - 1, wz) == BlockAir) {
          addQuad(chunk.quads, blockTextures.bottom, bottomColor,
                  {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0});
        }
        if (blockAt(world, wx + 1, wy, wz) == BlockAir) {
          addQuad(chunk.quads, blockTextures.side, sideColor,
                  {x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0});
        }
        if (blockAt(world, wx - 1, wy, wz) == BlockAir) {
          addQuad(chunk.quads, blockTextures.side, sideColor,
                  {x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1});
        }
        if (blockAt(world, wx, wy, wz + 1) == BlockAir) {
          addQuad(chunk.quads, blockTextures.side, sideColor,
                  {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1});
        }
        if (blockAt(world, wx, wy, wz - 1) == BlockAir) {
          addQuad(chunk.quads, blockTextures.side, sideColor,
                  {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0});
        }
      }
    }
  }
}

static void rebuildWorldMeshes(World& world, const TextureAssets& textures) {
  for (Chunk& chunk : world.chunks) {
    buildChunkMesh(world, textures, chunk);
  }
}

static void initWorld(World& world) {
  world.originX = -0.5f * kWorldX * kBlockSize;
  world.originZ = -0.5f * kWorldZ * kBlockSize;

  world.chunks.resize(kChunksX * kChunksY * kChunksZ);
  for (int cx = 0; cx < kChunksX; ++cx) {
    for (int cy = 0; cy < kChunksY; ++cy) {
      for (int cz = 0; cz < kChunksZ; ++cz) {
        Chunk& chunk = world.chunks[chunkIndex(cx, cy, cz)];
        chunk.cx = cx;
        chunk.cy = cy;
        chunk.cz = cz;
        chunk.blocks.assign(kChunkVolume, static_cast<uint8_t>(BlockAir));
      }
    }
  }

  generateTerrain(world);
}

static bool worldToColumn(const World& world, float x, float z, int& ix, int& iz) {
  float fx = (x - world.originX) / kBlockSize;
  float fz = (z - world.originZ) / kBlockSize;
  ix = static_cast<int>(std::floor(fx));
  iz = static_cast<int>(std::floor(fz));
  if (ix < 0 || ix >= kWorldX || iz < 0 || iz >= kWorldZ) {
    return false;
  }
  return true;
}

static float groundHeightAt(const World& world, float x, float z, bool& valid) {
  int ix = 0;
  int iz = 0;
  if (!worldToColumn(world, x, z, ix, iz)) {
    valid = false;
    return 0.0f;
  }

  int top = world.columnHeights[columnIndex(ix, iz)];
  if (top < 0) {
    valid = false;
    return 0.0f;
  }

  valid = true;
  return (static_cast<float>(top) + 1.0f) * kBlockSize;
}

static void drawWorld(const World& world) {
  unsigned int bound = 0;
  bool started = false;

  for (const Chunk& chunk : world.chunks) {
    for (const Quad& quad : chunk.quads) {
      if (!started || quad.textureId != bound) {
        if (started) {
          glEnd();
        }
        glBindTexture(GL_TEXTURE_2D, quad.textureId);
        glBegin(GL_QUADS);
        bound = quad.textureId;
        started = true;
      }

      for (const Vertex& vertex : quad.v) {
        glColor3f(vertex.r, vertex.g, vertex.b);
        glTexCoord2f(vertex.u, vertex.v);
        glVertex3f(vertex.x, vertex.y, vertex.z);
      }
    }
  }

  if (started) {
    glEnd();
  }
}

static void updateCameraVectors(Camera& camera) {
  Vec3 front;
  front.x = std::cos(radians(camera.yaw)) * std::cos(radians(camera.pitch));
  front.y = std::sin(radians(camera.pitch));
  front.z = std::sin(radians(camera.yaw)) * std::cos(radians(camera.pitch));
  camera.front = normalize(front);
  camera.right = normalize(cross(camera.front, worldUp));
  camera.up = normalize(cross(camera.right, camera.front));
}

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
  (void)window;
  glViewport(0, 0, width, height);
}

static void handleMouse(AppState& app, double xpos, double ypos) {
  if (app.mouse.first) {
    app.mouse.lastX = xpos;
    app.mouse.lastY = ypos;
    app.mouse.first = false;
  }

  float xoffset = static_cast<float>(xpos - app.mouse.lastX);
  float yoffset = static_cast<float>(app.mouse.lastY - ypos);
  app.mouse.lastX = xpos;
  app.mouse.lastY = ypos;

  float sensitivity = 0.1f;
  xoffset *= sensitivity;
  yoffset *= sensitivity;

  app.camera.yaw += xoffset;
  app.camera.pitch += yoffset;

  if (app.camera.pitch > 89.0f) app.camera.pitch = 89.0f;
  if (app.camera.pitch < -89.0f) app.camera.pitch = -89.0f;

  updateCameraVectors(app.camera);
}

static void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
  AppState* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (!app) {
    return;
  }
  handleMouse(*app, xpos, ypos);
}

static void processInput(GLFWwindow* window, AppState& app, float deltaTime, double timeNow) {
  float velocity = kMoveSpeed * deltaTime;

  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }

  Vec3 flatFront = normalize({app.camera.front.x, 0.0f, app.camera.front.z});
  Vec3 flatRight = normalize(cross(flatFront, worldUp));

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
    app.camera.position = app.camera.position + flatFront * velocity;
  }
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
    app.camera.position = app.camera.position - flatFront * velocity;
  }
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
    app.camera.position = app.camera.position - flatRight * velocity;
  }
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
    app.camera.position = app.camera.position + flatRight * velocity;
  }

  bool jumpDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
  if (jumpDown && !app.controller.prevJumpDown) {
    if (timeNow - app.controller.lastJumpTap <= kDoubleTapWindow) {
      app.controller.flyMode = !app.controller.flyMode;
      app.controller.verticalVelocity = 0.0f;
      app.controller.grounded = false;
    }

    app.controller.lastJumpTap = timeNow;

    if (!app.controller.flyMode && app.controller.grounded) {
      app.controller.verticalVelocity = kJumpVelocity;
      app.controller.grounded = false;
    }
  }
  app.controller.prevJumpDown = jumpDown;

  if (app.controller.flyMode) {
    if (jumpDown) {
      app.camera.position.y += velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
      app.camera.position.y -= velocity;
    }
  }
}

static void updatePhysics(AppState& app, float deltaTime) {
  if (app.controller.flyMode) {
    app.controller.verticalVelocity = 0.0f;
    return;
  }

  app.controller.verticalVelocity += kGravity * deltaTime;
  app.camera.position.y += app.controller.verticalVelocity * deltaTime;

  bool validGround = false;
  float groundHeight = groundHeightAt(app.world, app.camera.position.x, app.camera.position.z, validGround);
  if (validGround) {
    float minY = groundHeight + kPlayerHeight;
    if (app.camera.position.y <= minY) {
      app.camera.position.y = minY;
      app.controller.verticalVelocity = 0.0f;
      app.controller.grounded = true;
    } else {
      app.controller.grounded = false;
    }
  } else {
    app.controller.grounded = false;
  }
}

static void initApp(AppState& app) {
  app.camera.position = {0.0f, 12.0f, 24.0f};
  app.camera.front = {0.0f, 0.0f, -1.0f};
  app.camera.up = worldUp;
  app.camera.right = {1.0f, 0.0f, 0.0f};
  app.camera.yaw = -90.0f;
  app.camera.pitch = -15.0f;

  app.controller.verticalVelocity = 0.0f;
  app.controller.flyMode = false;
  app.controller.grounded = false;
  app.controller.prevJumpDown = false;
  app.controller.lastJumpTap = -10.0;

  app.mouse.first = true;
  app.mouse.lastX = 0.0f;
  app.mouse.lastY = 0.0f;

  updateCameraVectors(app.camera);
  initWorld(app.world);
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
  rebuildWorldMeshes(app.world, app.textures);

  double lastFrame = glfwGetTime();

  while (!glfwWindowShouldClose(window)) {
    double currentFrame = glfwGetTime();
    float deltaTime = static_cast<float>(currentFrame - lastFrame);
    lastFrame = currentFrame;

    processInput(window, app, deltaTime, currentFrame);
    updatePhysics(app, deltaTime);

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width == 0 || height == 0) {
      glfwPollEvents();
      continue;
    }

    float aspect = static_cast<float>(width) / static_cast<float>(height);
    Mat4 proj = perspective(60.0f, aspect, 0.1f, 400.0f);
    Mat4 view = lookAt(app.camera.position, app.camera.position + app.camera.front, app.camera.up);

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    drawWorld(app.world);

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
