#pragma once

#include "unarj.hpp"
#include <span>

// Extract DATA1.ARJ from DungeonHackUSA's MODE1/2352 CD image, then decode
// the installed game files. All offsets are checked before reading.
bool extractDungeonHackCD(std::span<const uint8_t> bin,
                          std::vector<ArjEntry>& files, std::string& err);
