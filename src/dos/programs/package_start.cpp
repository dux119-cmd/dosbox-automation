// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/package_start.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <string_view>
#include <system_error>

#include "augra/log.h"
#include "config/config.h"
#include "config/setup.h"
#include "dos/archive/archive_toml.h"
#include "dos/archive/member_path.h"
#include "dos/archive/package_autoexec.h"
#include "dos/programs/mount_archive_internal.h"
#include "dos/programs/package_runtime.h"
#include "misc/cross.h"
#include "misc/support.h"
#include "shell/autoexec_archive.h"
#include "shell/command_line.h"
#include "utils/checks.h"
#include "utils/fs_utils.h"

CHECK_NARROWING();

namespace PackageStart {

namespace {

namespace fs = std::filesystem;
using namespace ArchiveMount;

constexpr auto LogComponent = "package";

std::string FolderOf(const std::string& utf8_path)
{
	const auto slash = utf8_path.find_last_of('/');
	return slash == std::string::npos ? std::string()
	                                  : utf8_path.substr(0, slash);
}

bool SameIgnoringCase(const std::string_view a, const std::string_view b)
{
	return a.size() == b.size() &&
	       std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
		       return std::toupper(static_cast<unsigned char>(x)) ==
		              std::toupper(static_cast<unsigned char>(y));
	       });
}

// Where a conf's relative MOUNT paths count from: the profile's working
// folder beside the conf when the archive has it, as the archive spells it.
std::string BaseFolder(const std::string& conf_folder, const std::string& working_dir,
                       const std::vector<Entry>& entries)
{
	if (working_dir.empty()) {
		return conf_folder;
	}
	const auto wanted = conf_folder.empty() ? working_dir
	                                        : conf_folder + "/" + working_dir;
	for (const auto& entry : entries) {
		auto path = std::string_view(entry.utf8_path);
		while (path.ends_with('/')) {
			path.remove_suffix(1);
		}
		const auto head = path.substr(0, wanted.size());
		if (SameIgnoringCase(head, wanted) &&
		    (path.size() == wanted.size() ? entry.is_dir
		                                  : path[wanted.size()] == '/')) {
			return std::string(head);
		}
	}
	return conf_folder;
}

// Several games: the conf at the archive's root, as each profile names it.
std::optional<PackageProfile::Match> RootMatch(
        const std::vector<PackageProfile::Profile>& order,
        const std::vector<Entry>& entries)
{
	auto at_root = order;
	for (auto& profile : at_root) {
		profile.paths = {"."};
	}
	return PackageProfile::FindPackage(at_root, entries);
}

std::optional<PackageProfile::Match> MatchGame(
        const std::vector<PackageProfile::Profile>& order,
        const std::vector<Entry>& entries)
{
	auto match = PackageProfile::FindPackage(order, entries);
	if (match && match->games.size() > 1) {
		match = RootMatch(order, entries);
	}
	return match;
}

// A game folder as an entry list (ada-i8ec); a linked folder is listed but
// not entered, so a link loop ends.
std::optional<std::vector<Entry>> WalkFolder(const fs::path& root,
                                             const int64_t max_entries,
                                             std::string& why)
{
	std::vector<Entry> entries = {};
	std::error_code ec         = {};
	auto it                    = fs::recursive_directory_iterator(root,
                                                   fs::directory_options::none,
                                                   ec);
	for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		if (static_cast<int64_t>(entries.size()) >= max_entries) {
			why = "more than archive_max_entries (" +
			      std::to_string(max_entries) + ") entries";
			return {};
		}
		std::error_code item_ec = {};
		auto relative = PathToUtf8(it->path().lexically_relative(root));
		std::replace(relative.begin(), relative.end(), '\\', '/');
		Entry e     = {};
		e.ordinal   = static_cast<int>(entries.size());
		e.utf8_path = std::move(relative);
		e.is_dir    = it->is_directory(item_ec);
		if (!e.is_dir && it->is_regular_file(item_ec)) {
			e.size = static_cast<int64_t>(it->file_size(item_ec));
		}
		// An unreadable subfolder would end the whole walk
		if (e.is_dir) {
			std::error_code open_ec = {};
			fs::directory_iterator probe(it->path(), open_ec);
			if (open_ec) {
				augra::log_warn(LogComponent,
				                "%s",
				                PackageAutoexec::Printable(
				                        PathToUtf8(root) + ": folder " +
				                        e.utf8_path + " not read, " +
				                        open_ec.message())
				                        .c_str());
				it.disable_recursion_pending();
			}
		}
		entries.push_back(std::move(e));
	}
	if (ec) {
		why = ec.message();
		return {};
	}
	return entries;
}

