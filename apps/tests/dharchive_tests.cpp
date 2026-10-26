#include "gtest/gtest.h"
#include "../launcher/dharchive.hpp"

#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iterator>

TEST(DhArchive, RejectsTruncatedImage) {
    std::vector<ArjEntry> files;
    std::string err;
    EXPECT_FALSE(extractDungeonHackCD({}, files, err));
    EXPECT_FALSE(err.empty());
    std::vector<uint8_t> bytes(2352 * 17 - 1);
    EXPECT_FALSE(extractDungeonHackCD(bytes, files, err));
}

namespace {
std::vector<uint8_t> image() {
    std::vector<uint8_t> b(2352 * 21);
    for (size_t i = 0; i < b.size(); i += 2352) {
        std::fill(b.begin() + i + 1, b.begin() + i + 11, 0xFF);
        b[i + 15] = 1;
    }
    auto* p = b.data() + 16 * 2352 + 16;
    p[0] = 1;
    std::copy_n("CD001", 5, p + 1);
    p[6] = 1;
    p[129] = 8;
    p[156] = 34;
    p[158] = 20; // root LBA
    p[167] = 8;  // root size = 2048
    return b;
}
}

TEST(DhArchive, ValidatesCDAndDirectoryBounds) {
    std::vector<ArjEntry> files;
    std::string err;
    auto b = image();
    EXPECT_FALSE(extractDungeonHackCD(b, files, err));
    EXPECT_EQ(err, "DATA1.ARJ not found on CD");
    b[16 * 2352 + 16 + 158] = 250;
    EXPECT_FALSE(extractDungeonHackCD(b, files, err));
    EXPECT_EQ(err, "CD root directory outside image");
    b = image();
    b[20 * 2352 + 16] = 33;
    EXPECT_FALSE(extractDungeonHackCD(b, files, err));
    EXPECT_EQ(err, "Malformed ISO9660 directory record");
    b = image();
    b[15] = 2;
    EXPECT_FALSE(extractDungeonHackCD(b, files, err));
    EXPECT_EQ(err, "Invalid MODE1 CD sector");
}

// Optional integration test against the user-provided original distribution.
TEST(DhArchive, OriginalCD) {
    const char* path = std::getenv("THIRDEYE_TEST_DH_BIN");
    if (!path) GTEST_SKIP() << "Set THIRDEYE_TEST_DH_BIN to the original CD BIN";
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {});
    std::vector<ArjEntry> files;
    std::string err;
    ASSERT_TRUE(extractDungeonHackCD(b, files, err)) << err;
    EXPECT_GT(files.size(), 10u);
    if (const char* reference = std::getenv("THIRDEYE_TEST_DH_DIR")) {
        for (const auto& file : files) {
            std::string name = file.name;
            std::replace(name.begin(), name.end(), '\\', '/');
            std::ifstream expected(std::filesystem::path(reference) / name, std::ios::binary);
            ASSERT_TRUE(expected.good()) << name;
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(expected)), {});
            EXPECT_EQ(file.data, bytes) << name;
        }
    }
    for (const char* name : {"HACK.RES", "OPEN.RES", "HACK.BAT"})
        EXPECT_TRUE(std::any_of(files.begin(), files.end(), [&](const auto& f) {
            return f.name == name && !f.data.empty();
        })) << name;
}
