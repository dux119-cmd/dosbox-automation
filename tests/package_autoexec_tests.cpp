// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/package_autoexec.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace PackageAutoexec;

namespace {

// A trailing '/' marks a folder; the DOS path is the name in upper case.
std::vector<ArchiveMount::Entry> Entries(const std::vector<std::string>& paths)
{
	std::vector<ArchiveMount::Entry> out = {};
	for (const auto& path : paths) {
		ArchiveMount::Entry e = {};
		e.ordinal             = static_cast<int>(out.size());
		e.is_dir              = path.ends_with('/');
		e.utf8_path = e.is_dir ? path.substr(0, path.size() - 1) : path;
		e.dos_path  = e.utf8_path;
		std::replace(e.dos_path.begin(), e.dos_path.end(), '/', '\\');
		std::transform(e.dos_path.begin(),
		               e.dos_path.end(),
		               e.dos_path.begin(),
		               [](const unsigned char c) {
			               return static_cast<char>(std::toupper(c));
		               });
		out.push_back(e);
	}
	return out;
}

// An entry whose DOS path is not its long name, as the reader assigns it.
ArchiveMount::Entry Aliased(const std::string& utf8, const std::string& dos,
                            const bool is_dir)
{
	ArchiveMount::Entry e = {};
	e.utf8_path           = utf8;
	e.dos_path            = dos;
	e.is_dir              = is_dir;
	return e;
}

bool AnyHas(const std::vector<std::string>& lines, const std::string& text)
{
	return std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
		return line.find(text) != std::string::npos;
	});
}

using Paths = std::vector<std::string>;

struct Rewritten {
	Paths lines   = {};
	Paths refused = {};
};

// RewriteLine over a whole [autoexec], as the package start reads it.
Rewritten RewriteEach(const Paths& autoexec, const std::string_view archive,
                      const std::string_view folder,
                      const std::vector<ArchiveMount::Entry>& entries)
{
	Rewritten out = {};
	for (const auto& line : autoexec) {
		std::string reason = {};
		if (auto kept = RewriteLine(line, archive, folder, entries, reason)) {
			out.lines.push_back(std::move(*kept));
		} else {
			out.refused.push_back(line + ": " + reason);
		}
	}
	return out;
}

} // namespace

TEST(PackageAutoexec, ResolveInsideFollowsTheConfsFolder)
{
	EXPECT_EQ(ResolveInside("DOSBOX", "\"..\""), "");
	EXPECT_EQ(ResolveInside("dosbox_linux/daum", "\"../..\""), "");
	EXPECT_EQ(ResolveInside("", ".\\C"), "C");
	EXPECT_EQ(ResolveInside("games/EOB", ".\\GAME"), "games/EOB/GAME");
	EXPECT_EQ(ResolveInside("", "data"), "data");
	EXPECT_EQ(ResolveInside("", "\".\""), "");
	EXPECT_EQ(ResolveInside("DOSBOX", "\"..\\GAME.INST\""), "GAME.INST");
	EXPECT_EQ(ResolveInside("DOSBox", "..\\base"), "base");
	EXPECT_EQ(ResolveInside("Star Control 1", "\"..\\Star Control 1\""),
	          "Star Control 1");
	EXPECT_EQ(ResolveInside("", "Resources/Crystal"), "Resources/Crystal");
}

TEST(PackageAutoexec, ResolveInsideRefusesWhatLeavesTheArchive)
{
	EXPECT_FALSE(ResolveInside("", ".."));
	EXPECT_FALSE(ResolveInside("", "\"..\\x\""));
	EXPECT_FALSE(ResolveInside("DOSBOX", "..\\..\\x"));
	EXPECT_FALSE(ResolveInside("a", "b/../../../x"));
	EXPECT_FALSE(ResolveInside("", "/etc/passwd"));
	EXPECT_FALSE(ResolveInside("", "\\GAMES"));
	EXPECT_FALSE(ResolveInside("", "C:\\GAMES"));
	EXPECT_FALSE(ResolveInside("", "c:game"));
	EXPECT_FALSE(ResolveInside("", "~/x"));
	EXPECT_FALSE(ResolveInside("", "\"\""));
	EXPECT_FALSE(ResolveInside("", std::string("a\0b", 3)));
}

