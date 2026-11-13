// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/programs/package_start.h"

#include "archive_fixtures.h"
#include "augra/log.h"
#include "config/setup.h"
#include "dos/archive/archive_helper_client.h"
#include "dos/programs/mount.h"
#include "dosbox_test_fixture.h"
#include "shell/command_line.h"
#include "test_temp_dir.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using namespace ArchiveFixtures;

namespace {

namespace fs = std::filesystem;

constexpr auto ConfText = "[cpu]\ncputype=386\n[autoexec]\nmount c ..\nGAME\n";

class PackageStartTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		tmp = TestTempDir::MakeUnique("package_start_");
		ASSERT_FALSE(tmp.empty());
		ArchiveMount::SetHelperPathOverride(DOSBOX_ARCHIVE_HELPER_PATH);
		Set("archive_cache_path", (tmp / "cache").string());
		MOUNT::ResetArchiveCacheForTests();
	}

	void TearDown() override
	{
		MOUNT::ResetArchiveCacheForTests();
		ArchiveMount::SetHelperPathOverride({});
		std::error_code ec = {};
		fs::remove_all(tmp, ec);
		DOSBoxTestFixture::TearDown();
	}

	static void Set(const char* name, const std::string& value)
	{
		auto section = get_section("dos");
		ASSERT_NE(section, nullptr);
		auto property = section->GetProperty(name);
		ASSERT_NE(property, nullptr) << name;
		ASSERT_TRUE(property->SetValue(value)) << name << " = " << value;
	}

	fs::path Zip(const std::string& name, const std::vector<FileEntry>& entries)
	{
		const auto path   = tmp / name;
		std::string error = {};
		EXPECT_TRUE(WriteArchive(path, Kind::ZipDeflate, entries, nullptr, &error))
		        << error;
		return path;
	}

	static void WriteFile(const fs::path& path, const std::string& text)
	{
		fs::create_directories(path.parent_path());
		WriteBytes(path, std::vector<uint8_t>(text.begin(), text.end()));
	}

	static std::vector<PackageProfile::Profile> Default()
	{
		return {PackageProfile::BuiltInDefault()};
	}

	fs::path tmp = {};
};

} // namespace

TEST_F(PackageStartTest, OneGameGivesItsConf)
{
	const auto zip     = Zip("game.zip",
	                         {
                                     {"_DOSBOX/dosbox.conf", ConfText},
                                     {           "GAME.EXE",     "MZ"}
        });
	const auto package = PackageStart::ReadPackage(zip, Default());
	ASSERT_TRUE(package);
	EXPECT_EQ(package->profile, "default");
	EXPECT_EQ(package->path, zip.string());
	EXPECT_EQ(package->base_dir, tmp);
	ASSERT_EQ(package->confs.size(), 1u);
	EXPECT_EQ(package->confs[0].name, zip.string() + "/_DOSBOX/dosbox.conf");
	EXPECT_EQ(package->confs[0].text, ConfText);
	EXPECT_EQ(package->confs[0].folder, "_DOSBOX");
	EXPECT_EQ(package->entries.size(), 3u);
}

// A conf over 1 MiB is not read, and the archive starts as one without a
// package conf.
TEST_F(PackageStartTest, AConfOverOneMebibyteIsNotRead)
{
	auto text = std::string(ConfText);
	text.resize(PackageStart::MaxConfBytes + 1, '#');
	const auto zip = Zip("game.zip",
	                     {
	                             {"dosbox.conf", text},
                                     {   "GAME.EXE", "MZ"}
        });
	EXPECT_FALSE(PackageStart::ReadPackage(zip, Default()));
}

TEST_F(PackageStartTest, AConfOfExactlyOneMebibyteIsRead)
{
	auto text = std::string(ConfText);
	text.resize(PackageStart::MaxConfBytes, '#');
	const auto zip     = Zip("game.zip",
	                         {
                                     {"dosbox.conf", text},
                                     {   "GAME.EXE", "MZ"}
        });
	const auto package = PackageStart::ReadPackage(zip, Default());
	ASSERT_TRUE(package);
	ASSERT_EQ(package->confs.size(), 1u);
	EXPECT_EQ(package->confs[0].text.size(), PackageStart::MaxConfBytes);
}

