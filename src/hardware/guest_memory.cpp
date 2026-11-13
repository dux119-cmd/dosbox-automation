// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "hardware/guest_memory.h"

#include <algorithm>

#include "cpu/paging.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace GuestMemory {

namespace {

constexpr uint64_t AddressSpaceEnd = uint64_t{1} << 32;

bool FitsAddressSpace(const PhysPt start, const size_t len)
{
	return static_cast<uint64_t>(start) + len <= AddressSpaceEnd;
}

uint64_t PageOf(const uint64_t address)
{
	return address & ~static_cast<uint64_t>(MemPageSize - 1);
}

} // namespace

uint64_t AddressLimit()
{
	return PAGING_Enabled()
	             ? AddressSpaceEnd
	             : static_cast<uint64_t>(MEM_TotalPages()) * MemPageSize;
}

Expected<void, PhysPt> CheckRange(const PhysPt start, const size_t len,
                                  const bool writing)
{
	if (!FitsAddressSpace(start, len)) {
		return MakeUnexpected(start);
	}
	if (len == 0) {
		return {};
	}
	const auto end = static_cast<uint64_t>(start) + len;
	for (auto page = PageOf(start); page < end; page += MemPageSize) {
		if (!PAGING_HostCanAccess(static_cast<PhysPt>(page), writing)) {
			const auto refused = std::max<uint64_t>(page, start);
			return MakeUnexpected(static_cast<PhysPt>(refused));
		}
	}
	return {};
}

Expected<void, PhysPt> Read(const PhysPt start, const std::span<uint8_t> out)
{
	if (const auto checked = CheckRange(start, out.size(), false); !checked) {
		return checked;
	}
	MEM_BlockRead(start, out.data(), out.size());
	return {};
}

Expected<void, PhysPt> Write(const PhysPt start, const std::span<const uint8_t> data)
{
	if (const auto checked = CheckRange(start, data.size(), true); !checked) {
		return checked;
	}
	MEM_BlockWrite(start, data.data(), data.size());
	return {};
}

Expected<std::vector<Range>, PhysPt> ReadMapped(const PhysPt start,
                                                const std::span<uint8_t> out)
{
	// Range::end is exclusive and 32 bits wide, so the last byte stays out.
	if (static_cast<uint64_t>(start) + out.size() >= AddressSpaceEnd) {
		return MakeUnexpected(start);
	}
	std::ranges::fill(out, uint8_t{0});

	std::vector<Range> skipped = {};
	const auto end             = static_cast<uint64_t>(start) + out.size();

	for (uint64_t chunk = start; chunk < end;) {
		const auto chunk_end = std::min(PageOf(chunk) + MemPageSize, end);
		const auto first = static_cast<PhysPt>(chunk);

		if (PAGING_HostCanAccess(first, false)) {
			MEM_BlockRead(first,
			              out.data() + (chunk - start),
			              static_cast<size_t>(chunk_end - chunk));
		} else if (!skipped.empty() && skipped.back().end == first) {
			skipped.back().end = static_cast<PhysPt>(chunk_end);
		} else {
			skipped.push_back({first, static_cast<PhysPt>(chunk_end)});
		}
		chunk = chunk_end;
	}
	return skipped;
}

} // namespace GuestMemory
