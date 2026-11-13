// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "config/config.h"
#include "config/setup.h"
#include "misc/cross.h"

#include "test_temp_dir.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>

extern std_fs::path current_config_dir;

namespace {

std::unique_ptr<Config> SmallConfig()
{
	auto conf             = std::make_unique<Config>();
	constexpr auto Always = Property::Changeable::Always;

	auto cpu = conf->AddSection("cpu");
	cpu->AddString("cputype", Always, "auto");
	cpu->AddString("cycles", Always, "auto");

	auto webserver = conf->AddSection("webserver");
	webserver->AddString("mount_allowed_bases", Always, "");

	auto render = conf->AddSection("render");
	render->AddString("glshader", Always, "default");

	auto sdl = conf->AddSection("sdl");
	sdl->AddPath("mapperfile", Always, "mapper.map");

	conf->AddAutoexecSection();
	return conf;
}

struct Snapshot {
	std::string cputype     = {};
	std::string cycles      = {};
	std::string bases       = {};
	std::string glshader    = {};
	std::string mapperfile  = {};
	std_fs::path mapperpath = {};
	std::string joined      = {};
	std::string overwritten = {};

	bool operator==(const Snapshot&) const = default;
};

void PrintTo(const Snapshot& s, std::ostream* os)
{
	*os << "{cputype=" << s.cputype << " cycles=" << s.cycles
	    << " bases=" << s.bases << " glshader=" << s.glshader
	    << " mapperfile=" << s.mapperfile << " path=" << s.mapperpath
	    << " joined=[" << s.joined << "] overwritten=[" << s.overwritten
	    << "]}";
}

Snapshot Take(const Config& conf)
{
	auto value = [&](const char* section, const char* setting) {
		return conf.GetSection(section)->GetPropertyValue(setting);
	};
	const auto sdl  = dynamic_cast<SectionProp*>(conf.GetSection("sdl"));
	const auto path = sdl ? sdl->GetPath("mapperfile") : nullptr;
	const auto autoexec = dynamic_cast<AutoExecSection*>(
	        conf.GetSection("autoexec"));

	return {value("cpu", "cputype"),
	        value("cpu", "cycles"),
	        value("webserver", "mount_allowed_bases"),
	        value("render", "glshader"),
	        value("sdl", "mapperfile"),
	        path ? path->realpath : std_fs::path{},
	        autoexec ? autoexec->data : std::string{},
	        conf.GetOverwrittenAutoexecSection().data};
}

void WriteFile(const std_fs::path& path, const std::string& text)
{
	auto file = std::fopen(path.string().c_str(), "wb");
	ASSERT_NE(file, nullptr);
	std::fwrite(text.data(), 1, text.size(), file);
	std::fclose(file);
}

class ConfigText : public testing::Test {
protected:
	void SetUp() override
	{
		// A path setting's default resolves against the config dir.
		init_config_dir();
		dir = TestTempDir::MakeUnique("config_text_");
		ASSERT_FALSE(dir.empty());
	}

	void TearDown() override
	{
		control.reset();
		std::error_code ec = {};
		std_fs::remove_all(dir, ec);
	}

	// SectionProp::HandleInputLine asserts the global control.
	Config& Fresh()
	{
		control = SmallConfig();
		return *control;
	}

	std_fs::path dir = {};
};

} // namespace

// A conf that is no host file, such as one inside a game archive, must load
// exactly as the same text passed with -conf (ada-3id2).
TEST_F(ConfigText, LoadsAsTheSameTextFromAConfFile)
{
	const std::string text =
	        "% a comment\r\n"
	        "[cpu]\r\n"
	        "cputype = 386\r\n"
	        "# another comment\r\n"
	        "[nosuchsection]\r\n"
	        "cycles = 3000\r\n"
	        "[webserver]\r\n"
	        "mount_allowed_bases = /home\r\n"
	        "[render]\r\n"
	        "glshader = crt\r\n"
	        "[sdl]\r\n"
	        "mapperfile = game.map\r\n"
	        "[autoexec]\r\n"
	        "\r\n"
	        "mount C \"..\"\r\n"
	        "# not a line\r\n"
	        "config -set cycles=8000\r\n"
	        "GAME\r\n"
	        "exit\r\n"
	        "[cpu]\r\n"
	        "cputype=486";

	const auto file = dir / "game.conf";
	WriteFile(file, text);

	ASSERT_TRUE(Fresh().ParseConfigFile("custom", file.string()));
	const auto expected = Take(*control);

	Fresh().ParseConfigText("package", "game.zip/game.conf", text, dir);
	EXPECT_EQ(Take(*control), expected);
	EXPECT_EQ(expected.cputype, "486");
	EXPECT_EQ(expected.cycles, "3000");
	EXPECT_EQ(expected.bases, "/home");
	EXPECT_EQ(expected.glshader, "crt");
	EXPECT_NE(expected.joined.find("exit"), std::string::npos);
}