TEST_F(PackageStartTest, NoConfMeansNoPackage)
{
	const auto zip = Zip("game.zip",
	                     {
	                             {"GAME.EXE", "MZ"}
        });
	EXPECT_FALSE(PackageStart::ReadPackage(zip, Default()));
}

// A profile's chained confs come after its conf, as several -conf files.
TEST_F(PackageStartTest, ChainedConfsFollowTheConf)
{
	PackageProfile::Profile gog = {};
	gog.name                    = "gog";
	gog.paths                   = {"DOSBOX"};
	gog.files                   = {"dosbox_game.conf"};
	gog.chained                 = {"dosbox_game_single.conf"};
	const auto zip              = Zip(
                "gog.zip",
                {
                        {"DOSBOX/dosbox_game_single.conf",   "[autoexec]\nGAME\n"},
                        {       "DOSBOX/dosbox_game.conf", "[cpu]\ncputype=386\n"}
        });
	const auto package = PackageStart::ReadPackage(zip, {gog});
	ASSERT_TRUE(package);
	ASSERT_EQ(package->confs.size(), 2u);
	EXPECT_EQ(package->confs[0].folder, "DOSBOX");
	EXPECT_EQ(package->confs[0].text, "[cpu]\ncputype=386\n");
	EXPECT_EQ(package->confs[1].text, "[autoexec]\nGAME\n");
}

// Several games: the conf at the archive's root counts, if there is one.
TEST_F(PackageStartTest, SeveralGamesTakeTheConfAtTheRoot)
{
	PackageProfile::Profile collection = {};
	collection.name                    = "collection";
	collection.paths                   = {"*/DOSBOX"};
	collection.files                   = {"dosbox.conf"};
	const std::vector<FileEntry> games = {
	        {"KQ1/DOSBOX/dosbox.conf", "[autoexec]\nKQ1\n"},
	        {"KQ2/DOSBOX/dosbox.conf", "[autoexec]\nKQ2\n"}
        };

	const auto without = Zip("without.zip", games);
	EXPECT_FALSE(PackageStart::ReadPackage(without, {collection}));

	auto with_root = games;
	with_root.push_back({"dosbox.conf", ConfText});
	const auto zip     = Zip("with.zip", with_root);
	const auto package = PackageStart::ReadPackage(zip, {collection});
	ASSERT_TRUE(package);
	ASSERT_EQ(package->confs.size(), 1u);
	EXPECT_EQ(package->confs[0].name, zip.string() + "/dosbox.conf");
	EXPECT_EQ(package->confs[0].folder, "");
}

TEST_F(PackageStartTest, AnArchiveThatWillNotOpenGivesNone)
{
	const auto path = tmp / "broken.zip";
	WriteBytes(path, std::vector<uint8_t>{'P', 'K', 3, 4, 0, 0, 0});
	EXPECT_FALSE(PackageStart::ReadPackage(path, Default()));
}

// Directories and CD images are passed over for an archive; the first other
// argument decides. Without an archive the folder the autoexec mounts as C:
// (the last one) is the package.
TEST_F(PackageStartTest, TheCommandLineGameIsFoundAsAutoexecFindsIt)
{
	const auto zip = Zip("game.zip",
	                     {
	                             {"GAME.EXE", "MZ"}
        });
	const auto iso = tmp / "cd.iso";
	WriteBytes(iso, std::vector<uint8_t>(16, 0));
	const auto other = tmp / "other";
	fs::create_directories(other);

	auto find = [](std::vector<std::string> args) {
		args.insert(args.begin(), "dosbox");
		std::vector<const char*> argv = {};
		for (const auto& arg : args) {
			argv.push_back(arg.c_str());
		}
		CommandLine cmdline(static_cast<int>(argv.size()), argv.data());
		return PackageStart::FindCommandLinePackage(cmdline);
	};
	auto is = [](const std::optional<PackageStart::Source>& found,
	             const fs::path& path,
	             const bool is_folder) {
		return found && found->path == path && found->is_folder == is_folder;
	};

	EXPECT_TRUE(is(find({zip.string()}), zip, false));
	EXPECT_TRUE(is(find({tmp.string(), iso.string(), zip.string()}), zip, false));
	EXPECT_FALSE(find({(tmp / "GAME.EXE").string(), zip.string()}));
	EXPECT_FALSE(find({iso.string()}));
	EXPECT_FALSE(find({}));
	EXPECT_TRUE(is(find({tmp.string()}), tmp, true));
	EXPECT_TRUE(is(find({tmp.string(), iso.string()}), tmp, true));
	EXPECT_TRUE(is(find({tmp.string(), other.string()}), other, true));
	EXPECT_FALSE(find({tmp.string(), (tmp / "GAME.EXE").string()}));
}

