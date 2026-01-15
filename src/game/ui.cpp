#include "game/ui.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace {
struct Color {
  float r;
  float g;
  float b;
  float a;
};

struct Rect {
  float x;
  float y;
  float w;
  float h;
};

struct UiLayout {
  float slotSize;
  float padding;
  float panelPadding;
  float panelGap;
  float mainX;
  float mainY;
  float mainW;
  float mainH;
  float paletteX;
  float paletteY;
  float paletteW;
  float paletteH;
  float hotbarX;
  float hotbarY;
  float storageX;
  float storageY;
  float craftingX;
  float craftingY;
  float craftW;
  float craftH;
  float outputX;
  float outputY;
  float outputSize;
};

constexpr int kPaletteColumns = 2;
static const BlockType kPaletteBlocks[] = {
  BlockGrass,
  BlockDirt,
  BlockStone,
  BlockCobblestone,
  BlockStoneBricks,
  BlockBricks,
  BlockGravel,
  BlockSand,
  BlockSandstone,
  BlockWood,
  BlockLeaves,
  BlockPlanks,
  BlockCraftingTable,
  BlockGlass,
  BlockFurnace,
  BlockStick,
};
static constexpr int kPaletteCount = static_cast<int>(sizeof(kPaletteBlocks) / sizeof(kPaletteBlocks[0]));
static constexpr double kDragThresholdSq = 16.0;

UiLayout computeLayout(int width, int height, int craftingSize) {
  UiLayout layout = {};
  float baseScale = std::min(width / 1280.0f, height / 720.0f);
  float uiScale = std::min(1.6f, std::max(0.8f, baseScale * 1.0f));

  layout.slotSize = 40.0f * uiScale;
  layout.padding = 4.0f * uiScale;
  layout.panelPadding = 12.0f * uiScale;
  layout.panelGap = 14.0f * uiScale;

  float gridW = layout.slotSize * kHotbarSlots + layout.padding * (kHotbarSlots - 1);
  float gridH = layout.slotSize * 3 + layout.padding * 2;

  float craftSize = static_cast<float>(craftingSize);
  layout.craftW = layout.slotSize * craftSize + layout.padding * (craftSize - 1.0f);
  layout.craftH = layout.slotSize * craftSize + layout.padding * (craftSize - 1.0f);
  layout.outputSize = layout.slotSize;
  float craftRowW = layout.craftW + layout.padding * 2.0f + layout.outputSize;

  layout.mainW = std::max(gridW, craftRowW) + layout.panelPadding * 2.0f;
  layout.mainH = layout.craftH + layout.padding * 2.0f + gridH + layout.panelPadding * 2.0f;

  int paletteRows = (kPaletteCount + kPaletteColumns - 1) / kPaletteColumns;
  float paletteInnerW = layout.slotSize * kPaletteColumns + layout.padding * (kPaletteColumns - 1);
  float paletteInnerH = layout.slotSize * paletteRows + layout.padding * (paletteRows - 1);
  layout.paletteW = paletteInnerW + layout.panelPadding * 2.0f;
  layout.paletteH = paletteInnerH + layout.panelPadding * 2.0f;

  float totalW = layout.mainW + layout.panelGap + layout.paletteW;
  float totalH = std::max(layout.mainH, layout.paletteH);
  layout.mainX = (width - totalW) * 0.5f;
  layout.mainY = (height - totalH) * 0.5f;

  layout.paletteX = layout.mainX + layout.mainW + layout.panelGap;
  layout.paletteY = layout.mainY + (totalH - layout.paletteH) * 0.5f;

  layout.craftingX = layout.mainX + layout.panelPadding;
  layout.craftingY = layout.mainY + layout.panelPadding;
  layout.outputX = layout.craftingX + layout.craftW + layout.padding * 2.0f;
  layout.outputY = layout.craftingY + (layout.craftH - layout.outputSize) * 0.5f;

  layout.storageX = layout.mainX + layout.panelPadding;
  layout.storageY = layout.craftingY + layout.craftH + layout.padding * 2.0f;

  layout.hotbarX = (width - gridW) * 0.5f;
  layout.hotbarY = height - layout.slotSize - 20.0f * uiScale;

  return layout;
}

