// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "config/host_only.h"

#include "config/config.h"
#include "config/setup.h"
#include "utils/checks.h"
#include "utils/string_utils.h"

#include <algorithm>
#include <iterator>

CHECK_NARROWING();

namespace HostOnly {

bool Covers(const std::string_view section, const std::string_view setting)
{
	return std::any_of(Settings.begin(), Settings.end(), [&](const std::string& entry) {
		const auto dot = entry.find('.');
		if (dot == std::string::npos) {
			return iequals(entry, section);
		}
		return iequals(std::string_view(entry).substr(0, dot), section) &&
		       iequals(std::string_view(entry).substr(dot + 1), setting);
	});
}

static std::vector<std::string> running_values()
{
	std::vector<std::string> pins = {};
	if (!control) {
		return pins;
	}
	for (auto* section : *control) {
		const auto properties = dynamic_cast<SectionProp*>(section);
		if (!properties) {
			continue;
		}
		for (const auto* property : *properties) {
			// The mount roots count only from the primary conf file, and on
			// Windows one ending in '\' would break the quoted command line.
			if (property->IsDeprecated() ||
			    property->propname.starts_with("mount_allowed_") ||
			    !Covers(section->GetName(), property->propname)) {
				continue;
			}
			pins.emplace_back("--set");
			pins.emplace_back(property->propname + "=" +
			                  property->GetValue().ToString());
		}
	}
	return pins;
}

void ApplyRestartPins(std::vector<std::string>& parameters)
{
	const auto pins = running_values();
	for (size_t i = 0; i + 1 < pins.size(); i += 2) {
		const auto& pin = pins[i + 1];
		const auto key  = pin.substr(0, pin.find('=') + 1);
		auto it         = parameters.begin();
		while (it != parameters.end() && std::next(it) != parameters.end()) {
			if (*it == "--set" && std::next(it)->starts_with(key)) {
				it = parameters.erase(it, std::next(it, 2));
			} else {
				++it;
			}
		}
	}
	parameters.insert(parameters.end(), pins.begin(), pins.end());
}

} // namespace HostOnly
