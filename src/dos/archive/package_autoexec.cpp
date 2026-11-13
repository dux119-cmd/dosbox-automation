// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/archive/package_autoexec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "shell/shell.h"
#include "utils/checks.h"
#include "utils/string_utils.h"

CHECK_NARROWING();

namespace PackageAutoexec {

namespace {

char Fold(const char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string Lower(const std::string_view s)
{
	std::string out(s);
	std::transform(out.begin(), out.end(), out.begin(), Fold);
	return out;
}

bool EqualFolded(const std::string_view a, const std::string_view b)
{
	return a.size() == b.size() &&
	       std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
		       return Fold(x) == Fold(y);
	       });
}

std::vector<std::string_view> SplitSlashes(const std::string_view path)
{
	std::vector<std::string_view> out = {};
	size_t start                      = 0;
	while (start <= path.size()) {
		const auto end = path.find_first_of("/\\", start);
		const auto len = (end == std::string_view::npos ? path.size() : end) -
		                 start;
		out.push_back(path.substr(start, len));
		if (end == std::string_view::npos) {
			break;
		}
		start = end + 1;
	}
	return out;
}

// Blank-separated words; a double-quoted word keeps its blanks and quotes.
std::vector<std::string> Words(const std::string_view line)
{
	std::vector<std::string> out = {};
	size_t i                     = 0;
	while (i < line.size()) {
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
			++i;
		}
		if (i >= line.size()) {
			break;
		}
		const auto start = i;
		bool quoted      = false;
		while (i < line.size() &&
		       (quoted || (line[i] != ' ' && line[i] != '\t'))) {
			if (line[i] == '"') {
				quoted = !quoted;
			}
			++i;
		}
		out.emplace_back(line.substr(start, i - start));
	}
	return out;
}

std::string_view Unquoted(std::string_view s)
{
	if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
		s = s.substr(1, s.size() - 2);
	}
	return s;
}

// Switches of MOUNT that take the next word as their value.
bool TakesValue(const std::string_view lower_switch)
{
	constexpr std::array<std::string_view, 10> WithValue = {"-t",
	                                                        "-fs",
	                                                        "-freesize",
	                                                        "-size",
	                                                        "-label",
	                                                        "-ide",
	                                                        "-chs",
	                                                        "-root",
	                                                        "-run",
	                                                        "-sidecar"};
	return std::find(WithValue.begin(), WithValue.end(), lower_switch) !=
	       WithValue.end();
}

// The command word as the shell cuts it (DOS_Shell::DoCommand), and its name
// in lower case without drive, path and .COM/.EXE.
struct Command {
	bool builtin     = false;
	std::string word = {};
	std::string name = {};
};

Command CommandOf(std::string_view line)
{
	while (!line.empty() && (line.front() == '@' || line.front() == ' ' ||
	                         line.front() == '\t')) {
		line.remove_prefix(1);
	}
	constexpr std::string_view Delimiters = " /\t=\";,";
	std::string word                      = {};
	for (const char c : line) {
		if (Delimiters.find(c) != std::string_view::npos) {
			break;
		}
		if ((c == '.' || c == '\\') && SHELL_IsBuiltin(word)) {
			return {true, word, Lower(word)};
		}
		word += c;
	}
	if (SHELL_IsBuiltin(word)) {
		return {true, word, Lower(word)};
	}
	auto name = std::string_view(word);
	if (name.size() >= 2 && name[1] == ':') {
		name.remove_prefix(2);
	}
	if (const auto slash = name.find_last_of('\\');
	    slash != std::string_view::npos) {
		name.remove_prefix(slash + 1);
	}
	auto lower = Lower(name);
	if (lower.ends_with(".com") || lower.ends_with(".exe")) {
		lower.resize(lower.size() - 4);
	}
	return {false, word, lower};
}

bool IsMount(const Command& command)
{
	return !command.builtin &&
	       (command.name == "mount" || command.name == "imgmount");
}

enum class Kind { Missing, File, Folder, CaseTwins };

struct Member {
	Kind kind           = Kind::Missing;
	std::string spelled = {};
	std::string dos     = {};
};

// The first `count` backslash-separated components of a DOS path.
std::string_view DosPrefix(const std::string_view dos_path, const size_t count)
{
	size_t end = 0;
	for (size_t i = 0; i < count; ++i) {
		end = dos_path.find('\\', i == 0 ? 0 : end + 1);
		if (end == std::string_view::npos) {
			return i + 1 == count ? dos_path : std::string_view{};
		}
	}
	return dos_path.substr(0, end);
}