constexpr std::string_view Zip = "/games/lol.zip";

// GOG Lands of Lore: every line stays, the mounts name the archive.
TEST(PackageAutoexec, AGogAutoexecKeepsEveryLineAndMountsTheArchive)
{
	const auto e = Entries(
	        {"DOSBOX/", "DOSBOX/dosboxLOL1.conf", "GAME.DAT", "LOLCD.EXE"});
	const auto r = RewriteEach({"@echo off",
	                            "mount C \"..\"",
	                            "mount C \"..\\cloud_saves\" -t overlay",
	                            "imgmount d \"..\\GAME.DAT\" -t iso -fs iso",
	                            "c:",
	                            "cls",
	                            "LOLCD.EXE",
	                            "exit"},
	                           Zip,
	                           "DOSBOX",
	                           e);
	EXPECT_EQ(r.lines,
	          (Paths{"@echo off",
	                 "mount C \"/games/lol.zip\"",
	                 "mount C \"/games/lol.zip/cloud_saves\" -t overlay",
	                 "imgmount d \"/games/lol.zip/GAME.DAT\" -t iso -fs iso",
	                 "c:",
	                 "cls",
	                 "LOLCD.EXE",
	                 "exit"}));
	EXPECT_TRUE(r.refused.empty());
}

// A folder inside the archive is MOUNT's -root, in DOS form.
TEST(PackageAutoexec, AFolderBecomesTheRootOfTheArchiveMount)
{
	const auto e = Entries({"c_drive/ARCTIC/AA1.EXE", "games/EOB/START.EXE"});
	const auto r = RewriteEach(
	        {"mount c c_drive", "mount d \".\\games\\EOB\""}, Zip, "", e);
	EXPECT_EQ(r.lines,
	          (Paths{"mount c \"/games/lol.zip\" -root \"C_DRIVE\"",
	                 "mount d \"/games/lol.zip\" -root \"GAMES\\EOB\""}));
}

// MOUNT finds -root among the DOS names, so a long folder name goes by its
// 8.3 alias: ZOOM's Bio-Menace, Steam's Star Control.
TEST(PackageAutoexec, ALongFolderNameBecomesItsDosAlias)
{
	const std::vector<ArchiveMount::Entry> e = {
	        Aliased("Bio-Menace", "BIO-ME~1", true),
	        Aliased("Bio-Menace/BMENACE1.EXE", "BIO-ME~1\\BMENACE1.EXE", false),
	        Aliased("Star Control 1/Data/SC1.EXE", "STARCO~1\\DATA\\SC1.EXE", false)};
	const auto r = RewriteEach({"mount c .", "mount e \"..\\Star Control 1\\Data\""},
	                           Zip,
	                           "Bio-Menace",
	                           e);
	EXPECT_EQ(r.lines,
	          (Paths{"mount c \"/games/lol.zip\" -root \"BIO-ME~1\"",
	                 "mount e \"/games/lol.zip\" -root \"STARCO~1\\DATA\""}));
	EXPECT_TRUE(r.refused.empty());
}

// Images keep their letter and every switch as written.
TEST(PackageAutoexec, ImagesKeepTheirSwitches)
{
	const auto e = Entries({"game/cd/cen_intl.cue",
	                        "game/cd/cen_intl.bin",
	                        "DISK1.IMG",
	                        "DISK2.IMG",
	                        "GAME.ISO",
	                        "XCOM/XCOM.EXE"});
	const auto r = RewriteEach({"imgmount e \"game\\cd\\cen_intl.cue\" -t cdrom",
	                            "imgmount a DISK1.IMG DISK2.IMG -t floppy",
	                            "imgmount d GAME.ISO -ide -t cdrom",
	                            "mount c XCOM -freesize 1024"},
	                           Zip,
	                           "",
	                           e);
	EXPECT_EQ(r.lines,
	          (Paths{"imgmount e \"/games/lol.zip/game/cd/cen_intl.cue\" -t cdrom",
	                 "imgmount a \"/games/lol.zip/DISK1.IMG\" \"/games/lol.zip/DISK2.IMG\" -t floppy",
	                 "imgmount d \"/games/lol.zip/GAME.ISO\" -ide -t cdrom",
	                 "mount c \"/games/lol.zip\" -root \"XCOM\" -freesize 1024"}));
}

