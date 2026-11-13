// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "hardware/guest_memory.h"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "cpu/cpu.h"
#include "cpu/paging.h"
#include "hardware/memory.h"
#include "paging_test_fixture.h"

namespace {

using GuestMemoryTest = PagingTestFixture;

TEST_F(GuestMemoryTest, TheAddressLimitIsTheRamWithoutPagingAndFourGigabytesWithIt)
{
	EXPECT_EQ(GuestMemory::AddressLimit(),
	          static_cast<uint64_t>(MEM_TotalPages()) * MemPageSize);
	EnablePaging();
	EXPECT_EQ(GuestMemory::AddressLimit(), uint64_t{1} << 32);
}

TEST_F(GuestMemoryTest, EverythingIsAccessibleWithoutPaging)
{
	EXPECT_TRUE(PAGING_HostCanAccess(Hole, false));
	EXPECT_TRUE(PAGING_HostCanAccess(Hole, true));
	EXPECT_TRUE(PAGING_HostCanAccess(NoTable, true));
}

TEST_F(GuestMemoryTest, PresentPagesAreAccessible)
{
	EnablePaging();
	EXPECT_TRUE(PAGING_HostCanAccess(0x100000, false));
	EXPECT_TRUE(PAGING_HostCanAccess(0x100000, true));
	EXPECT_TRUE(PAGING_HostCanAccess(Hole - 1, true));
	EXPECT_TRUE(PAGING_HostCanAccess(Hole + 0x1000, false));
}

TEST_F(GuestMemoryTest, MissingTableEntryIsNotAccessible)
{
	EnablePaging();
	EXPECT_FALSE(PAGING_HostCanAccess(Hole, false));
	EXPECT_FALSE(PAGING_HostCanAccess(Hole + 0xfff, true));
}

TEST_F(GuestMemoryTest, MissingDirectoryEntryIsNotAccessible)
{
	EnablePaging();
	EXPECT_FALSE(PAGING_HostCanAccess(NoTable, false));
}

TEST_F(GuestMemoryTest, AFailedCheckLeavesCr2AndThePendingExceptionAlone)
{
	EnablePaging();
	paging.cr2          = 0x12345678;
	cpu.exception.which = 0;
	cpu.exception.error = 0;

	EXPECT_FALSE(PAGING_HostCanAccess(Hole, true));
	EXPECT_FALSE(PAGING_HostCanAccess(NoTable, false));

	EXPECT_EQ(paging.cr2, 0x12345678u);
	EXPECT_EQ(cpu.exception.which, 0u);
	EXPECT_EQ(cpu.exception.error, 0u);
}

// The check walks the tables only; the TLB keeps what the guest last linked.
TEST_F(GuestMemoryTest, ACheckLinksNoPageIntoTheTlb)
{
	EnablePaging();
	constexpr PhysPt Page = 0x150000;
	const auto read_before  = get_tlb_readhandler(Page);
	const auto write_before = get_tlb_writehandler(Page);

	EXPECT_TRUE(PAGING_HostCanAccess(Page, true));
	EXPECT_TRUE(PAGING_HostCanAccess(Page, false));

	EXPECT_EQ(get_tlb_readhandler(Page), read_before);
	EXPECT_EQ(get_tlb_writehandler(Page), write_before);
}

TEST_F(GuestMemoryTest, UserCodeSeesNeitherSupervisorPagesNorWritesToReadOnlyOnes)
{
	EnablePaging();
	RunAsUser();
	EXPECT_FALSE(PAGING_HostCanAccess(SupervisorPage, false));
	EXPECT_TRUE(PAGING_HostCanAccess(UserReadOnly, false));
	EXPECT_FALSE(PAGING_HostCanAccess(UserReadOnly, true));
}

TEST_F(GuestMemoryTest, SupervisorCodeReachesEveryPresentPage)
{
	EnablePaging();
	cpu.cpl = 0;
	EXPECT_TRUE(PAGING_HostCanAccess(SupervisorPage, false));
	EXPECT_TRUE(PAGING_HostCanAccess(UserReadOnly, true));
}

TEST_F(GuestMemoryTest, ReadReturnsTheBytesOfPresentPages)
{
	phys_writed(0x100ffe, 0x44332211);
	EnablePaging();

	std::array<uint8_t, 4> buf = {};
	const auto result          = GuestMemory::Read(0x100ffe, buf);

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(buf, (std::array<uint8_t, 4>{0x11, 0x22, 0x33, 0x44}));
}

TEST_F(GuestMemoryTest, ReadIntoAHoleNamesItsFirstAddress)
{
	EnablePaging();
	paging.cr2 = 0x12345678;

	std::array<uint8_t, 16> buf = {};
	const auto result           = GuestMemory::Read(Hole - 8, buf);

	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), Hole);
	EXPECT_EQ(paging.cr2, 0x12345678u);
}