// What a path names in the archive: the entry list holds files and folders,
// and every parent of an entry is a folder even without an entry of its own.
Member Look(const std::vector<ArchiveMount::Entry>& entries,
            const std::string_view path)
{
	if (path.empty()) {
		return {Kind::Folder, "", ""};
	}
	std::vector<Member> found = {};

	auto add = [&](const Kind kind,
	               const std::string_view spelled,
	               const std::string_view dos) {
		const bool seen = std::any_of(found.begin(),
		                              found.end(),
		                              [&](const auto& m) {
			                              return m.kind == kind &&
			                                     m.spelled == spelled;
		                              });
		if (!seen) {
			found.push_back(
			        {kind, std::string(spelled), std::string(dos)});
		}
	};
	const auto slashes    = std::count(path.begin(), path.end(), '/');
	const auto components = static_cast<size_t>(slashes) + 1;
	for (const auto& e : entries) {
		const std::string_view p = e.utf8_path;
		if (EqualFolded(p, path)) {
			add(e.is_dir ? Kind::Folder : Kind::File, p, e.dos_path);
		}
		if (p.size() > path.size() && p[path.size()] == '/' &&
		    EqualFolded(p.substr(0, path.size()), path)) {
			add(Kind::Folder,
			    p.substr(0, path.size()),
			    DosPrefix(e.dos_path, components));
		}
	}
	if (found.empty()) {
		return {};
	}
	if (found.size() > 1) {
		return {Kind::CaseTwins, ""};
	}
	return found.front();
}

// A relative host path in a MOUNT line: no root, no drive, no home.
bool IsRelative(const std::string_view spelled)
{
	const auto s = Unquoted(spelled);
	return !s.empty() && s.front() != '/' && s.front() != '\\' &&
	       s.front() != '~' && s.find(':') == std::string_view::npos;
}

// The batch expansion reads '%' as a variable.
std::string Quoted(const std::string_view text)
{
	std::string out = "\"";
	for (const char c : text) {
		out += c;
		if (c == '%') {
			out += '%';
		}
	}
	return out + "\"";
}

// Where the shell's redirection or pipe starts, outside quotes; npos if none.
size_t RedirectionAt(const std::string_view line)
{
	bool quoted = false;
	for (size_t i = 0; i < line.size(); ++i) {
		if (line[i] == '"') {
			quoted = !quoted;
		} else if (!quoted &&
		           (line[i] == '>' || line[i] == '<' || line[i] == '|')) {
			return i;
		}
	}
	return std::string_view::npos;
}

// Where a MOUNT line's drive letter and paths are, and whether it unmounts.
struct MountWords {
	std::vector<std::string> words = {};
	std::optional<size_t> letter   = {};
	std::vector<size_t> paths      = {};
	bool unmount                   = false;
};

MountWords ParseMount(const std::string_view line)
{
	MountWords out = {};
	out.words      = Words(line);
	for (size_t i = 1; i < out.words.size(); ++i) {
		const auto lower = Lower(out.words[i]);
		if (!lower.empty() && lower.front() == '-') {
			out.unmount = out.unmount || lower == "-u";
			// As MOUNT's own option parsing: a value never starts
			// with '-'.
			if (TakesValue(lower) && i + 1 < out.words.size() &&
			    !out.words[i + 1].empty() &&
			    out.words[i + 1].front() != '-') {
				++i;
			}
		} else if (!out.letter) {
			out.letter = i;
		} else {
			out.paths.push_back(i);
		}
	}
	return out;
}

// The archive MOUNT opens, or a game folder on the host.
struct Target {
	std::string_view path = {};
	bool is_folder        = false;
};

