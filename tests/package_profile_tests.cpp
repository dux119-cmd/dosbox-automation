// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/archive_toml.h"
#include "dos/archive/package_profile.h"

#include "test_temp_dir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace PackageProfile;

namespace {

std::vector<ArchiveMount::Entry> Entries(const std::vector<std::string>& paths)
{
	std::vector<ArchiveMount::Entry> out = {};
	for (const auto& path : paths) {
		ArchiveMount::Entry e = {};
		e.ordinal             = static_cast<int>(out.size());
		e.is_dir              = path.ends_with('/');
		e.utf8_path = e.is_dir ? path.substr(0, path.size() - 1) : path;
		out.push_back(e);
	}
	return out;
}

std::vector<std::string> ConfPaths(const std::vector<ArchiveMount::Entry>& entries,
                                   const Found& found)
{
	std::vector<std::string> out = {};
	for (const auto i : found.confs) {
		out.push_back(entries.at(i).utf8_path);
	}
	return out;
}

Profile Make(std::string name, std::vector<std::string> paths,
             std::vector<std::string> files, std::vector<std::string> chained = {})
{
	Profile p = {};
	p.name    = std::move(name);
	p.paths   = std::move(paths);
	p.files   = std::move(files);
	p.chained = std::move(chained);
	return p;
}

std::vector<std::string> Names(const WorkingList& list)
{
	std::vector<std::string> out = {};
	for (const auto& p : list.order) {
		out.push_back(p.name);
	}
	return out;
}

bool AnyNoteHas(const std::vector<std::string>& notes, std::string_view text)
{
	return std::any_of(notes.begin(), notes.end(), [&](const auto& n) {
		return n.find(text) != std::string::npos;
	});
}

const std::string GogProfiles =
        "[[global]]\n"
        "default_profile = 'gog'\n"
        "\n"
        "[[gog]]\n"
        "enabled = true\n"
        "dosconfig_paths = ['DOSBOX', '.']\n"
        "dosconfig_file = ['dosbox*.conf']\n"
        "dosconfig_chained = ['dosbox*_single.conf']\n";

} // namespace

TEST(PackageProfile, PatternsMatchOneComponentIgnoringCase)
{
	EXPECT_TRUE(PatternMatches("dosbox*.conf", "dosboxLOL1.conf"));
	EXPECT_TRUE(PatternMatches("DOSBOX.CNF", "dosbox.cnf"));
	EXPECT_TRUE(PatternMatches("*_single.conf", "dosboxQG4_single.conf"));
	EXPECT_FALSE(PatternMatches("*_single.conf", "dosboxQG4.conf"));
	EXPECT_TRUE(PatternMatches("game?.cfg", "GAME1.CFG"));
	EXPECT_FALSE(PatternMatches("game?.cfg", "game.cfg"));
	EXPECT_FALSE(PatternMatches("*.conf", "a/b.conf"));
	EXPECT_TRUE(PatternMatches("*", "anything"));
}

TEST(PackageProfile, TheDotDotScopeRule)
{
	EXPECT_TRUE(PathAllowed("."));
	EXPECT_TRUE(PathAllowed("_DOSBOX"));
	EXPECT_TRUE(PathAllowed("*/DOSBOX"));
	EXPECT_TRUE(PathAllowed("../game*"));
	EXPECT_TRUE(PathAllowed("..\\game*"));
	EXPECT_FALSE(PathAllowed(".."));
	EXPECT_FALSE(PathAllowed("../*"));
	EXPECT_FALSE(PathAllowed("../?*"));
	EXPECT_FALSE(PathAllowed("a/../../b"));
	EXPECT_FALSE(PathAllowed("a/.."));
	EXPECT_FALSE(PathAllowed("/etc"));
	EXPECT_FALSE(PathAllowed("C:\\GAMES"));
	EXPECT_FALSE(PathAllowed("a//b"));
	EXPECT_FALSE(PathAllowed(""));
}

TEST(PackageProfile, NamesAndFileNamesFollowTheRules)
{
	EXPECT_TRUE(IsValidName("local_dosbox_conf"));
	EXPECT_TRUE(IsValidName("steam-074-2"));
	EXPECT_FALSE(IsValidName("gog.074"));
	EXPECT_FALSE(IsValidName("a b"));
	EXPECT_FALSE(IsValidName(""));
	EXPECT_TRUE(IsPlainFileName("dosbox*_single.conf"));
	EXPECT_FALSE(IsPlainFileName("DOSBOX/dosbox.conf"));
	EXPECT_FALSE(IsPlainFileName("..\\dosbox.conf"));
	EXPECT_FALSE(IsPlainFileName(".."));
	EXPECT_FALSE(IsPlainFileName(""));
}

TEST(PackageProfile, TheBuiltInDefaultFindsTheHouseFormBeforeTheRoot)
{
	const auto entries = Entries(
	        {"_DOSBOX/DOSBOX.CNF", "dosbox.conf", "GAME.EXE"});
	const auto m = FindPackage({BuiltInDefault()}, entries);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "default");
	ASSERT_EQ(m->games.size(), 1u);
	EXPECT_EQ(m->games[0].folder, "_DOSBOX");
	EXPECT_EQ(ConfPaths(entries, m->games[0]),
	          std::vector<std::string>{"_DOSBOX/DOSBOX.CNF"});
}

