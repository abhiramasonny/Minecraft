#include "render/textures.h"

#include <GLFW/glfw3.h>

#include "third_party/stb_image.h"

#include <cstdio>

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

void loadTextures(TextureAssets& textures) {
  stbi_set_flip_vertically_on_load(true);

  textures.grassTop = loadTexture("textures/blocks/grass_carried.png", 100, 200, 100);
  textures.grassSide = loadTexture("textures/blocks/grass_side_carried.png", 120, 90, 60);
  textures.dirt = loadTexture("textures/blocks/dirt.png", 120, 90, 60);
  textures.stone = loadTexture("textures/blocks/stone.png", 140, 140, 140);
  textures.woodTop = loadTexture("textures/blocks/log_oak_top.png", 140, 100, 70);
  textures.woodSide = loadTexture("textures/blocks/log_big_oak.png", 120, 90, 60);
  textures.leaves = loadTexture("textures/blocks/leaves_oak_opaque.png", 90, 130, 90);
  textures.planks = loadTexture("textures/blocks/planks_oak.png", 150, 120, 90);
  textures.craftingTop = loadTexture("textures/blocks/crafting_table_top.png", 170, 130, 90);
  textures.craftingSide = loadTexture("textures/blocks/crafting_table_side.png", 160, 120, 80);
  textures.craftingFront = loadTexture("textures/blocks/crafting_table_front.png", 160, 120, 80);
  textures.cobblestone = loadTexture("textures/blocks/cobblestone.png", 120, 120, 120);
  textures.gravel = loadTexture("textures/blocks/gravel.png", 130, 120, 110);
  textures.sand = loadTexture("textures/blocks/sand.png", 210, 200, 150);
  textures.sandstoneTop = loadTexture("textures/blocks/sandstone_top.png", 220, 210, 160);
  textures.sandstoneSide = loadTexture("textures/blocks/sandstone_normal.png", 210, 200, 150);
  textures.sandstoneBottom = loadTexture("textures/blocks/sandstone_bottom.png", 200, 190, 140);
  textures.stoneBricks = loadTexture("textures/blocks/stonebrick.png", 130, 130, 140);
  textures.bricks = loadTexture("textures/blocks/brick.png", 160, 70, 60);
  textures.glass = loadTexture("textures/blocks/tinted_glass.png", 180, 220, 220);
  textures.furnaceTop = loadTexture("textures/blocks/furnace_top.png", 120, 120, 120);
  textures.furnaceSide = loadTexture("textures/blocks/furnace_side.png", 120, 120, 120);
  textures.furnaceFront = loadTexture("textures/blocks/furnace_front_off.png", 120, 120, 120);
  textures.stick = loadTexture("textures/items/stick.png", 150, 120, 70);
  textures.water = loadTexture("textures/blocks/water_still.png", 80, 120, 180);
  textures.torch = loadTexture("textures/blocks/torch_on.png", 240, 210, 120);
}