Rect slotRect(float baseX, float baseY, float slotSize, float padding, int index) {
  float x = baseX + index * (slotSize + padding);
  return {x, baseY, slotSize, slotSize};
}

bool pointInRect(double x, double y, const Rect& rect) {
  return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

enum class ClickKind {
  Left,
  Right,
};

void drawRect(const Rect& rect, const Color& color) {
  glColor4f(color.r, color.g, color.b, color.a);
  glBegin(GL_QUADS);
  glVertex2f(rect.x, rect.y);
  glVertex2f(rect.x + rect.w, rect.y);
  glVertex2f(rect.x + rect.w, rect.y + rect.h);
  glVertex2f(rect.x, rect.y + rect.h);
  glEnd();
}

void drawTexturedRect(const Rect& rect) {
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 0.0f);
  glVertex2f(rect.x, rect.y);
  glTexCoord2f(1.0f, 0.0f);
  glVertex2f(rect.x + rect.w, rect.y);
  glTexCoord2f(1.0f, 1.0f);
  glVertex2f(rect.x + rect.w, rect.y + rect.h);
  glTexCoord2f(0.0f, 1.0f);
  glVertex2f(rect.x, rect.y + rect.h);
  glEnd();
}

unsigned int textureForBlock(BlockType type, const TextureAssets& textures) {
  switch (type) {
    case BlockGrass:
      return textures.grassTop.id;
    case BlockDirt:
      return textures.dirt.id;
    case BlockStone:
      return textures.stone.id;
    case BlockCobblestone:
      return textures.cobblestone.id;
    case BlockGravel:
      return textures.gravel.id;
    case BlockSand:
      return textures.sand.id;
    case BlockSandstone:
      return textures.sandstoneTop.id;
    case BlockStoneBricks:
      return textures.stoneBricks.id;
    case BlockBricks:
      return textures.bricks.id;
    case BlockGlass:
      return textures.glass.id;
    case BlockFurnace:
      return textures.furnaceFront.id;
    case BlockStick:
      return textures.stick.id;
    case BlockWood:
      return textures.woodSide.id;
    case BlockLeaves:
      return textures.leaves.id;
    case BlockPlanks:
      return textures.planks.id;
    case BlockCraftingTable:
      return textures.craftingTop.id;
    default:
      return 0u;
  }
}

void drawSlotBackground(const Rect& rect, bool selected) {
  Color border = selected ? Color{0.95f, 0.82f, 0.45f, 0.95f} : Color{0.2f, 0.2f, 0.2f, 0.9f};
  Color fill = {0.05f, 0.05f, 0.05f, 0.85f};
  Rect outer = {rect.x - 2.0f, rect.y - 2.0f, rect.w + 4.0f, rect.h + 4.0f};
  drawRect(outer, border);
  drawRect(rect, fill);
}

void drawDigit(const Rect& rect, int digit, const Color& color) {
  static const char* patterns[10][5] = {
    {"111", "101", "101", "101", "111"},
    {"010", "110", "010", "010", "111"},
    {"111", "001", "111", "100", "111"},
    {"111", "001", "111", "001", "111"},
    {"101", "101", "111", "001", "001"},
    {"111", "100", "111", "001", "111"},
    {"111", "100", "111", "101", "111"},
    {"111", "001", "001", "001", "001"},
    {"111", "101", "111", "101", "111"},
    {"111", "101", "111", "001", "111"},
  };

  if (digit < 0 || digit > 9) {
    return;
  }

  float cellW = rect.w / 3.0f;
  float cellH = rect.h / 5.0f;
  for (int row = 0; row < 5; ++row) {
    for (int col = 0; col < 3; ++col) {
      if (patterns[digit][row][col] == '1') {
        Rect cell = {rect.x + col * cellW, rect.y + row * cellH, cellW, cellH};
        drawRect(cell, color);
      }
    }
  }
}

void drawCount(const Rect& rect, int count) {
  if (count <= 1) {
    return;
  }

  Color color = {0.95f, 0.95f, 0.95f, 0.9f};
  int tens = count / 10;
  int ones = count % 10;

  float digitW = rect.w * 0.26f;
  float digitH = rect.h * 0.35f;
  float startX = rect.x + rect.w - digitW - 2.0f;
  float startY = rect.y + rect.h - digitH - 2.0f;

  if (tens > 0) {
    Rect tensRect = {startX - digitW - 2.0f, startY, digitW, digitH};
    drawDigit(tensRect, tens, color);
  }

  Rect onesRect = {startX, startY, digitW, digitH};
  drawDigit(onesRect, ones, color);
}