TEST(PackageProfile, NamesAndPathsIgnoreCase)
{
	const auto entries = Entries({"_dosbox/dosbox.cnf"});
	const auto m       = FindPackage({BuiltInDefault()}, entries);
	ASSERT_TRUE(m);
	ASSERT_EQ(m->games.size(), 1u);
	EXPECT_EQ(m->games[0].folder, "_dosbox");
}

TEST(PackageProfile, TheFileListIsTriedInOrder)
{
	const auto entries = Entries({"dosbox.conf", "dosbox-automation.conf"});
	const auto m       = FindPackage({BuiltInDefault()}, entries);
	ASSERT_TRUE(m);
	ASSERT_EQ(m->games.size(), 1u);
	EXPECT_EQ(ConfPaths(entries, m->games[0]),
	          std::vector<std::string>{"dosbox-automation.conf"});
}

TEST(PackageProfile, ChainedFilesFollowTheConfAndCountAsChained)
{
	const auto entries = Entries({"DOSBOX/dosboxLOL1_single.conf",
	                              "DOSBOX/dosboxLOL1.conf",
	                              "LOLCD.EXE"});
	const auto gog     = Make("gog",
	                          {"DOSBOX", "."},
	                          {"dosbox*.conf"},
	                          {"dosbox*_single.conf"});
	const auto m       = FindPackage({gog}, entries);
	ASSERT_TRUE(m);
	ASSERT_EQ(m->games.size(), 1u);
	EXPECT_EQ(m->games[0].folder, "DOSBOX");
	EXPECT_EQ(ConfPaths(entries, m->games[0]),
	          (std::vector<std::string>{"DOSBOX/dosboxLOL1.conf",
	                                    "DOSBOX/dosboxLOL1_single.conf"}));
}

TEST(PackageProfile, AMissingChainedFileMeansNoFullMatch)
{
	const auto entries = Entries({"DOSBOX/dosboxLOL1.conf", "dosbox.conf"});
	const auto gog = Make("gog", {"DOSBOX"}, {"dosbox*.conf"}, {"dosbox*_single.conf"});
	const auto m = FindPackage({gog, BuiltInDefault()}, entries);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "default");
	ASSERT_EQ(m->games.size(), 1u);
	EXPECT_EQ(m->games[0].folder, "");
}

TEST(PackageProfile, AMissingFolderOrConfIsNoMatch)
{
	const auto entries = Entries({"GAME.EXE", "DATA/"});
	EXPECT_FALSE(FindPackage({BuiltInDefault()}, entries));
	EXPECT_FALSE(FindPackage({Make("x", {"NOPE"}, {"*.conf"})},
	                         Entries({"a.conf"})));
}

TEST(PackageProfile, AWildcardPathGivesOneGamePerFolder)
{
	const auto entries = Entries({"PQ2/DOSBOX/dosboxPQ2.conf",
	                              "PQ1/DOSBOX/dosboxPQ1.conf",
	                              "PQ3/DATA/x.conf",
	                              "SierraLauncher.exe"});
	const auto m = FindPackage({Make("sierra", {"*/DOSBOX"}, {"dosbox*.conf"})},
	                           entries);
	ASSERT_TRUE(m);
	ASSERT_EQ(m->games.size(), 2u);
	EXPECT_EQ(m->games[0].folder, "PQ1/DOSBOX");
	EXPECT_EQ(m->games[1].folder, "PQ2/DOSBOX");
}

TEST(PackageProfile, AParentPathIsLeftToTheMount)
{
	// `..` reaches outside the mount; the archive's own entries cannot
	// answer it, not even a hostile list that names `..`.
	const auto entries = Entries({"game1/dosbox.conf", "../game1/dosbox.conf"});
	EXPECT_FALSE(FindPackage({Make("up", {"../game*"}, {"dosbox.conf"})},
	                         entries));
}

TEST(PackageProfile, AProfilesFileIsReadAndBrokenProfilesAreSkipped)
{
	const auto file = ParseProfileFile(
	        "# my profiles\n"
	        "[[global]]\n"
	        "default_profile = 'gog'\n"
	        "\n"
	        "[[gog]]\n"
	        "dosconfig_file = ['dosbox*.conf']\n"
	        "\n"
	        "[[no_file]]\n"
	        "enabled = true\n"
	        "\n"
	        "[[gog]]\n"
	        "dosconfig_file = ['again.conf']\n"
	        "\n"
	        "[[typo]]\n"
	        "enable = false\n"
	        "dosconfig_file = ['x.conf']\n"
	        "\n"
	        "[[broken]]\n"
	        "dosconfig_file = ['unterminated]\n"
	        "\n"
	        "[[pathy]]\n"
	        "dosconfig_file = ['DOSBOX/x.conf']\n"
	        "\n"
	        "[[up]]\n"
	        "dosconfig_paths = ['../*']\n"
	        "dosconfig_file = ['x.conf']\n"
	        "\n"
	        "[[default]]\n"
	        "dosconfig_file = ['x.conf']\n"
	        "\n"
	        "[[gog.074]]\n"
	        "dosconfig_file = ['x.conf']\n"
	        "\n"
	        "[[last]]\n"
	        "enabled = false\n"
	        "dosconfig_file = ['last.conf']\n",
	        "user");
	EXPECT_TRUE(file.has_global);
	EXPECT_EQ(file.default_profile, "gog");
	ASSERT_EQ(file.profiles.size(), 2u);
	EXPECT_EQ(file.profiles[0].name, "gog");
	EXPECT_EQ(file.profiles[0].paths, std::vector<std::string>{"."});
	EXPECT_TRUE(file.profiles[0].enabled);
	EXPECT_EQ(file.profiles[1].name, "last");
	EXPECT_FALSE(file.profiles[1].enabled);
	for (const auto* name :
	     {"no_file", "'gog'", "typo", "broken", "pathy", "up", "'default'", "gog.074"}) {
		EXPECT_TRUE(AnyNoteHas(file.notes, name)) << name;
	}
	EXPECT_TRUE(AnyNoteHas(file.notes, "enable"));
	EXPECT_EQ(file.notes.size(), 8u);
}

