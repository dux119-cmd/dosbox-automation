// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
// A named audio capture file is always a new one (ada-11qi).

#include "capture/private/capture_audio.h"

#include "test_temp_dir.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

class CaptureAudioTest : public testing::Test {
protected:
	fs::path dir = {};

	void SetUp() override
	{
		dir = TestTempDir::MakeUnique("capture_audio_");
		ASSERT_FALSE(dir.empty());
	}

	void TearDown() override
	{
		capture_audio_discard_output();
		std::error_code ec = {};
		fs::remove_all(dir, ec);
	}

	static std::string Contents(const fs::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), {}};
	}
};

TEST_F(CaptureAudioTest, AnExistingFileIsNeverOpened)
{
	const auto kept = dir / "kept.wav";
	std::ofstream(kept, std::ios::binary) << "precious";

	EXPECT_FALSE(capture_audio_open_output(kept));
	EXPECT_EQ(Contents(kept), "precious");
}

#if !defined(WIN32)
TEST_F(CaptureAudioTest, ADanglingLinkIsNeverFollowed)
{
	const auto ghost = dir / "ghost.wav";
	const auto link  = dir / "link.wav";
	fs::create_symlink(ghost, link);

	EXPECT_FALSE(capture_audio_open_output(link));
	EXPECT_FALSE(fs::exists(ghost));
}
#endif

TEST_F(CaptureAudioTest, ADiscardedFileIsRemoved)
{
	const auto fresh = dir / "fresh.wav";
	ASSERT_TRUE(capture_audio_open_output(fresh));
	EXPECT_TRUE(fs::exists(fresh));

	capture_audio_discard_output();
	EXPECT_FALSE(fs::exists(fresh));
}

} // namespace
