#include "gtest/gtest.h"
#include "config.hpp"
#include "../thirdeye/runtime/internal.hpp"
#include "../thirdeye/graphics/graphics.hpp"
#include "../thirdeye/resources/res.hpp"
#include "../thirdeye/savegame/transfer.hpp"
#include "../thirdeye/vm/events.hpp"
#include "../thirdeye/vm/objects.hpp"

#include <algorithm>
#include <cstdlib>

// Graphics effects call the engine's host pump; these draw_bitmap tests never
// enter an effect loop and do not boot the engine.
namespace THIRDEYE::runtime {
void pumpHost(GRAPHICS::Graphics &, VM::EventSystem &, VM::ObjectSystem &,
              RESOURCES::Resource &) {}
}

namespace {
class RuntimeGraphics : public ::testing::TestWithParam<bool> {
protected:
    RESOURCES::Resource res{std::filesystem::path(TESTS_DATA_DIR) / "SAMPLE.RES"};
    VM::ObjectSystem objects;
    VM::EventSystem events{objects};
    std::map<int32_t, int32_t> mem;
    THIRDEYE::savegame::TransferState transfer;
    VM::Interpreter vm{std::vector<uint8_t>(14)};
    const bool previousGame = THIRDEYE::runtime::gDungeonHack;

    void TearDown() override {
        THIRDEYE::runtime::gDungeonHack = previousGame;
    }

    void checkBackdrop(bool dungeonHack) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        GRAPHICS::Graphics gfx;
        THIRDEYE::runtime::Context ctx{objects, events, &gfx, res, mem, transfer, vm};
        THIRDEYE::runtime::gDungeonHack = dungeonHack;
        // A sparse 3x1 shape: index 0, index 240, then an omitted pixel.
        // ID 190 is EOB3's Backdrop but DH's Marble Wallset.
        res.getAsset(uint16_t(190)) = {
            0, 0, 0, 0, 1, 0, 10, 0, 0, 0,
            3, 0, 1, 0, 0, 0, 0x80, 2, 0, 2, 0, 240, 0xFF};
        const bool offscreen = GetParam();
        const int page = offscreen ? events.assignWindow(0, 0, 0, 319, 199, true) : 1;
        if (offscreen) ASSERT_TRUE(gfx.beginPage(page, 320, 200));
        gfx.drawIndexed({0, 240}, 2, 1, 0, 0);
        const auto paintedBlack = gfx.peekPixel(0, 0);
        const auto paintedColor = gfx.peekPixel(1, 0);
        gfx.fillRectRGB(0, 0, 319, 199, 12, 34, 56);
        const auto background = gfx.peekPixel(25, 25);
        const auto black = gfx.mapColor(0);
        ASSERT_NE(background, black);
        if (offscreen) gfx.endPage();
        VM::Value result = -1;
        ASSERT_TRUE(THIRDEYE::runtime::graphics::tryHandle(
            ctx, "draw_bitmap", {page, 190, 0, 20, 20, 0, 0, 0, 0}, result));
        if (offscreen) ASSERT_TRUE(gfx.blitPage(page, 1, 0, 0));
        // Both games paint the shape. Only EOB3 clears its background first.
        EXPECT_EQ(gfx.peekPixel(20, 20), paintedBlack);
        EXPECT_EQ(gfx.peekPixel(21, 20), paintedColor);
        EXPECT_EQ(gfx.peekPixel(22, 20), dungeonHack ? background : black);
        EXPECT_EQ(gfx.peekPixel(25, 25), dungeonHack ? background : black);
        EXPECT_EQ(gfx.peekPixel(19, 25), background);
    }
};

TEST_P(RuntimeGraphics, DungeonHackMarbleWallPreservesSurroundings) {
    checkBackdrop(true);
}

TEST_P(RuntimeGraphics, EyeOfTheBeholderBackdropStillClearsBackground) {
    checkBackdrop(false);
}

INSTANTIATE_TEST_SUITE_P(ScreenAndOffscreen, RuntimeGraphics, ::testing::Bool());

