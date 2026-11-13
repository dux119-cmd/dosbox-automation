// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
// A restart from inside DOS keeps the running REST API (ada-rafi).

#include "config/host_only.h"

#include "config/setup.h"
#include "dosbox_test_fixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

class HostOnlyTest : public DOSBoxTestFixture {};

bool HasPin(const std::vector<std::string>& args, const std::string& pin)
{
	for (size_t i = 0; i + 1 < args.size(); ++i) {
		if (args[i] == "--set" && args[i + 1] == pin) {
			return true;
		}
	}
	return false;
}

std::vector<std::string> Pins()
{
	std::vector<std::string> parameters = {};
	HostOnly::ApplyRestartPins(parameters);
	return parameters;
}

TEST(HostOnly, CoversAWholeSectionAndASingleSettingInAnySpelling)
{
	EXPECT_TRUE(HostOnly::Covers("webserver", "webserver_port"));
	EXPECT_TRUE(HostOnly::Covers("WebServer", "anything"));
	EXPECT_TRUE(HostOnly::Covers("ethernet", "host_loopback"));
	EXPECT_TRUE(HostOnly::Covers("ETHERNET", "HOST_LOOPBACK"));
	EXPECT_FALSE(HostOnly::Covers("ethernet", "macaddr"));
	EXPECT_TRUE(HostOnly::Covers("dosbox", "browser"));
	EXPECT_TRUE(HostOnly::Covers("dos", "shell_history_file"));
	EXPECT_TRUE(HostOnly::Covers("dos", "archive_max_size"));
	EXPECT_TRUE(HostOnly::Covers("capture", "capture_dir"));
	EXPECT_FALSE(HostOnly::Covers("dosbox", "memsize"));
	EXPECT_TRUE(HostOnly::Covers("serial", "serial4"));
	EXPECT_TRUE(HostOnly::Covers("ethernet", "udp_port_forwards"));
	EXPECT_FALSE(HostOnly::Covers("serial", "phonebookfile"));
	EXPECT_FALSE(HostOnly::Covers("cpu", "host_loopback"));
}

TEST_F(HostOnlyTest, EveryRunningWebserverSettingIsPinned)
{
	auto* section = get_section("webserver");
	ASSERT_NE(section, nullptr);
	ASSERT_TRUE(section->HandleInputLine("webserver_port=9123"));

	const auto args = Pins();

	EXPECT_TRUE(HasPin(args, "webserver_port=9123"));
	for (const auto* property : *section) {
		if (property->IsDeprecated() ||
		    !property->propname.starts_with("webserver_")) {
			continue;
		}
		EXPECT_TRUE(HasPin(args,
		                   property->propname + "=" +
		                           property->GetValue().ToString()))
		        << property->propname;
	}
}

// A guest can restart the engine (CONFIG -r, a keyboard controller reset)
// after writing a local conf; host-only settings keep their running value.
TEST_F(HostOnlyTest, HostLoopbackKeepsItsRunningValue)
{
	const auto value =
	        get_section("ethernet")->GetProperty("host_loopback")->GetValue().ToString();
	EXPECT_TRUE(HasPin(Pins(), "host_loopback=" + value));
}

// The mount roots count only from the primary conf file; on Windows a root
// ending in a backslash would also break the quoted command line.
TEST_F(HostOnlyTest, TheMountRootsAreNotPinned)
{
	for (const auto& arg : Pins()) {
		EXPECT_FALSE(arg.starts_with("mount_allowed_")) << arg;
	}
}

// A guest reboot restarts the engine too, from a command line that already
// carries the last restart's pins.
TEST_F(HostOnlyTest, RepeatedRestartsPinEachSettingOnce)
{
	std::vector<std::string> parameters = {"dosbox-automation"};
	HostOnly::ApplyRestartPins(parameters);
	HostOnly::ApplyRestartPins(parameters);
	HostOnly::ApplyRestartPins(parameters);

	for (const auto& pin : Pins()) {
		if (pin == "--set") {
			continue;
		}
		const auto key   = pin.substr(0, pin.find('=') + 1);
		const auto count = std::count_if(parameters.begin(),
		                                 parameters.end(),
		                                 [&](const auto& a) {
			                                 return a.starts_with(key);
		                                 });
		EXPECT_EQ(count, 1) << key;
	}
}

} // namespace
