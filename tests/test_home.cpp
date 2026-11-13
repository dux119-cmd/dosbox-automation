// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "test_home.h"

#include "test_temp_dir.h"

#include "dosbox_config.h"
#include "misc/cross.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <system_error>

namespace {

std::filesystem::path root           = {};
std::optional<std::string> user_home = {};
bool user_home_saved                 = false;

std::optional<std::string> GetEnv(const char* name)
{
	const char* value = std::getenv(name);
	if (value == nullptr) {
		return std::nullopt;
	}
	return std::string(value);
}

void SetEnv(const char* name, const std::optional<std::string>& value)
{
#if defined(WIN32)
	// An empty value removes the variable on Windows.
	_putenv_s(name, value ? value->c_str() : "");
#else
	if (value) {
		setenv(name, value->c_str(), 1);
	} else {
		unsetenv(name);
	}
#endif
}

class TestHomeEnvironment : public ::testing::Environment {
public:
	void SetUp() override
	{
		user_home       = GetEnv("HOME");
		user_home_saved = true;

		root = TestTempDir::MakeUnique("dosbox-tests-home-");
		ASSERT_FALSE(root.empty()) << "cannot create the test home under "
		                           << TestTempDir::Base();

		// The base folders exist, as in a real home; the engine creates
		// only its own folder inside them on Windows and macOS.
		for (const auto* base : {"config", "data", "cache"}) {
			std::error_code ec = {};
			std::filesystem::create_directories(root / base, ec);
			ASSERT_FALSE(ec) << "cannot create " << base << ": "
			                 << ec.message();
		}
		SetEnv("XDG_CONFIG_HOME", (root / "config").string());
		SetEnv("XDG_DATA_HOME", (root / "data").string());
		SetEnv("XDG_CACHE_HOME", (root / "cache").string());
#if defined(MACOSX)
		// The macOS folders come from HOME alone (cross.cpp), and the
		// engine creates only their last level, as every real home has
		// the rest.
		SetEnv("HOME", root.string());
		for (const auto* dir :
		     {"Preferences", "Caches", "Application Support"}) {
			std::error_code ec = {};
			std::filesystem::create_directories(root / "Library" / dir,
			                                    ec);
			ASSERT_FALSE(ec) << "cannot create Library/" << dir
			                 << ": " << ec.message();
		}
#endif

		// A redirect that did not take stops every test before one runs.
		init_config_dir();
		for (const auto& dir :
		     {get_config_dir(), get_data_dir(), get_cache_dir()}) {
			ASSERT_TRUE(TestHome::Contains(dir))
			        << dir << " lies outside the test home " << root;
		}
		ASSERT_TRUE(std::filesystem::is_directory(get_config_dir()))
		        << get_config_dir() << " was not created";
	}

	void TearDown() override
	{
		if (root.empty()) {
			return;
		}
		std::error_code ec = {};
		std::filesystem::remove_all(root, ec);
		root.clear();
	}
};

[[maybe_unused]] auto* const environment = ::testing::AddGlobalTestEnvironment(
        new TestHomeEnvironment);

} // namespace

namespace TestHome {

std::filesystem::path Root()
{
	return root;
}

bool Contains(const std::filesystem::path& path)
{
	if (root.empty()) {
		return false;
	}
	std::error_code ec  = {};
	const auto resolved = std::filesystem::weakly_canonical(path, ec);
	if (ec) {
		return false;
	}
	const auto relative = resolved.lexically_relative(root);
	return !relative.empty() && *relative.begin() != "..";
}

std::optional<std::string> UserHome()
{
	return user_home_saved ? user_home : GetEnv("HOME");
}

ScopedEnv::ScopedEnv(const char* name, const std::optional<std::string>& value)
        : name(name),
          previous(GetEnv(name))
{
	SetEnv(name, value);
}

ScopedEnv::~ScopedEnv()
{
	SetEnv(name.c_str(), previous);
}

} // namespace TestHome
