// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/memory.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "hardware/guest_memory.h"
#include "hardware/memory.h"
#include "paging_test_fixture.h"

namespace {

using Webserver::ReadMemoryCommand;
using Webserver::SearchMemoryCommand;
using Webserver::Segment;
using Webserver::WriteMemoryCommand;

// ada-q50n: a guest #PF raised inside the Bridge would lock the engine.
using WebserverMemoryPagingTest = PagingTestFixture;

TEST_F(WebserverMemoryPagingTest, ReadOfAHoleNamesItInsteadOfFaulting)
{
	EnablePaging();
	paging.cr2 = 0x12345678;

	ReadMemoryCommand cmd(Segment::None, Hole - 4, 8);
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), Hole);
	EXPECT_EQ(paging.cr2, 0x12345678u);
}

TEST_F(WebserverMemoryPagingTest, ReadOfPresentPagesReturnsTheBytes)
{
	phys_writed(Hole - 4, 0x64636261);
	EnablePaging();

	ReadMemoryCommand cmd(Segment::None, Hole - 4, 4);
	cmd.Execute();

	EXPECT_FALSE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(cmd.Memory(), "abcd");
}

TEST_F(WebserverMemoryPagingTest, WriteAcrossAHoleWritesNothing)
{
	phys_writed(Hole - 4, 0x01020304);
	EnablePaging();

	WriteMemoryCommand cmd(Segment::None, Hole - 4, std::string(8, 'x'), {});
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), Hole);
	EXPECT_EQ(phys_readd(Hole - 4), 0x01020304u);
}

TEST_F(WebserverMemoryPagingTest, ConditionalWriteOnAHoleNamesIt)
{
	EnablePaging();
	paging.cr2 = 0x12345678;

	WriteMemoryCommand cmd(Segment::None, Hole, "y", "x");
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), Hole);
	EXPECT_EQ(paging.cr2, 0x12345678u);
}

TEST_F(WebserverMemoryPagingTest, WriteToPresentPagesLands)
{
	EnablePaging();
	WriteMemoryCommand cmd(Segment::None, Hole - 2, "zz", {});
	cmd.Execute();
	EXPECT_FALSE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(phys_readb(Hole - 2), 'z');
	EXPECT_EQ(phys_readb(Hole - 1), 'z');
}

TEST_F(WebserverMemoryPagingTest, SearchSkipsHolesAndReportsThem)
{
	phys_writeb(Hole - 1, 0x5a);
	phys_writeb(Hole + 0x1000, 0x5a);
	EnablePaging();

	SearchMemoryCommand cmd(Hole - 0x1000, Hole + 0x2000, 0x5a, 1);
	cmd.Execute();

	EXPECT_EQ(cmd.Matches(), (std::vector<uint32_t>{Hole - 1, Hole + 0x1000}));
	ASSERT_EQ(cmd.Skipped().size(), 1u);
	EXPECT_EQ(cmd.Skipped()[0], (GuestMemory::Range{Hole, Hole + 0x1000}));
}

TEST_F(WebserverMemoryPagingTest, SearchFindsNothingInsideAHole)
{
	EnablePaging();
	SearchMemoryCommand cmd(Hole, Hole + 0x1000, 0, 1);
	cmd.Execute();
	EXPECT_TRUE(cmd.Matches().empty());
	ASSERT_EQ(cmd.Skipped().size(), 1u);
	EXPECT_EQ(cmd.Skipped()[0], (GuestMemory::Range{Hole, Hole + 0x1000}));
}

TEST_F(WebserverMemoryPagingTest, SearchWithoutPagingSkipsNothing)
{
	phys_writeb(Hole + 3, 0x5a);
	SearchMemoryCommand cmd(Hole, Hole + 0x1000, 0x5a, 1);
	cmd.Execute();
	EXPECT_EQ(cmd.Matches(), (std::vector<uint32_t>{Hole + 3}));
	EXPECT_TRUE(cmd.Skipped().empty());
}

TEST_F(WebserverMemoryPagingTest, ReadAboveTheRamThroughAMappedPageReturnsTheBytes)
{
	phys_writed(HighFrame, 0x64636261);
	EnablePaging();

	ReadMemoryCommand cmd(Segment::None, HighLinear, 4);
	cmd.Execute();

	EXPECT_EQ(cmd.error, "");
	EXPECT_EQ(cmd.Memory(), "abcd");
}

TEST_F(WebserverMemoryPagingTest, WriteAboveTheRamThroughAMappedPageLands)
{
	EnablePaging();

	WriteMemoryCommand cmd(Segment::None, HighLinear + 8, "wxyz", {});
	cmd.Execute();

	EXPECT_EQ(cmd.error, "");
	EXPECT_EQ(phys_readd(HighFrame + 8), 0x7a797877u);
}

TEST_F(WebserverMemoryPagingTest, SearchAboveTheRamThroughAMappedPageFindsTheValue)
{
	phys_writeb(HighFrame + 0x10, 0x5a);
	EnablePaging();

	SearchMemoryCommand cmd(HighLinear, HighLinear + 0x1000, 0x5a, 1);
	cmd.Execute();

	EXPECT_EQ(cmd.error, "");
	EXPECT_EQ(cmd.Matches(), (std::vector<uint32_t>{HighLinear + 0x10}));
}

TEST_F(WebserverMemoryPagingTest, ReadAboveTheRamWithoutPagingIsRefused)
{
	ReadMemoryCommand cmd(Segment::None, HighLinear, 4);
	cmd.Execute();

	EXPECT_EQ(cmd.error,
	          "Address range 0x40000000 + 4 exceeds emulated memory size (" +
	                  std::to_string(MEM_TotalPages() * MemPageSize) + " bytes)");
	EXPECT_FALSE(cmd.NotMappedAt().has_value());
}

} // namespace