// Members are found without regard to case and named as the archive spells
// them. A missing name stays as the conf wrote it, for MOUNT to report. Two
// members that differ only in case are ambiguous and refused.
TEST(PackageAutoexec, MembersAreMatchedIgnoringCase)
{
	const auto e = Entries({"Base/X.EXE", "game.ins", "Twin/A", "TWIN/B"});
	const auto r = RewriteEach({"mount c BASE",
	                            "imgmount d GAME.INS -t iso",
	                            "imgmount f NOTHERE.ISO -t iso",
	                            "mount g twin"},
	                           Zip,
	                           "",
	                           e);
	EXPECT_EQ(r.lines,
	          (Paths{"mount c \"/games/lol.zip\" -root \"BASE\"",
	                 "imgmount d \"/games/lol.zip/game.ins\" -t iso",
	                 "imgmount f \"/games/lol.zip/NOTHERE.ISO\" -t iso"}));
	ASSERT_EQ(r.refused.size(), 1u);
	EXPECT_TRUE(AnyHas(r.refused, "differ only in case"));
}

// An archive may name a member with a '"', which a DOS command line cannot
// quote; the conf reaches it through ".." without spelling it.
TEST(PackageAutoexec, AMemberNameWithAQuoteIsRefused)
{
	const auto e = Entries(
	        {"a\"b/DOSBOX/dosbox.conf", "a\"b/GAME.EXE", "a\"b/CD.ISO"});
	const auto r = RewriteEach({"mount c ..", "imgmount d ..\\CD.ISO -t iso", "GAME"},
	                           Zip,
	                           "a\"b/DOSBOX",
	                           e);
	EXPECT_EQ(r.lines, Paths{"GAME"});
	EXPECT_EQ(r.refused.size(), 2u);
}

// A relative path never leaves the archive; host paths are MOUNT's policy.
TEST(PackageAutoexec, ARelativePathAboveTheRootIsLeftOut)
{
	const auto e = Entries({"GAME/A.EXE"});
	const auto r = RewriteEach({"mount c /home/x/games",
	                            "mount d C:\\CD -t cdrom",
	                            "mount e ~/cd",
	                            "imgmount f ..\\..\\outside.iso -t iso",
	                            "mount g",
	                            "mount -u c"},
	                           Zip,
	                           "DOSBOX",
	                           e);
	EXPECT_EQ(r.lines,
	          (Paths{"mount c /home/x/games",
	                 "mount d C:\\CD -t cdrom",
	                 "mount e ~/cd",
	                 "mount g",
	                 "mount -u c"}));
	ASSERT_EQ(r.refused.size(), 1u);
	EXPECT_TRUE(AnyHas(r.refused, "outside.iso"));
}

// Nothing but the MOUNT paths changes: the package's lines run as a -conf
// file's autoexec would run them.
TEST(PackageAutoexec, EveryOtherLineIsKeptAsWritten)
{
	const auto e      = Entries({});
	const Paths lines = {"",
	                     "config -securemode",
	                     "IPXNET STARTSERVER",
	                     "serial1 nullmodem server:x",
	                     "imgmake hdd.img -t hd",
	                     "cycles=3000",
	                     "set BLASTER=A220 I7 D1",
	                     "BEEP\x07",
	                     std::string(500, 'A'),
	                     ":label",
	                     "exit"};
	const auto r      = RewriteEach(lines, Zip, "", e);
	EXPECT_EQ(r.lines, lines);
	EXPECT_TRUE(r.refused.empty());
}

// MOUNT is a Z: program; a path or an extension in front of its name, a
// colon after the letter and an '@' do not hide it.
TEST(PackageAutoexec, TheMountCommandIsKnownByAnySpelling)
{
	const auto e = Entries({"GAME/A.EXE", "C/CALGAMES/X.EXE"});
	const auto r = RewriteEach({"Z:\\MOUNT.COM c GAME",
	                            "mount.com d GAME",
	                            "MOUNT C .\\C",
	                            "@mount c: game"},
	                           Zip,
	                           "",
	                           e);
	EXPECT_EQ(r.lines,
	          (Paths{"Z:\\MOUNT.COM c \"/games/lol.zip\" -root \"GAME\"",
	                 "mount.com d \"/games/lol.zip\" -root \"GAME\"",
	                 "MOUNT C \"/games/lol.zip\" -root \"C\"",
	                 "@mount c: \"/games/lol.zip\" -root \"GAME\""}));
}