// A game folder is read as its archive would be (ada-i8ec).
TEST_F(PackageStartTest, AGameFolderGivesItsConfAsItsArchiveWould)
{
	const auto game = tmp / "game";
	WriteFile(game / "_DOSBOX" / "dosbox.conf", ConfText);
	WriteFile(game / "GAME.EXE", "MZ");

	const auto package = PackageStart::ReadPackage(game, Default());
	ASSERT_TRUE(package);
	EXPECT_TRUE(package->is_folder);
	EXPECT_EQ(package->profile, "default");
	EXPECT_EQ(package->path, game.string());
	EXPECT_EQ(package->base_dir, game / "_DOSBOX");
	ASSERT_EQ(package->confs.size(), 1u);
	EXPECT_EQ(package->confs[0].name, game.string() + "/_DOSBOX/dosbox.conf");
	EXPECT_EQ(package->confs[0].text, ConfText);
	EXPECT_EQ(package->confs[0].folder, "_DOSBOX");
	EXPECT_EQ(package->confs[0].host_file, game / "_DOSBOX" / "dosbox.conf");
	EXPECT_EQ(package->entries.size(), 3u);
}

TEST_F(PackageStartTest, AGameFolderOverTheEntryLimitGivesNone)
{
	const auto game = tmp / "game";
	WriteFile(game / "dosbox.conf", ConfText);
	WriteFile(game / "A.EXE", "MZ");
	WriteFile(game / "B.EXE", "MZ");
	Set("archive_max_entries", "2");
	EXPECT_FALSE(PackageStart::ReadPackage(game, Default()));
}

// An unreadable subfolder is listed but not entered, with a warning that
// names it; the rest of the folder still gives its package.
TEST_F(PackageStartTest, AGameFolderWalkSkipsAnUnreadableSubfolder)
{
	const auto game = tmp / "game";
	WriteFile(game / "dosbox.conf", ConfText);
	WriteFile(game / "GAME.EXE", "MZ");
	WriteFile(game / "LOCKED" / "SAVE.DAT", "x");
	fs::permissions(game / "LOCKED", fs::perms::none);
	std::error_code probe_ec = {};
	fs::directory_iterator(game / "LOCKED", probe_ec);
	if (!probe_ec) {
		fs::permissions(game / "LOCKED", fs::perms::owner_all);
		GTEST_SKIP() << "a folder without permissions stays readable here";
	}

	std::vector<std::string> logged = {};
	augra::Logger::instance().add_sink([&](augra::LogLevel level,
	                                       const char* component,
	                                       const std::string& message) {
		if (std::string_view(component) == "package" &&
		    level == augra::LogLevel::Warn) {
			logged.push_back(message);
		}
	});
	const auto package = PackageStart::ReadPackage(game, Default());
	augra::Logger::instance().clear_sinks();
	fs::permissions(game / "LOCKED", fs::perms::owner_all);

	ASSERT_TRUE(package);
	EXPECT_EQ(package->entries.size(), 3u);
	ASSERT_EQ(logged.size(), 1u);
	EXPECT_NE(logged[0].find("LOCKED"), std::string::npos) << logged[0];
}

