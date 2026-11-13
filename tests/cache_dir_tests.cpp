// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "misc/cross.h"

#include "test_home.h"
#include "utils/fs_utils.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using TestHome::ScopedEnv;

#if defined(MACOSX)
TEST(CacheDir, MacUsesLibraryCaches)
{
	const auto dir = get_cache_dir().string();
	EXPECT_TRUE(dir.ends_with("/Library/Caches/dosbox-automation")) << dir;
}
#else
TEST(CacheDir, XdgCacheHomeOverridesTheDefault)
{
	const auto dir = [] {
		ScopedEnv cache("XDG_CACHE_HOME", "/tmp/override-cache");
		return get_cache_dir();
	}();
	EXPECT_EQ(dir, std::filesystem::path("/tmp/override-cache") / "dosbox-automation");
}
#endif

#if !defined(WIN32) && !defined(MACOSX)
TEST(CacheDir, UnsetFallsBackToDotCache)
{
	ScopedEnv cache("XDG_CACHE_HOME", std::nullopt);
	const auto dir = get_cache_dir().string();
	EXPECT_TRUE(dir.ends_with("/.cache/dosbox-automation")) << dir;
	EXPECT_TRUE(get_xdg_cache_home().string().ends_with("/.cache"));
}
#endif

#if defined(WIN32)
TEST(CacheDir, WindowsUsesLocalAppDataCache)
{
	ScopedEnv cache("XDG_CACHE_HOME", std::nullopt);
	const auto dir = get_cache_dir().string();
	EXPECT_TRUE(dir.ends_with("\\dosbox-automation\\cache")) << dir;
}
#endif

#if !defined(WIN32) && !defined(MACOSX)
TEST(DataDir, XdgDataHomeOverridesAndFallsBackToLocalShare)
{
	const auto dir = [] {
		ScopedEnv data("XDG_DATA_HOME", "/tmp/override-data");
		return get_data_dir();
	}();
	EXPECT_EQ(dir, std::filesystem::path("/tmp/override-data") / "dosbox-automation");
	ScopedEnv data("XDG_DATA_HOME", std::nullopt);
	EXPECT_TRUE(get_data_dir().string().ends_with(
	        "/.local/share/dosbox-automation"));
}
#endif
#if defined(MACOSX)
TEST(DataDir, MacUsesApplicationSupport)
{
	EXPECT_TRUE(get_data_dir().string().ends_with(
	        "/Library/Application Support/dosbox-automation"));
}
#endif
#if defined(WIN32)
TEST(DataDir, WindowsUsesLocalAppData)
{
	ScopedEnv data("XDG_DATA_HOME", std::nullopt);
	EXPECT_TRUE(get_data_dir().string().ends_with("\\dosbox-automation"));
}
#endif

} // namespace
