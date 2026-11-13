// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_CONFIG_HOST_ONLY_H
#define DOSBOX_CONFIG_HOST_ONLY_H

#include <string>
#include <string_view>
#include <vector>

// Settings only the host sets: a game package ignores them, CONFIG inside DOS
// refuses them, and a restart keeps their running values (ada-rafi).
namespace HostOnly {

// Whole sections by name, single settings as "section.key": the REST API,
// the ways to the host's network (loopback, serial links, port forwards), a
// host command line, the settings that write host files, the archive limits.
inline const std::vector<std::string> Settings = {
        "webserver",
        "ethernet.host_loopback",
        "ethernet.tcp_port_forwards",
        "ethernet.udp_port_forwards",
        "serial.serial1",
        "serial.serial2",
        "serial.serial3",
        "serial.serial4",
        "dosbox.browser",
        "dosbox.captures",
        "capture.capture_dir",
        "sdl.mapperfile",
        "log.logfile",
        "dos.shell_history_file",
        "dos.setver_table_file",
        "dos.archive_cache_path",
        "dos.archive_sidecar_path",
        "dos.archive_cache_max_size",
        "dos.archive_cache_min_free",
        "dos.archive_cache_ttl",
        "dos.archive_memory_limit",
        "dos.archive_max_entries",
        "dos.archive_max_size",
        "dos.archive_sequential"};

bool Covers(std::string_view section, std::string_view setting);

// Each setting once, however often the engine restarted before.
void ApplyRestartPins(std::vector<std::string>& parameters);

} // namespace HostOnly

#endif