void LogFound(const std::string& shown, const Package& package)
{
	augra::log_info(LogComponent,
	                "%s",
	                PackageAutoexec::Printable(shown + ": package conf " +
	                                           package.confs.front().name +
	                                           " (profile " + package.profile + ")")
	                        .c_str());
}

std::string_view Trimmed(std::string_view line)
{
	while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
		line.remove_prefix(1);
	}
	while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
	                         line.back() == '\r')) {
		line.remove_suffix(1);
	}
	return line;
}

// "C:" in any case; a leading @ only hides the echo.
bool IsPromptOfC(std::string_view line)
{
	line = Trimmed(line);
	if (line.starts_with('@')) {
		line = Trimmed(line.substr(1));
	}
	return line.size() == 2 && (line[0] == 'c' || line[0] == 'C') &&
	       line[1] == ':';
}

} // namespace

std::optional<Source> FindCommandLinePackage(CommandLine& cmdline)
{
	// The autoexec mounts the last folder argument as C:.
	std::optional<fs::path> folder = {};
	std::string argument           = {};
	unsigned int index             = 1;
	while (cmdline.FindCommand(index++, argument)) {
		if (argument.starts_with("-")) {
			continue;
		}
		const fs::path path = argument;
		std::error_code ec  = {};
		if (is_dir(path)) {
			folder = path;
			continue;
		}
		if (const auto in_cwd = fs::current_path(ec) / path;
		    !ec && is_dir(in_cwd)) {
			folder = in_cwd;
			continue;
		}
		auto extension = path.extension().string();
		for (auto& c : extension) {
			c = static_cast<char>(
			        std::toupper(static_cast<unsigned char>(c)));
		}
		if (extension == ".ISO" || extension == ".CUE") {
			continue;
		}
		const auto kind = AutoexecArchive::Classify(path);
		if (kind == AutoexecArchive::Argument::CdContainer) {
			continue;
		}
		if (kind == AutoexecArchive::Argument::Archive) {
			return Source{path, false};
		}
		return {};
	}
	if (folder) {
		return Source{*folder, true};
	}
	return {};
}

bool PackageConfEnabled()
{
	// As windowresolution and window_size: the name parsed later wins.
	auto* dos         = get_section("dos");
	const auto legacy = dos->GetString("archive_package_conf");
	if (!legacy.empty() &&
	    control->GetSettingParseOrder("dos", "archive_package_conf") >
	            control->GetSettingParseOrder("dos", "package_conf")) {
		set_section_property_value("dos", "package_conf", legacy);
	}
	set_section_property_value("dos", "archive_package_conf", "");
	return dos->GetBool("package_conf");
}

std::vector<PackageProfile::Profile> ProfileOrder()
{
	auto profiles = PackageProfile::Load(
	        get_config_dir() / PackageProfile::FileName,
	        get_resource_path("package-profiles", PackageProfile::FileName));
	for (const auto& note : profiles.notes) {
		augra::log_info(LogComponent, "%s", PackageAutoexec::Printable(note).c_str());
	}
	return std::move(profiles.order);
}