TEST_F(PackageStartTest, AGameFolderWalkDoesNotFollowALinkedFolder)
{
	const auto game = tmp / "game";
	WriteFile(game / "dosbox.conf", ConfText);
	std::error_code ec = {};
	fs::create_directory_symlink(game, game / "loop", ec);
	if (ec) {
		GTEST_SKIP() << "no directory symlinks here: " << ec.message();
	}
	const auto package = PackageStart::ReadPackage(game, Default());
	ASSERT_TRUE(package);
	EXPECT_EQ(package->entries.size(), 2u);
}

TEST_F(PackageStartTest, AConfInAGameFolderOverOneMebibyteIsNotRead)
{
	const auto game = tmp / "game";
	auto text       = std::string(ConfText);
	text.resize(PackageStart::MaxConfBytes + 1, '#');
	WriteFile(game / "dosbox.conf", text);
	WriteFile(game / "GAME.EXE", "MZ");
	EXPECT_FALSE(PackageStart::ReadPackage(game, Default()));
}

// Relative MOUNT paths become the host paths they name in the folder.
TEST_F(PackageStartTest, AGameFolderLoadsWithHostPaths)
{
	PackageStart::Package package = {};
	package.path                  = "/g/Doom";
	package.is_folder             = true;
	package.base_dir              = tmp;
	package.entries.push_back({});
	package.entries.back().utf8_path = "game.inst";
	package.entries.push_back({});
	package.entries.back().utf8_path = "DATA";
	package.entries.back().is_dir    = true;
	package.confs.push_back({"/g/Doom/_DOSBOX/dosbox.conf",
	                         "[autoexec]\nmount c ..\nimgmount d ..\\GAME.INST -t iso\n"
	                         "mount e ..\\data\n",
	                         "_DOSBOX",
	                         {}});
	PackageStart::LoadPackage(*control, package);
	EXPECT_EQ(control->GetOverwrittenAutoexecSection().data,
	          "mount c \"/g/Doom\"\nimgmount d \"/g/Doom/game.inst\" -t iso\n"
	          "mount e \"/g/Doom/DATA\"");
	EXPECT_TRUE(control->package_mounts_drive);
	EXPECT_TRUE(control->package_mounts_c);
}

// A folder that is also the working directory: its dosbox.conf loaded as the
// local conf already and is not loaded a second time.
TEST_F(PackageStartTest, AConfLoadedAsTheLocalConfIsNotLoadedAgain)
{
	const auto game = tmp / "game";
	WriteFile(game / "dosbox.conf", "[cpu]\ncputype=386\n");
	control->loaded_config_paths_canonical.push_back(
	        fs::canonical(game / "dosbox.conf"));
	PackageStart::Package package = {};
	package.path                  = game.string();
	package.is_folder             = true;
	package.base_dir              = game;
	package.confs.push_back({game.string() + "/dosbox.conf",
	                         "[cpu]\ncputype=386\n",
	                         "",
	                         game / "dosbox.conf"});
	const auto listed = control->config_files.size();
	PackageStart::LoadPackage(*control, package);
	EXPECT_EQ(control->config_files.size(), listed);
	EXPECT_FALSE(control->package_conf_loaded);
}

// A launcher may pass the same file as -conf (GOG's own launch line does):
// it loads once, as the -conf the user named, with a -conf's trust.
TEST_F(PackageStartTest, AFolderConfAlsoGivenAsDashConfLoadsAsTheDashConf)
{
	const auto game = tmp / "game";
	WriteFile(game / "dosbox.conf", "[cpu]\ncputype=386\n");
	control->arguments.conf = {(game / "dosbox.conf").string()};
	PackageStart::Package package = {};
	package.path                  = game.string();
	package.is_folder             = true;
	package.base_dir              = game;
	package.confs.push_back({game.string() + "/dosbox.conf",
	                         "[cpu]\ncputype=386\n",
	                         "",
	                         game / "dosbox.conf"});
	PackageStart::LoadPackage(*control, package);
	EXPECT_FALSE(control->package_conf_loaded);
	const auto listed = control->config_files.size();
	EXPECT_TRUE(control->ParseConfigFile("custom",
	                                     (game / "dosbox.conf").string()));
	EXPECT_EQ(control->config_files.size(), listed + 1);
}

