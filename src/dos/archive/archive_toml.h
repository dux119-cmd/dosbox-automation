// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_ARCHIVE_TOML_H
#define DOSBOX_ARCHIVE_TOML_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The TOML subset the sidecar, cache and package profile files use: bare
// keys, basic and literal one-line strings, integers, booleans, arrays of
// strings, [[table]] arrays; anything else is an error (ada-xke9, ada-52dg).
namespace ArchiveToml {

constexpr size_t MaxDocumentBytes = 1024 * 1024;
constexpr int MaxDocumentLines    = 100000;
constexpr size_t MaxKeyBytes      = 64;
// For an array, the cap covers all its items together.
constexpr size_t MaxValueBytes = 64 * 1024;

struct Value {
	enum class Kind { String, Integer, Boolean, StringArray };
	Kind kind                      = Kind::String;
	std::string text               = {};
	int64_t integer                = 0;
	bool boolean                   = false;
	std::vector<std::string> items = {};
};

struct Table {
	std::vector<std::pair<std::string, Value>> pairs = {};

	const Value* Find(std::string_view key) const;
	std::optional<std::string> String(std::string_view key) const;
	std::optional<int64_t> Integer(std::string_view key) const;
	std::optional<bool> Boolean(std::string_view key) const;
	std::optional<std::vector<std::string>> Strings(std::string_view key) const;

	// Replaces the value of an existing key in place, else appends. The
	// const char* overload exists because a literal would otherwise
	// convert to bool; pass integers as int64_t for the same reason.
	void Set(std::string_view key, std::string_view text);
	void Set(std::string_view key, const char* text);
	void Set(std::string_view key, int64_t integer);
	void Set(std::string_view key, bool boolean);
	void Set(std::string_view key, std::vector<std::string> items);
};

struct Document {
	Table root = {};
	// Every [[name]] table in file order, with its name.
	std::vector<std::pair<std::string, Table>> arrays = {};

	std::vector<const Table*> Array(std::string_view name) const;
};

std::optional<Document> Parse(std::string_view text, std::string* error);

// Root pairs in order, then each array table under its header. Strings
// are quoted with every control byte, quote and backslash escaped.
std::string Serialize(const Document& document);

// Refuses anything that is not a regular file under MaxDocumentBytes.
std::optional<std::string> ReadTextFile(const std::filesystem::path& path,
                                        std::string* error);
std::optional<Document> ReadFile(const std::filesystem::path& path,
                                 std::string* error);

// A temporary in the same directory (0600, created exclusively), then a
// rename over the target, so a reader never sees half a file.
bool WriteTextAtomically(const std::filesystem::path& path,
                         std::string_view text, std::string* error);
bool WriteAtomically(const std::filesystem::path& path,
                     const Document& document, std::string* error);

} // namespace ArchiveToml

#endif // DOSBOX_ARCHIVE_TOML_H