// GOG starts DOSBox in DOSBOX\, so its confs' relative paths count from there.
TEST(PackageProfile, AWorkingFolderIsOneFolderName)
{
	const auto file = ParseProfileFile(
	        "[[gog]]\n"
	        "dosconfig_file = ['dosbox*.conf']\n"
	        "dosconfig_working_dir = 'DOSBOX'\n"
	        "[[nested]]\n"
	        "dosconfig_file = ['x.conf']\n"
	        "dosconfig_working_dir = 'a/b'\n"
	        "[[up]]\n"
	        "dosconfig_file = ['x.conf']\n"
	        "dosconfig_working_dir = '..'\n"
	        "[[listed]]\n"
	        "dosconfig_file = ['x.conf']\n"
	        "dosconfig_working_dir = ['DOSBOX']\n",
	        "user");
	ASSERT_EQ(file.profiles.size(), 1u);
	EXPECT_EQ(file.profiles[0].working_dir, "DOSBOX");
	for (const auto* name : {"nested", "up", "listed"}) {
		EXPECT_TRUE(AnyNoteHas(file.notes, name)) << name;
	}
}

TEST(PackageProfile, TheMatchCarriesTheWorkingFolder)
{
	const auto entries = Entries(
	        {"dosboxJILL.conf", "dosboxJILL_single.conf", "JILL1.EXE"});
	auto gog = Make("gog", {"."}, {"dosbox*.conf"}, {"dosbox*_single.conf"});
	gog.working_dir = "DOSBOX";
	const auto m    = FindPackage({gog}, entries);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->working_dir, "DOSBOX");
}

TEST(PackageProfile, AGlobalSectionWithProblemsIsNoted)
{
	const auto missing = ParseProfileFile("[[a]]\ndosconfig_file = ['x']\n",
	                                      "user");
	EXPECT_FALSE(missing.has_global);
	EXPECT_FALSE(missing.default_profile);
	const auto bad_key = ParseProfileFile("[[global]]\nnonsense = 1\n", "user");
	EXPECT_TRUE(bad_key.has_global);
	EXPECT_FALSE(bad_key.default_profile);
	EXPECT_TRUE(AnyNoteHas(bad_key.notes, "nonsense"));
	const auto bad_name = ParseProfileFile("[[global]]\ndefault_profile = 'a b'\n",
	                                       "user");
	EXPECT_FALSE(bad_name.default_profile);
	EXPECT_FALSE(bad_name.notes.empty());
	const auto twice = ParseProfileFile(
	        "[[global]]\ndefault_profile = 'a'\n[[global]]\ndefault_profile = 'b'\n",
	        "user");
	EXPECT_EQ(twice.default_profile, "a");
	EXPECT_FALSE(twice.notes.empty());
	const auto global_profile = ParseProfileFile("[[global]]\ndosconfig_file = ['x']\n",
	                                             "user");
	EXPECT_FALSE(global_profile.notes.empty());
}

TEST(PackageProfile, TheWorkingListIsDefaultThenEnabledThenTheBuiltIn)
{
	const auto user    = ParseProfileFile(GogProfiles +
                                                   "[[mine]]\n"
	                                              "dosconfig_file = ['mine.conf']\n"
	                                              "[[off]]\n"
	                                              "enabled = false\n"
	                                              "dosconfig_file = ['off.conf']\n",
                                           "user");
	const auto shipped = ParseProfileFile(
	        "[[global]]\n"
	        "default_profile = 'shipped_a'\n"
	        "[[shipped_a]]\n"
	        "dosconfig_file = ['a.conf']\n"
	        "[[gog]]\n"
	        "dosconfig_file = ['shipped-gog.conf']\n"
	        "[[off]]\n"
	        "dosconfig_file = ['shipped-off.conf']\n",
	        "shipped");
	const auto list = BuildWorkingList(user, shipped);
	ASSERT_EQ(Names(list),
	          (std::vector<std::string>{"gog", "mine", "shipped_a", "default"}));
	EXPECT_EQ(list.order[0].files, std::vector<std::string>{"dosbox*.conf"})
	        << "the user's gog, not the shipped one";
	EXPECT_FALSE(list.user_default_invalid);
}