// The conf anchor is an allowed root for mounts; a game's own conf must not
// move it to the game's folder.
TEST_F(PackageStartTest, AFolderConfLeavesTheConfAnchorAlone)
{
	const auto game = tmp / "game";
	WriteFile(game / "conf" / "dosbox.conf", "[cpu]\ncputype=386\n");
	const auto before = control->loaded_config_paths_canonical;
	PackageStart::Package package = {};
	package.path                  = game.string();
	package.is_folder             = true;
	package.base_dir              = game;
	package.confs.push_back({game.string() + "/conf/dosbox.conf",
	                         "[cpu]\ncputype=386\n",
	                         "conf",
	                         game / "conf" / "dosbox.conf"});
	PackageStart::LoadPackage(*control, package);
	EXPECT_TRUE(control->package_conf_loaded);
	EXPECT_EQ(control->loaded_config_paths_canonical, before);
}

// The setting's old name still works.
TEST_F(PackageStartTest, TheOldSettingNameStillTurnsPackageConfsOff)
{
	EXPECT_TRUE(PackageStart::PackageConfEnabled());
	ASSERT_TRUE(get_section("dos")->HandleInputLine("archive_package_conf=off"));
	EXPECT_FALSE(PackageStart::PackageConfEnabled());
	EXPECT_EQ(get_section("dos")->GetString("archive_package_conf"), "");
	EXPECT_FALSE(get_section("dos")->GetBool("package_conf"));
}

// As window_size and windowresolution: the name parsed later wins.
TEST_F(PackageStartTest, TheNameParsedLaterWins)
{
	auto* dos = get_section("dos");
	ASSERT_TRUE(dos->HandleInputLine("archive_package_conf=on"));
	ASSERT_TRUE(dos->HandleInputLine("package_conf=off"));
	EXPECT_FALSE(PackageStart::PackageConfEnabled());
}

// MOUNT enters a lone inner archive (nested archives design 2026-10-06, N3);
// the conf is read there and the MOUNT lines name the chain.
TEST_F(PackageStartTest, ALoneInnerArchiveIsWhereTheConfIsRead)
{
	const auto inner = Zip("inner.zip",
	                       {
	                               {"dosbox.conf", ConfText},
                                       {   "GAME.EXE",     "MZ"}
        });
	std::ifstream in(inner, std::ios::binary);
	const std::string bytes((std::istreambuf_iterator<char>(in)),
	                        std::istreambuf_iterator<char>());
	const auto outer = Zip("outer.zip",
	                       {
	                               {"game.zip", bytes}
        });

	const auto package = PackageStart::ReadPackage(outer, Default());
	ASSERT_TRUE(package);
	EXPECT_EQ(package->path, outer.string() + "/game.zip");
	ASSERT_EQ(package->confs.size(), 1u);
	EXPECT_EQ(package->confs[0].name, outer.string() + "/game.zip/dosbox.conf");
	EXPECT_EQ(package->confs[0].text, ConfText);
}

// The confs load as -conf files do; their relative MOUNT paths name the
// archive, a line climbing out of it is left out.
TEST_F(PackageStartTest, APackageLoadsLikeAConfFile)
{
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.entries.push_back({});
	package.entries.back().utf8_path = "GAME.EXE";
	package.confs.push_back({"/g/game.zip/_DOSBOX/dosbox.conf",
	                         "[cpu]\ncputype=386\n[autoexec]\nmount c ..\n"
	                         "mount d ..\\..\\x\nGAME\n",
	                         "_DOSBOX"});

	const auto listed = control->config_files.size();
	ASSERT_FALSE(control->package_conf_loaded);
	PackageStart::LoadPackage(*control, package);

	EXPECT_EQ(control->GetSection("cpu")->GetPropertyValue("cputype"), "386");
	EXPECT_EQ(control->GetOverwrittenAutoexecConf(),
	          "/g/game.zip/_DOSBOX/dosbox.conf");
	EXPECT_EQ(control->GetOverwrittenAutoexecSection().data,
	          "mount c \"/g/game.zip\"\nGAME");
	EXPECT_TRUE(control->package_conf_loaded);
	EXPECT_EQ(control->config_files.size(), listed);
}

