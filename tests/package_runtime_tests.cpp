// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/programs/package_runtime.h"

#include "config/config.h"
#include "config/setup.h"
#include "dosbox_test_fixture.h"
#include "misc/support.h"
#include "test_temp_dir.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace {

namespace fs = std::filesystem;

class PackageRuntimeTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		tmp = TestTempDir::MakeUnique("package_runtime_");
		ASSERT_FALSE(tmp.empty());
		saved_conf = control->arguments.conf;
		saved_set  = control->arguments.set;
		control->arguments.conf.clear();
		control->arguments.set.clear();
	}

	void TearDown() override
	{
		control->arguments.conf = saved_conf;
		control->arguments.set  = saved_set;
		std::error_code ec      = {};
		fs::remove_all(tmp, ec);
		DOSBoxTestFixture::TearDown();
	}

	PackageStart::Package Package(const std::string& text) const
	{
		PackageStart::Package package = {};
		package.path                  = "/g/game.zip";
		package.base_dir              = tmp;
		package.confs.push_back({"/g/game.zip/dosbox.conf", text, ""});
		return package;
	}

	std::string DashConf(const std::string& name, const std::string& text) const
	{
		const auto path = tmp / name;
		std::ofstream(path, std::ios::binary) << text;
		control->arguments.conf.push_back(path.string());
		return path.string();
	}

	static std::string Value(const char* section, const char* key)
	{
		return control->GetSection(section)->GetPropertyValue(key);
	}

	fs::path tmp                        = {};
	std::vector<std::string> saved_conf = {};
	std::vector<std::string> saved_set  = {};
};

TEST_F(PackageRuntimeTest, ASettingThatCanChangeNowIsApplied)
{
	const auto result = PackageRuntime::Apply(*control,
	                                          Package("[cpu]\ncputype=386\n"),
	                                          false);
	EXPECT_EQ(Value("cpu", "cputype"), "386");
	EXPECT_THAT(result.applied, ElementsAre("cpu.cputype"));
	EXPECT_THAT(result.start_only, IsEmpty());
}

TEST_F(PackageRuntimeTest, AStartOnlySettingKeepsItsValueAndIsNamed)
{
	const auto machine = Value("dosbox", "machine");
	ASSERT_NE(machine, "cga");
	const auto result = PackageRuntime::Apply(
	        *control, Package("[dosbox]\nmachine=cga\nmemsize=8\n"), false);
	EXPECT_EQ(Value("dosbox", "machine"), machine);
	EXPECT_NE(Value("dosbox", "memsize"), "8");
	EXPECT_THAT(result.start_only,
	            ElementsAre("dosbox.machine", "dosbox.memsize"));
	EXPECT_THAT(result.applied, IsEmpty());
	auto* dosbox = dynamic_cast<SectionProp*>(control->GetSection("dosbox"));
	ASSERT_NE(dosbox, nullptr);
	EXPECT_FALSE(dosbox->GetProperty("machine")->GetQueuedValue().has_value());
}

TEST_F(PackageRuntimeTest, ADashConfWinsForASettingThePackageSets)
{
	DashConf("over.conf", "[cpu]\ncputype=486\n");
	const auto result = PackageRuntime::Apply(*control,
	                                          Package("[cpu]\ncputype=386\n"),
	                                          false);
	EXPECT_EQ(Value("cpu", "cputype"), "486");
	EXPECT_THAT(result.applied, ElementsAre("cpu.cputype"));
}

TEST_F(PackageRuntimeTest, ADashSetWinsForASettingThePackageSets)
{
	control->arguments.set = {"cputype=pentium"};
	PackageRuntime::Apply(*control, Package("[cpu]\ncputype=386\n"), false);
	EXPECT_EQ(Value("cpu", "cputype"), "pentium");
}

// A setting changed with CONFIG after start, which the package leaves alone,
// is not reset to the -conf file's value.
TEST_F(PackageRuntimeTest, ASettingThePackageLeavesAloneKeepsItsRuntimeValue)
{
	DashConf("over.conf", "[cpu]\ncputype=486\ncpu_cycles=1000\n");
	ASSERT_TRUE(control->GetSection("cpu")->HandleInputLine("cpu_cycles=2000"));
	PackageRuntime::Apply(*control, Package("[cpu]\ncputype=386\n"), false);
	EXPECT_EQ(Value("cpu", "cpu_cycles"), "2000");
}

