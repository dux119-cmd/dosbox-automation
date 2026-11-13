// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/package_profile.h"

#include "dos/archive/archive_toml.h"
#include "dos/archive/member_path.h"
#include "utils/checks.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <unordered_map>

CHECK_NARROWING();

namespace PackageProfile {

namespace {

constexpr size_t MaxNameBytes = ArchiveToml::MaxKeyBytes;
constexpr size_t MaxPathBytes = 255;

constexpr std::string_view KeyEnabled        = "enabled";
constexpr std::string_view KeyPaths          = "dosconfig_paths";
constexpr std::string_view KeyFile           = "dosconfig_file";
constexpr std::string_view KeyChained        = "dosconfig_chained";
constexpr std::string_view KeyWorkingDir     = "dosconfig_working_dir";
constexpr std::string_view KeyDefaultProfile = "default_profile";

char Fold(const char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualFolded(const std::string_view a, const std::string_view b)
{
	return a.size() == b.size() &&
	       std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
		       return Fold(x) == Fold(y);
	       });
}

bool LessFolded(const std::string& a, const std::string& b)
{
	return std::lexicographical_compare(a.begin(),
	                                    a.end(),
	                                    b.begin(),
	                                    b.end(),
	                                    [](const char x, const char y) {
		                                    return Fold(x) < Fold(y);
	                                    });
}

bool IsWildcard(const char c)
{
	return c == '*' || c == '?';
}

bool HasControlByte(const std::string_view s)
{
	return std::any_of(s.begin(), s.end(), [](const char c) {
		return static_cast<unsigned char>(c) < 0x20 || c == 0x7f;
	});
}

std::vector<std::string_view> SplitPath(const std::string_view path)
{
	std::vector<std::string_view> out = {};
	size_t start                      = 0;
	while (true) {
		const auto end = path.find_first_of("/\\", start);
		if (end == std::string_view::npos) {
			out.push_back(path.substr(start));
			return out;
		}
		out.push_back(path.substr(start, end - start));
		start = end + 1;
	}
}

std::string_view ParentOf(const std::string_view path)
{
	const auto slash = path.rfind('/');
	return slash == std::string_view::npos ? std::string_view()
	                                       : path.substr(0, slash);
}

std::string_view LastName(const std::string_view path)
{
	const auto slash = path.rfind('/');
	return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string_view Trim(std::string_view s)
{
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		s.remove_prefix(1);
	}
	while (!s.empty() &&
	       (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
		s.remove_suffix(1);
	}
	return s;
}

bool IsHeaderLine(const std::string_view line)
{
	return Trim(line).starts_with("[[");
}

// The name between the brackets as written, for a note about a section
// that did not parse; cut, its control bytes as '?'.
std::string RawHeaderName(const std::string_view line)
{
	auto inner       = Trim(line).substr(2);
	const auto close = inner.find("]]");
	if (close != std::string_view::npos) {
		inner = inner.substr(0, close);
	}
	auto out = std::string();
	for (const auto c : Trim(inner).substr(0, MaxNameBytes)) {
		out.push_back(static_cast<unsigned char>(c) < 0x20 ? '?' : c);
	}
	return out;
}

bool IsCommentOrBlank(const std::string_view line)
{
	const auto t = Trim(line);
	return t.empty() || t.front() == '#';
}

struct Section {
	int first_line   = 0;
	std::string text = {};
	// The header as written; empty before the first header.
	std::string label = {};
};

std::vector<Section> SplitSections(const std::string_view text)
{
	std::vector<Section> out = {
	        Section{1, {}, {}}
        };
	int line_number = 0;
	size_t pos      = 0;
	while (pos < text.size()) {
		++line_number;
		auto end = text.find('\n', pos);
		if (end == std::string_view::npos) {
			end = text.size();
		}
		const auto line = text.substr(pos, end - pos);
		pos             = end + 1;
		if (IsHeaderLine(line)) {
			out.push_back(Section{line_number, {}, RawHeaderName(line)});
		}
		out.back().text += line;
		out.back().text += '\n';
	}
	return out;
}

std::optional<std::string> CheckFileNames(const std::vector<std::string>& names,
                                          const std::string_view key)
{
	for (const auto& name : names) {
		if (!IsPlainFileName(name)) {
			return "'" + name + "' in " + std::string(key) +
			       " is not a plain file name";
		}
	}
	return {};
}

// Reads one profile's keys; an error text when the profile must be skipped.
std::optional<std::string> ReadProfile(const ArchiveToml::Table& table, Profile& profile)
{
	for (const auto& [key, value] : table.pairs) {
		if (key != KeyEnabled && key != KeyPaths && key != KeyFile &&
		    key != KeyChained && key != KeyWorkingDir) {
			return "unknown key '" + key + "'";
		}
	}
	if (table.Find(KeyEnabled)) {
		const auto enabled = table.Boolean(KeyEnabled);
		if (!enabled) {
			return std::string(KeyEnabled) + " must be true or false";
		}
		profile.enabled = *enabled;
	}
	for (const auto& [key, target] : {
	             std::pair{  KeyPaths,   &profile.paths},
	             std::pair{   KeyFile,   &profile.files},
	             std::pair{KeyChained, &profile.chained}
        }) {
		if (!table.Find(key)) {
			continue;
		}
		auto list = table.Strings(key);
		if (!list) {
			return std::string(key) + " must be a list of strings";
		}
		*target = std::move(*list);
	}
	if (table.Find(KeyWorkingDir)) {
		auto folder = table.String(KeyWorkingDir);
		if (!folder || !IsPlainFileName(*folder)) {
			return std::string(KeyWorkingDir) + " must be one folder name";
		}
		profile.working_dir = std::move(*folder);
	}
	if (profile.files.empty()) {
		return "incomplete, no " + std::string(KeyFile);
	}
	if (profile.paths.empty()) {
		return std::string(KeyPaths) + " is empty";
	}
	for (const auto& path : profile.paths) {
		if (!PathAllowed(path)) {
			return "path '" + path + "' is not allowed";
		}
	}
	if (auto why = CheckFileNames(profile.files, KeyFile)) {
		return why;
	}
	return CheckFileNames(profile.chained, KeyChained);
}

const Profile* FindByName(const ProfileFile& file, const std::string_view name)
{
	for (const auto& p : file.profiles) {
		if (p.name == name) {
			return &p;
		}
	}
	return nullptr;
}

// The reader's "line N: ..." counts from the section's header; the note
// names the line of the file.
std::string WithFileLine(const std::string& error, const int first_line)
{
	constexpr std::string_view Prefix = "line ";
	if (!error.starts_with(Prefix)) {
		return error;
	}
	const auto colon = error.find(':', Prefix.size());
	int line         = 0;
	const auto* from = error.data() + Prefix.size();
	const auto* to   = error.data() +
	                 (colon == std::string::npos ? error.size() : colon);
	if (std::from_chars(from, to, line).ptr != to) {
		return error;
	}
	return std::string(Prefix) + std::to_string(line + first_line - 1) +
	       error.substr(static_cast<size_t>(to - error.data()));
}

std::string FoldedKey(const std::string_view path)
{
	std::string key(path);
	std::transform(key.begin(), key.end(), key.begin(), Fold);
	return key;
}

// The mount's folders and files by folder, spelled as the entries spell them.
// Both indices are built once: a collection archive has tens of thousands of
// game folders, and a wildcard path asks about each of them.
struct Listing {
	std::vector<std::string> folders                = {};
	const std::vector<ArchiveMount::Entry>* entries = nullptr;

	std::unordered_map<std::string, std::vector<size_t>> children = {};
	std::unordered_map<std::string, std::vector<size_t>> files    = {};

	explicit Listing(const std::vector<ArchiveMount::Entry>& all)
	        : entries(&all)
	{
		folders.emplace_back();
		for (const auto& e : all) {
			std::string_view path = e.utf8_path;
			if (e.is_dir) {
				folders.emplace_back(path);
			}
			for (auto parent = ParentOf(path); !parent.empty();
			     parent      = ParentOf(parent)) {
				folders.emplace_back(parent);
			}
		}
		std::sort(folders.begin(), folders.end(), LessFolded);
		folders.erase(std::unique(folders.begin(), folders.end(), EqualFolded),
		              folders.end());
		for (size_t i = 0; i < folders.size(); ++i) {
			if (!folders[i].empty()) {
				children[FoldedKey(ParentOf(folders[i]))].push_back(i);
			}
		}

		for (size_t i = 0; i < all.size(); ++i) {
			const auto& e = all[i];
			if (!e.is_dir) {
				files[FoldedKey(ParentOf(e.utf8_path))].push_back(i);
			}
		}
		// Case twins in a stable order: the exact spelling breaks the tie.
		for (auto& [folder, in_folder] : files) {
			std::stable_sort(in_folder.begin(),
			                 in_folder.end(),
			                 [&](const size_t a, const size_t b) {
				                 const auto& x = all[a].utf8_path;
				                 const auto& y = all[b].utf8_path;
				                 return LessFolded(x, y) ||
				                        (!LessFolded(y, x) && x < y);
			                 });
		}
	}

	// Folders a relative path names, a wildcard segment giving several.
	std::vector<std::string> Expand(const std::string_view path) const
	{
		std::vector<std::string> current = {""};
		for (const auto segment : SplitPath(path)) {
			if (segment == ".") {
				continue;
			}
			std::vector<std::string> next = {};
			for (const auto& parent : current) {
				const auto it = children.find(FoldedKey(parent));
				if (it == children.end()) {
					continue;
				}
				for (const auto i : it->second) {
					if (PatternMatches(segment,
					                   LastName(folders[i]))) {
						next.push_back(folders[i]);
					}
				}
			}
			std::sort(next.begin(), next.end(), LessFolded);
			current = std::move(next);
		}
		return current;
	}

	const std::vector<size_t>& FilesIn(const std::string_view folder) const
	{
		static const std::vector<size_t> none = {};
		const auto it = files.find(FoldedKey(folder));
		return it == files.end() ? none : it->second;
	}
};

bool AnyPatternMatches(const std::vector<std::string>& patterns,
                       const std::string_view name)
{
	return std::any_of(patterns.begin(), patterns.end(), [&](const auto& p) {
		return PatternMatches(p, name);
	});
}

// A full match in one folder: a conf from the file list, then one file for
// every chained pattern; a name matching both lists counts as chained.
std::optional<Found> MatchFolder(const Profile& profile, const Listing& listing,
                                 const std::string& folder)
{
	const auto& files = listing.FilesIn(folder);
	const auto name  = [&](const size_t i) {
                return LastName((*listing.entries)[i].utf8_path);
	};
	Found found  = {};
	found.folder = folder;
	for (const auto& pattern : profile.files) {
		const auto it = std::find_if(files.begin(), files.end(), [&](const size_t i) {
			return PatternMatches(pattern, name(i)) &&
			       !AnyPatternMatches(profile.chained, name(i));
		});
		if (it != files.end()) {
			found.confs.push_back(*it);
			break;
		}
	}
	if (found.confs.empty()) {
		return {};
	}
	for (const auto& pattern : profile.chained) {
		const auto it = std::find_if(files.begin(), files.end(), [&](const size_t i) {
			return PatternMatches(pattern, name(i)) &&
			       std::find(found.confs.begin(), found.confs.end(), i) ==
			               found.confs.end();
		});
		if (it == files.end()) {
			return {};
		}
		found.confs.push_back(*it);
	}
	return found;
}

} // namespace

const Profile& BuiltInDefault()
{
	static const Profile profile = {
	        std::string(DefaultName),
	        true,
	        {"_DOSBOX", "."},
	        {"DOSBOX.CNF", "dosbox-automation.conf", "dosbox.conf"},
	        {}
        };
	return profile;
}

bool IsValidName(const std::string_view name)
{
	return !name.empty() && name.size() <= MaxNameBytes &&
	       std::all_of(name.begin(), name.end(), [](const char c) {
		       return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		              (c >= '0' && c <= '9') || c == '_' || c == '-';
	       });
}

bool IsPlainFileName(const std::string_view name)
{
	return !name.empty() && name.size() <= MaxPathBytes && name != "." &&
	       name != ".." &&
	       name.find_first_of("/\\:") == std::string_view::npos &&
	       !HasControlByte(name);
}

bool PathAllowed(const std::string_view path)
{
	if (path.empty() || path.size() > MaxPathBytes ||
	    HasControlByte(path) || path.find(':') != std::string_view::npos) {
		return false;
	}
	const auto segments = SplitPath(path);
	if (std::any_of(segments.begin(), segments.end(), [](const auto s) {
		    return s.empty();
	    })) {
		return false;
	}
	size_t i = 0;
	while (i < segments.size() && segments[i] == "..") {
		++i;
	}
	// `..` only with a minimum scope: `../game*`, not `../*`.
	if (i > 0 &&
	    (i == segments.size() ||
	     std::all_of(segments[i].begin(), segments[i].end(), IsWildcard))) {
		return false;
	}
	return std::none_of(segments.begin() + static_cast<std::ptrdiff_t>(i),
	                    segments.end(),
	                    [](const auto s) { return s == ".."; });
}

bool PatternMatches(const std::string_view pattern, const std::string_view name)
{
	if (name.find_first_of("/\\") != std::string_view::npos) {
		return false;
	}
	size_t p    = 0;
	size_t n    = 0;
	size_t star = std::string_view::npos;
	size_t mark = 0;
	while (n < name.size()) {
		if (p < pattern.size() && pattern[p] == '*') {
			star = p++;
			mark = n;
		} else if (p < pattern.size() &&
		           (pattern[p] == '?' || Fold(pattern[p]) == Fold(name[n]))) {
			++p;
			++n;
		} else if (star != std::string_view::npos) {
			p = star + 1;
			n = ++mark;
		} else {
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == '*') {
		++p;
	}
	return p == pattern.size();
}

ProfileFile ParseProfileFile(const std::string_view text, const std::string_view origin)
{
	ProfileFile file = {};
	const auto note  = [&](const Section& s, const std::string& what) {
                file.notes.push_back(std::string(origin) + " line " +
                                     std::to_string(s.first_line) + ": " + what);
	};
	for (const auto& section : SplitSections(text)) {
		if (section.label.empty()) {
			auto lines = std::string_view(section.text);
			bool stray = false;
			while (!lines.empty() && !stray) {
				const auto end = std::min(lines.find('\n'),
				                          lines.size());
				stray = !IsCommentOrBlank(lines.substr(0, end));
				lines.remove_prefix(std::min(end + 1, lines.size()));
			}
			if (stray) {
				note(section, "text before the first section ignored");
			}
			continue;
		}
		auto error      = std::string();
		const auto doc  = ArchiveToml::Parse(section.text, &error);
		error           = WithFileLine(error, section.first_line);
		const auto what = "section '" + section.label + "' skipped: ";
		if (!doc) {
			note(section, what + error);
			continue;
		}
		const auto& [name, table] = doc->arrays.front();
		if (name == GlobalName) {
			if (file.has_global) {
				note(section, what + "a second global section");
				continue;
			}
			file.has_global    = true;
			const auto unknown = std::find_if(table.pairs.begin(),
			                                  table.pairs.end(),
			                                  [](const auto& pair) {
				                                  return pair.first !=
				                                         KeyDefaultProfile;
			                                  });
			const auto value   = table.String(KeyDefaultProfile);
			if (unknown != table.pairs.end()) {
				note(section,
				     "global section ignored: unknown key '" +
				             unknown->first + "'");
			} else if (!value || !IsValidName(*value)) {
				note(section,
				     "global section ignored: default_profile must name a profile");
			} else {
				file.default_profile = *value;
			}
			continue;
		}
		if (name == DefaultName) {
			note(section,
			     what + "the built-in 'default' cannot be redefined");
			continue;
		}
		if (FindByName(file, name)) {
			note(section,
			     what + "profile '" + name + "' is defined twice");
			continue;
		}
		Profile profile = {};
		profile.name    = name;
		if (const auto why = ReadProfile(table, profile)) {
			note(section, what + *why);
			continue;
		}
		file.profiles.push_back(std::move(profile));
	}
	return file;
}

WorkingList BuildWorkingList(const ProfileFile& user, const ProfileFile& shipped)
{
	WorkingList list    = {};
	auto wanted         = std::string(DefaultName);
	bool wanted_by_user = false;
	if (user.has_global) {
		wanted_by_user = true;
		if (user.default_profile) {
			wanted = *user.default_profile;
		} else {
			list.user_default_invalid = true;
		}
	} else if (shipped.default_profile) {
		wanted = *shipped.default_profile;
	}
	const Profile* top = &BuiltInDefault();
	if (wanted != DefaultName) {
		const auto* found = FindByName(user, wanted);
		if (!found) {
			found = FindByName(shipped, wanted);
		}
		if (found && found->enabled) {
			top = found;
		} else {
			list.notes.push_back(
			        "default_profile '" + wanted +
			        "' names no enabled profile; the built-in default is used");
			list.user_default_invalid = list.user_default_invalid ||
			                            wanted_by_user;
		}
	}
	list.order.push_back(*top);
	const auto listed = [&](const std::string& name) {
		return std::any_of(list.order.begin(),
		                   list.order.end(),
		                   [&](const auto& p) { return p.name == name; });
	};
	for (const auto& p : user.profiles) {
		if (p.enabled && !listed(p.name)) {
			list.order.push_back(p);
		}
	}
	for (const auto& p : shipped.profiles) {
		if (FindByName(user, p.name)) {
			list.notes.push_back("shipped profile '" + p.name +
			                     "' is replaced by the user's");
		} else if (p.enabled && !listed(p.name)) {
			list.order.push_back(p);
		}
	}
	if (top != &BuiltInDefault()) {
		list.order.push_back(BuiltInDefault());
	}
	return list;
}

std::optional<Match> FindPackage(const std::vector<Profile>& order,
                                 const std::vector<ArchiveMount::Entry>& entries)
{
	const Listing listing(entries);
	for (const auto& profile : order) {
		for (const auto& path : profile.paths) {
			if (!PathAllowed(path) || SplitPath(path).front() == "..") {
				continue;
			}
			Match match       = {};
			match.profile     = profile.name;
			match.working_dir = profile.working_dir;
			for (const auto& folder : listing.Expand(path)) {
				if (auto found = MatchFolder(profile, listing, folder)) {
					match.games.push_back(std::move(*found));
				}
			}
			if (!match.games.empty()) {
				return match;
			}
		}
	}
	return {};
}

std::string DemoFileText()
{
	return "# Package profiles: where a game archive or folder keeps its DOSBox\n"
	       "# configuration. The profile named in default_profile is tried first,\n"
	       "# then every enabled profile from top to bottom, then the built-in\n"
	       "# 'default'. The first profile that matches fully is used.\n"
	       "\n"
	       "[[global]]\n"
	       "default_profile = \"default\"\n"
	       "\n"
	       "[[local_dosbox_conf]]\n"
	       "enabled = true\n"
	       "dosconfig_paths = [\".\"]\n"
	       "dosconfig_file = [\"dosbox.cnf\", \"dosbox-automation.conf\", \"dosbox.conf\"]\n";
}

std::string WithDefaultGlobal(const std::string_view text, const bool rewrite_existing)
{
	constexpr std::string_view DefaultLine = "default_profile = \"default\"";
	auto out        = std::string();
	bool in_global  = false;
	bool had_global = false;
	size_t pos      = 0;
	while (pos < text.size()) {
		auto end = text.find('\n', pos);
		if (end == std::string_view::npos) {
			end = text.size();
		}
		const auto line = text.substr(pos, end - pos);
		pos             = end + 1;
		if (IsHeaderLine(line)) {
			in_global = !had_global && RawHeaderName(line) == GlobalName;
			if (in_global) {
				had_global = true;
				if (rewrite_existing) {
					out += line;
					out += '\n';
					out += DefaultLine;
					out += '\n';
					continue;
				}
			}
		} else if (in_global && rewrite_existing && !IsCommentOrBlank(line)) {
			continue;
		}
		out += line;
		out += '\n';
	}
	if (!had_global) {
		return "[[global]]\n" + std::string(DefaultLine) + "\n\n" +
		       std::string(text);
	}
	if (!rewrite_existing) {
		return std::string(text);
	}
	return out;
}

WorkingList Load(const std::filesystem::path& user_file,
                 const std::filesystem::path& shipped_file)
{
	std::vector<std::string> notes = {};
	auto error                     = std::string();

	ProfileFile shipped     = {};
	const auto shipped_name = ArchiveMount::PathToUtf8(shipped_file);
	if (const auto text = ArchiveToml::ReadTextFile(shipped_file, &error)) {
		shipped = ParseProfileFile(*text, shipped_name);
	} else {
		notes.push_back("shipped profiles not read: " + error);
	}

	const auto user_name            = ArchiveMount::PathToUtf8(user_file);
	std::optional<std::string> text = {};
	std::error_code ec              = {};
	if (std::filesystem::symlink_status(user_file, ec).type() ==
	    std::filesystem::file_type::not_found) {
		text = DemoFileText();
		if (ArchiveToml::WriteTextAtomically(user_file, *text, &error)) {
			notes.push_back(user_name + ": created with a demo profile");
		} else {
			notes.push_back(user_name + ": not created: " + error);
		}
	} else {
		text = ArchiveToml::ReadTextFile(user_file, &error);
		if (!text) {
			notes.push_back("user profiles not read: " + error);
		}
	}

	auto user = text ? ParseProfileFile(*text, user_name) : ProfileFile{};
	auto list = BuildWorkingList(user, shipped);
	if (text && (!user.has_global || list.user_default_invalid)) {
		// A global header whose section did not parse is written over too.
		const auto repaired = WithDefaultGlobal(*text, true);
		if (ArchiveToml::WriteTextAtomically(user_file, repaired, &error)) {
			notes.push_back(user_name +
			                ": default_profile = \"default\" written into the global section");
		} else {
			notes.push_back(user_name +
			                ": global section not written: " + error);
		}
		// The list follows the repaired text even when the write
		// failed; the notes of the first pass say why it was repaired.
		user        = ParseProfileFile(repaired, user_name);
		auto again  = BuildWorkingList(user, shipped);
		again.notes = std::move(list.notes);
		list        = std::move(again);
	}
	notes.insert(notes.end(), user.notes.begin(), user.notes.end());
	notes.insert(notes.end(), shipped.notes.begin(), shipped.notes.end());
	notes.insert(notes.end(), list.notes.begin(), list.notes.end());
	list.notes = std::move(notes);
	return list;
}

} // namespace PackageProfile