// Exercise the original key-lock draw handler without booting or changing an
// installation's saves. The optional original assets are not distributed here.
TEST(DungeonHackGraphics, KeyLocksReachBitmapRenderer) {
    const char* dir = std::getenv("THIRDEYE_TEST_DH_DIR");
    if (!dir) GTEST_SKIP() << "Set THIRDEYE_TEST_DH_DIR to a Dungeon Hack install";
    struct RestoreGame {
        bool previous = THIRDEYE::runtime::gDungeonHack;
        ~RestoreGame() { THIRDEYE::runtime::gDungeonHack = previous; }
    } restoreGame;
    THIRDEYE::runtime::gDungeonHack = true;
    RESOURCES::Resource res{std::filesystem::path(dir) / "HACK.RES"};
    VM::ObjectSystem objects;
    for (const auto& name : res.getCodeResourceNames()) {
        VM::SopClass cls;
        cls.name = name;
        cls.number = res.getResourceNumber(name);
        cls.code = res.getAsset(name);
        cls.header = VM::Interpreter(cls.code).header();
        for (const auto& [tag, value] : res.getExports(name)) {
            if (tag.starts_with("M:")) cls.handlers[std::stoi(tag.substr(2))] = std::stoul(value);
            else if (tag == "N:PARENT") cls.header.parent = std::stoul(value);
            else if (tag.size() > 2 && tag[1] == ':' &&
                     (tag[0] == 'B' || tag[0] == 'W' || tag[0] == 'L'))
                cls.exportedVars[tag] = std::stoul(value);
        }
        for (const auto& [tag, value] : res.getImports(name)) {
            if (tag.starts_with("C:")) cls.imports[std::stoi(value)] = tag.substr(2);
            else if (auto comma = value.find(','); comma != std::string::npos)
                cls.externs[std::stoul(value.substr(0, comma))] = {tag, static_cast<uint32_t>(std::stoul(value.substr(comma + 1)))};
        }
        objects.addClass(std::move(cls));
    }
    objects.createInstanceAt(0, 1972); // kernel
    objects.createInstanceAt(2001, 1971); // dungeon
    objects.createInstanceAt(1005, 2903); // key lock
    auto* state = objects.staticsPtr(1005, 0, 14);
    ASSERT_NE(state, nullptr);
    // Slot 1005 in the user's level-3 save: (12,2), south-facing, key 5.
    const uint8_t saved[] = {255,255,12,2,2,4,255,255,255,255,0,0,0,5};
    std::copy(std::begin(saved), std::end(saved), state);
    std::vector<std::vector<VM::Value>> draws;
    objects.setRuntimeCall([&](VM::Interpreter&, const std::string& fn, const std::vector<VM::Value>& args) -> VM::Value {
        if (fn == "draw_bitmap") draws.push_back(args);
        return 0;
    });
    // Probe all view cells looking north; a south-facing wall lock must be
    // drawable from at least one view cell when looking north.
    for (int cell = 0; cell < 18; ++cell) objects.send(1005, 108, {1, cell});
    ASSERT_FALSE(draws.empty());
    for (const auto& args : draws) {
        ASSERT_EQ(args.size(), 9u);
        EXPECT_EQ(args[1], 143); // Keyhole 05
        EXPECT_GE(args[2], 0);
        EXPECT_LT(args[2], 19);
    }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    GRAPHICS::Graphics gfx;
    VM::EventSystem events{objects};
    std::map<int32_t, int32_t> mem;
    THIRDEYE::savegame::TransferState transfer;
    VM::Interpreter vm{std::vector<uint8_t>(14)};
    THIRDEYE::runtime::Context ctx{objects, events, &gfx, res, mem, transfer, vm};
    size_t painted = 0;
    for (const auto& args : draws) {
        gfx.fillRectRGB(0, 0, 319, 199, 12, 34, 56);
        const auto background = gfx.peekPixel(0, 0);
        VM::Value result = -1;
        ASSERT_TRUE(THIRDEYE::runtime::graphics::tryHandle(ctx, "draw_bitmap", args, result));
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 320; ++x)
                painted += gfx.peekPixel(x, y) != background;
    }
    EXPECT_GT(painted, 0u);
}
} // namespace
