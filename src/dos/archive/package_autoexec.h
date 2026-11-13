// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_PACKAGE_AUTOEXEC_H
#define DOSBOX_PACKAGE_AUTOEXEC_H

#include "dos/archive/archive_source.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A game package's [autoexec] runs as any conf's does, except that a relative
// MOUNT or IMGMOUNT path counts from the conf's folder inside the archive and
// never leaves it (ada-wika).
namespace PackageAutoexec {

// One [autoexec] line, its relative MOUNT paths named against archive_path
// (as MOUNT names it) from conf_folder ("" for the archive's root); none,
// with the reason, when it is left out.
std::optional<std::string> RewriteLine(const std::string& line,
                                       std::string_view archive_path,
                                       std::string_view conf_folder,
                                       const std::vector<ArchiveMount::Entry>& entries,
                                       std::string& reason);

// RewriteLine for a game folder on the host (ada-i8ec): a relative path
// becomes the host path of what it names inside folder_path.
std::optional<std::string> RewriteFolderLine(
        const std::string& line, std::string_view folder_path,
        std::string_view conf_folder,
        const std::vector<ArchiveMount::Entry>& entries, std::string& reason);

// Control bytes as \xNN: the notes reach the log and the DOS console, and an
// archive's escape sequence must not reach the host terminal.
std::string Printable(std::string_view text);

// A MOUNT or IMGMOUNT line, in any spelling.
bool IsMountLine(std::string_view line);

// The drive letter a MOUNT or IMGMOUNT line mounts; none for any other line
// and for an unmount.
std::optional<char> MountLetter(std::string_view line);

// A relative path as the conf's folder sees it ("..", ".\C", "..\GAME.INST",
// either slash, quotes stripped) as a utf8 path inside the archive; none when
// it climbs above the root or is not a relative path.
std::optional<std::string> ResolveInside(std::string_view conf_folder,
                                         std::string_view spelled);

} // namespace PackageAutoexec

#endif // DOSBOX_PACKAGE_AUTOEXEC_H
