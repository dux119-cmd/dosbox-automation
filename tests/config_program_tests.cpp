// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
// CONFIG inside DOS never reaches [webserver]: the REST API is set up from
// the host's confs and the command line only.

#include "dos/programs.h"

#include "config/config.h"
#include "config/setup.h"
#include "dos/programs/mount_policy.h"
#include "dosbox_test_fixture.h"
#include "misc/cross.h"
#include "shell/command_line.h"
#include "test_home.h"
#include "test_temp_dir.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <tuple>

std::unique_ptr<Program> CONFIG_ProgramCreate();

namespace {

class ConfigProgramTest : public DOSBoxTestFixture {
protected:
	static void RunConfig(const std::string& arguments)
	{
		auto config = CONFIG_ProgramCreate();
		delete config->cmd;
		config->cmd = new CommandLine("CONFIG", arguments);
		config->Run();
	}

	static Property* Setting(const std::string& section_name,
	                         const std::string& property_name)
	{
		auto* section = dynamic_cast<SectionProp*>(
		        control->GetSection(section_name));
		return section ? section->GetProperty(property_name) : nullptr;
	}
};

TEST_F(ConfigProgramTest, SetQueuesAStartSettingOutsideTheWebserver)
{
	auto* memsize = Setting("dosbox", "memsize");
	ASSERT_NE(memsize, nullptr);
	ASSERT_EQ(memsize->GetChange(), Property::Changeable::OnlyAtStart);

	RunConfig("-set memsize=8");

	EXPECT_EQ(memsize->GetQueuedValue(), std::optional<std::string>("8"));
}

TEST_F(ConfigProgramTest, SetRefusesEveryWebserverSpelling)
{
	const std::string spellings[] = {
	        "-set webserver_enabled=true",
	        "-set webserver webserver_port=9999",
	        "-set webserver webserver_port 9999",
	        "-set WEBSERVER_BIND_ADDRESS=0.0.0.0",
	        "-set webserver_allow_remote true",
	        "-set Webserver webserver_token_file=true",
	};
	for (const auto& line : spellings) {
		RunConfig(line);
	}

	for (const auto* name : {"webserver_enabled",
	                         "webserver_bind_address",
	                         "webserver_port",
	                         "webserver_allow_remote",
	                         "webserver_token_file"}) {
		const auto* property = Setting("webserver", name);
		ASSERT_NE(property, nullptr) << name;
		EXPECT_FALSE(property->GetQueuedValue().has_value()) << name;
	}
	EXPECT_EQ(Setting("webserver", "webserver_port")->GetValue().ToString(),
	          "8386");
}

TEST_F(ConfigProgramTest, SetRefusesEverySettingOfTheWebserverSection)
{
	auto* section = dynamic_cast<SectionProp*>(control->GetSection("webserver"));
	ASSERT_NE(section, nullptr);
	for (auto i = 0;; ++i) {
		auto* property = section->GetProperty(i);
		if (!property) {
			break;
		}
		const auto before = property->GetValue().ToString();
		RunConfig("-set webserver " + property->propname + "=x1");
		EXPECT_EQ(property->GetValue().ToString(), before)
		        << property->propname;
		EXPECT_FALSE(property->GetQueuedValue().has_value())
		        << property->propname;
	}
}

TEST_F(ConfigProgramTest, HostLoopbackIsOffByDefault)
{
	const auto* loopback = Setting("ethernet", "host_loopback");
	ASSERT_NE(loopback, nullptr);
	EXPECT_EQ(loopback->GetDefaultValue().ToString(), "off");
	EXPECT_EQ(loopback->GetValue().ToString(), "off");
}

TEST_F(ConfigProgramTest, SetRefusesHostLoopbackInEverySpelling)
{
	const auto* loopback = Setting("ethernet", "host_loopback");
	ASSERT_NE(loopback, nullptr);
	// Checked after each line: a value the setting rejects resets it to off.
	for (const auto* line : {"-set ethernet host_loopback=on",
	                         "-set host_loopback on",
	                         "-set HOST_LOOPBACK=true",
	                         "-set Ethernet Host_Loopback on"}) {
		RunConfig(line);
		EXPECT_EQ(loopback->GetValue().ToString(), "off") << line;
		EXPECT_FALSE(loopback->GetQueuedValue().has_value()) << line;
	}
}

// A host command line, a file the host writes, and a limit on the
// archives: none of them is the guest's to change.
TEST_F(ConfigProgramTest, SetRefusesTheOtherHostOnlySettings)
{
	for (const auto& [section, name, line] :
	     {std::tuple{"dosbox", "browser", "-set dosbox browser=sh -c evil"},
	      std::tuple{"dos", "shell_history_file", "-set shell_history_file=../.profile"},
	      std::tuple{"dos", "archive_max_size", "-set archive_max_size=999999"},
	      std::tuple{"serial", "serial1", "-set serial1=modem listenport:5000"},
	      std::tuple{"ethernet", "tcp_port_forwards", "-set tcp_port_forwards=8080:80"}}) {
		const auto* setting = Setting(section, name);
		ASSERT_NE(setting, nullptr) << name;
		const auto before = setting->GetValue().ToString();
		RunConfig(line);
		EXPECT_EQ(setting->GetValue().ToString(), before) << line;
		EXPECT_FALSE(setting->GetQueuedValue().has_value()) << line;
	}
}

TEST_F(ConfigProgramTest, SetStillChangesTheOtherEthernetSettings)
{
	RunConfig("-set ethernet macaddr=AC:DE:48:00:00:01");
	EXPECT_EQ(Setting("ethernet", "macaddr")->GetValue().ToString(),
	          "AC:DE:48:00:00:01");
}

// The guest writes no host file while the whitelist applies (ada-u6bo).
TEST_F(ConfigProgramTest, HostWritesAreRefusedWhileTheWhitelistApplies)
{
	const auto dir = TestTempDir::MakeUnique("config_write_");
	ASSERT_FALSE(dir.empty());
	MountPolicy::SetInjectionPossible(true);
	for (const auto* option : {"-wc", "-writeconf", "-wl", "-writelang"}) {
		const auto target = dir / "out.txt";
		RunConfig(std::string(option) + " " + target.string());
		EXPECT_FALSE(std::filesystem::exists(target)) << option;
	}
	const auto primary = get_primary_config_path();
	ASSERT_TRUE(TestHome::Contains(primary)) << primary;
	std::filesystem::remove(primary);
	RunConfig("-wcd");
	EXPECT_FALSE(std::filesystem::exists(primary));
	MountPolicy::SetInjectionPossible(false);
	std::filesystem::remove_all(dir);
}

TEST_F(ConfigProgramTest, TheOwnerWithoutTheApiStillWritesAConfig)
{
	const auto dir = TestTempDir::MakeUnique("config_write_");
	ASSERT_FALSE(dir.empty());
	MountPolicy::SetInjectionPossible(false);
	const auto target = dir / "out.conf";
	RunConfig("-wc " + target.string());
	EXPECT_TRUE(std::filesystem::exists(target));
	std::filesystem::remove_all(dir);
}

} // namespace
