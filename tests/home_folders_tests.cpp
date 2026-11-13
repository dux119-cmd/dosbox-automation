// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "misc/home_folders.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace {

using HomeFolders::Rule;
using Names = std::vector<std::string>;

// A path that is absolute on the host: a drive letter on Windows.
std::string Abs(const char* name)
{
#if defined(WIN32)
	return std::string("C:\\") + name;
#else
	return std::string("/") + name;
#endif
}

HomeFolders::EnvLookup Env(std::map<std::string, std::string> vars)
{
	return [vars = std::move(vars)](const char* name) {
		const auto it = vars.find(name);
		return it == vars.end() ? std::string() : it->second;
	};
}

TEST(HomeFolders, AHomeIsEnough)
{
	EXPECT_EQ(HomeFolders::Unset(Rule::Xdg, Env({{"HOME", "/home/u"}})), Names{});
	EXPECT_EQ(HomeFolders::Unset(Rule::HomeOnly, Env({{"HOME", "/Users/u"}})), Names{});
}

TEST(HomeFolders, NoHomeAndNoXdgNamesAllFour)
{
	EXPECT_EQ(HomeFolders::Unset(Rule::Xdg, Env({})),
	          (Names{"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}));
}

TEST(HomeFolders, TheConfigFolderAloneLeavesSavesAndCacheWithoutAPlace)
{
	EXPECT_EQ(HomeFolders::Unset(Rule::Xdg, Env({{"XDG_CONFIG_HOME", Abs("c")}})),
	          (Names{"HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}));
}

TEST(HomeFolders, TheThreeXdgFoldersStandInForHome)
{
	EXPECT_EQ(HomeFolders::Unset(Rule::Xdg,
	                             Env({{"XDG_CONFIG_HOME", Abs("c")},
	                                  {"XDG_DATA_HOME", Abs("d")},
	                                  {"XDG_CACHE_HOME", Abs("e")}})),
	          Names{});
}

// XDG Base Directory Specification 0.8: a relative path is invalid and ignored.
TEST(HomeFolders, ARelativeOrEmptyXdgValueCountsAsUnset)
{
	EXPECT_EQ(HomeFolders::Unset(Rule::Xdg,
	                             Env({{"HOME", ""},
	                                  {"XDG_CONFIG_HOME", "~/.config"},
	                                  {"XDG_DATA_HOME", ""},
	                                  {"XDG_CACHE_HOME", Abs("e")}})),
	          (Names{"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME"}));
}

TEST(HomeFolders, MacosNeedsHomeWhateverXdgSays)
{
	EXPECT_EQ(HomeFolders::Unset(Rule::HomeOnly,
	                             Env({{"XDG_CONFIG_HOME", Abs("c")},
	                                  {"XDG_DATA_HOME", Abs("d")},
	                                  {"XDG_CACHE_HOME", Abs("e")}})),
	          Names{"HOME"});
}

TEST(HomeFolders, TheRefusalNamesWhatIsMissingAndWhatWouldDo)
{
	EXPECT_EQ(HomeFolders::RefusalMessage(Rule::Xdg,
	                                      {"HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}),
	          "Cannot place the configuration, data and cache folders: "
	          "HOME, XDG_DATA_HOME, XDG_CACHE_HOME not set. Set HOME, or set "
	          "XDG_CONFIG_HOME, XDG_DATA_HOME and XDG_CACHE_HOME to absolute paths.");
	EXPECT_EQ(HomeFolders::RefusalMessage(Rule::HomeOnly, {"HOME"}),
	          "Cannot place the configuration, data and cache folders: "
	          "HOME not set. Set HOME.");
}

} // namespace
