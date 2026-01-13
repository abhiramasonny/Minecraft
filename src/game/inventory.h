#pragma once

#include "game/world.h"

#include <vector>

struct InventorySlot {
  BlockType type;
  int count;
};

struct Inventory {
  std::vector<InventorySlot> hotbar;
  std::vector<InventorySlot> storage;
  std::vector<InventorySlot> crafting;
  int craftingSize;
  int selected;
};

constexpr int kHotbarSlots = 9;
constexpr int kStorageSlots = 27;
constexpr int kCraftingSlots = 9;
constexpr int kStackSize = 64;

struct CraftResult {
  BlockType type;
  int count;
  std::vector<int> usedSlots;
};

void initInventory(Inventory& inventory);
void setSelectedSlot(Inventory& inventory, int index);
BlockType selectedBlock(const Inventory& inventory);
void setHotbarSlot(Inventory& inventory, int index, BlockType type, int count);
bool consumeSelected(Inventory& inventory, int amount);
bool addToInventory(Inventory& inventory, BlockType type, int amount);
int addToInventoryRemaining(Inventory& inventory, BlockType type, int amount);
int addToHotbarRemaining(Inventory& inventory, BlockType type, int amount);
int addToStorageRemaining(Inventory& inventory, BlockType type, int amount);
int inventoryCapacityFor(const Inventory& inventory, BlockType type);
void setCraftingSize(Inventory& inventory, int size);
bool getCraftResult(const Inventory& inventory, CraftResult& result);
bool takeCraftResult(Inventory& inventory, InventorySlot& cursor);
int craftToInventory(Inventory& inventory, int maxCrafts);
