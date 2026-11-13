// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver.h"
#include "bridge.h"
#include "private/memory.h"

#include <algorithm>
#include <span>

#include "augra/log.h"
#include "base64/base64.h"
#include "http/http.h"
#include "nlohmann/json.hpp"
#include "utils/string_utils.h"

#include "cpu/registers.h"
#include "dos/dos_memory.h"
#include "hardware/guest_memory.h"
#include "hardware/memory.h"

using json = nlohmann::json;
using httplib::Request, httplib::Response;

namespace Webserver {

static Segment str_to_base_segment(const std::string_view str)
{
	auto val = upcase(str);
	static const std::unordered_map<std::string_view, Segment> lookup = {
	        {"CS", Segment::CS},
	        {"SS", Segment::SS},
	        {"DS", Segment::DS},
	        {"ES", Segment::ES},
	        {"FS", Segment::FS},
	        {"GS", Segment::GS},
	};

	if (auto it = lookup.find(val); it != lookup.end()) {
		return it->second;
	} else {
		return Segment::None;
	}
}

static uint32_t base_segment_to_offset(const Segment segment)
{
	switch (segment) {
	case Segment::CS: return SegPhys(SegNames::cs);
	case Segment::SS: return SegPhys(SegNames::ss);
	case Segment::DS: return SegPhys(SegNames::ds);
	case Segment::ES: return SegPhys(SegNames::es);
	case Segment::FS: return SegPhys(SegNames::fs);
	case Segment::GS: return SegPhys(SegNames::gs);
	case Segment::None: return 0;
	default: return 0;
	}
}

static void parse_mem_addr(const httplib::Request& req, Segment& segment,
                           uint32_t& offset)
{
	offset  = num_param<uint32_t>(req, Source::Path, "offset");
	segment = Segment::None;

	if (req.path_params.find("segment") != req.path_params.end()) {
		auto& segment_param = req.path_params.at("segment");
		segment             = str_to_base_segment(segment_param);

		// Segment can either be a register to resolve later or an
		// address which we can already resolve here.
		if (segment == Segment::None) {
			const auto seg_addr = PhysicalMake(
			        num_param<uint16_t>(req, Source::Path, "segment"), 0);

			offset += seg_addr;
		}
	}
}

static std::string RangeError(const uint32_t start, const uint64_t len)
{
	const auto limit = GuestMemory::AddressLimit();
	if (start + len <= limit) {
		return {};
	}
	const auto range = format_str("Address range 0x%x + %llu",
	                              start,
	                              static_cast<unsigned long long>(len));
	if (limit == uint64_t{1} << 32) {
		return range + " exceeds the 4 GB linear address space";
	}
	return range + format_str(" exceeds emulated memory size (%llu bytes)",
	                          static_cast<unsigned long long>(limit));
}

void ReadMemoryCommand::Execute()
{
	regs.load();
	effective_addr = base_segment_to_offset(base) + offset;

	augra::log_debug("webserver", "ReadMemoryCommand(0x%06x, %u)", effective_addr, len);

	error = RangeError(effective_addr, len);
	if (!error.empty()) {
		return;
	}

	memory.resize(len);
	const auto read = GuestMemory::Read(effective_addr,
	                                    {reinterpret_cast<uint8_t*>(memory.data()),
	                                     memory.size()});
	if (!read) {
		not_mapped_at = read.error();
		memory.clear();
	}
}

void ReadMemoryCommand::Get(const Request& req, Response& res)
{
	// 128 MiB per request ought to be enough for everyone.
	// This limit just prevents bad things when accidentally requesting an
	// unreasonably large size.
	auto num_bytes = num_param<uint32_t>(req, Source::Path, "len", 1, 128 * 1024 * 1024);

	Segment segment;
	uint32_t offset;
	parse_mem_addr(req, segment, offset);

	ReadMemoryCommand cmd(segment, offset, num_bytes);
	cmd.WaitForCompletion();

	if (!cmd.error.empty()) {
		throw std::out_of_range(cmd.error);
	}
	if (cmd.not_mapped_at) {
		throw GuestPageNotMapped(*cmd.not_mapped_at);
	}

	if (req.get_header_value("accept").starts_with(TypeJson)) {
		json j;
		j["registers"]      = cmd.regs;
		j["memory"]["addr"] = cmd.effective_addr;
		j["memory"]["data"] = base64::to_base64(cmd.memory);

		send_json(res, j);

	} else {
		// Only do base64 if explicitly requested, binary/download by
		// default.
		res.set_header("Content-Disposition",
		               "attachment; filename =\"memory.bin\"");
		res.set_content(cmd.memory, TypeBinary);
	}
}

void WriteMemoryCommand::Execute()
{
	effective_addr = base_segment_to_offset(base) + offset;

	augra::log_debug("webserver",
	                 "WriteMemoryCommand(0x%06x, %zu)",
	                 effective_addr,
	                 data.size());

	error = RangeError(effective_addr, data.size());
	if (!error.empty()) {
		return;
	}

	const auto bytes = [](std::string& s) {
		return std::span<uint8_t>(reinterpret_cast<uint8_t*>(s.data()),
		                          s.size());
	};

	if (const auto writable = GuestMemory::CheckRange(effective_addr,
	                                                  data.size(),
	                                                  true);
	    !writable) {
		not_mapped_at = writable.error();
		return;
	}

	if (!expected_data.empty()) {
		conflict_data.resize(expected_data.size());

		if (const auto read = GuestMemory::Read(effective_addr,
		                                        bytes(conflict_data));
		    !read) {
			not_mapped_at = read.error();
			conflict_data.clear();
			return;
		}

		if (expected_data != conflict_data) {
			return;
		}

		conflict_data.clear();
	}

	if (const auto written = GuestMemory::Write(effective_addr, bytes(data));
	    !written) {
		not_mapped_at = written.error();
	}
}

void WriteMemoryCommand::Put(const httplib::Request& req, httplib::Response& res)
{
	Segment segment;
	uint32_t offset;
	parse_mem_addr(req, segment, offset);

	constexpr size_t MaxWriteBytes = 128 * 1024 * 1024; // 128 MiB

	std::string data;

	if (req.get_header_value("Content-Type") == TypeJson) {
		auto j = json::parse(req.body);
		data   = base64::from_base64(j.at("data").get<std::string>());

	} else if (req.get_header_value("Content-Type") == TypeBinary) {
		data = req.body;

	} else {
		throw std::invalid_argument("Content-Type must be either " +
		                            std::string(TypeJson) + " or " +
		                            std::string(TypeBinary));
	}

	if (data.size() > MaxWriteBytes) {
		throw std::invalid_argument(
		        "Write data exceeds maximum size of 128 MiB");
	}

	std::string expected_data;

	if (req.has_header("If-Match")) {
		// The standard requires ETags in this and ETags are quoted
		// but we accept unquoted because no one is gonna bother
		std::string etag_hdr  = req.get_header_value("If-Match");
		std::string_view etag = etag_hdr;
		if (etag.starts_with('"') && etag.ends_with('"')) {
			etag.remove_prefix(1);
			etag.remove_suffix(1);
		}
		expected_data = base64::from_base64(etag);
	}

	WriteMemoryCommand cmd(segment, offset, std::move(data), std::move(expected_data));
	cmd.WaitForCompletion();

	if (!cmd.error.empty()) {
		throw std::out_of_range(cmd.error);
	}
	if (cmd.not_mapped_at) {
		throw GuestPageNotMapped(*cmd.not_mapped_at);
	}

	json j;
	j["memory"]["addr"] = cmd.effective_addr;
	if (!cmd.conflict_data.empty()) {
		res.status = httplib::StatusCode::PreconditionFailed_412;
		j["memory"]["data"] = base64::to_base64(cmd.conflict_data);
	}

	send_json(res, j);
}

// --- Memory value scan ---

std::vector<uint32_t> ScanBufferForValue(const std::span<const uint8_t> buf,
                                         const uint32_t value, const int width)
{
	if (width != 1 && width != 2 && width != 4) {
		throw std::invalid_argument("width must be 1, 2, or 4");
	}
	std::vector<uint32_t> hits;
	if (buf.size() < static_cast<size_t>(width)) {
		return hits;
	}
	for (uint32_t i = 0; i + width <= buf.size(); ++i) {
		uint32_t v = 0;
		for (int b = 0; b < width; ++b) {
			v |= static_cast<uint32_t>(buf[i + b]) << (8 * b);
		}
		if (v == value) {
			hits.push_back(i);
		}
	}
	return hits;
}

std::vector<uint32_t> ScanMappedForValue(const std::span<const uint8_t> buf,
                                         const uint32_t base,
                                         const std::vector<GuestMemory::Range>& skipped,
                                         const uint32_t value, const int width)
{
	if (width != 1 && width != 2 && width != 4) {
		throw std::invalid_argument("width must be 1, 2, or 4");
	}
	const auto offset_of = [&](const PhysPt address) {
		const auto offset = int64_t{address} - int64_t{base};
		return static_cast<size_t>(std::clamp<int64_t>(
		        offset, 0, static_cast<int64_t>(buf.size())));
	};

	std::vector<uint32_t> hits = {};
	size_t run_start           = 0;

	const auto scan_run = [&](const size_t run_end) {
		if (run_end <= run_start) {
			return;
		}
		const auto run = buf.subspan(run_start, run_end - run_start);
		for (const auto hit : ScanBufferForValue(run, value, width)) {
			hits.push_back(base + static_cast<uint32_t>(run_start) + hit);
		}
	};
	for (const auto& hole : skipped) {
		scan_run(offset_of(hole.start));
		run_start = std::max(run_start, offset_of(hole.end));
	}
	scan_run(buf.size());
	return hits;
}

void SearchMemoryCommand::Execute()
{
	if (end > GuestMemory::AddressLimit() || start >= end) {
		error = "Invalid search range";
		return;
	}
	std::vector<uint8_t> buf(end - start);
	const auto mapped = GuestMemory::ReadMapped(start, buf);
	if (!mapped) {
		error = "Invalid search range";
		return;
	}
	skipped = *mapped;
	matches = ScanMappedForValue(buf, start, skipped, value, width);
}

void SearchMemoryCommand::Post(const Request& req, Response& res)
{
	auto j = json::parse(req.body);

	const uint32_t value = j.at("value").get<uint32_t>();
	const int width      = j.value("width", 1);
	const uint32_t start = j.at("start").get<uint32_t>();
	const uint32_t end   = j.at("end").get<uint32_t>();

	if (width != 1 && width != 2 && width != 4) {
		throw std::invalid_argument("width must be 1, 2, or 4");
	}

	constexpr uint32_t MaxSpan = 16 * 1024 * 1024;
	if (end <= start || end - start > MaxSpan) {
		throw std::invalid_argument("search span must be 1.." +
		                            std::to_string(MaxSpan) + " bytes");
	}

	SearchMemoryCommand cmd(start, end, value, width);
	cmd.WaitForCompletion(2000);
	if (!cmd.error.empty()) {
		throw std::out_of_range(cmd.error);
	}

	json result;
	result["matches"] = cmd.Matches();
	result["skipped"] = json::array();
	for (const auto& range : cmd.Skipped()) {
		json skipped;
		skipped["start"] = range.start;
		skipped["end"]   = range.end;
		result["skipped"].push_back(skipped);
	}
	send_json(res, result);
}

} // namespace Webserver
