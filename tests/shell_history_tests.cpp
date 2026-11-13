// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "shell/shell.h"

#include "dosbox_test_fixture.h"
#include "test_home.h"
#include "test_temp_dir.h"

#include "config/config.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

constexpr uint16_t CodePage437 = 437;

std::string ReadAll(std::istream& in)
{
	return std::string(std::istreambuf_iterator<char>(in),
	                   std::istreambuf_iterator<char>());
}

std::string ReadFile(const fs::path& path)
{
	auto in = std::ifstream(path, std::ios::binary);
	return ReadAll(in);
}

void WriteFile(const fs::path& path, const std::string& text)
{
	auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
	out << text;
}

class ShellHistoryTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		const auto* setting = get_section("dos")->GetPath("shell_history_file");
		ASSERT_NE(setting, nullptr);
		ASSERT_TRUE(TestHome::Contains(setting->realpath))
		        << setting->realpath << " is not the test home's";
		history_file = setting->realpath;
	}

	void TearDown() override
	{
		if (!history_file.empty()) {
			std::error_code ec = {};
			fs::remove(history_file, ec);
		}
		DOSBoxTestFixture::TearDown();
	}

	fs::path history_file = {};
};

// Every engine start creates a throwaway shell for COMMAND.COM's help, and
// its history used to write the file back; a second engine reading in that
// window loaded an empty file and saved it over the user's history.
TEST_F(ShellHistoryTest, UnchangedHistoryIsNotWrittenBack)
{
	WriteFile(history_file, "dir\ncd keen\n");
	const auto old_time = fs::last_write_time(history_file) -
	                      std::chrono::hours(1);
	fs::last_write_time(history_file, old_time);

	{
		ShellHistory history;
	}

	EXPECT_EQ(ReadFile(history_file), "dir\ncd keen\n");
	// Ticks, not time points: printing a file_clock time point needs
	// floating-point to_chars, which macOS 12.0 as the target lacks.
	EXPECT_EQ(fs::last_write_time(history_file).time_since_epoch().count(),
	          old_time.time_since_epoch().count());
}

TEST_F(ShellHistoryTest, AppendedCommandIsSaved)
{
	WriteFile(history_file, "dir\n");

	{
		ShellHistory history;
		history.Append("cd keen", CodePage437);
	}

	EXPECT_EQ(ReadFile(history_file), "dir\ncd keen\n");
}

#if !defined(WIN32)
// A reader that opened the file before the save keeps the old content
// whole: the save puts a new file in place instead of truncating this one.
// Windows refuses to rename over a file that is open, so this is POSIX only.
TEST_F(ShellHistoryTest, SaveReplacesTheFileInsteadOfTruncatingIt)
{
	WriteFile(history_file, "dir\n");
	auto reader = std::ifstream(history_file, std::ios::binary);
	ASSERT_TRUE(reader.is_open());

	{
		ShellHistory history;
		history.Append("cd keen", CodePage437);
	}

	EXPECT_EQ(ReadAll(reader), "dir\n");
	EXPECT_EQ(ReadFile(history_file), "dir\ncd keen\n");
}

TEST_F(ShellHistoryTest, SymlinkedHistoryFileStaysALink)
{
	const auto dir = TestTempDir::MakeUnique("shell-history-");
	ASSERT_FALSE(dir.empty());
	const auto target = dir / "history.txt";
	WriteFile(target, "dir\n");
	std::error_code ec = {};
	fs::remove(history_file, ec);
	fs::create_symlink(target, history_file, ec);
	ASSERT_FALSE(ec) << ec.message();

	{
		ShellHistory history;
		history.Append("cd keen", CodePage437);
	}

	EXPECT_TRUE(fs::is_symlink(history_file));
	EXPECT_EQ(ReadFile(target), "dir\ncd keen\n");
	fs::remove_all(dir, ec);
}
#endif

} // namespace