// --run: the package's drives, then the named program; only the MOUNT
// lines of its [autoexec] are taken.
TEST_F(PackageStartTest, UnderRunOnlyTheMountLinesAreTaken)
{
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.confs.push_back({"/g/game.zip/dosbox.conf",
	                         "[cpu]\ncputype=386\n[autoexec]\nmount c .\n"
	                         "imgmount d CD.ISO -t iso\nc:\nGAME\nexit\n",
	                         ""});

	PackageStart::LoadPackage(*control, package, true);

	EXPECT_EQ(control->GetSection("cpu")->GetPropertyValue("cputype"), "386");
	EXPECT_EQ(control->GetOverwrittenAutoexecSection().data,
	          "mount c \"/g/game.zip\"\n"
	          "imgmount d \"/g/game.zip/CD.ISO\" -t iso");
	EXPECT_TRUE(control->package_conf_loaded);
	EXPECT_TRUE(control->package_mounts_c);
}

TEST_F(PackageStartTest, APackageWithoutACMountSaysSo)
{
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.confs.push_back({"/g/game.zip/dosbox.conf",
	                         "[autoexec]\nimgmount d CD.ISO -t iso\n",
	                         ""});
	PackageStart::LoadPackage(*control, package, true);
	EXPECT_TRUE(control->package_conf_loaded);
	EXPECT_FALSE(control->package_mounts_c);
	EXPECT_TRUE(control->package_mounts_drive);
}

// A package that mounts nothing keeps the generated mount of the archive; an
// unmount is no mount.
TEST_F(PackageStartTest, APackageThatMountsNoDriveSaysSo)
{
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.confs.push_back({"/g/game.zip/dosbox.conf",
	                         "[cpu]\ncputype=386\n[autoexec]\nmount -u c\necho hi\n",
	                         ""});
	PackageStart::LoadPackage(*control, package);
	EXPECT_TRUE(control->package_conf_loaded);
	EXPECT_FALSE(control->package_mounts_drive);
	EXPECT_FALSE(control->package_mounts_c);
}

// An [autoexec] that is empty or whose last line is C:
// (any case) ends at the prompt, where UniMenu may come.
TEST_F(PackageStartTest, AnAutoexecEndingAtThePromptOfCIsNoted)
{
	const auto ends_at_prompt = [&](const std::string& text) {
		PackageStart::Package package = {};
		package.path                  = "/g/game.zip";
		package.base_dir              = tmp;
		package.confs.push_back({"/g/game.zip/game.conf", text, ""});
		PackageStart::LoadPackage(*control, package);
		return control->package_ends_at_prompt;
	};
	EXPECT_TRUE(ends_at_prompt("[autoexec]\nmount c .\nc:\n"));
	EXPECT_TRUE(ends_at_prompt("[autoexec]\nmount c .\nC:\n\n"));
	EXPECT_TRUE(ends_at_prompt("[autoexec]\nmount c .\n  @c:  \n"));
	EXPECT_TRUE(ends_at_prompt("[cpu]\ncputype=386\n[autoexec]\n"));
	EXPECT_TRUE(ends_at_prompt("[cpu]\ncputype=386\n"));
	EXPECT_FALSE(ends_at_prompt("[autoexec]\nmount c .\nc:\nGAME.EXE\n"));
	EXPECT_FALSE(ends_at_prompt("[autoexec]\nmount c .\nc:\ncls\n"));
	EXPECT_FALSE(ends_at_prompt("[autoexec]\nmount c .\nd:\n"));
	EXPECT_FALSE(ends_at_prompt("[autoexec]\nmount c .\nc:\\\n"));
}

