#include "gtest/gtest.h"
#include "config.hpp"
#include "../thirdeye/runtime/internal.hpp"
#include "../thirdeye/graphics/graphics.hpp"
#include "../thirdeye/resources/res.hpp"
#include "../thirdeye/savegame/transfer.hpp"
#include "../thirdeye/vm/events.hpp"
#include "../thirdeye/vm/objects.hpp"

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
} // namespace