TEST_F(ConfigText, ARelativePathCountsFromTheBaseFolder)
{
	auto& conf = Fresh();
	conf.ParseConfigText("package",
	                     "game.zip/_DOSBOX/dosbox.conf",
	                     "[sdl]\nmapperfile = game.map\n",
	                     dir);

	const auto sdl = dynamic_cast<SectionProp*>(conf.GetSection("sdl"));
	ASSERT_NE(sdl, nullptr);
	EXPECT_EQ(sdl->GetPath("mapperfile")->realpath, dir / "game.map");
	EXPECT_TRUE(current_config_dir.empty());
}

// MOUNT -pr and the mount policy's conf anchor read config_files as host
// paths; a conf without a host file stays out of them.
TEST_F(ConfigText, IsNotListedAsAHostConfFile)
{
	auto& conf = Fresh();
	conf.ParseConfigText("package", "game.zip/game.conf", "[cpu]\ncputype=386\n", dir);

	EXPECT_TRUE(conf.config_files.empty());
	EXPECT_TRUE(conf.loaded_config_paths_canonical.empty());
}

// The engine's autoexec rule: the last conf with an [autoexec] replaces the
// ones before it, whichever way each conf was read.
TEST_F(ConfigText, TakesItsPlaceInTheAutoexecRow)
{
	const auto first  = dir / "first.conf";
	const auto second = dir / "second.conf";
	WriteFile(first, "[autoexec]\nFIRST\n");
	WriteFile(second, "[autoexec]\nSECOND\n");

	auto& conf = Fresh();
	ASSERT_TRUE(conf.ParseConfigFile("primary", first.string()));
	conf.ParseConfigText("package", "game.zip/game.conf", "[autoexec]\nPACKAGE\n", dir);
	EXPECT_EQ(conf.GetOverwrittenAutoexecConf(), "game.zip/game.conf");
	EXPECT_EQ(conf.GetOverwrittenAutoexecSection().data, "PACKAGE");

	ASSERT_TRUE(conf.ParseConfigFile("custom", second.string()));
	EXPECT_EQ(conf.GetOverwrittenAutoexecSection().data, "SECOND");
}

// The one translation a package conf gets (its relative MOUNT paths name the
// archive) reaches both autoexec forms; a line it refuses reaches neither.
TEST_F(ConfigText, AnAutoexecLineFunctionSeesEveryAutoexecLine)
{
	auto& conf = Fresh();
	conf.ParseConfigText("package",
	                     "game.zip/game.conf",
	                     "[cpu]\ncputype=386\n[autoexec]\nmount c ..\nGAME\nLEAVE\n",
	                     dir,
	                     [](const std::string& line) -> std::optional<std::string> {
		                     if (line == "LEAVE") {
			                     return {};
		                     }
		                     return line == "mount c .."
		                                  ? "mount c \"/g/game.zip\""
		                                  : line;
	                     });

	const auto snapshot = Take(conf);
	EXPECT_EQ(snapshot.cputype, "386");
	EXPECT_EQ(snapshot.overwritten, "mount c \"/g/game.zip\"\nGAME");
	EXPECT_EQ(snapshot.joined, "mount c \"/g/game.zip\"\nGAME");
}

// A game package's [webserver] is ignored (ada-0lv8): its lines reach
// neither that section nor the one before it.
TEST_F(ConfigText, AnIgnoredSectionTakesItsLinesWithIt)
{
	auto& conf = Fresh();
	conf.ParseConfigText("package",
	                     "game.zip/game.conf",
	                     "[cpu]\ncputype=386\n[webserver]\nmount_allowed_bases=/\n"
	                     "cputype=486\n[render]\nglshader=crt\n",
	                     dir,
	                     {},
	                     {"webserver"});

	const auto snapshot = Take(conf);
	EXPECT_EQ(snapshot.cputype, "386");
	EXPECT_EQ(snapshot.bases, "");
	EXPECT_EQ(snapshot.glshader, "crt");
}

