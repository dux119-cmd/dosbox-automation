// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_MISC_HOME_FOLDERS_H
#define DOSBOX_MISC_HOME_FOLDERS_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Whether the engine's config, data and cache folders can be placed. Without
// a home they would fall back to a literal '~' under the working directory.
namespace HomeFolders {

// Xdg: Linux and BSD, where each XDG_*_HOME can stand in for HOME.
// HomeOnly: macOS, whose folders all live under ~/Library.
enum class Rule { Xdg, HomeOnly };

using EnvLookup = std::function<std::string(const char*)>;

// The variables that are missing, HOME first; empty when the folders can be
// placed. An empty or relative XDG value counts as unset.
std::vector<std::string> Unset(Rule rule, const EnvLookup& lookup);

std::string RefusalMessage(Rule rule, const std::vector<std::string>& unset);

// The refusal for this host's environment; nothing on Windows or when the
// folders can be placed.
std::optional<std::string> CheckHost();

} // namespace HomeFolders

#endif
