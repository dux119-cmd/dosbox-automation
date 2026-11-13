// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_TEST_HOME_H
#define DOSBOX_TEST_HOME_H

#include <filesystem>
#include <optional>
#include <string>

// The test binary never reaches the user's config, data and cache folders:
// a global test environment points them into a folder of its own before
// the first test runs, and removes that folder at the end.
namespace TestHome {

// The folder that stands in for the user's home; empty before set-up.
std::filesystem::path Root();

// True when the path lies inside the test home; tests that write or remove
// a file the engine resolved check this first.
bool Contains(const std::filesystem::path& path);

// HOME as the test binary found it, for tests that look for host tools.
std::optional<std::string> UserHome();

// Sets an environment variable for one scope and puts the previous value
// back, so a test that changes XDG_CACHE_HOME leaves the test home intact.
class ScopedEnv {
public:
	ScopedEnv(const char* name, const std::optional<std::string>& value);
	~ScopedEnv();
	ScopedEnv(const ScopedEnv&)            = delete;
	ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
	std::string name                    = {};
	std::optional<std::string> previous = {};
};

} // namespace TestHome

#endif // DOSBOX_TEST_HOME_H
