// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/docs_location.h"

#include <string>

#include <gtest/gtest.h>

#include "dosbox_config.h"

namespace {

TEST(DocsLocation, APreReleaseReadsTheManualOfItsRelease)
{
	EXPECT_EQ(DocsLocation::ReleaseVersion("0.86.0-pre1"), "0.86.0");
}

TEST(DocsLocation, AReleaseVersionStaysAsItIs)
{
	EXPECT_EQ(DocsLocation::ReleaseVersion("0.86.0"), "0.86.0");
}

TEST(DocsLocation, OnlineUrlUsesTheReleaseOfThisBuild)
{
	EXPECT_EQ(DocsLocation::OnlineUrl("introduction/about-this-manual/"),
	          "https://dosbox-automation.org/" +
	                  DocsLocation::ReleaseVersion(DOSBOX_VERSION) +
	                  "/introduction/about-this-manual/");
}

TEST(DocsLocation, OnlineUrlOfTheRootEndsWithTheReleaseVersion)
{
	EXPECT_EQ(DocsLocation::OnlineUrl(""),
	          "https://dosbox-automation.org/" +
	                  DocsLocation::ReleaseVersion(DOSBOX_VERSION) + "/");
}

TEST(DocsLocation, OnlineUrlLeavesOutAPreReleaseSuffix)
{
	const std::string site = "https://dosbox-automation.org/";
	const auto url         = DocsLocation::OnlineUrl("");
	ASSERT_EQ(url.rfind(site, 0), 0u);
	EXPECT_EQ(url.find('-', site.size()), std::string::npos);
}

TEST(DocsLocation, OnlineUrlNeverCarriesTheShortVersion)
{
	// The site has no /0.85/ tree, only /0.85.1/; the short form was
	// what MANUAL and GUIDE sent before and it answered 404.
	const auto url = DocsLocation::OnlineUrl("using-dosbox-automation/");
	EXPECT_EQ(url.find(std::string("/") + DOSBOX_VERSION_SHORT + "/"),
	          std::string::npos);
}

TEST(DocsLocation, BundledPathMirrorsTheSiteLayout)
{
	const auto release = DocsLocation::ReleaseVersion(DOSBOX_VERSION);
	EXPECT_EQ(DocsLocation::BundledRelativePath("using-dosbox-automation/"),
	          "docs/" + release + "/using-dosbox-automation/index.html");
	EXPECT_EQ(DocsLocation::BundledRelativePath(""),
	          "docs/" + release + "/index.html");
}

} // namespace