std::optional<Package> ReadPackage(const fs::path& given,
                                   const std::vector<PackageProfile::Profile>& order)
{
	using namespace MountArchiveInternal;
	// MOUNT's -pr resolves a relative host path elsewhere than the shell.
	std::error_code absolute_ec = {};
	const auto archive          = fs::absolute(given, absolute_ec);
	const auto shown            = PathToUtf8(archive);
	auto skip = [&](const std::string& why) -> std::optional<Package> {
		augra::log_warn(LogComponent,
		                "%s",
		                PackageAutoexec::Printable(
		                        shown + ": no package conf read: " + why)
		                        .c_str());
		return {};
	};
	if (absolute_ec) {
		return skip(absolute_ec.message());
	}

	const auto settings = ReadArchiveSettings();
	if (std::error_code dir_ec = {}; fs::is_directory(archive, dir_ec)) {
		std::string why = {};
		auto entries = WalkFolder(archive, settings.max_entries, why);
		if (!entries) {
			return skip(why);
		}
		Package package   = {};
		package.path      = shown;
		package.is_folder = true;
		package.entries   = std::move(*entries);
		const auto match  = MatchGame(order, package.entries);
		if (!match) {
			return {};
		}
		package.profile = match->profile;
		for (const auto index : match->games.front().confs) {
			const auto& member = package.entries.at(index);
			const auto too_large = member.utf8_path + ": larger than 1 MiB";
			if (member.size < 0 ||
			    static_cast<uint64_t>(member.size) > MaxConfBytes) {
				return skip(too_large);
			}
			const auto relative = PathFromUtf8(member.utf8_path);
			if (!relative) {
				return skip(member.utf8_path +
				            ": not a valid file name");
			}
			const auto file   = archive / *relative;
			std::string error = {};
			// ReadTextFile refuses a file over MaxConfBytes by itself.
			static_assert(ArchiveToml::MaxDocumentBytes <= MaxConfBytes);
			auto text = ArchiveToml::ReadTextFile(file, &error);
			if (!text) {
				return skip(error);
			}
			package.confs.push_back(
			        {package.path + "/" + member.utf8_path,
			         std::move(*text),
			         BaseFolder(FolderOf(member.utf8_path),
			                    match->working_dir,
			                    package.entries),
			         file});
		}
		// Relative path settings count from the conf's folder, as for
		// -conf.
		package.base_dir = package.confs.front().host_file.parent_path();
		LogFound(shown, package);
		return package;
	}

	std::string detail = {};
	auto* cache        = OpenStore(settings, &detail);
	if (!cache) {
		return skip(detail);
	}
	auto scratch = cache->MakeScratch();
	if (!scratch) {
		return skip("cannot create a scratch directory under the cache");
	}
	struct Discard {
		ArchiveCache::Scratch& scratch;
		~Discard()
		{
			ArchiveCache::DiscardScratch(scratch);
		}
	} discard{*scratch};

	Limits limits                        = {};
	limits.max_entries                   = settings.max_entries;
	limits.max_total_bytes               = settings.max_size_bytes;
	ArchiveHelperClient::Options options = {};
	options.scratch_dir                  = scratch->dir;

	auto opened = ArchiveHelperClient::Open(archive, limits, options);
	if (opened.error != Error::None) {
		return skip(opened.detail);
	}
	// MOUNT enters a lone inner archive by itself (nested archives design
	// 2026-10-06, N3); the conf is looked for where the drive will stand.
	auto entry = EnterForDrive(*opened.client,
	                           archive,
	                           {},
	                           *cache,
	                           ChainOptionsFor(settings, limits, options));
	if (entry.failed) {
		return skip(entry.detail);
	}
	ArchiveSource* source = opened.client.get();
	auto source_path      = archive;
	if (entry.entered) {
		source      = entry.entered->leaf.get();
		source_path = PathWithChain(archive, entry.entered->chain);
	}
	Package package = {};
	package.path    = PathToUtf8(source_path);
	package.entries = source->Entries();

	// A sequential archive serves a member only from the one unpacking pass
	// into the cache entry MOUNT then reuses.
	std::unique_ptr<ArchiveCache::Lease> lease = {};
	if (source->GetAccess() == Access::Sequential) {
		if (!settings.sequential_allowed) {
			return skip("a sequential archive and archive_sequential = refuse");
		}
		auto leased = cache->Acquire(*source, source_path);
		if (leased.status != CacheStatus::Ok) {
			return skip(std::string(CacheStatusText(leased.status)) +
			            ": " + leased.detail);
		}
		lease = std::move(leased.lease);
		if (!lease->IsComplete()) {
			augra::log_info(LogComponent,
			                "%s",
			                PackageAutoexec::Printable(
			                        shown + ": cannot be read in place: unpacking it "
			                                "into the archive cache")
			                        .c_str());
		}
	}
	auto read_member = [&](const Entry& member, std::string& text) {
		if (!lease) {
			std::vector<uint8_t> bytes = {};
			if (source->Materialize(member.ordinal, bytes, &detail) !=
			    Error::None) {
				return false;
			}
			text.assign(bytes.begin(), bytes.end());
			return true;
		}
		fs::path file = {};
		if (lease->ServeMember(member.ordinal, file, &detail) !=
		    CacheStatus::Ok) {
			return false;
		}
		std::ifstream in(file, std::ios::binary);
		text.assign(std::istreambuf_iterator<char>(in), {});
		return static_cast<bool>(in) || in.eof();
	};

	const auto match = MatchGame(order, package.entries);
	if (!match) {
		return {};
	}
	package.profile = match->profile;
	for (const auto index : match->games.front().confs) {
		const auto& member   = package.entries.at(index);
		const auto too_large = member.utf8_path + ": larger than 1 MiB";
		if (member.size < 0 ||
		    static_cast<uint64_t>(member.size) > MaxConfBytes) {
			return skip(too_large);
		}
		std::string text = {};
		if (!read_member(member, text)) {
			return skip(member.utf8_path + ": " + detail);
		}
		// The declared size is the archive's word; the bytes decide.
		if (text.size() > MaxConfBytes) {
			return skip(too_large);
		}
		package.confs.push_back({package.path + "/" + member.utf8_path,
		                         std::move(text),
		                         BaseFolder(FolderOf(member.utf8_path),
		                                    match->working_dir,
		                                    package.entries)});
	}
	package.base_dir = archive.parent_path();
	LogFound(shown, package);
	return package;
}