TEST_F(GuestMemoryTest, ReadStartingInsideAHoleNamesTheStart)
{
	EnablePaging();
	std::array<uint8_t, 4> buf = {};
	const auto result          = GuestMemory::Read(Hole + 0x10, buf);
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), Hole + 0x10);
}

TEST_F(GuestMemoryTest, WriteLandsOnPresentPages)
{
	EnablePaging();
	const std::array<uint8_t, 2> data = {0xaa, 0xbb};
	ASSERT_TRUE(GuestMemory::Write(0x100fff, data).has_value());
	EXPECT_EQ(phys_readb(0x100fff), 0xaa);
	EXPECT_EQ(phys_readb(0x101000), 0xbb);
}

TEST_F(GuestMemoryTest, WriteAcrossAHoleWritesNothing)
{
	phys_writed(Hole - 4, 0x01020304);
	EnablePaging();

	const std::array<uint8_t, 8> data = {0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee};
	const auto result = GuestMemory::Write(Hole - 4, data);

	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), Hole);
	EXPECT_EQ(phys_readd(Hole - 4), 0x01020304u);
}

TEST_F(GuestMemoryTest, WriteToAReadOnlyPageFromUserCodeWritesNothing)
{
	phys_writeb(UserReadOnly, 0x5a);
	EnablePaging();
	RunAsUser();

	const std::array<uint8_t, 1> data = {0x00};
	const auto result = GuestMemory::Write(UserReadOnly, data);

	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), UserReadOnly);
	EXPECT_EQ(phys_readb(UserReadOnly), 0x5a);
}

TEST_F(GuestMemoryTest, ReadMappedSkipsHolesAndReportsThem)
{
	phys_writeb(Hole - 1, 0x77);
	phys_writeb(Hole + 0x1000, 0x88);
	EnablePaging();

	std::vector<uint8_t> buf(0x2000, 0xff);
	const auto skipped = GuestMemory::ReadMapped(Hole - 0x1000, buf);

	ASSERT_TRUE(skipped.has_value());
	ASSERT_EQ(skipped->size(), 1u);
	EXPECT_EQ(skipped->at(0), (GuestMemory::Range{Hole, Hole + 0x1000}));
	EXPECT_EQ(buf[0x0fff], 0x77);
	EXPECT_EQ(buf[0x1000], 0x00);
	EXPECT_EQ(buf[0x1fff], 0x00);
}

TEST_F(GuestMemoryTest, ReadMappedMergesAdjacentHolesAndClipsToTheRange)
{
	EnablePaging();
	std::vector<uint8_t> buf(0x10);
	const auto skipped = GuestMemory::ReadMapped(NoTable + 0x1ff8, buf);
	ASSERT_TRUE(skipped.has_value());
	ASSERT_EQ(skipped->size(), 1u);
	EXPECT_EQ(skipped->at(0),
	          (GuestMemory::Range{NoTable + 0x1ff8, NoTable + 0x2008}));
}

TEST_F(GuestMemoryTest, ReadMappedWithoutPagingSkipsNothing)
{
	std::vector<uint8_t> buf(0x1000);
	const auto skipped = GuestMemory::ReadMapped(Hole, buf);
	ASSERT_TRUE(skipped.has_value());
	EXPECT_TRUE(skipped->empty());
}

TEST_F(GuestMemoryTest, CheckRangeNamesTheFirstInaccessibleAddress)
{
	EnablePaging();
	EXPECT_TRUE(GuestMemory::CheckRange(0, 0x1000, false).has_value());
	const auto result = GuestMemory::CheckRange(0x1f0000, 0x20000, false);
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), Hole);
}

// An empty range touches no page, aligned or not.
TEST_F(GuestMemoryTest, AnEmptyRangeIsAccessibleAnywhere)
{
	EnablePaging();
	EXPECT_TRUE(GuestMemory::CheckRange(Hole, 0, true).has_value());
	EXPECT_TRUE(GuestMemory::CheckRange(Hole + 0x10, 0, true).has_value());
}

TEST_F(GuestMemoryTest, RangesBeyondFourGigabytesAreRefused)
{
	std::array<uint8_t, 2> buf = {};
	const auto read            = GuestMemory::Read(0xffffffff, buf);
	ASSERT_FALSE(read.has_value());
	EXPECT_EQ(read.error(), 0xffffffffu);

	const auto mapped = GuestMemory::ReadMapped(0xffffffff, buf);
	ASSERT_FALSE(mapped.has_value());
	EXPECT_EQ(mapped.error(), 0xffffffffu);
}

} // namespace