void drawSlotItem(const Rect& rect, BlockType type, const TextureAssets& textures) {
  unsigned int textureId = textureForBlock(type, textures);
  if (textureId == 0u) {
    return;
  }

  glBindTexture(GL_TEXTURE_2D, textureId);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  Rect inner = {rect.x + rect.w * 0.18f, rect.y + rect.h * 0.18f, rect.w * 0.64f, rect.h * 0.64f};
  drawTexturedRect(inner);
}

bool slotEmpty(const InventorySlot& slot) {
  return slot.type == BlockAir || slot.count <= 0;
}

bool placeOneFromCursor(InventorySlot& slot, InventorySlot& cursor) {
  if (cursor.type == BlockAir || cursor.count <= 0) {
    return false;
  }

  if (slot.type == BlockAir || slot.count <= 0) {
    slot = {cursor.type, 1};
    cursor.count -= 1;
    if (cursor.count <= 0) {
      cursor = {BlockAir, 0};
    }
    return true;
  }

  if (slot.type != cursor.type || slot.count >= kStackSize) {
    return false;
  }

  slot.count += 1;
  cursor.count -= 1;
  if (cursor.count <= 0) {
    cursor = {BlockAir, 0};
  }
  return true;
}

int craftingSlotIndexAtMouse(const UiState& ui, const UiLayout& layout, int craftingSize) {
  for (int row = 0; row < craftingSize; ++row) {
    for (int col = 0; col < craftingSize; ++col) {
      Rect rect = slotRect(layout.craftingX, layout.craftingY + row * (layout.slotSize + layout.padding),
                           layout.slotSize, layout.padding, col);
      if (pointInRect(ui.mouseX, ui.mouseY, rect)) {
        return row * 3 + col;
      }
    }
  }
  return -1;
}

bool handleShiftClick(Inventory& inventory, const UiState& ui, const UiLayout& layout) {
  for (int i = 0; i < kHotbarSlots; ++i) {
    Rect rect = slotRect(layout.hotbarX, layout.hotbarY, layout.slotSize, layout.padding, i);
    if (pointInRect(ui.mouseX, ui.mouseY, rect)) {
      InventorySlot& slot = inventory.hotbar[static_cast<size_t>(i)];
      if (slotEmpty(slot)) {
        return true;
      }
      int remaining = addToStorageRemaining(inventory, slot.type, slot.count);
      slot.count = remaining;
      if (slot.count <= 0) {
        slot = {BlockAir, 0};
      }
      return true;
    }
  }

  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < kHotbarSlots; ++col) {
      int index = row * kHotbarSlots + col;
      Rect rect = slotRect(layout.storageX, layout.storageY + row * (layout.slotSize + layout.padding),
                           layout.slotSize, layout.padding, col);
      if (pointInRect(ui.mouseX, ui.mouseY, rect)) {
        if (index < static_cast<int>(inventory.storage.size())) {
          InventorySlot& slot = inventory.storage[static_cast<size_t>(index)];
          if (slotEmpty(slot)) {
            return true;
          }
          int remaining = addToHotbarRemaining(inventory, slot.type, slot.count);
          slot.count = remaining;
          if (slot.count <= 0) {
            slot = {BlockAir, 0};
          }
          return true;
        }
        return true;
      }
    }
  }

  for (int row = 0; row < inventory.craftingSize; ++row) {
    for (int col = 0; col < inventory.craftingSize; ++col) {
      Rect rect = slotRect(layout.craftingX, layout.craftingY + row * (layout.slotSize + layout.padding),
                           layout.slotSize, layout.padding, col);
      if (pointInRect(ui.mouseX, ui.mouseY, rect)) {
        int index = row * 3 + col;
        InventorySlot& slot = inventory.crafting[static_cast<size_t>(index)];
        if (slotEmpty(slot)) {
          return true;
        }
        int remaining = addToInventoryRemaining(inventory, slot.type, slot.count);
        slot.count = remaining;
        if (slot.count <= 0) {
          slot = {BlockAir, 0};
        }
        return true;
      }
    }
  }

  Rect outputRect = {layout.outputX, layout.outputY, layout.outputSize, layout.outputSize};
  if (pointInRect(ui.mouseX, ui.mouseY, outputRect)) {
    craftToInventory(inventory, 9999);
    return true;
  }

  return false;
}