// Autoexec lines go through the batch expansion, where '%' starts a variable.
TEST(PackageAutoexec, APercentInTheArchivePathIsDoubled)
{
	const auto e = Entries({"GAME.ISO"});
	const auto r = RewriteEach({"imgmount d GAME.ISO -t iso"},
	                           "/games/100%/x.zip",
	                           "",
	                           e);
	EXPECT_EQ(r.lines,
	          Paths{"imgmount d \"/games/100%%/x.zip/GAME.ISO\" -t iso"});
}

// A DOS command line cannot carry a '"' inside a quoted path.
TEST(PackageAutoexec, AnArchivePathWithAQuoteIsRefused)
{
	const auto e = Entries({"GAME.ISO"});
	const auto r = RewriteEach({"imgmount d GAME.ISO -t iso", "mount e /cd"},
	                           "/games/a\"b.zip",
	                           "",
	                           e);
	EXPECT_EQ(r.lines, Paths{"mount e /cd"});
	ASSERT_EQ(r.refused.size(), 1u);
	EXPECT_TRUE(AnyHas(r.refused, "'\"'"));
}

// One line at a time, for a conf read before the machine starts.
TEST(PackageAutoexec, RewriteLineTakesOneLine)
{
	const auto e    = Entries({"GAME.ISO"});
	std::string why = {};
	EXPECT_EQ(RewriteLine("imgmount d GAME.ISO -t iso", Zip, "", e, why),
	          "imgmount d \"/games/lol.zip/GAME.ISO\" -t iso");
	EXPECT_EQ(RewriteLine("GAME", Zip, "", e, why), "GAME");
	EXPECT_TRUE(why.empty());
	EXPECT_FALSE(RewriteLine("mount c ..", Zip, "", e, why));
	EXPECT_NE(why.find("above the archive's root"), std::string::npos);
}

TEST(PackageAutoexec, MountLetterNamesTheDriveAMountLineMounts)
{
	EXPECT_EQ(MountLetter("mount C \"..\""), 'C');
	EXPECT_EQ(MountLetter("@imgmount d -t iso GAME.ISO"), 'D');
	EXPECT_EQ(MountLetter("Z:\\MOUNT.COM c: game"), 'C');
	EXPECT_FALSE(MountLetter("mount -u c"));
	EXPECT_FALSE(MountLetter("mount"));
	EXPECT_FALSE(MountLetter("c:"));
	EXPECT_FALSE(MountLetter("GAME C"));
}

// MOUNT's own options that take a value keep it; it is no path.
TEST(PackageAutoexec, OptionValuesAreNoPaths)
{
	const auto e = Entries({"GAME.EXE"});
	const auto r = RewriteEach({"mount c . -run GAME.EXE",
	                            "mount d . -sidecar saves -ide 1"},
	                           Zip,
	                           "",
	                           e);
	EXPECT_EQ(r.lines,
	          (Paths{"mount c \"/games/lol.zip\" -run GAME.EXE",
	                 "mount d \"/games/lol.zip\" -sidecar saves -ide 1"}));
}

// The shell's redirection ends MOUNT's arguments; the rest stays as written.
TEST(PackageAutoexec, ARedirectionStaysAsWritten)
{
	const auto e = Entries({"GAME.EXE"});
	const auto r = RewriteEach({"mount c . > nul", "mount c ..>nul", "mount c . | more"},
	                           Zip,
	                           "DOSBOX",
	                           e);
	EXPECT_EQ(r.lines,
	          (Paths{"mount c \"/games/lol.zip/DOSBOX\" > nul",
	                 "mount c \"/games/lol.zip\" >nul",
	                 "mount c \"/games/lol.zip/DOSBOX\" | more"}));
}

