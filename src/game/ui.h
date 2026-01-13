#pragma once

#include "game/inventory.h"
#include "render/textures.h"

struct UiState {
  bool inventoryOpen;
  bool openedFromTable;
  bool prevToggleDown;
  bool prevMouseDown;
  bool prevRightDown;
  bool dragCandidate;
  bool dragging;
  float scaleX;
  float scaleY;
  bool rightDragActive;
  unsigned int rightDragMask;
  double mouseX;
  double mouseY;
  double dragStartX;
  double dragStartY;
  InventorySlot cursor;
};

void initUi(UiState& ui);
bool toggleInventory(UiState& ui, bool toggleDown);
void updateUiMouse(UiState& ui, double x, double y);
void handleInventoryClick(UiState& ui, Inventory& inventory, int width, int height,
                          bool mouseDown, bool rightDown, bool shiftDown);
void drawUi(const Inventory& inventory, const TextureAssets& textures, const UiState& ui,
            int width, int height);
