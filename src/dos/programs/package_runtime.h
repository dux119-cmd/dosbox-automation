// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_PACKAGE_RUNTIME_H
#define DOSBOX_PACKAGE_RUNTIME_H

#include "dos/programs/package_start.h"

#include <filesystem>
#include <string>
#include <vector>

class Config;

// A package conf read at a MOUNT after start (ada-nb2a): loaded onto the
// running config as a -conf would be, the -conf files and --set winning.
namespace PackageRuntime {

struct Result {
	// "section.key" of each setting the package changed now.
	std::vector<std::string> applied = {};
	// Settings that only apply at start; they keep their value.
	std::vector<std::string> start_only = {};
	// The package's [autoexec] lines as they run, or with mounts_only its
	// MOUNT lines.
	std::vector<std::string> lines = {};
	// Its MOUNT lines mount some drive, and C:.
	bool mounts_drive = false;
	bool mounts_c     = false;
};

Result Apply(Config& conf, const PackageStart::Package& package, bool mounts_only);

// An archive whose package is in force: the command line's for the whole
// run, one read at a MOUNT while its own lines run.
bool IsActive(const std::filesystem::path& archive);
void MarkActive(const std::filesystem::path& archive);
void Release(const std::filesystem::path& archive);

} // namespace PackageRuntime

#endif // DOSBOX_PACKAGE_RUNTIME_H