void clickSlot(InventorySlot& slot, InventorySlot& cursor) {
  if (cursor.type == BlockAir || cursor.count <= 0) {
    if (slot.type == BlockAir || slot.count <= 0) {
      return;
    }
    cursor = slot;
    slot = {BlockAir, 0};
    return;
  }

  if (slot.type == BlockAir || slot.count <= 0) {
    slot = cursor;
    cursor = {BlockAir, 0};
    return;
  }

  if (slot.type == cursor.type) {
    int space = kStackSize - slot.count;
    int move = cursor.count < space ? cursor.count : space;
    slot.count += move;
    cursor.count -= move;
    if (cursor.count <= 0) {
      cursor = {BlockAir, 0};
    }
    return;
  }

  InventorySlot temp = slot;
  slot = cursor;
  cursor = temp;
}

void rightClickSlot(InventorySlot& slot, InventorySlot& cursor) {
  if (cursor.type == BlockAir || cursor.count <= 0) {
    if (slot.type == BlockAir || slot.count <= 0) {
      return;
    }
    int take = (slot.count + 1) / 2;
    cursor = {slot.type, take};
    slot.count -= take;
    if (slot.count <= 0) {
      slot = {BlockAir, 0};
    }
    return;
  }

  if (slot.type == BlockAir || slot.count <= 0) {
    slot = {cursor.type, 1};
    cursor.count -= 1;
    if (cursor.count <= 0) {
      cursor = {BlockAir, 0};
    }
    return;
  }

  if (slot.type == cursor.type) {
    if (slot.count < kStackSize) {
      slot.count += 1;
      cursor.count -= 1;
      if (cursor.count <= 0) {
        cursor = {BlockAir, 0};
      }
    }
    return;
  }

  InventorySlot temp = slot;
  slot = cursor;
  cursor = temp;
}

bool handleSlotAction(Inventory& inventory, UiState& ui, const UiLayout& layout, ClickKind kind,
                      bool allowOutput, bool allowPalette) {
  auto applyClick = [&](InventorySlot& slot) {
    if (kind == ClickKind::Right) {
      rightClickSlot(slot, ui.cursor);
    } else {
      clickSlot(slot, ui.cursor);
    }
  };

  for (int i = 0; i < kHotbarSlots; ++i) {
    Rect rect = slotRect(layout.hotbarX, layout.hotbarY, layout.slotSize, layout.padding, i);
    if (pointInRect(ui.mouseX, ui.mouseY, rect)) {
      applyClick(inventory.hotbar[static_cast<size_t>(i)]);
      return true;
    }
  }

  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < kHotbarSlots; ++col) {
      int index = row * kHotbarSlots + col;
      Rect rect = slotRect(layout.storageX, layout.storageY + row * (layout.slotSize + layout.padding),
                           layout.slotSize, layout.padding, col);
      if (pointInRect(ui.mouseX, ui.mouseY, rect)) {
        if (index < static_cast<int>(inventory.storage.size())) {
          applyClick(inventory.storage[static_cast<size_t>(index)]);
        }
        return true;
      }
    }
  }

  for (int row = 0; row < inventory.craftingSize; ++row) {
    for (int col = 0; col < inventory.craftingSize; ++col) {
      Rect rect = slotRect(layout.craftingX, layout.craftingY + row * (layout.slotSize + layout.padding),
                           layout.slotSize, layout.padding, col);
      if (pointInRect(ui.mouseX, ui.mouseY, rect)) {
        int index = row * 3 + col;
        applyClick(inventory.crafting[static_cast<size_t>(index)]);
        return true;
      }
    }
  }

  Rect outputRect = {layout.outputX, layout.outputY, layout.outputSize, layout.outputSize};
  if (allowOutput && pointInRect(ui.mouseX, ui.mouseY, outputRect)) {
    takeCraftResult(inventory, ui.cursor);
    return true;
  }

  if (allowPalette) {
    int paletteRows = (kPaletteCount + kPaletteColumns - 1) / kPaletteColumns;
    for (int index = 0; index < kPaletteCount; ++index) {
      int row = index / kPaletteColumns;
      int col = index % kPaletteColumns;
      if (row >= paletteRows) {
        continue;
      }
      Rect rect = {
        layout.paletteX + layout.panelPadding + col * (layout.slotSize + layout.padding),
        layout.paletteY + layout.panelPadding + row * (layout.slotSize + layout.padding),
        layout.slotSize,
        layout.slotSize,
      };
      if (pointInRect(ui.mouseX, ui.mouseY, rect)) {
        ui.cursor = {kPaletteBlocks[index], kStackSize};
        return true;
      }
    }
  }

  return false;
}

