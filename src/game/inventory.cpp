#include "game/inventory.h"

#include <algorithm>

void initInventory(Inventory& inventory) {
  inventory.hotbar.assign(kHotbarSlots, {BlockAir, 0});
  inventory.storage.assign(kStorageSlots, {BlockAir, 0});
  inventory.crafting.assign(kCraftingSlots, {BlockAir, 0});
  inventory.craftingSize = 2;
  inventory.selected = 0;

  inventory.hotbar[0] = {BlockGrass, kStackSize};
  inventory.hotbar[1] = {BlockDirt, kStackSize};
  inventory.hotbar[2] = {BlockStone, kStackSize};
  inventory.hotbar[3] = {BlockWood, kStackSize};
  inventory.hotbar[4] = {BlockLeaves, kStackSize};
  inventory.hotbar[5] = {BlockTorch, kStackSize};
}

void setSelectedSlot(Inventory& inventory, int index) {
  if (index < 0 || index >= kHotbarSlots) {
    return;
  }
  inventory.selected = index;
}

BlockType selectedBlock(const Inventory& inventory) {
  if (inventory.selected < 0 || inventory.selected >= kHotbarSlots) {
    return BlockAir;
  }
  const InventorySlot& slot = inventory.hotbar[static_cast<size_t>(inventory.selected)];
  return slot.count > 0 ? slot.type : BlockAir;
}

void setHotbarSlot(Inventory& inventory, int index, BlockType type, int count) {
  if (index < 0 || index >= kHotbarSlots) {
    return;
  }
  int clamped = count < 0 ? 0 : (count > kStackSize ? kStackSize : count);
  if (clamped == 0 || type == BlockAir) {
    inventory.hotbar[static_cast<size_t>(index)] = {BlockAir, 0};
  } else {
    inventory.hotbar[static_cast<size_t>(index)] = {type, clamped};
  }
}

static int addToSlots(std::vector<InventorySlot>& slots, BlockType type, int amount) {
  if (type == BlockAir || amount <= 0) {
    return amount;
  }

  for (InventorySlot& slot : slots) {
    if (slot.type == type && slot.count < kStackSize) {
      int space = kStackSize - slot.count;
      int add = amount < space ? amount : space;
      slot.count += add;
      amount -= add;
      if (amount == 0) {
        return 0;
      }
    }
  }

  for (InventorySlot& slot : slots) {
    if (slot.type == BlockAir) {
      int add = amount < kStackSize ? amount : kStackSize;
      slot.type = type;
      slot.count = add;
      amount -= add;
      if (amount == 0) {
        return 0;
      }
    }
  }

  return amount;
}

bool addToInventory(Inventory& inventory, BlockType type, int amount) {
  int remaining = addToInventoryRemaining(inventory, type, amount);
  return remaining < amount;
}

int addToInventoryRemaining(Inventory& inventory, BlockType type, int amount) {
  int remaining = addToSlots(inventory.hotbar, type, amount);
  remaining = addToSlots(inventory.storage, type, remaining);
  return remaining;
}

int addToHotbarRemaining(Inventory& inventory, BlockType type, int amount) {
  return addToSlots(inventory.hotbar, type, amount);
}

int addToStorageRemaining(Inventory& inventory, BlockType type, int amount) {
  return addToSlots(inventory.storage, type, amount);
}

static int capacityForSlots(const std::vector<InventorySlot>& slots, BlockType type) {
  if (type == BlockAir) {
    return 0;
  }

  int capacity = 0;
  for (const InventorySlot& slot : slots) {
    if (slot.type == type && slot.count < kStackSize) {
      capacity += kStackSize - slot.count;
    }
  }

  for (const InventorySlot& slot : slots) {
    if (slot.type == BlockAir || slot.count <= 0) {
      capacity += kStackSize;
    }
  }

  return capacity;
}

int inventoryCapacityFor(const Inventory& inventory, BlockType type) {
  return capacityForSlots(inventory.hotbar, type) + capacityForSlots(inventory.storage, type);
}

bool consumeSelected(Inventory& inventory, int amount) {
  if (amount <= 0 || inventory.selected < 0 || inventory.selected >= kHotbarSlots) {
    return false;
  }

  InventorySlot& slot = inventory.hotbar[static_cast<size_t>(inventory.selected)];
  if (slot.type == BlockAir || slot.count < amount) {
    return false;
  }

  slot.count -= amount;
  if (slot.count <= 0) {
    slot.type = BlockAir;
    slot.count = 0;
  }
  return true;
}

void setCraftingSize(Inventory& inventory, int size) {
  if (size < 2) {
    size = 2;
  } else if (size > 3) {
    size = 3;
  }

  if (size == inventory.craftingSize) {
    return;
  }

  if (size < inventory.craftingSize) {
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        if (row < size && col < size) {
          continue;
        }
        int index = row * 3 + col;
        InventorySlot& slot = inventory.crafting[static_cast<size_t>(index)];
        if (slot.type == BlockAir || slot.count <= 0) {
          slot = {BlockAir, 0};
          continue;
        }
        int remaining = addToInventoryRemaining(inventory, slot.type, slot.count);
        if (remaining == 0) {
          slot = {BlockAir, 0};
        } else {
          slot.count = remaining;
        }
      }
    }
  }

  inventory.craftingSize = size;
}

struct Recipe {
  int width;
  int height;
  BlockType output;
  int outputCount;
  BlockType pattern[9];
};

