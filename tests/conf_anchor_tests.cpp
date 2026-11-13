// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
// Which conf gives the mount policy its anchor (ada-fl3c).

#include "dos/programs/mount_policy.h"

#include "config/config.h"
#include "dosbox_test_fixture.h"
#include "misc/cross.h"
#include "test_home.h"
#include "test_temp_dir.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace {

namespace fs = std::filesystem;

class ConfAnchorTest : public DOSBoxTestFixture {
protected:
	fs::path tmp                = {};
	std::vector<fs::path> saved = {};

	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		tmp = TestTempDir::MakeUnique("conf_anchor_");
		ASSERT_FALSE(tmp.empty());
		saved = control->loaded_config_paths_canonical;
		control->anchor_config_path.clear();
	}

	void TearDown() override
	{
		control->loaded_config_paths_canonical = saved;
		control->anchor_config_path.clear();
		std::error_code ec = {};
		fs::remove_all(tmp, ec);
		DOSBoxTestFixture::TearDown();
	}

	static fs::path WriteConf(const fs::path& path)
	{
		fs::create_directories(path.parent_path());
		std::ofstream(path) << "[cpu]\n";
		return path;
	}
};

TEST_F(ConfAnchorTest, ThePrimaryConfSetsNoAnchor)
{
	const auto primary = WriteConf(get_config_dir() / "anchor-probe.conf");
	ASSERT_TRUE(control->ParseConfigFile("primary", primary.string()));
	EXPECT_TRUE(MountPolicy::ConfAnchor().empty());
	fs::remove(primary);
}

TEST_F(ConfAnchorTest, ACustomConfAnchorsAtItsFolder)
{
	const auto game = WriteConf(tmp / "game" / "game.conf");
	ASSERT_TRUE(control->ParseConfigFile("custom", game.string()));
	EXPECT_EQ(MountPolicy::ConfAnchor(), fs::canonical(tmp / "game"));
}

TEST_F(ConfAnchorTest, ALocalConfAnchorsAtItsFolder)
{
	const auto local = WriteConf(tmp / "here" / "dosbox.conf");
	ASSERT_TRUE(control->ParseConfigFile("local", local.string()));
	EXPECT_EQ(MountPolicy::ConfAnchor(), fs::canonical(tmp / "here"));
}

TEST_F(ConfAnchorTest, APrimaryConfLoadedLaterKeepsTheAnchor)
{
	const auto game    = WriteConf(tmp / "game" / "game.conf");
	const auto primary = WriteConf(get_config_dir() / "anchor-probe.conf");
	ASSERT_TRUE(control->ParseConfigFile("custom", game.string()));
	ASSERT_TRUE(control->ParseConfigFile("primary", primary.string()));
	EXPECT_EQ(MountPolicy::ConfAnchor(), fs::canonical(tmp / "game"));
	fs::remove(primary);
}

TEST_F(ConfAnchorTest, ACustomConfInsideTheConfigFolderSetsNoAnchor)
{
	const auto inside = WriteConf(get_config_dir() / "custom-probe.conf");
	ASSERT_TRUE(control->ParseConfigFile("custom", inside.string()));
	EXPECT_TRUE(MountPolicy::ConfAnchor().empty());
	fs::remove(inside);
}

TEST_F(ConfAnchorTest, ACustomConfAboveTheConfigFolderSetsNoAnchor)
{
	const auto above = WriteConf(TestHome::Root() / "home-probe.conf");
	ASSERT_TRUE(control->ParseConfigFile("custom", above.string()));
	EXPECT_TRUE(MountPolicy::ConfAnchor().empty());
	fs::remove(above);
}

} // namespace
