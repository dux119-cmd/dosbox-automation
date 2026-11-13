// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "test_home.h"

#include "dosbox_test_fixture.h"

#include "config/config.h"
#include "misc/cross.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

namespace fs = std::filesystem;

testing::AssertionResult IsInsideTestHome(const fs::path& path)
{
	if (!TestHome::Contains(path)) {
		return testing::AssertionFailure()
		    << path << " lies outside the test home " << TestHome::Root();
	}
	return testing::AssertionSuccess();
}

TEST(TestHome, ConfigDirLiesInsideIt)
{
	init_config_dir();
	EXPECT_TRUE(IsInsideTestHome(get_config_dir()));
}

TEST(TestHome, DataAndCacheDirsLieInsideIt)
{
	EXPECT_TRUE(IsInsideTestHome(get_data_dir()));
	EXPECT_TRUE(IsInsideTestHome(get_cache_dir()));
}

TEST(TestHome, ScopedEnvPutsThePreviousValueBack)
{
	const auto before = std::getenv("XDG_CACHE_HOME");
	const auto saved = before ? std::optional<std::string>(before) : std::nullopt;
	{
		TestHome::ScopedEnv override_cache("XDG_CACHE_HOME",
		                                   "/somewhere/else");
		EXPECT_STREQ(std::getenv("XDG_CACHE_HOME"), "/somewhere/else");
	}
	const auto after = std::getenv("XDG_CACHE_HOME");
	ASSERT_EQ(after != nullptr, saved.has_value());
	if (saved) {
		EXPECT_EQ(std::string(after), *saved);
	}
}

class TestHomeFixture : public DOSBoxTestFixture {};

TEST_F(TestHomeFixture, ShellHistoryFileLiesInsideIt)
{
	const auto section = get_section("dos");
	ASSERT_NE(section, nullptr);
	const auto* history = section->GetPath("shell_history_file");
	ASSERT_NE(history, nullptr);
	EXPECT_TRUE(IsInsideTestHome(history->realpath));
}

} // namespace
