// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/package_runtime.h"

#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <system_error>

#include "augra/log.h"
#include "config/config.h"
#include "config/setup.h"
#include "dos/archive/member_path.h"
#include "dos/archive/package_autoexec.h"
#include "misc/cross.h"
#include "utils/checks.h"
#include "utils/string_utils.h"

CHECK_NARROWING();

namespace PackageRuntime {

namespace {

namespace fs = std::filesystem;

constexpr auto LogComponent = "package";

std::set<std::string> active = {};

std::string Key(const fs::path& archive)
{
	std::error_code ec = {};
	auto path          = fs::absolute(archive, ec);
	if (ec) {
		path = archive;
	}
	auto canonical = fs::weakly_canonical(path, ec);
	if (ec) {
		canonical = path.lexically_normal();
	}
	return ArchiveMount::PathToUtf8(canonical);
}

std::string Name(const SectionProp& section, const Property& property)
{
	return std::string(section.GetName()) + "." + property.propname;
}

struct Before {
	SectionProp* section = nullptr;
	Property* property   = nullptr;
	std::string value    = {};
};

std::vector<Before> Snapshot(Config& conf)
{
	std::vector<Before> settings = {};
	for (auto* section : conf) {
		auto* props = dynamic_cast<SectionProp*>(section);
		if (!props) {
			continue;
		}
		for (auto* property : *props) {
			settings.push_back(
			        {props, property, property->GetValue().ToString()});
		}
	}
	return settings;
}

// The -conf files again, found as ParseConfigFiles finds them; ignored holds
// every setting they may not change.
void ReapplyDashConfs(Config& conf, const std::vector<std::string>& ignored)
{
	const auto drop = [](const std::string&) -> std::optional<std::string> {
		return {};
	};
	for (const auto& given : conf.arguments.conf) {
		std::error_code ec = {};
		auto file          = fs::canonical(given, ec);
		if (ec) {
			file = fs::canonical(get_config_dir() / given, ec);
		}
		if (ec) {
			continue;
		}
		std::ifstream in(file, std::ios::binary);
		const std::string text(std::istreambuf_iterator<char>(in), {});
		conf.ParseConfigText("custom", given, text, file.parent_path(), drop, ignored);
	}
}

} // namespace

Result Apply(Config& conf, const PackageStart::Package& package, const bool mounts_only)
{
	Result result      = {};
	const auto before  = Snapshot(conf);
	auto ignored_first = PackageStart::IgnoredInPackages;
	for (const auto& one : before) {
		if (one.property->GetChange() == Property::Changeable::OnlyAtStart) {
			ignored_first.push_back(Name(*one.section, *one.property));
		}
	}

	for (const auto& one : package.confs) {
		const auto collect =
		        [&](const std::string& line) -> std::optional<std::string> {
			if (auto kept = PackageStart::PackageLine(
			            package, one, line, mounts_only)) {
				if (const auto letter = PackageAutoexec::MountLetter(
				            line)) {
					result.mounts_drive = true;
					result.mounts_c |= (*letter == 'C');
				}
				result.lines.push_back(std::move(*kept));
			}
			return {};
		};
		for (auto& name : conf.ParseConfigText("package",
		                                       one.name,
		                                       one.text,
		                                       package.base_dir,
		                                       collect,
		                                       ignored_first)) {
			if (std::find(PackageStart::IgnoredInPackages.begin(),
			              PackageStart::IgnoredInPackages.end(),
			              name) == PackageStart::IgnoredInPackages.end() &&
			    std::find(result.start_only.begin(),
			              result.start_only.end(),
			              name) == result.start_only.end()) {
				result.start_only.push_back(std::move(name));
			}
		}
	}

	// As at start, -conf and --set win over the package, for what it set.
	std::set<std::string> changed = {};
	for (const auto& one : before) {
		if (one.property->GetValue().ToString() != one.value) {
			changed.insert(Name(*one.section, *one.property));
		}
	}
	if (!changed.empty()) {
		std::vector<std::string> ignored_again = {"webserver"};
		for (const auto& one : before) {
			if (!changed.contains(Name(*one.section, *one.property))) {
				ignored_again.push_back(
				        Name(*one.section, *one.property));
			}
		}
		ReapplyDashConfs(conf, ignored_again);
		conf.ApplySetCommands(conf.arguments.set,
		                      false,
		                      [&](const std::string& section,
		                          const std::string& key) {
			                      auto name = section + "." + key;
			                      lowcase(name);
			                      return changed.contains(name);
		                      });
	}

	for (const auto& one : before) {
		if (one.property->GetValue().ToString() == one.value) {
			continue;
		}
		one.section->ExecuteUpdate(*one.property);
		result.applied.push_back(Name(*one.section, *one.property));
	}
	for (const auto& name : result.start_only) {
		augra::log_info(LogComponent,
		                "%s",
		                PackageAutoexec::Printable(package.path + ": " + name +
		                                           " only applies at start")
		                        .c_str());
	}
	return result;
}

bool IsActive(const fs::path& archive)
{
	return active.contains(Key(archive));
}

void MarkActive(const fs::path& archive)
{
	active.insert(Key(archive));
}

void Release(const fs::path& archive)
{
	active.erase(Key(archive));
}

} // namespace PackageRuntime