// The relative paths of a MOUNT line named against the archive or the game
// folder; none, with the reason, when one cannot be.
std::optional<std::string> RewriteMount(const std::string& line, const Target target,
                                        const std::string_view conf_folder,
                                        const std::vector<ArchiveMount::Entry>& entries,
                                        std::string& reason)
{
	const auto archive_path = target.path;
	const auto cut          = std::min(RedirectionAt(line), line.size());
	const auto tail         = std::string_view(line).substr(cut);
	auto parsed    = ParseMount(std::string_view(line).substr(0, cut));
	auto& words    = parsed.words;
	const auto& at = parsed.paths;

	std::optional<std::string> root = {};
	for (const auto i : at) {
		if (!IsRelative(words[i])) {
			continue;
		}
		const auto inside = ResolveInside(conf_folder, words[i]);
		if (!inside) {
			reason = target.is_folder
			               ? "a relative path above the game folder"
			               : "a relative path above the archive's root";
			return {};
		}
		const auto unquotable = [](const char c) {
			return c == '"' || static_cast<unsigned char>(c) < 0x20;
		};
		if (std::any_of(inside->begin(), inside->end(), unquotable)) {
			reason = "the conf's folder holds a '\"' or a control character";
			return {};
		}
		// A DOS command line has no way to quote a '"'.
		if (archive_path.find('"') != std::string_view::npos) {
			reason = target.is_folder
			               ? "the game folder's path holds a '\"'"
			               : "the archive's path holds a '\"'";
			return {};
		}
		const auto member = Look(entries, *inside);
		if (member.kind == Kind::CaseTwins) {
			reason = words[i] +
			         " names two members that differ only in case";
			return {};
		}
		if (member.spelled.find('"') != std::string::npos) {
			reason = "the member's name holds a '\"'";
			return {};
		}
		if (target.is_folder) {
			const auto& name = (member.kind == Kind::Missing)
			                         ? *inside
			                         : member.spelled;
			words[i] = Quoted(name.empty() ? std::string(archive_path)
			                               : std::string(archive_path) +
			                                         "/" + name);
			continue;
		}
		// MOUNT looks -root up among the DOS names (8.3 aliases), not
		// the long names.
		if (member.kind == Kind::Folder && at.size() == 1) {
			if (!member.spelled.empty()) {
				if (member.dos.empty()) {
					reason = words[i] +
					         " has no DOS name in the archive";
					return {};
				}
				root = "-root " + Quoted(member.dos);
			}
			words[i] = Quoted(archive_path);
			continue;
		}
		const auto& name = (member.kind == Kind::File) ? member.spelled
		                                               : *inside;
		words[i] = Quoted(std::string(archive_path) + "/" + name);
	}

	std::string out = {};
	for (size_t i = 0; i < words.size(); ++i) {
		if (i > 0) {
			out += ' ';
		}
		out += words[i];
		if (root && !at.empty() && i == at.front()) {
			out += ' ' + *root;
		}
	}
	if (!tail.empty()) {
		out += ' ';
		out += tail;
	}
	return out;
}

} // namespace

std::string Printable(const std::string_view text)
{
	return escape_control_bytes(text);
}

std::optional<std::string> ResolveInside(const std::string_view conf_folder,
                                         const std::string_view spelled)
{
	const auto s = Unquoted(spelled);
	if (!IsRelative(s) || s.find('"') != std::string_view::npos ||
	    std::any_of(s.begin(), s.end(), [](const char c) {
		    return static_cast<unsigned char>(c) < 0x20;
	    })) {
		return {};
	}

	std::vector<std::string_view> parts = {};
	if (!conf_folder.empty()) {
		parts = SplitSlashes(conf_folder);
	}
	for (const auto segment : SplitSlashes(s)) {
		if (segment.empty() || segment == ".") {
			continue;
		}
		if (segment == "..") {
			if (parts.empty()) {
				return {};
			}
			parts.pop_back();
			continue;
		}
		parts.push_back(segment);
	}

	std::string out = {};
	for (const auto part : parts) {
		if (!out.empty()) {
			out += '/';
		}
		out += part;
	}
	return out;
}

bool IsMountLine(const std::string_view line)
{
	return IsMount(CommandOf(line));
}

std::optional<char> MountLetter(const std::string_view line)
{
	if (!IsMount(CommandOf(line))) {
		return {};
	}
	const auto parsed = ParseMount(line);
	if (parsed.unmount || !parsed.letter) {
		return {};
	}
	const auto& word = parsed.words[*parsed.letter];
	if (word.empty() || word.size() > 2 || (word.size() == 2 && word[1] != ':') ||
	    !std::isalpha(static_cast<unsigned char>(word[0]))) {
		return {};
	}
	return static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
}

std::optional<std::string> RewriteLine(const std::string& line,
                                       const std::string_view archive_path,
                                       const std::string_view conf_folder,
                                       const std::vector<ArchiveMount::Entry>& entries,
                                       std::string& reason)
{
	if (!IsMount(CommandOf(line))) {
		return line;
	}
	return RewriteMount(line, {archive_path, false}, conf_folder, entries, reason);
}

std::optional<std::string> RewriteFolderLine(
        const std::string& line, const std::string_view folder_path,
        const std::string_view conf_folder,
        const std::vector<ArchiveMount::Entry>& entries, std::string& reason)
{
	if (!IsMount(CommandOf(line))) {
		return line;
	}
	return RewriteMount(line, {folder_path, true}, conf_folder, entries, reason);
}

} // namespace PackageAutoexec