std::optional<std::string> PackageLine(const Package& package, const Conf& one,
                                       const std::string& line, const bool mounts_only)
{
	if (mounts_only && !PackageAutoexec::IsMountLine(line)) {
		return {};
	}
	std::string reason = {};
	auto kept          = package.is_folder
	                           ? PackageAutoexec::RewriteFolderLine(line,
                                                               package.path,
                                                               one.folder,
                                                               package.entries,
                                                               reason)
	                           : PackageAutoexec::RewriteLine(line,
                                                         package.path,
                                                         one.folder,
                                                         package.entries,
                                                         reason);
	if (!kept) {
		augra::log_warn(LogComponent,
		                "%s",
		                PackageAutoexec::Printable(one.name + ": autoexec line left out: " +
		                                           line + ": " + reason)
		                        .c_str());
	}
	return kept;
}

// The -conf files as ParseConfigFiles looks for them: as given, then in the
// config directory.
static std::vector<fs::path> DashConfFiles(const Config& conf)
{
	std::vector<fs::path> files = {};
	for (const auto& name : conf.arguments.conf) {
		for (const auto& candidate : {fs::path(name), get_config_dir() / name}) {
			std::error_code ec   = {};
			const auto canonical = fs::canonical(candidate, ec);
			if (!ec) {
				files.push_back(canonical);
				break;
			}
		}
	}
	return files;
}

void LoadPackage(Config& conf, const Package& package, const bool mounts_only)
{
	auto loaded_any       = false;
	std::string last_line = {};
	const auto dash_confs = DashConfFiles(conf);
	for (const auto& one : package.confs) {
		// The local conf loaded already; a -conf loads later with its own trust.
		if (!one.host_file.empty()) {
			std::error_code ec   = {};
			const auto canonical = fs::canonical(one.host_file, ec);
			const auto& loaded   = conf.loaded_config_paths_canonical;
			const auto named_by  = [&](const std::vector<fs::path>& files) {
				return std::find(files.begin(), files.end(), canonical) !=
				       files.end();
			};
			if (!ec && (named_by(loaded) || named_by(dash_confs))) {
				augra::log_info(LogComponent,
				                "%s",
				                PackageAutoexec::Printable(
				                        one.name + ": loaded as the local conf or a -conf")
				                        .c_str());
				continue;
			}
		}
		auto autoexec_line =
		        [&](const std::string& line) -> std::optional<std::string> {
			const auto letter = PackageAutoexec::MountLetter(line);
			auto kept = PackageLine(package, one, line, mounts_only);
			if (kept && letter) {
				conf.package_mounts_drive = true;
				conf.package_mounts_c |= (*letter == 'C');
			}
			if (kept && !Trimmed(*kept).empty()) {
				last_line = *kept;
			}
			return kept;
		};
		conf.ParseConfigText("package",
		                     one.name,
		                     one.text,
		                     package.base_dir,
		                     autoexec_line,
		                     IgnoredInPackages);
		loaded_any = true;
	}
	if (loaded_any) {
		conf.package_conf_loaded    = true;
		conf.package_is_folder      = package.is_folder;
		conf.package_ends_at_prompt = last_line.empty() ||
		                              IsPromptOfC(last_line);
	}
}

void LoadCommandLinePackage(Config& conf)
{
	const auto dos = get_section("dos");
	if (!conf.cmdline || !dos || !PackageConfEnabled()) {
		return;
	}
	const auto source = FindCommandLinePackage(*conf.cmdline);
	if (!source) {
		return;
	}
	if (const auto package = ReadPackage(source->path, ProfileOrder())) {
		LoadPackage(conf, *package, !conf.arguments.run.empty());
		// Its own MOUNT lines must not read it again.
		PackageRuntime::MarkActive(source->path);
	}
}

} // namespace PackageStart
