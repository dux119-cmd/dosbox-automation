// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver.h"
#include "bridge.h"
#include "private/dos.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "augra/log.h"
#include "http/http.h"
#include "nlohmann/json.hpp"

#include "cpu/paging.h"
#include "cpu/registers.h"
#include "dos/dos.h"
#include "dos/dos_memory.h"
#include "hardware/guest_memory.h"
#include "utils/string_utils.h"

using json = nlohmann::json;

namespace Webserver {

constexpr int DosBlockSize = 16;

// A DOS allocation counts paragraphs in 16 bits.
constexpr uint32_t MaxDosAllocBytes = 0xffff * DosBlockSize;

std::vector<McbBlock> WalkMcbChain(const uint16_t start_segment,
                                   const McbReader& reader,
                                   const int max_blocks)
{
	std::vector<McbBlock> chain;
	uint16_t seg = start_segment;

	for (int i = 0; i < max_blocks; ++i) {
		auto block = reader(seg);
		block.segment = seg;

		if (block.type == 0x5A) {
			block.is_last = true;
			chain.push_back(block);
			break;
		}
		if (block.type != 0x4D) {
			break;
		}

		chain.push_back(block);
		seg = seg + block.size_paras + 1;
	}
	return chain;
}

static McbBlock ReadMcbFromGuest(const uint16_t segment)
{
	DOS_MCB mcb(segment);
	McbBlock block = {};
	block.segment     = segment;
	block.type        = mcb.GetType();
	block.psp_segment = mcb.GetPSPSeg();
	block.size_paras  = mcb.GetSize();

	char name[9] = {};
	mcb.GetFileName(name);
	block.filename = name;
	return block;
}

// Every header a walk from `start` reads, a trashed chain's included; for the
// allocator also each free block, which it splits by writing an MCB inside.
static std::optional<PhysPt> UnmappedInChain(const uint16_t start, const bool writing)
{
	std::optional<PhysPt> unmapped = {};
	const auto checked_reader = [&](const uint16_t segment) {
		McbBlock block = {};
		const auto header = GuestMemory::CheckRange(PhysicalMake(segment, 0),
		                                            DosBlockSize,
		                                            writing);
		if (!header) {
			unmapped = header.error();
			return block;
		}
		block = ReadMcbFromGuest(segment);
		if (writing && block.psp_segment == 0 && block.size_paras > 0) {
			const auto inside = GuestMemory::CheckRange(
			        PhysicalMake(segment, 0) + DosBlockSize,
			        uint32_t{block.size_paras} * DosBlockSize,
			        true);
			if (!inside) {
				unmapped   = inside.error();
				block.type = 0;
			}
		}
		return block;
	};
	WalkMcbChain(start, checked_reader, 1000);
	return unmapped;
}

// DOS keeps its own structures in conventional memory and, when linked, in
// the UMB chain; a guest with paging on may leave pages of them out.
static std::optional<PhysPt> UnmappedDosMemory(const bool writing,
                                               const std::optional<uint16_t> target_mcb = {})
{
	constexpr PhysPt ConventionalEnd = 0xa0000;
	if (const auto conv = GuestMemory::CheckRange(0, ConventionalEnd, writing);
	    !conv) {
		return conv.error();
	}
	if (const auto conv = UnmappedInChain(dos.firstMCB, writing); conv) {
		return conv;
	}
	if (const auto umb_chain = dos_infoblock.GetStartOfUMBChain();
	    umb_chain != 0xffff) {
		if (const auto umb = UnmappedInChain(umb_chain, writing); umb) {
			return umb;
		}
	}
	const auto psp_mcb = static_cast<uint16_t>(dos.psp() - 1);
	for (const auto segment : {std::optional<uint16_t>(psp_mcb), target_mcb}) {
		if (!segment) {
			continue;
		}
		const auto header = GuestMemory::CheckRange(PhysicalMake(*segment, 0),
		                                            DosBlockSize,
		                                            writing);
		if (!header) {
			return header.error();
		}
	}
	return {};
}

void DosInternalsCommand::Execute()
{
	not_mapped_at = UnmappedDosMemory(false);
	if (not_mapped_at) {
		return;
	}
	list_of_lists      = RealToPhysical(dos_infoblock.GetPointer());
	dos_swappable_area = PhysicalMake(DOS_SDA_SEG, DOS_SDA_OFS);
	first_shell        = PhysicalMake(DOS_FIRST_SHELL, 0);

	memory_map = WalkMcbChain(dos.firstMCB, ReadMcbFromGuest, 1000);

	augra::log_debug("webserver", "DosInternalsCommand()");
}

void DosInternalsCommand::Get(const httplib::Request&, httplib::Response& res)
{
	DosInternalsCommand cmd;
	cmd.WaitForCompletion();
	if (cmd.not_mapped_at) {
		throw GuestPageNotMapped(*cmd.not_mapped_at);
	}

	json j;
	j["listOfLists"]      = cmd.list_of_lists;
	j["dosSwappableArea"] = cmd.dos_swappable_area;
	j["firstShell"]       = cmd.first_shell;

	json map = json::array();
	for (const auto& b : cmd.memory_map) {
		json entry;
		entry["segment"]    = b.segment;
		entry["type"]       = b.type;
		entry["pspSegment"] = b.psp_segment;
		entry["sizeParas"]  = b.size_paras;
		entry["sizeBytes"]  = static_cast<uint32_t>(b.size_paras) * 16;
		entry["filename"]   = b.filename;
		entry["isLast"]     = b.is_last;
		map.push_back(entry);
	}
	j["memoryMap"] = map;

	send_json(res, j);
}

void AllocMemoryCommand::AllocDos()
{
	assert(bytes <= MaxDosAllocBytes);
	auto blocks = static_cast<uint16_t>((bytes + DosBlockSize - 1) / DosBlockSize);
	auto old_strategy = DOS_GetMemAllocStrategy();
	uint16_t segment  = 0;

	uint16_t new_strategy = 0;

	switch (area) {
	case MemoryArea::Conv:
		switch (strategy) {
		case AllocStrategy::FirstFit:
			new_strategy = DosMemAllocStrategy::LowMemoryFirstFit;
			break;
		case AllocStrategy::BestFit:
			new_strategy = DosMemAllocStrategy::LowMemoryBestFit;
			break;
		case AllocStrategy::LastFit:
			new_strategy = DosMemAllocStrategy::LowMemoryLastFit;
			break;
		default: assertm(false, "Invalid alloc strategy"); break;
		}
		break;

	case MemoryArea::Uma:
		switch (strategy) {
		case AllocStrategy::FirstFit:
			new_strategy = DosMemAllocStrategy::UmbMemoryFirstFit;
			break;
		case AllocStrategy::BestFit:
			new_strategy = DosMemAllocStrategy::UmbMemoryBestFit;
			break;
		case AllocStrategy::LastFit:
			new_strategy = DosMemAllocStrategy::UmbMemoryLastFit;
			break;
		default: assertm(false, "Invalid alloc strategy"); break;
		}
		break;
	default: assertm(false, "Invalid memory area"); break;
	}

	DOS_SetMemAllocStrategy(new_strategy);

	auto ok = DOS_AllocateMemory(&segment, &blocks);
	addr    = PhysicalMake(segment, 0);

	augra::log_debug("webserver",
	                 "AllocMemoryCommand(%u): result=%d, %d bytes at 0x%06x (DOS allocator)",
	                 bytes,
	                 ok,
	                 blocks * DosBlockSize,
	                 addr);

	DOS_SetMemAllocStrategy(old_strategy);

	if (!ok) {
		addr = 0;
	} else if (static_cast<uint32_t>(blocks) * DosBlockSize < bytes) {
		DOS_FreeMemory(segment);
		addr = 0;
	}
}

void AllocMemoryCommand::AllocXms()
{
	const auto num_pages = (uint64_t{bytes} + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;
	const auto handle = MEM_AllocatePages(static_cast<Bitu>(num_pages), true);

	// Returns 0 on error or out of memory, nullptr is handled as error below.
	addr = handle * MEM_PAGE_SIZE;
	augra::log_debug("webserver",
	                 "AllocMemoryCommand(%u), handle=%d: %llu bytes at 0x%06x (XMS/page allocator)",
	                 bytes,
	                 handle,
	                 static_cast<unsigned long long>(num_pages * MEM_PAGE_SIZE),
	                 addr);
}

void AllocMemoryCommand::Execute()
{
	if (area < MemoryArea::Xms) {
		not_mapped_at = UnmappedDosMemory(true);
		if (not_mapped_at) {
			return;
		}
		AllocDos();
	} else {
		AllocXms();
	}
}

// What the area can never serve is a client error; a lack of room is not.
static uint32_t alloc_size(const json& value, const MemoryArea area)
{
	if (!value.is_number_integer()) {
		throw std::invalid_argument("size must be an integer");
	}
	const uint64_t max_bytes = (area == MemoryArea::Xms)
	                                 ? std::numeric_limits<uint32_t>::max()
	                                 : MaxDosAllocBytes;
	if (!value.is_number_unsigned() || value.get<uint64_t>() == 0 ||
	    value.get<uint64_t>() > max_bytes) {
		throw std::invalid_argument("size out of range for the memory area");
	}
	return static_cast<uint32_t>(value.get<uint64_t>());
}

void AllocMemoryCommand::Post(const httplib::Request& req, httplib::Response& res)
{
	auto j = json::parse(req.body);

	const auto area = [&]() {
		using enum MemoryArea;

		if (j.contains("area")) {
			std::string req_area = j["area"];
			upcase(req_area);

			if (req_area == "CONV") {
				return Conv;
			} else if (req_area == "UMA") {
				return Uma;
			} else if (req_area == "XMS") {
				return Xms;
			} else {
				throw std::invalid_argument(
				        "Invalid memory area: " + req_area);
			}
		} else {
			return Conv;
		}
	}();

	const auto strategy = [&]() {
		using enum AllocStrategy;

		if (j.contains("strategy")) {
			std::string req_strategy = j["strategy"];
			upcase(req_strategy);

			if (req_strategy == "FIRST_FIT") {
				return FirstFit;
			} else if (req_strategy == "BEST_FIT") {
				return BestFit;
			} else if (req_strategy == "LAST_FIT") {
				return LastFit;
			} else {
				throw std::invalid_argument(
				        "Invalid alloc strategy: " + req_strategy);
			}
		} else {
			return BestFit;
		}
	}();

	if (area == MemoryArea::Xms && strategy != AllocStrategy::BestFit) {
		throw std::invalid_argument("XMS allocator only supports best_fit");
	}
	const auto size = alloc_size(j.at("size"), area);

	AllocMemoryCommand cmd(size, area, strategy);
	cmd.WaitForCompletion();
	if (cmd.not_mapped_at) {
		throw GuestPageNotMapped(*cmd.not_mapped_at);
	}

	if (cmd.addr) {
		json j;
		j["addr"] = cmd.addr;
		send_json(res, j);
	} else {
		res.status = httplib::StatusCode::ServiceUnavailable_503;
	}
}

void FreeMemoryCommand::Execute()
{
	constexpr uint32_t DosSegmentsEnd = 0x10000 * DosBlockSize;
	if (addr < XMS_START * MEM_PAGE_SIZE) {
		const auto segment = addr / DosBlockSize;
		if (addr >= DosSegmentsEnd || segment == 0) {
			success = false;
			return;
		}
		not_mapped_at = UnmappedDosMemory(true,
		                                  static_cast<uint16_t>(segment - 1));
		if (not_mapped_at) {
			return;
		}
		success = DOS_FreeMemory(static_cast<uint16_t>(segment));
		augra::log_debug("webserver",
		                 "FreeMemoryCommand(0x%06x): success=%d (DOS allocator)",
		                 addr,
		                 success);
	} else {
		auto free_before = MEM_FreeTotal();
		MEM_ReleasePages(addr / MEM_PAGE_SIZE);

		auto released = static_cast<int64_t>(MEM_FreeTotal()) - free_before;
		success = released > 0;

		augra::log_debug("webserver",
		                 "FreeMemoryCommand(0x%06x): released=%lld (page allocator)",
		                 addr,
		                 static_cast<long long>(released));
	}
}

void FreeMemoryCommand::Post(const httplib::Request& req, httplib::Response& res)
{
	auto j        = json::parse(req.body);
	uint32_t addr = j.at("addr");

	FreeMemoryCommand cmd(addr);
	cmd.WaitForCompletion();
	if (cmd.not_mapped_at) {
		throw GuestPageNotMapped(*cmd.not_mapped_at);
	}

	if (!cmd.success) {
		res.status = httplib::StatusCode::BadRequest_400;
	}
}

} // namespace Webserver
