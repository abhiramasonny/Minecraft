#pragma once

#include "game/inventory.h"
#include "game/player.h"
#include "render/textures.h"
#include "game/world.h"

bool saveGame(const char* path, const World& world, const Player& player, const Inventory& inventory);
bool loadGame(const char* path, World& world, Player& player, Inventory& inventory, const TextureAssets& textures);