// Chained confs run their [autoexec] one after the other: the last line of
// the last one counts.
TEST_F(PackageStartTest, TheLastConfsLastLineDecidesThePrompt)
{
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.confs.push_back(
	        {"/g/game.zip/a.conf", "[autoexec]\nmount c .\nc:\n", ""});
	package.confs.push_back({"/g/game.zip/b.conf", "[autoexec]\nGAME\n", ""});
	PackageStart::LoadPackage(*control, package);
	EXPECT_FALSE(control->package_ends_at_prompt);
}

// A sequential archive is unpacked into the archive cache to read its conf, as
// MOUNT unpacks it; archive_sequential = refuse keeps it closed.
TEST_F(PackageStartTest, ASolidArchiveIsUnpackedToReadItsConf)
{
	const auto path   = tmp / "game.7z";
	std::string error = {};
	ASSERT_TRUE(WriteArchive(path,
	                         Kind::SevenZip,
	                         {
	                                 {"dosbox.conf", ConfText},
                                         {   "GAME.EXE",     "MZ"}
        },
	                         nullptr,
	                         &error))
	        << error;

	Set("archive_sequential", "refuse");
	EXPECT_FALSE(PackageStart::ReadPackage(path, Default()));

	Set("archive_sequential", "unpack");
	const auto package = PackageStart::ReadPackage(path, Default());
	ASSERT_TRUE(package);
	ASSERT_EQ(package->confs.size(), 1u);
	EXPECT_EQ(package->confs[0].text, ConfText);
}

// The log reaches the host terminal; an archive's escape sequence must not.
TEST_F(PackageStartTest, ARefusedLineReachesTheLogEscaped)
{
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.confs.push_back({"/g/game.zip/\x1b]2;x\x07.conf",
	                         "[autoexec]\nmount c ..\\\x1b[2J\n",
	                         ""});

	std::vector<std::string> logged = {};
	augra::Logger::instance().add_sink([&](augra::LogLevel,
	                                       const char* component,
	                                       const std::string& message) {
		if (std::string_view(component) == "package") {
			logged.push_back(message);
		}
	});
	PackageStart::LoadPackage(*control, package);
	augra::Logger::instance().clear_sinks();

	ASSERT_FALSE(logged.empty());
	for (const auto& message : logged) {
		EXPECT_EQ(message.find('\x1b'), std::string::npos) << message;
		EXPECT_EQ(message.find('\x07'), std::string::npos) << message;
	}
}

// GOG's root pair says mount C ".." from DOSBOX\, its DOSBox working folder
// (ada-ubkn); the profile's working folder is where that path counts from.
std::vector<PackageProfile::Profile> GogRootPair()
{
	PackageProfile::Profile gog = {};
	gog.name                    = "gog";
	gog.files                   = {"dosbox*.conf"};
	gog.chained                 = {"dosbox*_single.conf"};
	gog.working_dir             = "DOSBOX";
	return {gog};
}

constexpr auto GogSingle = "[autoexec]\nmount C \"..\"\n"
                          "mount C \"..\\cloud_saves\" -t overlay\nc:\nVIEW.COM\n";

TEST_F(PackageStartTest, GogsRootPairCountsFromItsWorkingFolder)
{
	const auto zip = Zip("view.zip",
	                     {{"dosboxVIEW.conf", "[cpu]\ncputype=386\n"},
	                      {"dosboxVIEW_single.conf", GogSingle},
	                      {"DOSBOX/", "", true},
	                      {"DOSBOX/README.TXT", "x"},
	                      {"VIEW.COM", "\xc3"}});
	const auto package = PackageStart::ReadPackage(zip, GogRootPair());
	ASSERT_TRUE(package);
	ASSERT_EQ(package->confs.size(), 2u);
	EXPECT_EQ(package->confs[1].folder, "DOSBOX");
	PackageStart::LoadPackage(*control, *package);
	EXPECT_EQ(control->GetOverwrittenAutoexecSection().data,
	          "mount C \"" + package->path + "\"\nmount C \"" + package->path +
	                  "/cloud_saves\" -t overlay\nc:\nVIEW.COM");
	EXPECT_TRUE(control->package_mounts_c);
}