TEST_F(PackageRuntimeTest, ASetCommandForASettingThePackageLeavesAloneIsNotRepeated)
{
	control->arguments.set = {"cpu_cycles=1000"};
	ASSERT_TRUE(control->GetSection("cpu")->HandleInputLine("cpu_cycles=2000"));
	PackageRuntime::Apply(*control, Package("[cpu]\ncputype=386\n"), false);
	EXPECT_EQ(Value("cpu", "cpu_cycles"), "2000");
}

TEST_F(PackageRuntimeTest, TheWebserverAndTheHostLoopbackStayOut)
{
	const auto port   = Value("webserver", "webserver_port");
	const auto result = PackageRuntime::Apply(
	        *control,
	        Package("[webserver]\nwebserver_port=1\n[ethernet]\nhost_loopback=on\n"),
	        false);
	EXPECT_EQ(Value("webserver", "webserver_port"), port);
	EXPECT_EQ(Value("ethernet", "host_loopback"), "off");
	EXPECT_THAT(result.applied, IsEmpty());
}

TEST_F(PackageRuntimeTest, TheLinesAreCollectedAndTheAutoexecStaysAsItIs)
{
	const auto data   = control->GetOverwrittenAutoexecSection().data;
	const auto result = PackageRuntime::Apply(
	        *control, Package("[autoexec]\nmount c .\nc:\nGAME\n"), false);
	EXPECT_THAT(result.lines,
	            ElementsAre("mount c \"/g/game.zip\"", "c:", "GAME"));
	EXPECT_EQ(control->GetOverwrittenAutoexecSection().data, data);
	EXPECT_FALSE(control->package_conf_loaded);
}

TEST_F(PackageRuntimeTest, MountsOnlyKeepsTheMountLines)
{
	const auto result = PackageRuntime::Apply(
	        *control,
	        Package("[autoexec]\nmount c .\nimgmount d CD.ISO -t iso\nc:\nGAME\n"),
	        true);
	EXPECT_THAT(result.lines,
	            ElementsAre("mount c \"/g/game.zip\"",
	                        "imgmount d \"/g/game.zip/CD.ISO\" -t iso"));
}

// MOUNT gives its drive back to a package that mounts its own.
TEST_F(PackageRuntimeTest, ItSaysWhetherThePackageMountsADriveAndC)
{
	auto r = PackageRuntime::Apply(*control,
	                               Package("[autoexec]\nmount c .\nc:\n"),
	                               false);
	EXPECT_TRUE(r.mounts_drive);
	EXPECT_TRUE(r.mounts_c);
	r = PackageRuntime::Apply(*control,
	                          Package("[autoexec]\nimgmount d CD.ISO -t iso\n"),
	                          true);
	EXPECT_TRUE(r.mounts_drive);
	EXPECT_FALSE(r.mounts_c);
	r = PackageRuntime::Apply(*control, Package("[autoexec]\nGAME\n"), false);
	EXPECT_FALSE(r.mounts_drive);
	// A line left out mounts nothing.
	r = PackageRuntime::Apply(*control,
	                          Package("[autoexec]\nmount c ..\\..\\x\n"),
	                          false);
	EXPECT_FALSE(r.mounts_drive);
	EXPECT_FALSE(r.mounts_c);
}

TEST_F(PackageRuntimeTest, AnArchiveIsActiveUntilReleased)
{
	const auto zip = tmp / "game.zip";
	std::ofstream(zip, std::ios::binary) << "PK";
	EXPECT_FALSE(PackageRuntime::IsActive(zip));
	PackageRuntime::MarkActive(zip);
	EXPECT_TRUE(PackageRuntime::IsActive(zip));
	EXPECT_TRUE(PackageRuntime::IsActive(tmp / "sub" / ".." / "game.zip"));
	PackageRuntime::Release(zip);
	EXPECT_FALSE(PackageRuntime::IsActive(zip));
}

} // namespace
