#include "gtest/gtest.h"
#include "../thirdeye/graphics/graphics.hpp"

#include <cstdio>
#include <memory>

namespace {

std::vector<uint8_t> palette(std::initializer_list<uint8_t> rgb) {
	std::vector<uint8_t> data(26, 0);
	data[0] = static_cast<uint8_t>(rgb.size() / 3);
	data.insert(data.end(), rgb);
	return data;
}

TEST(PaletteCanvas, PaletteWritesRecolourDrawnIndicesIndependently) {
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	GRAPHICS::Graphics gfx;
	auto black = palette({0, 0, 0, 0, 0, 0});
	gfx.setPaletteRange(black, 240);
	// The menu draws its labels while their palette entries are black.
	gfx.drawIndexed({240, 241}, 2, 1, 10, 10);
	auto gold = palette({63, 40, 10});
	gfx.setPaletteRange(gold, 240);
	gfx.saveScreenshot("palette-canvas-test.bmp");
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
	    SDL_LoadBMP("palette-canvas-test.bmp"), SDL_DestroySurface);
	std::remove("palette-canvas-test.bmp");
	ASSERT_NE(image, nullptr);
	Uint8 r, g, b, a;
	ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), 10, 10, &r, &g, &b, &a));
	EXPECT_EQ(r, 252); EXPECT_EQ(g, 160); EXPECT_EQ(b, 40);
	ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), 11, 10, &r, &g, &b, &a));
	EXPECT_EQ(r, 0); EXPECT_EQ(g, 0); EXPECT_EQ(b, 0);
}

TEST(PaletteCanvas, MaskedBitmapKeepsPaintedIndicesAndTransparentGaps) {
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	GRAPHICS::Graphics gfx;
	gfx.fillRectRGB(10, 10, 12, 10, 12, 34, 56);
	// One scanline: x=0 paints index 0, x=1 paints 240, x=2 is omitted.
	std::vector<uint8_t> bitmap{
	    0, 0, 0, 0, 1, 0, 10, 0, 0, 0,
	    3, 0, 1, 0, 0, 0, 0x80, 2, 0, 2, 0, 240, 0xFF};
	gfx.drawImage(bitmap, 0, 10, 10, true);
	auto red = palette({63, 0, 0});
	auto gold = palette({63, 40, 10});
	gfx.setPaletteRange(red, 0);
	gfx.setPaletteRange(gold, 240);
	gfx.saveScreenshot("palette-canvas-test.bmp");
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
	    SDL_LoadBMP("palette-canvas-test.bmp"), SDL_DestroySurface);
	std::remove("palette-canvas-test.bmp");
	ASSERT_NE(image, nullptr);
	Uint8 r, g, b, a;
	ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), 10, 10, &r, &g, &b, &a));
	EXPECT_EQ(r, 252); EXPECT_EQ(g, 0); EXPECT_EQ(b, 0);
	ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), 11, 10, &r, &g, &b, &a));
	EXPECT_EQ(r, 252); EXPECT_EQ(g, 160); EXPECT_EQ(b, 40);
	ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), 12, 10, &r, &g, &b, &a));
	EXPECT_EQ(r, 12); EXPECT_EQ(g, 34); EXPECT_EQ(b, 56);
}

TEST(PaletteCanvas, PageCopiesKeepIndicesAndRgbOverlaysStayIntact) {
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	GRAPHICS::Graphics gfx;
	ASSERT_TRUE(gfx.beginPage(10, 2, 1));
	gfx.drawIndexed({240, 241}, 2, 1, 0, 0);
	gfx.endPage();
	ASSERT_TRUE(gfx.blitPage(10, 1, 10, 10));
	gfx.fillRectRGB(11, 10, 11, 10, 12, 34, 56);
	auto gold = palette({63, 40, 10, 63, 63, 63});
	gfx.setPaletteRange(gold, 240);
	gfx.saveScreenshot("palette-canvas-test.bmp");
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
	    SDL_LoadBMP("palette-canvas-test.bmp"), SDL_DestroySurface);
	std::remove("palette-canvas-test.bmp");
	ASSERT_NE(image, nullptr);
	Uint8 r, g, b, a;
	ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), 10, 10, &r, &g, &b, &a));
	EXPECT_EQ(r, 252); EXPECT_EQ(g, 160); EXPECT_EQ(b, 40);
	ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), 11, 10, &r, &g, &b, &a));
	EXPECT_EQ(r, 12); EXPECT_EQ(g, 34); EXPECT_EQ(b, 56);
}

} // namespace
