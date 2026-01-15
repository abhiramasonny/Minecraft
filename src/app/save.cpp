#include "app/save.h"

#include <filesystem>
#include <fstream>

namespace {
constexpr uint32_t kSaveVersion = 1u;
constexpr int kChunkSize = 16;
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;

template <typename T>
bool writeValue(std::ofstream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
  return static_cast<bool>(out);
}

template <typename T>
bool readValue(std::ifstream& in, T& value) {
  in.read(reinterpret_cast<char*>(&value), sizeof(T));
  return static_cast<bool>(in);
}

bool writeSlot(std::ofstream& out, const InventorySlot& slot) {
  uint8_t type = static_cast<uint8_t>(slot.type);
  int32_t count = slot.count;
  return writeValue(out, type) && writeValue(out, count);
}

bool readSlot(std::ifstream& in, InventorySlot& slot) {
  uint8_t type = 0u;
  int32_t count = 0;
  if (!readValue(in, type) || !readValue(in, count)) {
    return false;
  }
  slot.type = static_cast<BlockType>(type);
  slot.count = count;
  if (slot.count <= 0) {
    slot.type = BlockAir;
    slot.count = 0;
  }
  return true;
}
} // namespace

bool saveGame(const char* path, const World& world, const Player& player, const Inventory& inventory) {
  std::filesystem::path savePath(path);
  std::filesystem::create_directories(savePath.parent_path());

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }

  const char magic[4] = {'M', 'T', 'F', 'C'};
  out.write(magic, sizeof(magic));
  if (!out) {
    return false;
  }

  uint32_t seed = world.seed;
  int32_t renderDistance = world.renderDistance;
  int32_t chunkSize = kChunkSize;
  int32_t worldHeightChunks = 12;
  uint32_t chunkCount = static_cast<uint32_t>(world.chunks.size());

  if (!writeValue(out, kSaveVersion)
      || !writeValue(out, seed)
      || !writeValue(out, renderDistance)
      || !writeValue(out, chunkSize)
      || !writeValue(out, worldHeightChunks)
      || !writeValue(out, chunkCount)) {
    return false;
  }

  for (const auto& entry : world.chunks) {
    const Chunk& chunk = entry.second;
    int32_t cx = chunk.cx;
    int32_t cy = chunk.cy;
    int32_t cz = chunk.cz;
    uint32_t blocksSize = static_cast<uint32_t>(chunk.blocks.size());
    if (!writeValue(out, cx)
        || !writeValue(out, cy)
        || !writeValue(out, cz)
        || !writeValue(out, blocksSize)) {
      return false;
    }
    out.write(reinterpret_cast<const char*>(chunk.blocks.data()), chunk.blocks.size());
    if (!out) {
      return false;
    }
  }

  if (!writeValue(out, player.position.x)
      || !writeValue(out, player.position.y)
      || !writeValue(out, player.position.z)
      || !writeValue(out, player.yaw)
      || !writeValue(out, player.pitch)
      || !writeValue(out, player.verticalVelocity)) {
    return false;
  }
  uint8_t flyMode = player.flyMode ? 1u : 0u;
  if (!writeValue(out, flyMode)) {
    return false;
  }

  int32_t selected = inventory.selected;
  int32_t craftingSize = inventory.craftingSize;
  uint32_t hotbarCount = static_cast<uint32_t>(inventory.hotbar.size());
  uint32_t storageCount = static_cast<uint32_t>(inventory.storage.size());
  uint32_t craftingCount = static_cast<uint32_t>(inventory.crafting.size());

  if (!writeValue(out, selected)
      || !writeValue(out, craftingSize)
      || !writeValue(out, hotbarCount)
      || !writeValue(out, storageCount)
      || !writeValue(out, craftingCount)) {
    return false;
  }

  for (const InventorySlot& slot : inventory.hotbar) {
    if (!writeSlot(out, slot)) {
      return false;
    }
  }
  for (const InventorySlot& slot : inventory.storage) {
    if (!writeSlot(out, slot)) {
      return false;
    }
  }
  for (const InventorySlot& slot : inventory.crafting) {
    if (!writeSlot(out, slot)) {
      return false;
    }
  }

  return static_cast<bool>(out);
}

