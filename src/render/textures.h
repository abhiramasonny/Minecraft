#pragma once

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
  Texture woodTop;
  Texture woodSide;
  Texture leaves;
  Texture planks;
  Texture craftingTop;
  Texture craftingSide;
  Texture craftingFront;
  Texture cobblestone;
  Texture gravel;
  Texture sand;
  Texture sandstoneTop;
  Texture sandstoneSide;
  Texture sandstoneBottom;
  Texture stoneBricks;
  Texture bricks;
  Texture glass;
  Texture furnaceTop;
  Texture furnaceSide;
  Texture furnaceFront;
  Texture stick;
};

void loadTextures(TextureAssets& textures);