static const Recipe kRecipes[] = {
  {1, 1, BlockPlanks, 4, {BlockWood}},
  {1, 2, BlockStick, 4, {BlockPlanks, BlockPlanks}},
  {2, 2, BlockCraftingTable, 1, {BlockPlanks, BlockPlanks, BlockPlanks, BlockPlanks}},
  {2, 2, BlockCobblestone, 4, {BlockStone, BlockStone, BlockStone, BlockStone}},
  {2, 2, BlockStoneBricks, 4, {BlockCobblestone, BlockCobblestone, BlockCobblestone, BlockCobblestone}},
  {2, 2, BlockGravel, 4, {BlockDirt, BlockDirt, BlockDirt, BlockDirt}},
  {2, 2, BlockSand, 4, {BlockGravel, BlockGravel, BlockGravel, BlockGravel}},
  {2, 2, BlockSandstone, 4, {BlockSand, BlockSand, BlockSand, BlockSand}},
  {1, 1, BlockGlass, 1, {BlockSand}},
  {2, 2, BlockBricks, 4, {BlockStoneBricks, BlockStoneBricks, BlockStoneBricks, BlockStoneBricks}},
  {3, 3, BlockFurnace, 1,
   {BlockCobblestone, BlockCobblestone, BlockCobblestone,
    BlockCobblestone, BlockAir, BlockCobblestone,
    BlockCobblestone, BlockCobblestone, BlockCobblestone}},
};

static BlockType craftingAt(const Inventory& inventory, int row, int col) {
  if (row < 0 || col < 0 || row >= inventory.craftingSize || col >= inventory.craftingSize) {
    return BlockAir;
  }
  int index = row * 3 + col;
  const InventorySlot& slot = inventory.crafting[static_cast<size_t>(index)];
  return slot.count > 0 ? slot.type : BlockAir;
}

bool getCraftResult(const Inventory& inventory, CraftResult& result) {
  result.type = BlockAir;
  result.count = 0;
  result.usedSlots.clear();

  int minRow = 3;
  int maxRow = -1;
  int minCol = 3;
  int maxCol = -1;

  for (int row = 0; row < inventory.craftingSize; ++row) {
    for (int col = 0; col < inventory.craftingSize; ++col) {
      if (craftingAt(inventory, row, col) != BlockAir) {
        if (row < minRow) minRow = row;
        if (row > maxRow) maxRow = row;
        if (col < minCol) minCol = col;
        if (col > maxCol) maxCol = col;
      }
    }
  }

  if (maxRow < minRow || maxCol < minCol) {
    return false;
  }

  int width = maxCol - minCol + 1;
  int height = maxRow - minRow + 1;

  for (const Recipe& recipe : kRecipes) {
    if (recipe.width != width || recipe.height != height) {
      continue;
    }

    bool matches = true;
    std::vector<int> usedSlots;
    for (int row = 0; row < height && matches; ++row) {
      for (int col = 0; col < width; ++col) {
        BlockType expected = recipe.pattern[row * recipe.width + col];
        BlockType actual = craftingAt(inventory, minRow + row, minCol + col);
        if (expected != actual) {
          matches = false;
          break;
        }
        if (expected != BlockAir) {
          int index = (minRow + row) * 3 + (minCol + col);
          usedSlots.push_back(index);
        }
      }
    }

    if (matches) {
      result.type = recipe.output;
      result.count = recipe.outputCount;
      result.usedSlots = std::move(usedSlots);
      return true;
    }
  }

  return false;
}

bool takeCraftResult(Inventory& inventory, InventorySlot& cursor) {
  CraftResult result;
  if (!getCraftResult(inventory, result)) {
    return false;
  }

  if (cursor.type != BlockAir && cursor.type != result.type) {
    return false;
  }
  if (cursor.count + result.count > kStackSize) {
    return false;
  }

  cursor.type = result.type;
  cursor.count += result.count;

  for (int index : result.usedSlots) {
    InventorySlot& slot = inventory.crafting[static_cast<size_t>(index)];
    if (slot.count > 0) {
      slot.count -= 1;
      if (slot.count <= 0) {
        slot.type = BlockAir;
        slot.count = 0;
      }
    }
  }

  return true;
}

int craftToInventory(Inventory& inventory, int maxCrafts) {
  if (maxCrafts <= 0) {
    return 0;
  }

  CraftResult result;
  if (!getCraftResult(inventory, result)) {
    return 0;
  }

  int capacity = inventoryCapacityFor(inventory, result.type);
  if (capacity < result.count) {
    return 0;
  }

  int maxByCapacity = capacity / result.count;
  int crafts = std::min(maxCrafts, maxByCapacity);

  int minInput = 0;
  bool first = true;
  for (int index : result.usedSlots) {
    InventorySlot& slot = inventory.crafting[static_cast<size_t>(index)];
    if (first) {
      minInput = slot.count;
      first = false;
    } else {
      minInput = std::min(minInput, slot.count);
    }
  }

  if (first || minInput <= 0) {
    return 0;
  }

  crafts = std::min(crafts, minInput);
  if (crafts <= 0) {
    return 0;
  }

  for (int i = 0; i < crafts; ++i) {
    addToInventoryRemaining(inventory, result.type, result.count);
    for (int index : result.usedSlots) {
      InventorySlot& slot = inventory.crafting[static_cast<size_t>(index)];
      slot.count -= 1;
      if (slot.count <= 0) {
        slot = {BlockAir, 0};
      }
    }
  }

  return crafts;
}
