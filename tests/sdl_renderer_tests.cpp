// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/render/sdl_renderer.h"

#include "dosbox_test_fixture.h"

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <memory>

namespace {

// Headless runs use SDL's offscreen driver (ada-kje4).
class SdlRendererOffscreenTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
		ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
		ASSERT_STREQ(SDL_GetCurrentVideoDriver(), "offscreen");
	}

	void TearDown() override
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
		DOSBoxTestFixture::TearDown();
	}
};

TEST_F(SdlRendererOffscreenTest, CreatesItsWindowWithTheEngineFlags)
{
	std::unique_ptr<SdlRenderer> renderer = {};
	ASSERT_NO_THROW(renderer = std::make_unique<SdlRenderer>(
	                        0,
	                        0,
	                        640,
	                        480,
	                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
	                        "auto",
	                        TextureFilterMode::NearestNeighbour));
	ASSERT_NE(renderer, nullptr);
	EXPECT_NE(renderer->GetWindow(), nullptr);
}

} // namespace