void drawCrosshair(int width, int height) {
  float size = 10.0f;
  float thickness = 2.0f;
  float cx = width * 0.5f;
  float cy = height * 0.5f;

  Color color = {0.95f, 0.95f, 0.95f, 0.9f};
  Rect horiz = {cx - size, cy - thickness * 0.5f, size * 2.0f, thickness};
  Rect vert = {cx - thickness * 0.5f, cy - size, thickness, size * 2.0f};
  drawRect(horiz, color);
  drawRect(vert, color);
}
}

void initUi(UiState& ui) {
  ui.inventoryOpen = false;
  ui.openedFromTable = false;
  ui.prevToggleDown = false;
  ui.prevMouseDown = false;
  ui.prevRightDown = false;
  ui.dragCandidate = false;
  ui.dragging = false;
  ui.scaleX = 1.0f;
  ui.scaleY = 1.0f;
  ui.rightDragActive = false;
  ui.rightDragMask = 0u;
  ui.mouseX = 0.0;
  ui.mouseY = 0.0;
  ui.dragStartX = 0.0;
  ui.dragStartY = 0.0;
  ui.cursor = {BlockAir, 0};
}

bool toggleInventory(UiState& ui, bool toggleDown) {
  bool toggled = false;
  if (toggleDown && !ui.prevToggleDown) {
    ui.inventoryOpen = !ui.inventoryOpen;
    toggled = true;
  }
  ui.prevToggleDown = toggleDown;
  return toggled;
}

void updateUiMouse(UiState& ui, double x, double y) {
  ui.mouseX = x * ui.scaleX;
  ui.mouseY = y * ui.scaleY;
}

void handleInventoryClick(UiState& ui, Inventory& inventory, int width, int height, bool mouseDown, bool rightDown,
                          bool shiftDown) {
  if (!ui.inventoryOpen) {
    ui.prevMouseDown = mouseDown;
    ui.prevRightDown = rightDown;
    ui.dragCandidate = false;
    ui.dragging = false;
    ui.rightDragActive = false;
    ui.rightDragMask = 0u;
    return;
  }

  UiLayout layout = computeLayout(width, height, inventory.craftingSize);

  bool leftClick = mouseDown && !ui.prevMouseDown;
  bool rightClick = rightDown && !ui.prevRightDown;
  bool leftRelease = !mouseDown && ui.prevMouseDown;
  bool rightRelease = !rightDown && ui.prevRightDown;

  if (rightClick) {
    ui.rightDragActive = true;
    ui.rightDragMask = 0u;
  }
  if (rightRelease) {
    ui.rightDragActive = false;
    ui.rightDragMask = 0u;
  }

  if (ui.dragCandidate && mouseDown && !ui.dragging) {
    double dx = ui.mouseX - ui.dragStartX;
    double dy = ui.mouseY - ui.dragStartY;
    if (dx * dx + dy * dy > kDragThresholdSq) {
      ui.dragging = true;
    }
  }

  if (leftClick && shiftDown) {
    if (handleShiftClick(inventory, ui, layout)) {
      ui.prevMouseDown = mouseDown;
      ui.prevRightDown = rightDown;
      return;
    }
  }

  if (leftClick || rightClick) {
    ClickKind kind = (rightClick && !leftClick) ? ClickKind::Right : ClickKind::Left;
    bool cursorWasEmpty = slotEmpty(ui.cursor);
    bool handled = handleSlotAction(inventory, ui, layout, kind, true, true);

    if (rightClick) {
      int craftIndex = craftingSlotIndexAtMouse(ui, layout, inventory.craftingSize);
      if (craftIndex >= 0) {
        ui.rightDragMask |= (1u << craftIndex);
      }
    }

    if (leftClick) {
      if (handled && cursorWasEmpty && !slotEmpty(ui.cursor)) {
        ui.dragCandidate = true;
        ui.dragging = false;
        ui.dragStartX = ui.mouseX;
        ui.dragStartY = ui.mouseY;
      } else {
        ui.dragCandidate = false;
        ui.dragging = false;
      }
    } else if (rightClick) {
      ui.dragCandidate = false;
      ui.dragging = false;
    }

    if (handled) {
      ui.prevMouseDown = mouseDown;
      ui.prevRightDown = rightDown;
      return;
    }
  }

  if (rightDown && ui.rightDragActive && !slotEmpty(ui.cursor)) {
    int craftIndex = craftingSlotIndexAtMouse(ui, layout, inventory.craftingSize);
    if (craftIndex >= 0) {
      unsigned int mask = 1u << craftIndex;
      if ((ui.rightDragMask & mask) == 0u) {
        if (placeOneFromCursor(inventory.crafting[static_cast<size_t>(craftIndex)], ui.cursor)) {
          ui.rightDragMask |= mask;
        }
      }
    }
  }

  if (leftRelease && ui.dragging) {
    handleSlotAction(inventory, ui, layout, ClickKind::Left, false, false);
  }

  if (leftRelease) {
    ui.dragCandidate = false;
    ui.dragging = false;
  }

  ui.prevMouseDown = mouseDown;
  ui.prevRightDown = rightDown;
}

