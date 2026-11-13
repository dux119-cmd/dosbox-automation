// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_HARDWARE_GUEST_MEMORY_H
#define DOSBOX_HARDWARE_GUEST_MEMORY_H

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "hardware/memory.h"

// Guest memory as the host side (API, Lua, freezes) reaches it, by linear
// address. A page the guest's tables do not grant is reported, never raised
// as a page fault in the guest (ada-q50n); the error is the first refused.
namespace GuestMemory {

struct Range {
	PhysPt start = 0;
	// One past the last byte.
	PhysPt end = 0;

	bool operator==(const Range&) const = default;
};

// One past the highest linear address a caller may name: the RAM size with
// paging off; with paging on the whole 4 GB, and the page tables decide.
uint64_t AddressLimit();

std::expected<void, PhysPt> CheckRange(PhysPt start, size_t len, bool writing);

std::expected<void, PhysPt> Read(PhysPt start, std::span<uint8_t> out);

// Writes nothing unless every page of the range accepts the write.
std::expected<void, PhysPt> Write(PhysPt start, std::span<const uint8_t> data);

// Fills `out` from every readable page and zeroes the rest; returns the
// skipped ranges, merged and clipped to the request.
std::expected<std::vector<Range>, PhysPt> ReadMapped(PhysPt start,
                                                     std::span<uint8_t> out);

} // namespace GuestMemory

#endif
