#include "gtest/gtest.h"
#include "../thirdeye/savegame/dh_save.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace THIRDEYE::savegame;

TEST(DHSave, NativeHeaderAndFileRecords) {
    DHSave s;
    s.character = 0x12345678; s.seed = 0x89ABCDEF;
    s.settings[0] = 15; s.visited[2] = 1; s.level = 2;
    s.files["SAVEGAME\\ITEMS.DAT"] = {0, 1, 0xFF};
    const auto bytes = encodeDHSave(s);
    EXPECT_EQ(bytes[0], 0x78); EXPECT_EQ(bytes[4], 0xEF);
    EXPECT_EQ(bytes[8], 15); EXPECT_EQ(bytes[29], 1); EXPECT_EQ(bytes[52], 2);
    EXPECT_EQ(bytes[53], 19); // DOS path length including NUL
    EXPECT_EQ(bytes[54], 0);
    EXPECT_TRUE(std::all_of(bytes.end() - 4, bytes.end(), [](auto c) { return c == 0xFF; }));
    DHSave restored; std::string error;
    ASSERT_TRUE(decodeDHSave(bytes, restored, error)) << error;
    EXPECT_EQ(restored.character, s.character);
    EXPECT_EQ(restored.seed, s.seed);
    EXPECT_EQ(restored.settings, s.settings);
    EXPECT_EQ(restored.visited, s.visited);
    EXPECT_EQ(restored.level, s.level);
    EXPECT_EQ(restored.files, s.files);
}

TEST(DHSave, EveryTruncationIsRejected) {
    DHSave s;
    s.files["SAVEGAME\\FEA00.DAT"] = {1, 2, 3};
    auto bytes = encodeDHSave(s);
    DHSave restored; std::string error;
    for (size_t n = 0; n < bytes.size(); ++n)
        EXPECT_FALSE(decodeDHSave(std::span(bytes).first(n), restored, error)) << n;
}

TEST(DHSave, RejectsUnsafePathsAndInvalidLevels) {
    DHSave s, restored;
    std::string error;
    for (const char* name : {"../HACK.RES", "SAVEGAME\\..\\HACK.RES", "SAVEGAME\\HACK.RES"}) {
        s.files.clear(); s.files[name] = {1};
        EXPECT_FALSE(decodeDHSave(encodeDHSave(s), restored, error)) << name;
    }
    s.files.clear(); s.files[std::string("SAVEGAME\\FEA00.DAT\0evil", 23)] = {1};
    EXPECT_FALSE(decodeDHSave(encodeDHSave(s), restored, error));
    s.files.clear(); s.settings[0] = 16;
    EXPECT_FALSE(decodeDHSave(encodeDHSave(s), restored, error));
    s.settings[0] = 0; s.level = 10;
    EXPECT_FALSE(decodeDHSave(encodeDHSave(s), restored, error));
}

TEST(DHSave, OriginalDOSSave) {
    const char* path = std::getenv("THIRDEYE_TEST_DH_SAVE");
    if (!path) GTEST_SKIP() << "Set THIRDEYE_TEST_DH_SAVE to a DOS SAVE_nn.BIN";
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    DHSave restored;
    std::string error;
    ASSERT_TRUE(decodeDHSave(bytes, restored, error)) << error;
    EXPECT_TRUE(restored.files.contains("SAVEGAME\\LEVELS.DAT"));
    EXPECT_TRUE(restored.files.contains("SAVEGAME\\ITEMS.DAT"));
    EXPECT_TRUE(restored.files.contains("SAVEGAME\\VISIBLE.DAT"));
    DHSave again;
    ASSERT_TRUE(decodeDHSave(encodeDHSave(restored), again, error)) << error;
    EXPECT_EQ(restored.files, again.files);
    EXPECT_EQ(restored.seed, again.seed);
    EXPECT_EQ(restored.character, again.character);
}