TEST(PackageProfile, TheShippedDefaultProfileCountsWhenTheUserNamesNone)
{
	const auto user = ParseProfileFile("[[mine]]\ndosconfig_file = ['m']\n",
	                                   "user");
	const auto shipped = ParseProfileFile(
	        "[[global]]\ndefault_profile = 'b'\n"
	        "[[a]]\ndosconfig_file = ['a']\n"
	        "[[b]]\ndosconfig_file = ['b']\n",
	        "shipped");
	EXPECT_EQ(Names(BuildWorkingList(user, shipped)),
	          (std::vector<std::string>{"b", "mine", "a", "default"}));
}

TEST(PackageProfile, TheBuiltInDefaultOnTopIsNotRepeated)
{
	const auto user = ParseProfileFile(
	        "[[global]]\ndefault_profile = 'default'\n"
	        "[[mine]]\ndosconfig_file = ['m']\n",
	        "user");
	EXPECT_EQ(Names(BuildWorkingList(user, {})),
	          (std::vector<std::string>{"default", "mine"}));
}

TEST(PackageProfile, AnUnknownOrDisabledDefaultProfileIsInvalid)
{
	for (const auto* text :
	     {"[[global]]\ndefault_profile = 'nowhere'\n",
	      "[[global]]\ndefault_profile = 'off'\n"
	      "[[off]]\nenabled = false\ndosconfig_file = ['o']\n"}) {
		const auto list = BuildWorkingList(ParseProfileFile(text, "user"),
		                                   {});
		EXPECT_EQ(Names(list), std::vector<std::string>{"default"}) << text;
		EXPECT_TRUE(list.user_default_invalid) << text;
		EXPECT_FALSE(list.notes.empty()) << text;
	}
}

TEST(PackageProfile, TheDemoFileIsValidAndFindsARootConf)
{
	const auto file = ParseProfileFile(DemoFileText(), "user");
	EXPECT_TRUE(file.notes.empty());
	EXPECT_EQ(file.default_profile, "default");
	ASSERT_EQ(file.profiles.size(), 1u);
	EXPECT_EQ(file.profiles[0].name, "local_dosbox_conf");
	const auto list    = BuildWorkingList(file, {});
	const auto entries = Entries({"dosbox.conf"});
	const auto m       = FindPackage(list.order, entries);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "default");
}

TEST(PackageProfile, TheGlobalSectionIsAddedOrRepairedAndTheRestKept)
{
	const std::string mine = "# my comment\n[[mine]]\ndosconfig_file = ['m']\n";
	EXPECT_EQ(WithDefaultGlobal(GogProfiles, false), GogProfiles);
	const auto added = WithDefaultGlobal(mine, false);
	EXPECT_NE(added.find("# my comment\n[[mine]]\ndosconfig_file = ['m']\n"),
	          std::string::npos);
	const auto reread = ParseProfileFile(added, "user");
	EXPECT_EQ(reread.default_profile, "default");
	EXPECT_TRUE(reread.notes.empty());

	const std::string bad =
	        "[[global]]\n# keep me\ndefault_profile = 'nowhere'\n\n"
	        "[[mine]]\ndosconfig_file = ['m']\n";
	const auto list = BuildWorkingList(ParseProfileFile(bad, "user"), {});
	ASSERT_TRUE(list.user_default_invalid);
	const auto repaired = WithDefaultGlobal(bad, true);
	EXPECT_NE(repaired.find("# keep me"), std::string::npos);
	EXPECT_EQ(repaired.find("nowhere"), std::string::npos);
	EXPECT_EQ(ParseProfileFile(repaired, "user").default_profile, "default");
	EXPECT_NE(repaired.find("[[mine]]\ndosconfig_file = ['m']\n"),
	          std::string::npos);
}

namespace {

std::string Slurp(const std::filesystem::path& path)
{
	auto error = std::string();
	return ArchiveToml::ReadTextFile(path, &error).value_or("<unreadable: " + error + ">");
}

void Spit(const std::filesystem::path& path, const std::string& text)
{
	auto error = std::string();
	ASSERT_TRUE(ArchiveToml::WriteTextAtomically(path, text, &error)) << error;
}

class PackageProfileLoad : public ::testing::Test {
protected:
	void SetUp() override
	{
		dir = TestTempDir::MakeUnique("package_profile_");
		ASSERT_FALSE(dir.empty());
		user    = dir / "package-profiles.toml";
		shipped = dir / "shipped.toml";
		Spit(shipped, "[[b]]\ndosconfig_file = ['b.conf']\n");
	}
	void TearDown() override
	{
		std::error_code ec = {};
		std::filesystem::remove_all(dir, ec);
	}
	std::filesystem::path dir     = {};
	std::filesystem::path user    = {};
	std::filesystem::path shipped = {};
};

} // namespace

TEST_F(PackageProfileLoad, AMissingUserFileIsCreatedFromTheDemo)
{
	const auto list = Load(user, shipped);
	EXPECT_EQ(Slurp(user), DemoFileText());
	EXPECT_EQ(Names(list),
	          (std::vector<std::string>{"default", "local_dosbox_conf", "b"}));
	EXPECT_TRUE(AnyNoteHas(list.notes, "created"));
}