TEST(PackageAutoexec, APercentInTheRootIsDoubled)
{
	const auto e = Entries({"100%GAME/X.EXE"});
	const auto r = RewriteEach({"mount c 100%GAME"}, Zip, "", e);
	EXPECT_EQ(r.lines, Paths{"mount c \"/games/lol.zip\" -root \"100%%GAME\""});
}

// The conf's own folder may hold a '"' too.
TEST(PackageAutoexec, AQuoteInTheConfsFolderIsRefused)
{
	const auto e = Entries({"a\"b/DOSBOX/dosbox.conf"});
	const auto r = RewriteEach({"imgmount d NOTHERE.ISO -t iso"},
	                           Zip,
	                           "a\"b/DOSBOX",
	                           e);
	EXPECT_TRUE(r.lines.empty());
	EXPECT_EQ(r.refused.size(), 1u);
}

// Under --run every MOUNT line counts, a drive number included.
TEST(PackageAutoexec, IsMountLineKnowsEveryMountLine)
{
	EXPECT_TRUE(IsMountLine("mount c .."));
	EXPECT_TRUE(IsMountLine("imgmount 2 hdd.img -size 512,63,16,142"));
	EXPECT_TRUE(IsMountLine("@Z:\\MOUNT.COM -u c"));
	EXPECT_FALSE(IsMountLine("c:"));
	EXPECT_FALSE(IsMountLine("GAME"));
}

// A game folder on the command line (ada-i8ec): relative paths resolve from
// the conf's folder as in an archive and become the real host path of what
// they name, matched without regard to case.
TEST(PackageAutoexec, AFolderPackageMountsTheHostPathItNames)
{
	const auto entries = Entries({"__support/",
	                              "__support/app/",
	                              "__support/app/dosboxDOOM.conf",
	                              "DOOM.EXE",
	                              "game.inst"});
	std::string reason = {};
	EXPECT_EQ(RewriteFolderLine("mount C \"..\\..\"",
	                            "/g/Doom",
	                            "__support/app",
	                            entries,
	                            reason),
	          "mount C \"/g/Doom\"");
	EXPECT_EQ(RewriteFolderLine("imgmount d \"..\\..\\GAME.INST\" -t iso",
	                            "/g/Doom",
	                            "__support/app",
	                            entries,
	                            reason),
	          "imgmount d \"/g/Doom/game.inst\" -t iso");
	EXPECT_EQ(RewriteFolderLine("mount D ..\\..\\__SUPPORT",
	                            "/g/Doom",
	                            "__support/app",
	                            entries,
	                            reason),
	          "mount D \"/g/Doom/__support\"");
}

TEST(PackageAutoexec, AFolderPackageKeepsAbsolutePathsAndOtherLines)
{
	const auto entries = Entries({"dosbox.conf", "GAME.EXE"});
	std::string reason = {};
	EXPECT_EQ(RewriteFolderLine("mount c /home/x/other", "/g/Game", "", entries, reason),
	          "mount c /home/x/other");
	EXPECT_EQ(RewriteFolderLine("GAME.EXE", "/g/Game", "", entries, reason),
	          "GAME.EXE");
	EXPECT_EQ(RewriteFolderLine("mount -u c", "/g/Game", "", entries, reason),
	          "mount -u c");
}

TEST(PackageAutoexec, AFolderPackageRefusesWhatLeavesTheFolder)
{
	const auto entries = Entries({"dosbox.conf"});
	std::string reason = {};
	EXPECT_FALSE(RewriteFolderLine("mount c ..", "/g/Game", "", entries, reason));
	EXPECT_EQ(reason, "a relative path above the game folder");
	reason = {};
	EXPECT_FALSE(RewriteFolderLine("mount c .", "/g/Ga\"me", "", entries, reason));
	EXPECT_EQ(reason, "the game folder's path holds a '\"'");
}

TEST(PackageAutoexec, AFolderPackageDoublesPercentForTheBatchExpansion)
{
	const auto entries = Entries({"100%/", "100%/dosbox.conf"});
	std::string reason = {};
	EXPECT_EQ(RewriteFolderLine("mount c .", "/g/Game", "100%", entries, reason),
	          "mount c \"/g/Game/100%%\"");
}