void drawUi(const Inventory& inventory, const TextureAssets& textures, const UiState& ui,
            int width, int height) {
  UiLayout layout = computeLayout(width, height, inventory.craftingSize);

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

  if (ui.inventoryOpen) {
    drawRect({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)}, {0.0f, 0.0f, 0.0f, 0.4f});

    Rect mainPanel = {layout.mainX, layout.mainY, layout.mainW, layout.mainH};
    drawRect(mainPanel, {0.12f, 0.12f, 0.12f, 0.95f});
    Rect innerMain = {mainPanel.x + 2.0f, mainPanel.y + 2.0f, mainPanel.w - 4.0f, mainPanel.h - 4.0f};
    drawRect(innerMain, {0.08f, 0.08f, 0.08f, 0.95f});

    Rect palettePanel = {layout.paletteX, layout.paletteY, layout.paletteW, layout.paletteH};
    drawRect(palettePanel, {0.12f, 0.12f, 0.12f, 0.95f});
    Rect innerPalette = {palettePanel.x + 2.0f, palettePanel.y + 2.0f, palettePanel.w - 4.0f, palettePanel.h - 4.0f};
    drawRect(innerPalette, {0.08f, 0.08f, 0.08f, 0.95f});

    for (int row = 0; row < inventory.craftingSize; ++row) {
      for (int col = 0; col < inventory.craftingSize; ++col) {
        Rect rect = slotRect(layout.craftingX, layout.craftingY + row * (layout.slotSize + layout.padding),
                             layout.slotSize, layout.padding, col);
        drawSlotBackground(rect, false);
      }
    }

    Rect outputRect = {layout.outputX, layout.outputY, layout.outputSize, layout.outputSize};
    drawSlotBackground(outputRect, false);

    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < kHotbarSlots; ++col) {
        Rect rect = slotRect(layout.storageX, layout.storageY + row * (layout.slotSize + layout.padding),
                             layout.slotSize, layout.padding, col);
        drawSlotBackground(rect, false);
      }
    }

    int paletteRows = (kPaletteCount + kPaletteColumns - 1) / kPaletteColumns;
    for (int index = 0; index < kPaletteCount; ++index) {
      int row = index / kPaletteColumns;
      int col = index % kPaletteColumns;
      if (row >= paletteRows) {
        continue;
      }
      Rect rect = {
        layout.paletteX + layout.panelPadding + col * (layout.slotSize + layout.padding),
        layout.paletteY + layout.panelPadding + row * (layout.slotSize + layout.padding),
        layout.slotSize,
        layout.slotSize,
      };
      drawSlotBackground(rect, false);
    }
  }

  for (int i = 0; i < kHotbarSlots; ++i) {
    Rect rect = slotRect(layout.hotbarX, layout.hotbarY, layout.slotSize, layout.padding, i);
    drawSlotBackground(rect, i == inventory.selected);
  }

  glEnable(GL_TEXTURE_2D);

  if (ui.inventoryOpen) {
    for (int row = 0; row < inventory.craftingSize; ++row) {
      for (int col = 0; col < inventory.craftingSize; ++col) {
        int index = row * 3 + col;
        Rect rect = slotRect(layout.craftingX, layout.craftingY + row * (layout.slotSize + layout.padding),
                             layout.slotSize, layout.padding, col);
        drawSlotItem(rect, inventory.crafting[static_cast<size_t>(index)].type, textures);
      }
    }

    CraftResult result;
    if (getCraftResult(inventory, result)) {
      Rect outputRect = {layout.outputX, layout.outputY, layout.outputSize, layout.outputSize};
      drawSlotItem(outputRect, result.type, textures);
      glDisable(GL_TEXTURE_2D);
      drawCount(outputRect, result.count);
      glEnable(GL_TEXTURE_2D);
    }

    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < kHotbarSlots; ++col) {
        int index = row * kHotbarSlots + col;
        Rect rect = slotRect(layout.storageX, layout.storageY + row * (layout.slotSize + layout.padding),
                             layout.slotSize, layout.padding, col);
        if (index < static_cast<int>(inventory.storage.size())) {
          drawSlotItem(rect, inventory.storage[static_cast<size_t>(index)].type, textures);
        }
      }
    }

    int paletteRows = (kPaletteCount + kPaletteColumns - 1) / kPaletteColumns;
    for (int index = 0; index < kPaletteCount; ++index) {
      int row = index / kPaletteColumns;
      int col = index % kPaletteColumns;
      if (row >= paletteRows) {
        continue;
      }
      Rect rect = {
        layout.paletteX + layout.panelPadding + col * (layout.slotSize + layout.padding),
        layout.paletteY + layout.panelPadding + row * (layout.slotSize + layout.padding),
        layout.slotSize,
        layout.slotSize,
      };
      drawSlotItem(rect, kPaletteBlocks[index], textures);
    }
  }

  for (int i = 0; i < kHotbarSlots; ++i) {
    Rect rect = slotRect(layout.hotbarX, layout.hotbarY, layout.slotSize, layout.padding, i);
    drawSlotItem(rect, inventory.hotbar[static_cast<size_t>(i)].type, textures);
  }

  glDisable(GL_TEXTURE_2D);
  for (int i = 0; i < kHotbarSlots; ++i) {
    Rect rect = slotRect(layout.hotbarX, layout.hotbarY, layout.slotSize, layout.padding, i);
    drawCount(rect, inventory.hotbar[static_cast<size_t>(i)].count);
  }

  if (ui.inventoryOpen) {
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < kHotbarSlots; ++col) {
        int index = row * kHotbarSlots + col;
        if (index < static_cast<int>(inventory.storage.size())) {
          Rect rect = slotRect(layout.storageX, layout.storageY + row * (layout.slotSize + layout.padding),
                               layout.slotSize, layout.padding, col);
          drawCount(rect, inventory.storage[static_cast<size_t>(index)].count);
        }
      }
    }

    for (int row = 0; row < inventory.craftingSize; ++row) {
      for (int col = 0; col < inventory.craftingSize; ++col) {
        int index = row * 3 + col;
        Rect rect = slotRect(layout.craftingX, layout.craftingY + row * (layout.slotSize + layout.padding),
                             layout.slotSize, layout.padding, col);
        drawCount(rect, inventory.crafting[static_cast<size_t>(index)].count);
      }
    }
  }

  if (ui.inventoryOpen) {
    if (ui.cursor.type != BlockAir && ui.cursor.count > 0) {
      Rect cursorRect = {static_cast<float>(ui.mouseX) - layout.slotSize * 0.5f,
                         static_cast<float>(ui.mouseY) - layout.slotSize * 0.5f,
                         layout.slotSize,
                         layout.slotSize};
      drawSlotBackground(cursorRect, false);
      glEnable(GL_TEXTURE_2D);
      drawSlotItem(cursorRect, ui.cursor.type, textures);
      glDisable(GL_TEXTURE_2D);
      drawCount(cursorRect, ui.cursor.count);
    }
  }

  if (!ui.inventoryOpen) {
    drawCrosshair(width, height);
  }

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