TEST_F(PackageProfileLoad, AUserFileWithoutGlobalGetsItAndKeepsTheRest)
{
	const std::string mine = "# keep this\n[[mine]]\ndosconfig_file = ['m']\n";
	Spit(user, mine);
	const auto list = Load(user, shipped);
	const auto now  = Slurp(user);
	EXPECT_TRUE(now.starts_with("[[global]]\ndefault_profile = \"default\"\n"))
	        << now;
	EXPECT_TRUE(now.ends_with(mine)) << now;
	EXPECT_EQ(Names(list), (std::vector<std::string>{"default", "mine", "b"}));
	EXPECT_TRUE(AnyNoteHas(list.notes, "default_profile"));
}

// The global section written on load makes the user file's default win over
// the one the shipped file names.
TEST_F(PackageProfileLoad, TheWrittenUserGlobalWinsOverTheShippedDefault)
{
	Spit(shipped,
	     "[[global]]\ndefault_profile = 'b'\n[[b]]\ndosconfig_file = ['b.conf']\n");
	Spit(user, "[[mine]]\ndosconfig_file = ['m']\n");
	const auto list = Load(user, shipped);
	EXPECT_EQ(Names(list), (std::vector<std::string>{"default", "mine", "b"}));
}

TEST_F(PackageProfileLoad, AnInvalidDefaultProfileIsRewrittenAndNoted)
{
	Spit(user, "[[global]]\ndefault_profile = 'nowhere'\n");
	const auto list = Load(user, shipped);
	EXPECT_EQ(Slurp(user), "[[global]]\ndefault_profile = \"default\"\n");
	EXPECT_EQ(Names(list), (std::vector<std::string>{"default", "b"}));
	EXPECT_TRUE(AnyNoteHas(list.notes, "nowhere"));
}

TEST_F(PackageProfileLoad, AValidUserFileIsLeftAlone)
{
	const std::string mine = "[[global]]\ndefault_profile = 'b' # shipped one\n";
	Spit(user, mine);
	const auto list = Load(user, shipped);
	EXPECT_EQ(Slurp(user), mine);
	EXPECT_EQ(Names(list), (std::vector<std::string>{"b", "default"}));
}

TEST_F(PackageProfileLoad, AMissingShippedFileIsNotedAndTheRestWorks)
{
	std::error_code ec = {};
	std::filesystem::remove(shipped, ec);
	const auto list = Load(user, shipped);
	EXPECT_EQ(Names(list),
	          (std::vector<std::string>{"default", "local_dosbox_conf"}));
	EXPECT_TRUE(AnyNoteHas(list.notes, "shipped.toml"));
}

#if !defined(WIN32)
// The user file is read without following a link and never written through
// one; a refused file is noted and the shipped profiles still count.
TEST_F(PackageProfileLoad, ALinkedUserFileIsNotedAndNeverWritten)
{
	const auto real = dir / "real.toml";
	Spit(real, "[[mine]]\ndosconfig_file = ['m']\n");
	std::filesystem::create_symlink(real, user);
	const auto list = Load(user, shipped);
	EXPECT_TRUE(std::filesystem::is_symlink(user));
	EXPECT_EQ(Slurp(real), "[[mine]]\ndosconfig_file = ['m']\n");
	EXPECT_EQ(Names(list), (std::vector<std::string>{"default", "b"}));
	EXPECT_TRUE(AnyNoteHas(list.notes, "package-profiles.toml"));
}
#endif

namespace {

// ctest runs the tests from the source tree.
const std::filesystem::path ShippedProfiles = "resources/package-profiles/package-profiles.toml";

ProfileFile ReadShipped()
{
	std::string error = {};
	const auto text   = ArchiveToml::ReadTextFile(ShippedProfiles, &error);
	EXPECT_TRUE(text) << error;
	return ParseProfileFile(text.value_or(""), "shipped");
}

// What a mount finds with no user file: the built-in default, then the
// shipped profiles.
std::optional<Match> FindShipped(const std::vector<std::string>& paths)
{
	const auto entries = Entries(paths);
	return FindPackage(BuildWorkingList({}, ReadShipped()).order, entries);
}

std::vector<std::vector<std::string>> AllConfs(const std::vector<std::string>& paths,
                                               const Match& match)
{
	const auto entries                        = Entries(paths);
	std::vector<std::vector<std::string>> out = {};
	for (const auto& game : match.games) {
		out.push_back(ConfPaths(entries, game));
	}
	return out;
}

} // namespace

TEST(PackageProfileShipped, TheFileIsValidAndEveryProfileEnabled)
{
	const auto shipped = ReadShipped();
	EXPECT_TRUE(shipped.notes.empty())
	        << (shipped.notes.empty() ? "" : shipped.notes.front());
	std::vector<std::string> names = {};
	for (const auto& p : shipped.profiles) {
		names.push_back(p.name);
		EXPECT_TRUE(p.enabled) << p.name;
	}
	EXPECT_EQ(names,
	          (std::vector<std::string>{"dosbox-gog-pair",
	                                    "dosbox-gog-pair-folders",
	                                    "steam-nightdive-linux",
	                                    "gog-bethesda-doom",
	                                    "steam-bethesda-wolf3d",
	                                    "steam-sneg-forgotten-realms",
	                                    "steam-apogee-daum",
	                                    "steam-pixelgames-uk",
	                                    "zoom-platform"}));
}

