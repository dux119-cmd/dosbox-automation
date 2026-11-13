// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/memory.h"

#include "hardware/guest_memory.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

using GuestMemory::Range;
using Webserver::ScanBufferForValue;
using Webserver::ScanMappedForValue;

namespace {

TEST(MemSearch, FindsByteMatches)
{
	std::vector<uint8_t> buf = {0x01, 0x63, 0x02, 0x63, 0x63};
	auto hits = ScanBufferForValue(buf, 0x63, 1);
	EXPECT_EQ(hits, (std::vector<uint32_t>{1, 3, 4}));
}

TEST(MemSearch, FindsWordMatchesLittleEndian)
{
	std::vector<uint8_t> buf = {0x63, 0x00, 0xFF, 0x63, 0x00};
	auto hits = ScanBufferForValue(buf, 0x0063, 2);
	EXPECT_EQ(hits, (std::vector<uint32_t>{0, 3}));
}

TEST(MemSearch, FindsDwordMatch)
{
	std::vector<uint8_t> buf = {0x78, 0x56, 0x34, 0x12, 0x00};
	auto hits = ScanBufferForValue(buf, 0x12345678, 4);
	EXPECT_EQ(hits, (std::vector<uint32_t>{0}));
}

TEST(MemSearch, NoMatchReturnsEmpty)
{
	std::vector<uint8_t> buf = {0x01, 0x02, 0x03};
	auto hits = ScanBufferForValue(buf, 0xFF, 1);
	EXPECT_TRUE(hits.empty());
}

TEST(MemSearch, RejectsBadWidth)
{
	std::vector<uint8_t> buf = {0x00};
	EXPECT_THROW(ScanBufferForValue(buf, 0, 3), std::invalid_argument);
}

TEST(MemSearch, BufferSmallerThanWidthReturnsEmpty)
{
	std::vector<uint8_t> buf = {0x63};
	auto hits = ScanBufferForValue(buf, 0x0063, 2);
	EXPECT_TRUE(hits.empty());
}

TEST(MemSearch, EmptyBufferReturnsEmpty)
{
	std::vector<uint8_t> buf;
	auto hits = ScanBufferForValue(buf, 0, 1);
	EXPECT_TRUE(hits.empty());
}

TEST(MemSearch, MappedScanWithoutHolesReturnsAddresses)
{
	std::vector<uint8_t> buf = {0x63, 0x00, 0x63};
	auto hits = ScanMappedForValue(buf, 0x1000, {}, 0x63, 1);
	EXPECT_EQ(hits, (std::vector<uint32_t>{0x1000, 0x1002}));
}

// ada-q50n: holes read as zeros; nothing may match in them or across them.
TEST(MemSearch, MappedScanIgnoresHolesAndTheirEdges)
{
	std::vector<uint8_t> buf = {0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x63, 0x00};
	const std::vector<Range> holes(1, Range{0x1002, 0x1006});

	EXPECT_EQ(ScanMappedForValue(buf, 0x1000, holes, 0x0063, 2),
	          (std::vector<uint32_t>{0x1006}));
	EXPECT_EQ(ScanMappedForValue(buf, 0x1000, holes, 0x00, 1),
	          (std::vector<uint32_t>{0x1000, 0x1007}));
}

TEST(MemSearch, MappedScanOfOnlyAHoleIsEmpty)
{
	std::vector<uint8_t> buf(16);
	const std::vector<Range> everything(1, Range{0x1000, 0x1010});
	EXPECT_TRUE(ScanMappedForValue(buf, 0x1000, everything, 0, 1).empty());
}

} // namespace