// A package must not open the host's loopback to the guest: one setting of
// a section can be ignored, in any spelling.
TEST_F(ConfigText, AnIgnoredSettingIsLeftAndItsSectionStays)
{
	for (const auto* text : {"[cpu]\ncputype=386\ncycles=max\n",
	                         "[CPU]\nCPUTYPE = 386\ncycles=max\n",
	                         "[cpu]\n[nonsense]\ncputype=386\ncycles=max\n"}) {
		auto& conf = Fresh();
		conf.ParseConfigText("package",
		                     "game.zip/game.conf",
		                     text,
		                     dir,
		                     {},
		                     {"cpu.cputype"});
		const auto snapshot = Take(conf);
		EXPECT_EQ(snapshot.cputype, "auto") << text;
		EXPECT_EQ(snapshot.cycles, "max") << text;
	}
}

TEST_F(ConfigText, AnIgnoredSettingDoesNotReachAnotherSection)
{
	auto& conf = Fresh();
	conf.ParseConfigText("package",
	                     "game.zip/game.conf",
	                     "[render]\nglshader=crt\n",
	                     dir,
	                     {},
	                     {"cpu.glshader"});
	EXPECT_EQ(Take(conf).glshader, "crt");
}

// GetSection matches without regard to case, and an unknown section leaves
// its lines with the section before it; both reach the autoexec handling.
TEST_F(ConfigText, EveryAutoexecLineReachesTheAutoexecHandling)
{
	const auto mark = [](const std::string& line) -> std::optional<std::string> {
		return "seen " + line;
	};
	auto& conf = Fresh();
	conf.ParseConfigText("package", "a.conf", "[AUTOEXEC]\nmount c ..\n", dir, mark);
	EXPECT_EQ(conf.GetOverwrittenAutoexecSection().data, "seen mount c ..");

	auto& again = Fresh();
	again.ParseConfigText("package",
	                      "b.conf",
	                      "[autoexec]\nFIRST\n[nosuchsection]\nSECOND\n",
	                      dir,
	                      mark);
	EXPECT_EQ(again.GetOverwrittenAutoexecSection().data, "seen FIRST\nseen SECOND");
}

// A header without its ']' changes no section, so the lines after it are
// still autoexec lines.
TEST_F(ConfigText, ABrokenHeaderKeepsTheAutoexecHandling)
{
	const auto mark = [](const std::string& line) -> std::optional<std::string> {
		return "seen " + line;
	};
	auto& conf = Fresh();
	conf.ParseConfigText("package",
	                     "a.conf",
	                     "[autoexec]\nFIRST\n[x\nmount c ..\n",
	                     dir,
	                     mark);
	EXPECT_EQ(conf.GetOverwrittenAutoexecSection().data,
	          "seen FIRST\nseen mount c ..");
}

// No setting takes control bytes; the line goes before any warning could
// print them to the console or the log. [autoexec] keeps ESC for ANSI colour.
TEST_F(ConfigText, ASettingLineWithControlBytesIsRefused)
{
	auto& conf = Fresh();
	conf.ParseConfigText("package",
	                     "a.conf",
	                     "[cpu]\ncputype=386\x1b]0;x\x07\ncycles=max\n"
	                     "[autoexec]\nECHO \x1b[1mhi\n",
	                     dir);
	const auto snapshot = Take(conf);
	EXPECT_EQ(snapshot.cputype, "auto");
	EXPECT_EQ(snapshot.cycles, "max");
	EXPECT_EQ(snapshot.overwritten, "ECHO \x1b[1mhi");
}

// The setting lookup must not stop at a NUL the ignore list compares past.
TEST_F(ConfigText, ANulInTheNameDoesNotGetPastTheIgnoreList)
{
	using namespace std::string_literals;
	for (const auto& text : {"[cpu]\ncputype\0=386\n"s,
	                         "[cpu]\ncputype\0x = 386\n"s}) {
		auto& conf = Fresh();
		conf.ParseConfigText("package", "a.conf", text, dir, {}, {"cpu.cputype"});
		EXPECT_EQ(Take(conf).cputype, "auto");
	}
}