// GOG Descent: the pair at the root, client, server and settings beside it.
TEST(PackageProfileShipped, AGogPairAtTheRoot)
{
	const std::vector<std::string> paths = {"DESCENT.BAT",
	                                        "DOSBOX/",
	                                        "SETUP.EXE",
	                                        "cloud_saves/",
	                                        "dosboxDescent.conf",
	                                        "dosboxDescent_client.conf",
	                                        "dosboxDescent_server.conf",
	                                        "dosboxDescent_settings.conf",
	                                        "dosboxDescent_single.conf"};
	const auto m                         = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "dosbox-gog-pair");
	EXPECT_EQ(m->working_dir, "DOSBOX");
	EXPECT_EQ(AllConfs(paths, *m),
	          (std::vector<std::vector<std::string>>{
	                  {"dosboxDescent.conf", "dosboxDescent_single.conf"}
        }));
}

// Steam's Police Quest Collection: one GOG pair per game folder.
TEST(PackageProfileShipped, GogPairsOneFolderDown)
{
	const std::vector<std::string> paths = {"SierraLauncher.exe",
	                                        "PQ1/DOSBOX/dosboxPQ1.conf",
	                                        "PQ1/DOSBOX/dosboxPQ1_single.conf",
	                                        "PQ2/DOSBOX/dosboxPQ2.conf",
	                                        "PQ2/DOSBOX/dosboxPQ2_single.conf",
	                                        "PQ4/DOSBOX/dosboxPQ4cd.conf",
	                                        "PQ4/DOSBOX/dosboxPQ4cd_single.conf",
	                                        "PQ4/PQ4.gog"};
	const auto m                         = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "dosbox-gog-pair-folders");
	ASSERT_EQ(m->games.size(), 3u);
	EXPECT_EQ(m->games[2].folder, "PQ4/DOSBOX");
	EXPECT_EQ(AllConfs(paths, *m)[2],
	          (std::vector<std::string>{"PQ4/DOSBOX/dosboxPQ4cd.conf",
	                                    "PQ4/DOSBOX/dosboxPQ4cd_single.conf"}));
}

// Steam's Space Quest Collection keeps each pair in sqN/DOSBOX, beside a
// DOSBox 0.63 with a conf of its own.
TEST(PackageProfileShipped, GogPairsTwoFoldersDown)
{
	std::vector<std::string> paths = {"dosbox-0.63/dosbox.conf",
	                                  "SierraLauncher.exe"};
	for (const auto* n : {"1", "2", "3", "4", "5", "6"}) {
		const auto dir = std::string("2016_SpaceQuestCollection/sq") +
		                 n + "/DOSBOX/";
		paths.push_back(dir + "dosboxSQ" + n + ".conf");
		paths.push_back(dir + "dosboxSQ" + n + "_single.conf");
	}
	const auto m = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "dosbox-gog-pair-folders");
	ASSERT_EQ(m->games.size(), 6u);
	EXPECT_EQ(m->games[0].folder, "2016_SpaceQuestCollection/sq1/DOSBOX");
}

// Steam's X-Wing: the classic and the CD release, each a GOG triple.
TEST(PackageProfileShipped, GogTriplesInTopFolders)
{
	const std::vector<std::string> paths = {
	        "Install.bat",
	        "classic/dosbox_xwing.conf",
	        "classic/dosbox_xwing_settings.conf",
	        "classic/dosbox_xwing_single.conf",
	        "collectors_cd/dosbox_xwingcd.conf",
	        "collectors_cd/dosbox_xwingcd_settings.conf",
	        "collectors_cd/dosbox_xwingcd_single.conf",
	        "collectors_cd/game.ins",
	        "remastered/"};
	const auto m = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "dosbox-gog-pair-folders");
	EXPECT_EQ(AllConfs(paths, *m),
	          (std::vector<std::vector<std::string>>{
	                  {        "classic/dosbox_xwing.conf","classic/dosbox_xwing_single.conf"                                  },
	                  {"collectors_cd/dosbox_xwingcd.conf",
	                   "collectors_cd/dosbox_xwingcd_single.conf"}
        }));
}

// Nightdive's F-19: the Linux pair, a Daum pair one level deeper unused.
TEST(PackageProfileShipped, TheNightdiveLinuxPair)
{
	const std::vector<std::string> paths = {
	        "MPS/F19/F19.EXE",
	        "dosbox_linux/dosbox_f19sf.conf",
	        "dosbox_linux/dosbox_f19sf_launch_linux.conf",
	        "dosbox_linux/daum/dosbox_f19sf.conf",
	        "dosbox_linux/daum/dosbox_f19sf_launch_linux.conf"};
	const auto m = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "steam-nightdive-linux");
	EXPECT_EQ(AllConfs(paths, *m),
	          (std::vector<std::vector<std::string>>{
	                  {"dosbox_linux/dosbox_f19sf.conf",
	                   "dosbox_linux/dosbox_f19sf_launch_linux.conf"}
        }));
}

