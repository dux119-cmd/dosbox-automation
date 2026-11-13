// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/docs_location.h"

#include "dosbox_config.h"

std::string DocsLocation::ReleaseVersion(const std::string_view version)
{
	return std::string(version.substr(0, version.find('-')));
}

// DOSBOX_VERSION_SHORT has no page behind it: the site publishes one
// tree per full release version, never per major.minor.
std::string DocsLocation::OnlineUrl(const std::string_view page)
{
	return "https://dosbox-automation.org/" +
	       ReleaseVersion(DOSBOX_VERSION) + "/" + std::string(page);
}

std::string DocsLocation::BundledRelativePath(const std::string_view page)
{
	return "docs/" + ReleaseVersion(DOSBOX_VERSION) + "/" +
	       std::string(page) + "index.html";
}
