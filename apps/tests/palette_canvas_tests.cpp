#include "gtest/gtest.h"
#include "../thirdeye/graphics/graphics.hpp"
#include "../thirdeye/graphics/bitmap.hpp"
#include "../thirdeye/resources/res.hpp"

#include <cstdio>
#include <cstdlib>
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

TEST(PaletteCanvas, ScaledAndClippedMaskedBitmapsKeepCoverageAndPaletteIndices) {
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	GRAPHICS::Graphics gfx;
	// Equivalent 8x8 sprites in DH's scanline and EOB3's VFX formats.
	// Only the central 4x4 is painted: left half index 0, right half 240.
	for (bool vfx : {false, true}) {
		std::vector<uint8_t> bitmap;
		if (vfx) {
			bitmap = {'1', '.', '1', '0', 1, 0, 0, 0,
			          16, 0, 0, 0, 0, 0, 0, 0};
			bitmap.resize(40, 0);
			bitmap[16] = bitmap[18] = 7; // height-1, width-1
			for (int y = 0; y < 8; ++y) {
				if (y >= 2 && y < 6)
					bitmap.insert(bitmap.end(), {1, 2, 4, 0, 4, 240});
				bitmap.push_back(0); // end of line
			}
		} else {
			bitmap = {0, 0, 0, 0, 1, 0, 10, 0, 0, 0, 8, 0, 8, 0};
			for (uint8_t y = 2; y < 6; ++y)
				bitmap.insert(bitmap.end(), {y, 2, 0x80, 4, 0, 3, 0, 3, 240});
			bitmap.push_back(0xFF);
		}
		for (int scale : {128, 256, 384}) {
			const int size = 8 * scale / 256;
			const int origin = 30 + (8 - size) / 2;
			for (int mirror = 0; mirror < 4; ++mirror) {
				// Unclipped, then clipped at each of the four viewport edges.
				for (int edge = 0; edge < 5; ++edge) {
					SCOPED_TRACE(::testing::Message() << "vfx=" << vfx
					    << " scale=" << scale << " mirror=" << mirror << " edge=" << edge);
					gfx.clearClip();
					gfx.fillRectRGB(20, 20, 50, 50, 12, 34, 56);
					SDL_Rect clip{20, 20, 31, 31};
					if (edge == 1) { clip.x = origin + 1; clip.w = 50 - clip.x; }
					if (edge == 2) clip.w = origin + size - 1 - clip.x;
					if (edge == 3) { clip.y = origin + 1; clip.h = 50 - clip.y; }
					if (edge == 4) clip.h = origin + size - 1 - clip.y;
					gfx.setClip(clip.x, clip.y, clip.w, clip.h);
					auto black = palette({0, 0, 0});
					gfx.setPaletteRange(black, 0);
					gfx.setPaletteRange(black, 240);
					gfx.drawImage(bitmap, 0, 30, 30, true, mirror, 0, scale);
					// Recolour AFTER drawing to verify that scaling kept index tags.
					auto red = palette({63, 0, 0});
					auto gold = palette({63, 40, 10});
					gfx.setPaletteRange(red, 0);
					gfx.setPaletteRange(gold, 240);
					gfx.saveScreenshot("palette-canvas-test.bmp");
					std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
					    SDL_LoadBMP("palette-canvas-test.bmp"), SDL_DestroySurface);
					std::remove("palette-canvas-test.bmp");
					ASSERT_NE(image, nullptr);
					for (int y = 0; y < size; ++y) {
						for (int x = 0; x < size; ++x) {
							int sx = (2 * x + 1) * 8 / (2 * size);
							int sy = (2 * y + 1) * 8 / (2 * size);
							if (mirror & 1) sx = 7 - sx;
							if (mirror & 2) sy = 7 - sy;
							SDL_Point p{origin + x, origin + y};
							const bool painted = SDL_PointInRect(&p, &clip) &&
							    sx >= 2 && sx < 6 && sy >= 2 && sy < 6;
							Uint8 r, g, b, a;
							ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), p.x, p.y, &r, &g, &b, &a));
							EXPECT_EQ(r, painted ? 252 : 12) << "x=" << x << " y=" << y;
							EXPECT_EQ(g, painted ? (sx < 4 ? 0 : 160) : 34);
							EXPECT_EQ(b, painted ? (sx < 4 ? 0 : 40) : 56);
						}
					}
				}
			}
		}
	}
}

TEST(PaletteCanvas, DungeonHackHobgoblinKeepsGapsAtViewportEdge) {
	const char *dir = std::getenv("THIRDEYE_TEST_DH_DIR");
	if (!dir) GTEST_SKIP() << "Set THIRDEYE_TEST_DH_DIR to a Dungeon Hack install";
	RESOURCES::Resource res{std::filesystem::path(dir) / "HACK.RES"};
	auto &data = res.getAsset(88); // Hobgob
	GRAPHICS::Bitmap bitmap(data);
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	GRAPHICS::Graphics gfx;
	int gaps = 0;
	for (uint16_t shape = 0; shape < bitmap.getNumberOfBitmaps(); ++shape) {
		SCOPED_TRACE(shape);
		std::vector<uint8_t> mask;
		bitmap.decodeScanlineMasked(shape, mask);
		const int w = bitmap.getWidth(shape), h = bitmap.getHeight(shape);
		const int dw = w / 2, dh = h / 2;
		if (dw == 0 || dh == 0) continue;
		const int ox = 10 + (w - dw) / 2, oy = 10 + (h - dh) / 2;
		ASSERT_LT(ox + dw, 320);
		ASSERT_LT(oy + dh, 200);
		gfx.clearClip();
		gfx.fillRectRGB(0, 0, 319, 199, 12, 34, 56);
		gfx.setClip(ox + 1, oy, dw - 1, dh);
		gfx.drawImage(data, shape, 10, 10, true, 0, 88, 128);
		gfx.saveScreenshot("palette-canvas-test.bmp");
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
		    SDL_LoadBMP("palette-canvas-test.bmp"), SDL_DestroySurface);
		std::remove("palette-canvas-test.bmp");
		ASSERT_NE(image, nullptr);
		for (int y = 0; y < dh; ++y) {
			for (int x = 1; x < dw; ++x) {
				int sx = (2 * x + 1) * w / (2 * dw);
				int sy = (2 * y + 1) * h / (2 * dh);
				if (mask[sy * w + sx]) continue;
				++gaps;
				Uint8 r, g, b, a;
				ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), ox + x, oy + y, &r, &g, &b, &a));
				ASSERT_EQ(r, 12); ASSERT_EQ(g, 34); ASSERT_EQ(b, 56);
			}
		}
	}
	EXPECT_GT(gaps, 0);
}

} // namespace
