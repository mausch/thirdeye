#include "gtest/gtest.h"
#include "config.hpp"
#include "../thirdeye/runtime/internal.hpp"
#include "../thirdeye/resources/res.hpp"
#include "../thirdeye/savegame/transfer.hpp"
#include "../thirdeye/savegame/lvl_tmp.hpp"
#include "../thirdeye/vm/events.hpp"
#include "../thirdeye/vm/objects.hpp"
#include <chrono>
#include <fstream>
#include <iterator>

namespace {
class RuntimeWindows : public ::testing::Test {
protected:
    RESOURCES::Resource res{std::filesystem::path(TESTS_DATA_DIR) / "SAMPLE.RES"};
    VM::ObjectSystem objects;
    VM::EventSystem events{objects};
    std::map<int32_t, int32_t> mem;
    THIRDEYE::savegame::TransferState transfer;
    VM::Interpreter vm{std::vector<uint8_t>(14)};
    THIRDEYE::runtime::Context ctx{objects, events, nullptr, res, mem, transfer, vm};

    void SetUp() override {
        VM::SopClass kernel;
        kernel.number = 1;
        kernel.header.parent = 0xFFFFFFFF;
        kernel.header.static_size = 1;
        kernel.code.resize(14);
        kernel.handlers[208] = 14;
        // MHDR(THIS), LSB 0, INC, SSB 0, END: count delivered attack clicks.
        const uint8_t attack[] = {2, 0, 0x39, 0, 0, 0x1B, 0x3C, 0, 0, 0x56};
        kernel.code.insert(kernel.code.end(), std::begin(attack), std::end(attack));
        objects.addClass(std::move(kernel));
        objects.createInstanceAt(0, 1);

        VM::SopClass menu;
        menu.number = 2;
        menu.header.parent = 0xFFFFFFFF;
        menu.code.resize(14);
        objects.addClass(std::move(menu));
    }

    VM::Value destroy(int owner) {
        VM::Value result = 0;
        EXPECT_TRUE(THIRDEYE::runtime::rtobject::tryHandle(
            ctx, "destroy_object", {owner}, result));
        return result;
    }

    bool live(int handle) {
        int32_t x0, y0, x1, y1;
        return events.windowRect(handle, x0, y0, x1, y1);
    }
};

TEST_F(RuntimeWindows, RepeatedMenuDestructionPreservesAttackRegion) {
    const int attack = events.assignWindow(0, 117, 161, 176, 173);
    events.notify(0, 208, VM::SYS_LEFT_CLICK_REGION, attack);

    for (int i = 0; i < 300; ++i) {
        objects.createInstanceAt(2006, 2);
        const int front = events.assignWindow(2006, 0, 0, 175, 119);
        const int back = events.assignWindow(2006, 0, 0, 175, 119, true);
        ASSERT_GE(front, 2);
        ASSERT_GE(back, 2);
        destroy(2006);
        EXPECT_EQ(objects.objectLookup(2006), -1);
        EXPECT_EQ(events.liveWindowCount(), 3u);
        EXPECT_FALSE(live(front));
        EXPECT_FALSE(live(back));
        ASSERT_TRUE(live(attack));
    }

    events.mouseMove(146, 165);
    events.mouseButton(true, false);
    events.drainEventQueue();
    EXPECT_EQ(*objects.staticsPtr(0, 0, 1), 1);
    EXPECT_TRUE(live(0));
    EXPECT_TRUE(live(1));
}

TEST_F(RuntimeWindows, DestructorCanUseAndAllocateWindowsBeforeCleanup) {
    VM::SopClass menu;
    menu.number = 3;
    menu.header.parent = 0xFFFFFFFF;
    menu.code.resize(14);
    menu.handlers[1] = 14; // MSG_DESTROY
    menu.imports[0] = "destroy_probe";
    // MHDR(THIS), RCRS 0, CALL 0, END.
    const uint8_t body[] = {2, 0, 0x20, 0, 0, 0x21, 0, 0x56};
    menu.code.insert(menu.code.end(), std::begin(body), std::end(body));
    objects.addClass(std::move(menu));
    objects.createInstanceAt(2006, 3);
    const int window = events.assignWindow(2006, 10, 10, 50, 50);
    int created = -1;
    objects.setRuntimeCall([&](VM::Interpreter&, const std::string& name,
                               const std::vector<VM::Value>&) -> VM::Value {
        EXPECT_EQ(name, "destroy_probe");
        EXPECT_TRUE(live(window));
        EXPECT_EQ(objects.objectLookup(2006), 2006);
        created = events.assignWindow(2006, 0, 0, 10, 10);
        return 7;
    });

    EXPECT_EQ(destroy(2006), 7);
    ASSERT_GE(created, 2);
    EXPECT_FALSE(live(window));
    EXPECT_FALSE(live(created));
    EXPECT_EQ(events.liveWindowCount(), 2u);
}

TEST_F(RuntimeWindows, LevelRestoreReleasesDeadSlotsWindows) {
    const auto dir = std::filesystem::temp_directory_path() /
        ("thirdeye_window_restore_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir / "SAVEGAME");
    struct Cleanup {
        std::filesystem::path dir;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    } cleanup{dir};
    std::filesystem::copy_file(std::filesystem::path(TESTS_DATA_DIR) / "SAMPLE.RES",
                               dir / "SAMPLE.RES");
    RESOURCES::Resource localRes{dir / "SAMPLE.RES"};
    // SF_BIN: dead slot 1000, with no saved statics.
    const uint8_t saved[] = {0x1A, 0xE8, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0};
    {
        std::ofstream file(dir / "SAVEGAME/LVL03.TMP", std::ios::binary);
        file.write(reinterpret_cast<const char*>(saved), sizeof(saved));
        ASSERT_TRUE(file.good());
    }
    VM::SopClass dungeon;
    dungeon.number = 1381;
    dungeon.header.parent = 0xFFFFFFFF;
    dungeon.header.static_size = 1024 + 3 * 2048;
    dungeon.code.resize(14);
    objects.addClass(std::move(dungeon));
    objects.createInstanceAt(2001, 1381);
    objects.createInstanceAt(1000, 2);
    const int window = events.assignWindow(1000, 0, 0, 20, 20);
    const int attack = events.assignWindow(0, 117, 161, 176, 173);

    EXPECT_EQ(THIRDEYE::savegame::loadLevelObjects(3, objects, events, localRes), 0);
    EXPECT_EQ(objects.objectLookup(1000), -1);
    EXPECT_FALSE(live(window));
    EXPECT_TRUE(live(attack));
    EXPECT_EQ(events.liveWindowCount(), 3u);
}
} // namespace
