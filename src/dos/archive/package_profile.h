// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_PACKAGE_PROFILE_H
#define DOSBOX_PACKAGE_PROFILE_H

#include "dos/archive/archive_source.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Where a game package keeps its DOSBox conf, as profiles in a file
// (ada-hnq9).
namespace PackageProfile {

constexpr std::string_view FileName    = "package-profiles.toml";
constexpr std::string_view DefaultName = "default";
constexpr std::string_view GlobalName  = "global";

struct Profile {
	std::string name                 = {};
	bool enabled                     = true;
	std::vector<std::string> paths   = {"."};
	std::vector<std::string> files   = {};
	std::vector<std::string> chained = {};
	// The folder beside the conf DOSBox runs from; relative MOUNT paths
	// count from it when the archive has it.
	std::string working_dir = {};
};

// One profiles file as read: its valid profiles in file order, and a note
// for every profile or section that was skipped.
struct ProfileFile {
	bool has_global                            = false;
	std::optional<std::string> default_profile = {};
	std::vector<Profile> profiles              = {};
	std::vector<std::string> notes             = {};
};

struct WorkingList {
	std::vector<Profile> order     = {};
	std::vector<std::string> notes = {};
	// The user file's default_profile names no enabled profile.
	bool user_default_invalid = false;
};

// One game: its folder (utf8, "" for the mount root) and the indices into
// the entry list of its confs in reading order, the conf first.
struct Found {
	std::string folder        = {};
	std::vector<size_t> confs = {};
};

struct Match {
	std::string profile      = {};
	std::vector<Found> games = {};
	std::string working_dir  = {};
};

const Profile& BuiltInDefault();

// Letters, digits, underscore and hyphen.
bool IsValidName(std::string_view name);
// A file name pattern without any path part.
bool IsPlainFileName(std::string_view name);
// Relative, no `..` after the first segments, and the segment after them
// carries at least one character that is not a wildcard.
bool PathAllowed(std::string_view path);
// `*` and `?` over one path component, ASCII case folded.
bool PatternMatches(std::string_view pattern, std::string_view name);

// origin names the file in the notes.
ProfileFile ParseProfileFile(std::string_view text, std::string_view origin);

// The order: the default profile on top (the user file's, else the
// shipped file's, else the built-in), the enabled profiles of both files
// with each name once, and the built-in default last unless on top.
WorkingList BuildWorkingList(const ProfileFile& user, const ProfileFile& shipped);

// The first profile in order with a full match among the mount's entries;
// paths reaching outside the mount (`..`) are not answered here.
std::optional<Match> FindPackage(const std::vector<Profile>& order,
                                 const std::vector<ArchiveMount::Entry>& entries);

// What the engine writes when the user has no profiles file.
std::string DemoFileText();

// The user's text with a global section naming the built-in default:
// prepended when the text has none, or, with rewrite_existing, written over
// the existing section's settings. Everything else in the text is kept.
std::string WithDefaultGlobal(std::string_view text, bool rewrite_existing);

// Both files read and the list built: a missing user file is created from
// the demo, a user file without a valid global section gets the default
// one written in. Everything skipped or written is in the notes.
WorkingList Load(const std::filesystem::path& user_file,
                 const std::filesystem::path& shipped_file);

} // namespace PackageProfile

#endif // DOSBOX_PACKAGE_PROFILE_H
