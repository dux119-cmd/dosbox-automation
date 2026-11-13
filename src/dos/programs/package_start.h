// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_PACKAGE_START_H
#define DOSBOX_PACKAGE_START_H

#include "config/host_only.h"
#include "dos/archive/archive_source.h"
#include "dos/archive/package_profile.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class CommandLine;
class Config;

// The archive or game folder on the command line and its package conf, read
// before the machine starts so the conf loads where a working folder's
// dosbox.conf would (ada-0lv8).
namespace PackageStart {

// A conf member above this is not read.
inline constexpr size_t MaxConfBytes = 1024 * 1024;

// A downloaded game must not open or move the REST API, nor reach it through
// the host's loopback.
inline const std::vector<std::string>& IgnoredInPackages = HostOnly::Settings;

struct Conf {
	// "<archive>/<member>": the conf's name in the log and the autoexec row.
	std::string name = {};
	std::string text = {};
	// The conf's own folder inside the archive (utf8, "" for the root).
	std::string folder = {};
	// In a game folder, the conf's file on the host; empty in an archive.
	std::filesystem::path host_file = {};
};

struct Package {
	std::string profile = {};
	// The archive as MOUNT names it, with the chain MOUNT enters by itself,
	// or the game folder.
	std::string path = {};
	bool is_folder   = false;
	// The base of relative path settings: the archive's folder on the host,
	// or in a game folder the conf's own folder, as for a -conf file.
	std::filesystem::path base_dir           = {};
	std::vector<ArchiveMount::Entry> entries = {};
	std::vector<Conf> confs                  = {};
};

struct Source {
	std::filesystem::path path = {};
	bool is_folder             = false;
};

// The game the command line names, as the autoexec reads it: the first
// archive, past directories and CD images, or else the folder it mounts as C:;
// none when another file comes first.
std::optional<Source> FindCommandLinePackage(CommandLine& cmdline);

// package_conf, with a value set under its old name archive_package_conf
// carried over first.
bool PackageConfEnabled();

// The built-in, shipped and user profiles in their order; notes are logged.
std::vector<PackageProfile::Profile> ProfileOrder();

// One game's confs from an archive or a game folder, or among several games
// the conf at the root; none without a conf or when it cannot be read (logged).
std::optional<Package> ReadPackage(const std::filesystem::path& archive_or_folder,
                                   const std::vector<PackageProfile::Profile>& order);

// One [autoexec] line of a package conf as it runs: relative MOUNT paths named
// against the archive or folder. None when it is left out (logged) or, with
// mounts_only, not a MOUNT line.
std::optional<std::string> PackageLine(const Package& package, const Conf& one,
                                       const std::string& line, bool mounts_only);

// Loads each conf as a -conf file with its relative MOUNT paths named against
// the archive or folder and its [webserver] ignored, and skips one already
// loaded as the local conf. With mounts_only (--run) only MOUNT lines run.
void LoadPackage(Config& conf, const Package& package, bool mounts_only = false);

// Finds the command line's archive or game folder, runs its profiles and loads
// its confs; it runs between the local dosbox.conf and the -conf files.
void LoadCommandLinePackage(Config& conf);

} // namespace PackageStart

#endif // DOSBOX_PACKAGE_START_H
