// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_PAGING_TEST_FIXTURE_H
#define DOSBOX_PAGING_TEST_FIXTURE_H

#include <cstdint>

#include <gtest/gtest.h>

#include "cpu/cpu.h"
#include "cpu/paging.h"
#include "dosbox_test_fixture.h"
#include "hardware/memory.h"

// Guest page tables for host-side access tests (ada-q50n): 0-4 MB user
// pages with a hole and a read-only page, 4-8 MB supervisor pages, nothing
// above. Paging stays off until a test calls EnablePaging().
class PagingTestFixture : public DOSBoxTestFixture {
protected:
	static constexpr PhysPt PageDirectory   = 0x300000;
	static constexpr PhysPt UserTable       = 0x301000;
	static constexpr PhysPt SupervisorTable = 0x302000;

	static constexpr PhysPt Hole           = 0x200000;
	static constexpr PhysPt UserReadOnly   = 0x201000;
	static constexpr PhysPt SupervisorPage = 0x400000;
	static constexpr PhysPt NoTable        = 0x800000;

	// Linear 1 GB, far above the fixture's RAM, mapped onto a low frame.
	static constexpr PhysPt HighLinear = 0x40000000;
	static constexpr PhysPt HighTable  = 0x303000;
	static constexpr PhysPt HighFrame  = 0x150000;

	static constexpr uint32_t Present  = 1;
	static constexpr uint32_t Writable = 2;
	static constexpr uint32_t User     = 4;

	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		ASSERT_GE(MEM_TotalPages(), 16u * 256u);

		saved_cpl = cpu.cpl;
		saved_mpl = cpu.mpl;

		for (uint32_t i = 0; i < 1024; ++i) {
			phys_writed(PageDirectory + i * 4, 0);
		}
		phys_writed(PageDirectory, UserTable | Present | Writable | User);
		phys_writed(PageDirectory + 4, SupervisorTable | Present | Writable);

		for (uint32_t i = 0; i < 1024; ++i) {
			phys_writed(UserTable + i * 4,
			            (i << 12) | Present | Writable | User);
			phys_writed(SupervisorTable + i * 4,
			            ((i + 1024) << 12) | Present | Writable);
		}
		phys_writed(UserTable + (Hole >> 12) * 4, 0);
		phys_writed(UserTable + (UserReadOnly >> 12) * 4,
		            UserReadOnly | Present | User);

		ASSERT_LT(static_cast<uint64_t>(MEM_TotalPages()) * MemPageSize,
		          HighLinear);
		for (uint32_t i = 0; i < 1024; ++i) {
			phys_writed(HighTable + i * 4, 0);
		}
		phys_writed(HighTable, HighFrame | Present | Writable | User);
		phys_writed(PageDirectory + (HighLinear >> 22) * 4,
		            HighTable | Present | Writable | User);
	}

	void TearDown() override
	{
		PAGING_Enable(false);
		cpu.cpl = saved_cpl;
		cpu.mpl = saved_mpl;
		DOSBoxTestFixture::TearDown();
	}

	static void EnablePaging()
	{
		PAGING_SetDirBase(PageDirectory);
		PAGING_Enable(true);
	}

	static void RunAsUser()
	{
		cpu.cpl = 3;
		cpu.mpl = 3;
	}

	static void UnmapPage(const PhysPt linear)
	{
		phys_writed(UserTable + (linear >> 12) * 4, 0);
		PAGING_ClearTLB();
	}

private:
	Bitu saved_cpl = 0;
	Bitu saved_mpl = 0;
};

#endif