TEST(PackageProfileShipped, DoomAndWolfensteinHaveTheirOwnProfiles)
{
	const std::vector<std::string> doom = {"DOOM.exe",
	                                       "dosdoom/base/DOOM.WAD",
	                                       "dosdoom/dosbox/doom.conf",
	                                       "dosdoom/dosbox/doom_single.conf"};
	const auto d                        = FindShipped(doom);
	ASSERT_TRUE(d);
	EXPECT_EQ(d->profile, "gog-bethesda-doom");
	EXPECT_EQ(AllConfs(doom, *d),
	          (std::vector<std::vector<std::string>>{
	                  {"dosdoom/dosbox/doom.conf",
	                   "dosdoom/dosbox/doom_single.conf"}
        }));

	const std::vector<std::string> wolf = {"DOSBox/wolf3d.conf",
	                                       "DOSBox/wolf3d_single.conf",
	                                       "base/WOLF3D.EXE"};
	const auto w                        = FindShipped(wolf);
	ASSERT_TRUE(w);
	EXPECT_EQ(w->profile, "steam-bethesda-wolf3d");
	EXPECT_EQ(AllConfs(wolf, *w),
	          (std::vector<std::vector<std::string>>{
	                  {"DOSBox/wolf3d.conf", "DOSBox/wolf3d_single.conf"}
        }));
}

// Forgotten Realms: base, graphics and game in the order its batch chains
// them; the setup conf and the launcher's resolution confs stay out.
TEST(PackageProfileShipped, ForgottenRealmsChainsThreeConfs)
{
	const std::vector<std::string> paths = {
	        "Launcher/launcher-confs/720p.conf",
	        "games/Eye of the Beholder ENG/base.conf",
	        "games/Eye of the Beholder ENG/game.conf",
	        "games/Eye of the Beholder ENG/graphics.conf",
	        "games/Eye of the Beholder ENG/setup.conf",
	        "games/Eye of the Beholder ENG/Default files/graphics.conf",
	        "games/Eye of the Beholder II - EN/base.conf",
	        "games/Eye of the Beholder II - EN/game.conf",
	        "games/Eye of the Beholder II - EN/graphics.conf"};
	const auto m = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "steam-sneg-forgotten-realms");
	ASSERT_EQ(m->games.size(), 2u);
	EXPECT_EQ(AllConfs(paths, *m)[0],
	          (std::vector<std::string>{"games/Eye of the Beholder ENG/base.conf",
	                                    "games/Eye of the Beholder ENG/graphics.conf",
	                                    "games/Eye of the Beholder ENG/game.conf"}));
}

// The Apogee platformers: the conf beside the game, at the root or one
// folder down, the bundled emulator's own dosbox.conf below it unused.
TEST(PackageProfileShipped, TheApogeePlatformers)
{
	const std::vector<std::string> crystal = {"Crystal Caves.bat",
	                                          "Crystal Caves/CC1.EXE",
	                                          "Crystal Caves/CRYSTAL.conf",
	                                          "Crystal Caves/Dosbox/dosbox.conf"};
	const auto c                           = FindShipped(crystal);
	ASSERT_TRUE(c);
	EXPECT_EQ(c->profile, "steam-apogee-daum");
	EXPECT_EQ(AllConfs(crystal, *c),
	          (std::vector<std::vector<std::string>>{
	                  {"Crystal Caves/CRYSTAL.conf"}}));

	const std::vector<std::string> duke = {"Dosbox/dosbox.conf",
	                                       "Duke Nukem 2.bat",
	                                       "NUKEM2.EXE",
	                                       "Nukem2.conf"};
	const auto d                        = FindShipped(duke);
	ASSERT_TRUE(d);
	EXPECT_EQ(d->profile, "steam-apogee-daum");
	EXPECT_EQ(AllConfs(duke, *d),
	          (std::vector<std::vector<std::string>>{{"Nukem2.conf"}}));
}

// California Games: the plain conf, not its 720 and 1080 twins.
TEST(PackageProfileShipped, TheSportsGamesTakeThePlainConf)
{
	const std::vector<std::string> paths = {"DOSBox.exe",
	                                        "c/CALGAMES/CALGAMES.EXE",
	                                        "calgames.conf",
	                                        "calgames1080.conf",
	                                        "calgames720.conf",
	                                        "game.exe"};
	const auto m                         = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "steam-pixelgames-uk");
	EXPECT_EQ(AllConfs(paths, *m),
	          (std::vector<std::vector<std::string>>{{"calgames.conf"}}));
}

// ZOOM Platform's Linux tarball: one game folder with its conf, the bundled
// emulator's empty conf below it unused.
TEST(PackageProfileShipped, TheZoomTarballConf)
{
	const std::vector<std::string> paths = {"Bio-Menace/BMENACE1.EXE",
	                                        "Bio-Menace/DOSBox/dosbox",
	                                        "Bio-Menace/DOSBox/dosbox-staging.conf",
	                                        "Bio-Menace/start.sh",
	                                        "Bio-Menace/zoom_biomenace.conf"};
	const auto m                         = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "zoom-platform");
	EXPECT_EQ(AllConfs(paths, *m),
	          (std::vector<std::vector<std::string>>{
	                  {"Bio-Menace/zoom_biomenace.conf"}}));
}