TEST_F(PackageStartTest, TheWorkingFolderIsMatchedInTheArchivesSpelling)
{
	const auto zip     = Zip("view.zip",
                             {{"dosboxVIEW.conf", "[cpu]\n"},
                              {"dosboxVIEW_single.conf", GogSingle},
                              {"DosBox/", "", true},
                              {"VIEW.COM", "\xc3"}});
	const auto package = PackageStart::ReadPackage(zip, GogRootPair());
	ASSERT_TRUE(package);
	EXPECT_EQ(package->confs[0].folder, "DosBox");
}

TEST_F(PackageStartTest, WithoutTheWorkingFolderThePathCountsFromTheConf)
{
	const auto zip     = Zip("view.zip",
                             {{"dosboxVIEW.conf", "[cpu]\n"},
                              {"dosboxVIEW_single.conf", GogSingle},
                              {"VIEW.COM", "\xc3"}});
	const auto package = PackageStart::ReadPackage(zip, GogRootPair());
	ASSERT_TRUE(package);
	EXPECT_EQ(package->confs[0].folder, "");
}

// A downloaded game must not open or move the REST API.
TEST_F(PackageStartTest, APackageCannotSetTheWebserver)
{
	const auto before = control->GetSection("webserver")
	                            ->GetPropertyValue("webserver_bind_address");
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.confs.push_back({"/g/game.zip/dosbox.conf",
	                         "[webserver]\nwebserver_bind_address=0.0.0.0\n"
	                         "[cpu]\ncputype=386\n",
	                         ""});
	PackageStart::LoadPackage(*control, package);
	EXPECT_EQ(control->GetSection("webserver")->GetPropertyValue("webserver_bind_address"),
	          before);
	EXPECT_EQ(control->GetSection("cpu")->GetPropertyValue("cputype"), "386");
}

// Nor open the host's loopback, where the REST API listens, to the guest.
TEST_F(PackageStartTest, APackageCannotOpenTheHostLoopback)
{
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.confs.push_back(
	        {"/g/game.zip/dosbox.conf",
	         "[ethernet]\nhost_loopback=on\nmacaddr=AC:DE:48:00:00:02\n",
	         ""});
	PackageStart::LoadPackage(*control, package);
	EXPECT_EQ(control->GetSection("ethernet")->GetPropertyValue("host_loopback"),
	          "off");
	EXPECT_EQ(control->GetSection("ethernet")->GetPropertyValue("macaddr"),
	          "AC:DE:48:00:00:02");
}

// Control bytes from the archive reach no log line raw.
TEST_F(PackageStartTest, ArchiveTextReachesEveryLogLineEscaped)
{
	PackageStart::Package package = {};
	package.path                  = "/g/game.zip";
	package.base_dir              = tmp;
	package.confs.push_back({"/g/game.zip/\x1b]2;x\x07.conf",
	                         "[cpu]\n\x1b[2J=1\n[webserver]\n",
	                         ""});

	std::vector<std::string> logged = {};
	augra::Logger::instance().add_sink(
	        [&](augra::LogLevel, const char*, const std::string& message) {
		        logged.push_back(message);
	        });
	PackageStart::LoadPackage(*control, package);
	augra::Logger::instance().clear_sinks();

	ASSERT_FALSE(logged.empty());
	for (const auto& message : logged) {
		EXPECT_EQ(message.find('\x1b'), std::string::npos) << message;
		EXPECT_EQ(message.find('\x07'), std::string::npos) << message;
	}
}

// MOUNT lines and -pr resolve a relative host path differently; the
// package names the archive by its absolute path, as autoexec.cpp does.
TEST_F(PackageStartTest, ARelativeArchiveArgumentIsMadeAbsolute)
{
	const auto zip  = Zip("game.zip",
	                      {
                                     {"dosbox.conf", ConfText},
                                     {   "GAME.EXE",     "MZ"}
        });
	const auto here = fs::current_path();
	fs::current_path(tmp);
	const auto package = PackageStart::ReadPackage("game.zip", Default());
	fs::current_path(here);
	ASSERT_TRUE(package);
	EXPECT_EQ(package->path, zip.string());
}
