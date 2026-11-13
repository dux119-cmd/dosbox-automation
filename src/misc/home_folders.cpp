// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "misc/home_folders.h"

#include "utils/checks.h"
#include "utils/env_utils.h"

#include <array>
#include <filesystem>

CHECK_NARROWING();

namespace HomeFolders {

namespace {

constexpr std::array<const char*, 3> XdgVariables = {"XDG_CONFIG_HOME",
                                                     "XDG_DATA_HOME",
                                                     "XDG_CACHE_HOME"};

} // namespace

std::vector<std::string> Unset(const Rule rule, const EnvLookup& lookup)
{
	if (!lookup("HOME").empty()) {
		return {};
	}
	if (rule == Rule::HomeOnly) {
		return {"HOME"};
	}
	std::vector<std::string> unset = {};
	for (const auto* name : XdgVariables) {
		// XDG Base Directory Specification 0.8 ignores relative paths.
		if (!std::filesystem::path(lookup(name)).is_absolute()) {
			unset.emplace_back(name);
		}
	}
	if (!unset.empty()) {
		unset.insert(unset.begin(), "HOME");
	}
	return unset;
}

std::string RefusalMessage(const Rule rule, const std::vector<std::string>& unset)
{
	std::string names = {};
	for (const auto& name : unset) {
		names += names.empty() ? name : ", " + name;
	}
	std::string text = "Cannot place the configuration, data and cache folders: " +
	                   names + " not set. Set HOME";
	if (rule == Rule::Xdg) {
		text += ", or set XDG_CONFIG_HOME, XDG_DATA_HOME and XDG_CACHE_HOME to absolute paths";
	}
	return text + ".";
}

std::optional<std::string> CheckHost()
{
#if defined(WIN32)
	return {};
#else
#if defined(MACOSX)
	constexpr auto HostRule = Rule::HomeOnly;
#else
	constexpr auto HostRule = Rule::Xdg;
#endif
	const auto unset = Unset(HostRule, [](const char* name) {
		return get_env_var(name);
	});
	if (unset.empty()) {
		return {};
	}
	return RefusalMessage(HostRule, unset);
#endif
}

} // namespace HomeFolders