// The older ZOOM releases name the conf z_<game>.conf; an unpacked Windows
// installer keeps it in app/.
TEST(PackageProfileShipped, TheShortZoomConfInAnInstallerFolder)
{
	const std::vector<std::string> paths = {"app/DOSBOX/DOSBox.exe",
	                                        "app/Launch Monuments of Mars.bat",
	                                        "app/MARS1.EXE",
	                                        "app/z_mom.bat",
	                                        "app/z_mom.conf"};
	const auto m                         = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "zoom-platform");
	EXPECT_EQ(AllConfs(paths, *m),
	          (std::vector<std::vector<std::string>>{{"app/z_mom.conf"}}));
}

// A plain DOSBox distribution with dosbox.conf at the root (Ziggurat) is
// the built-in default's, tried before every shipped profile.
TEST(PackageProfileShipped, ARootDosboxConfStaysWithTheDefault)
{
	const std::vector<std::string> paths = {"dosbox.conf",
	                                        "dosbox.exe",
	                                        "game/WTSS.EXE"};
	const auto m                         = FindShipped(paths);
	ASSERT_TRUE(m);
	EXPECT_EQ(m->profile, "default");
}

// Names and paths ignore case, so two spellings of a folder are one game.
TEST(PackageProfile, ACaseTwinFolderIsOneGame)
{
	const auto m = FindPackage(
	        {Make("p", {"*"}, {"dosbox*.conf"}, {"dosbox*_single.conf"})},
	        Entries({"Game/dosboxGame.conf",
	                 "Game/dosboxGame_single.conf",
	                 "GAME/readme.txt"}));
	ASSERT_TRUE(m);
	EXPECT_EQ(m->games.size(), 1u);
}

// Each section is read on its own; the line in a note is still the file's.
TEST(PackageProfile, ANoteNamesTheLineOfTheFile)
{
	const auto file = ParseProfileFile("[[a]]\ndosconfig_file = ['a']\n\n[[b]]\n\nbroken\n",
	                                   "user.toml");
	ASSERT_EQ(file.notes.size(), 1u);
	EXPECT_NE(file.notes[0].find("line 6"), std::string::npos) << file.notes[0];
}

TEST(PackageProfile, AFileOfManySectionsIsReadInLinearTime)
{
	std::string text = {};
	for (int i = 0; i < 100000; ++i) {
		text += "[[a]]\n";
	}
	const auto start = std::chrono::steady_clock::now();
	const auto file  = ParseProfileFile(text, "user.toml");
	const auto spent = std::chrono::steady_clock::now() - start;
	EXPECT_LT(spent, std::chrono::seconds(10));
	EXPECT_FALSE(file.notes.empty());
}

TEST(PackageProfile, AWildcardPathOverManyFoldersIsFoundInLinearTime)
{
	constexpr int Games            = 30000;
	std::vector<std::string> paths = {};
	for (int i = 0; i < Games; ++i) {
		const auto folder = "G" + std::to_string(100000 + i);
		paths.push_back(folder + "/DOSBOX/dosbox.conf");
		paths.push_back(folder + "/GAME.EXE");
	}
	const auto entries = Entries(paths);
	const auto start   = std::chrono::steady_clock::now();
	const auto m = FindPackage({Make("p", {"*/DOSBOX"}, {"dosbox*.conf"})},
	                           entries);
	const auto spent = std::chrono::steady_clock::now() - start;
	EXPECT_LT(spent, std::chrono::seconds(10));
	ASSERT_TRUE(m);
	ASSERT_EQ(m->games.size(), static_cast<size_t>(Games));
	EXPECT_EQ(m->games.front().folder, "G100000/DOSBOX");
	EXPECT_EQ(m->games.back().folder, "G129999/DOSBOX");
}

// Games come in the folded order of their whole folder path, so "A-B" sorts
// before "A" ('-' is below '/').
TEST(PackageProfile, WildcardGamesFollowTheFoldedOrderOfTheirPaths)
{
	const auto m = FindPackage({Make("p", {"*/DOSBOX"}, {"dosbox.conf"})},
	                           Entries({"A/DOSBOX/dosbox.conf",
	                                    "a-b/DOSBOX/dosbox.conf",
	                                    "B/DOSBOX/dosbox.conf"}));
	ASSERT_TRUE(m);
	ASSERT_EQ(m->games.size(), 3u);
	EXPECT_EQ(m->games[0].folder, "a-b/DOSBOX");
	EXPECT_EQ(m->games[1].folder, "A/DOSBOX");
	EXPECT_EQ(m->games[2].folder, "B/DOSBOX");
}

TEST_F(PackageProfileLoad, AGlobalSectionThatDoesNotParseIsRepaired)
{
	Spit(user, "[[global]]\ndefault_profile = default\n\n[[mine]]\ndosconfig_file = ['m']\n");
	(void)Load(user, shipped);
	const auto repaired = Slurp(user);
	EXPECT_NE(repaired.find("default_profile = \"default\""), std::string::npos)
	        << repaired;
	EXPECT_NE(repaired.find("[[mine]]"), std::string::npos) << repaired;

	const auto again = Load(user, shipped);
	EXPECT_EQ(Slurp(user), repaired);
	EXPECT_FALSE(AnyNoteHas(again.notes, "written into the global section"));
}
