#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace THIRDEYE::savegame {
// AESOP.EXE save_game/prepare_save/explode_save: plain binary header plus
// length-prefixed DOS files. The original calls this packing, not compression.
struct DHSave {
    uint32_t character = 0, seed = 0;
    std::array<uint8_t, 19> settings{};
    std::array<uint8_t, 25> visited{};
    uint8_t level = 0;
    std::map<std::string, std::vector<uint8_t>> files;
};
std::vector<uint8_t> encodeDHSave(const DHSave& save);
bool decodeDHSave(std::span<const uint8_t> bytes, DHSave& save, std::string& error);
}