bool loadGame(const char* path, World& world, Player& player, Inventory& inventory, const TextureAssets& textures) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }

  char magic[4] = {};
  in.read(magic, sizeof(magic));
  if (!in || magic[0] != 'M' || magic[1] != 'T' || magic[2] != 'F' || magic[3] != 'C') {
    return false;
  }

  uint32_t version = 0;
  uint32_t seed = 0;
  int32_t renderDistance = 0;
  int32_t chunkSize = 0;
  int32_t worldHeightChunks = 0;
  uint32_t chunkCount = 0;

  if (!readValue(in, version)
      || !readValue(in, seed)
      || !readValue(in, renderDistance)
      || !readValue(in, chunkSize)
      || !readValue(in, worldHeightChunks)
      || !readValue(in, chunkCount)) {
    return false;
  }

  if (version != kSaveVersion || chunkSize != kChunkSize || worldHeightChunks != 12) {
    return false;
  }

  world.seed = seed;
  world.renderDistance = renderDistance;
  if (world.renderDistance < 12) {
    world.renderDistance = 12;
  }
  world.chunks.clear();
  world.visibleChunks.clear();
  world.pendingChunks.clear();
  world.queuedChunks.clear();
  world.queuedMeshes.clear();
  world.buildTasks.clear();
  world.meshTasks.clear();

  for (uint32_t i = 0; i < chunkCount; ++i) {
    int32_t cx = 0;
    int32_t cy = 0;
    int32_t cz = 0;
    uint32_t blocksSize = 0;
    if (!readValue(in, cx)
        || !readValue(in, cy)
        || !readValue(in, cz)
        || !readValue(in, blocksSize)) {
      return false;
    }

    if (blocksSize != static_cast<uint32_t>(kChunkVolume)) {
      in.seekg(static_cast<std::streamoff>(blocksSize), std::ios::cur);
      if (!in) {
        return false;
      }
      continue;
    }

    Chunk chunk = {};
    chunk.cx = cx;
    chunk.cy = cy;
    chunk.cz = cz;
    chunk.vbo = 0;
    chunk.ibo = 0;
    chunk.meshDirty = false;
    chunk.lodStep = 1;
    chunk.blocks.assign(kChunkVolume, 0u);
    in.read(reinterpret_cast<char*>(chunk.blocks.data()), chunk.blocks.size());
    if (!in) {
      return false;
    }
    world.chunks.emplace(ChunkCoord{cx, cy, cz}, std::move(chunk));
  }

  if (!readValue(in, player.position.x)
      || !readValue(in, player.position.y)
      || !readValue(in, player.position.z)
      || !readValue(in, player.yaw)
      || !readValue(in, player.pitch)
      || !readValue(in, player.verticalVelocity)) {
    return false;
  }

  uint8_t flyMode = 0u;
  if (!readValue(in, flyMode)) {
    return false;
  }
  player.flyMode = flyMode != 0u;
  syncPlayerCamera(player);
  player.firstMouse = true;

  int32_t selected = 0;
  int32_t craftingSize = 2;
  uint32_t hotbarCount = 0;
  uint32_t storageCount = 0;
  uint32_t craftingCount = 0;

  if (!readValue(in, selected)
      || !readValue(in, craftingSize)
      || !readValue(in, hotbarCount)
      || !readValue(in, storageCount)
      || !readValue(in, craftingCount)) {
    return false;
  }

  inventory.hotbar.assign(kHotbarSlots, {BlockAir, 0});
  inventory.storage.assign(kStorageSlots, {BlockAir, 0});
  inventory.crafting.assign(kCraftingSlots, {BlockAir, 0});
  inventory.craftingSize = craftingSize;
  inventory.selected = selected;

  for (uint32_t i = 0; i < hotbarCount && i < inventory.hotbar.size(); ++i) {
    if (!readSlot(in, inventory.hotbar[i])) {
      return false;
    }
  }
  for (uint32_t i = 0; i < storageCount && i < inventory.storage.size(); ++i) {
    if (!readSlot(in, inventory.storage[i])) {
      return false;
    }
  }
  for (uint32_t i = 0; i < craftingCount && i < inventory.crafting.size(); ++i) {
    if (!readSlot(in, inventory.crafting[i])) {
      return false;
    }
  }

  rebuildWorldMeshes(world, textures);

  return static_cast<bool>(in);
}
