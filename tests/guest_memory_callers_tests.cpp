// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include <algorithm>
#include <cstdint>
#include <functional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augra/log.h"
#include "dos/dos.h"
#include "hardware/memory.h"
#include "ints/int10.h"
#include "lua/lua_api.h"
#include "lua/lua_coroutine.h"
#include "lua/lua_engine.h"
#include "paging_test_fixture.h"
#include "webserver/bridge.h"
#include "webserver/private/dos.h"
#include "webserver/private/freeze.h"
#include "webserver/video.h"

namespace {

// ada-q50n: no host-side reader of guest memory may raise a guest #PF.
class GuestMemoryCallersTest : public PagingTestFixture {
protected:
	static constexpr PhysPt HmaPage = 0x101000; // FFFF:1010

	void TearDown() override
	{
		CurMode = saved_mode;
		Webserver::FreezeRegistry::Instance().Clear();
		augra::Logger::instance().clear_sinks();
		augra::Logger::instance().clear_component_level("webserver");
		PagingTestFixture::TearDown();
	}

	static std::vector<std::string> WebserverDebugLines(
	        const std::function<void()>& action)
	{
		auto& logger                   = augra::Logger::instance();
		std::vector<std::string> lines = {};
		logger.set_component_level("webserver", augra::LogLevel::Debug);
		logger.add_sink([&](const augra::LogLevel level,
		                    const char* component,
		                    const std::string& message) {
			if (level == augra::LogLevel::Debug &&
			    std::string_view(component) == "webserver") {
				lines.push_back(message);
			}
		});
		action();
		logger.clear_sinks();
		logger.clear_component_level("webserver");
		return lines;
	}

	// The fixture starts in a graphics mode, where screen text reads nothing.
	void UseTextMode3()
	{
		const auto mode3 = std::ranges::find_if(ModeList_VGA, [](const auto& m) {
			return m.mode == 0x03;
		});
		ASSERT_NE(mode3, ModeList_VGA.end());
		CurMode = mode3;
		ASSERT_TRUE(INT10_IsTextMode(*CurMode));

		const PhysPt bda = BIOSMEM_SEG << 4;
		phys_writew(bda + BIOSMEM_NB_COLS, 80);
		phys_writew(bda + BIOSMEM_PAGE_SIZE, 0x1000);
		phys_writeb(bda + BIOSMEM_CURRENT_PAGE, 0);
	}

	video_mode_block_iterator_t saved_mode = CurMode;

	Lua::ScriptState RunScript(const std::string& script)
	{
		const auto loaded = engine.LoadScript(script, "test");
		EXPECT_TRUE(loaded.ok) << loaded.error;
		EXPECT_TRUE(coroutine.Start());
		auto state = Lua::ScriptState::Running;
		for (uint64_t frame = 1; frame <= 10; ++frame) {
			state = coroutine.DispatchFrame(frame);
			if (state != Lua::ScriptState::Running) {
				break;
			}
		}
		return state;
	}

	void ExpectScriptErrorNames(const std::string& script,
	                            const std::string& address)
	{
		paging.cr2 = 0x12345678;
		EXPECT_EQ(RunScript(script), Lua::ScriptState::Error);
		EXPECT_NE(coroutine.ErrorMessage().find("not map"), std::string::npos)
		        << coroutine.ErrorMessage();
		EXPECT_NE(coroutine.ErrorMessage().find(address), std::string::npos)
		        << coroutine.ErrorMessage();
		EXPECT_EQ(paging.cr2, 0x12345678u);
	}

	Lua::LuaEngine engine;
	Lua::LuaCoroutine coroutine{engine};
};

TEST_F(GuestMemoryCallersTest, FreezeOnAHoleSkipsItAndKeepsTheEntry)
{
	EnablePaging();
	paging.cr2    = 0x12345678;
	auto& freezes = Webserver::FreezeRegistry::Instance();
	ASSERT_TRUE(freezes.Add(Hole, 0x42, 1));
	ASSERT_TRUE(freezes.Add(Hole - 1, 0x43, 1));

	Webserver::ApplyFreezes();

	EXPECT_EQ(paging.cr2, 0x12345678u);
	EXPECT_EQ(freezes.List().size(), 2u);
	EXPECT_EQ(phys_readb(Hole - 1), 0x43);
}

TEST_F(GuestMemoryCallersTest, LuaMemReadByteOfAHoleIsAnError)
{
	EnablePaging();
	UnmapPage(HmaPage);
	ExpectScriptErrorNames("dosbox.mem_read_byte(0xFFFF, 0x1010)", "0x101000");
}

TEST_F(GuestMemoryCallersTest, LuaMemReadWordOfAHoleIsAnError)
{
	EnablePaging();
	UnmapPage(HmaPage);
	ExpectScriptErrorNames("dosbox.mem_read_word(0xFFFF, 0x100F)", "0x101000");
}

TEST_F(GuestMemoryCallersTest, LuaMemReadAcrossAHoleIsAnError)
{
	EnablePaging();
	UnmapPage(HmaPage);
	ExpectScriptErrorNames("dosbox.mem_read(0xFFFF, 0x1000, 32)", "0x101000");
}

TEST_F(GuestMemoryCallersTest, LuaMemWriteAcrossAHoleWritesNothing)
{
	phys_writeb(HmaPage - 1, 0x11);
	EnablePaging();
	UnmapPage(HmaPage);
	ExpectScriptErrorNames("dosbox.mem_write(0xFFFF, 0x100F, 'ab')", "0x101000");
	EXPECT_EQ(phys_readb(HmaPage - 1), 0x11);
}

TEST_F(GuestMemoryCallersTest, LuaMemReadOfPresentPagesStillWorks)
{
	phys_writeb(0x10000, 0x7e);
	EnablePaging();
	EXPECT_EQ(RunScript("assert(dosbox.mem_read_byte(0x1000, 0) == 0x7e)"),
	          Lua::ScriptState::Completed)
	        << coroutine.ErrorMessage();
}

TEST_F(GuestMemoryCallersTest, LuaScreenTextWithoutTheBiosDataAreaIsAnError)
{
	UseTextMode3();
	EnablePaging();
	UnmapPage(0);
	ExpectScriptErrorNames("dosbox.screen_text()", "0x400");
}

TEST_F(GuestMemoryCallersTest, ScreenTextCommandWithoutTheBiosDataAreaNamesIt)
{
	UseTextMode3();
	EnablePaging();
	UnmapPage(0);
	paging.cr2 = 0x12345678;

	Webserver::ScreenTextCommand cmd;
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), 0x400u);
	EXPECT_EQ(paging.cr2, 0x12345678u);
}

TEST_F(GuestMemoryCallersTest, DosInternalsWithAnUnmappedMcbNamesIt)
{
	EnablePaging();
	const PhysPt mcb_page = (PhysPt{dos.firstMCB} * 16) & ~PhysPt{0xfff};
	UnmapPage(mcb_page);
	paging.cr2 = 0x12345678;

	Webserver::DosInternalsCommand cmd;
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), mcb_page);
	EXPECT_EQ(paging.cr2, 0x12345678u);
}

TEST_F(GuestMemoryCallersTest, DosAllocateWithAnUnmappedMcbNamesIt)
{
	EnablePaging();
	const PhysPt mcb_page = (PhysPt{dos.firstMCB} * 16) & ~PhysPt{0xfff};
	UnmapPage(mcb_page);
	paging.cr2 = 0x12345678;

	Webserver::AllocMemoryCommand cmd(64,
	                                  Webserver::MemoryArea::Conv,
	                                  Webserver::AllocStrategy::BestFit);
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), mcb_page);
	EXPECT_EQ(paging.cr2, 0x12345678u);
}

TEST_F(GuestMemoryCallersTest, DosFreeWithAnUnmappedMcbNamesIt)
{
	EnablePaging();
	const PhysPt mcb_page = (PhysPt{dos.firstMCB} * 16) & ~PhysPt{0xfff};
	UnmapPage(mcb_page);
	paging.cr2 = 0x12345678;

	Webserver::FreeMemoryCommand cmd(0x20000);
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), mcb_page);
	EXPECT_EQ(paging.cr2, 0x12345678u);
}

// FFFFFFFEh + 4 must not wrap past the range check to 2.
TEST_F(GuestMemoryCallersTest, AFreezeThatWrapsAtFourGigabytesIsRefused)
{
	httplib::Request req = {};
	req.body = R"({"address": 4294967294, "value": 1, "width": 4})";
	httplib::Response res = {};
	Webserver::FreezeHandlers::Post(req, res);
	EXPECT_EQ(res.status, 400);
	EXPECT_TRUE(Webserver::FreezeRegistry::Instance().List().empty());
}

// The block's own MCB sits outside both chains' checked memory.
TEST_F(GuestMemoryCallersTest, DosFreeInUpperMemoryChecksTheBlocksMcb)
{
	EnablePaging();
	UnmapPage(0xc0000);
	paging.cr2 = 0x12345678;

	Webserver::FreeMemoryCommand cmd(0xc0010);
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), 0xc0000u);
	EXPECT_EQ(paging.cr2, 0x12345678u);
}

// DOS segments end at FFFFh; 10F000h must not become segment 0F00h.
TEST_F(GuestMemoryCallersTest, DosFreeAboveTheFirstMegabyteFreesNothing)
{
	constexpr PhysPt Mcb = 0xeff0;
	phys_writeb(Mcb, 'M');
	phys_writew(Mcb + 1, 0x1234);
	phys_writew(Mcb + 3, 0x10);

	Webserver::FreeMemoryCommand cmd(0x10f000);
	cmd.Execute();

	EXPECT_EQ(phys_readw(Mcb + 1), 0x1234);
}

// ada-p0yv: addresses print as hex, never through %p.
TEST_F(GuestMemoryCallersTest, DosAllocateAndFreeLogTheAddressInHex)
{
	const auto allocated = WebserverDebugLines([] {
		Webserver::AllocMemoryCommand cmd(64,
		                                  Webserver::MemoryArea::Conv,
		                                  Webserver::AllocStrategy::BestFit);
		cmd.Execute();
	});
	ASSERT_EQ(allocated.size(), 1u);
	std::smatch match = {};
	const std::regex form(
	        R"(AllocMemoryCommand\(64\): result=1, 64 bytes at 0x([0-9a-f]{6}) \(DOS allocator\))");
	ASSERT_TRUE(std::regex_match(allocated[0], match, form)) << allocated[0];
	const std::string hex = match[1];

	const auto freed = WebserverDebugLines([&] {
		Webserver::FreeMemoryCommand cmd(std::stoul(hex, nullptr, 16));
		cmd.Execute();
	});
	ASSERT_EQ(freed.size(), 1u);
	EXPECT_EQ(freed[0],
	          "FreeMemoryCommand(0x" + hex + "): success=1 (DOS allocator)");
}

TEST_F(GuestMemoryCallersTest, XmsAllocateAndFreeLogTheAddressAndTheReleasedPages)
{
	const auto allocated = WebserverDebugLines([] {
		Webserver::AllocMemoryCommand cmd(4096,
		                                  Webserver::MemoryArea::Xms,
		                                  Webserver::AllocStrategy::BestFit);
		cmd.Execute();
	});
	ASSERT_EQ(allocated.size(), 1u);
	std::smatch match = {};
	const std::regex form(
	        R"(AllocMemoryCommand\(4096\), handle=[1-9][0-9]*: 4096 bytes at 0x([0-9a-f]{6,8}) \(XMS/page allocator\))");
	ASSERT_TRUE(std::regex_match(allocated[0], match, form)) << allocated[0];
	const std::string hex = match[1];

	const auto freed = WebserverDebugLines([&] {
		Webserver::FreeMemoryCommand cmd(std::stoul(hex, nullptr, 16));
		cmd.Execute();
	});
	ASSERT_EQ(freed.size(), 1u);
	EXPECT_EQ(freed[0],
	          "FreeMemoryCommand(0x" + hex + "): released=1 (page allocator)");
}

// ada-npdp: the size travels at full width.
TEST_F(GuestMemoryCallersTest, DosAllocateKeepsASizeAbove64KiB)
{
	const auto allocated = WebserverDebugLines([] {
		Webserver::AllocMemoryCommand cmd(70000,
		                                  Webserver::MemoryArea::Conv,
		                                  Webserver::AllocStrategy::BestFit);
		cmd.Execute();
	});
	ASSERT_EQ(allocated.size(), 1u);
	std::smatch match = {};
	const std::regex form(R"(.* at 0x([0-9a-f]{6}) \(DOS allocator\))");
	ASSERT_TRUE(std::regex_match(allocated[0], match, form)) << allocated[0];
	const auto addr = static_cast<PhysPt>(std::stoul(match[1].str(), nullptr, 16));

	constexpr PhysPt McbSizeOffset = 3;
	EXPECT_EQ(phys_readw(addr - 16 + McbSizeOffset), (70000 + 15) / 16);
}

TEST_F(GuestMemoryCallersTest, XmsAllocateKeepsASizeAbove64KiB)
{
	const auto allocated = WebserverDebugLines([] {
		Webserver::AllocMemoryCommand cmd(70000,
		                                  Webserver::MemoryArea::Xms,
		                                  Webserver::AllocStrategy::BestFit);
		cmd.Execute();
	});
	ASSERT_EQ(allocated.size(), 1u);
	EXPECT_NE(allocated[0].find("AllocMemoryCommand(70000), handle="),
	          std::string::npos)
	        << allocated[0];
	EXPECT_NE(allocated[0].find(": 73728 bytes at 0x"), std::string::npos)
	        << allocated[0];
}

// The page count of the largest size must not wrap to zero pages.
TEST_F(GuestMemoryCallersTest, XmsAllocateOfTheLargestSizeAsksForAllItsPages)
{
	const auto allocated = WebserverDebugLines([] {
		Webserver::AllocMemoryCommand cmd(4294967295u,
		                                  Webserver::MemoryArea::Xms,
		                                  Webserver::AllocStrategy::BestFit);
		cmd.Execute();
	});
	ASSERT_EQ(allocated.size(), 1u);
	EXPECT_NE(allocated[0].find("handle=0: 4294967296 bytes"), std::string::npos)
	        << allocated[0];
}

TEST_F(GuestMemoryCallersTest, AllocateRefusesASizeTheAreaCannotServe)
{
	for (const auto* body : {R"({"size": 0})",
	                         R"({"size": -1})",
	                         R"({"size": 1.5})",
	                         R"({"size": "64"})",
	                         R"({"size": 1048561})",
	                         R"({"size": 1048561, "area": "UMA"})",
	                         R"({"size": 4294967296, "area": "XMS"})"}) {
		httplib::Request req  = {};
		req.body              = body;
		httplib::Response res = {};
		EXPECT_THROW(Webserver::AllocMemoryCommand::Post(req, res),
		             std::invalid_argument)
		        << body;
	}
}

// Past the size check the command waits for the emulation thread, which the
// fixture does not run.
TEST_F(GuestMemoryCallersTest, AllocateTakesTheLargestSizeEachAreaCanServe)
{
	for (const auto* body : {R"({"size": 1048560})",
	                         R"({"size": 1048560, "area": "UMA"})",
	                         R"({"size": 4294967295, "area": "XMS"})"}) {
		httplib::Request req  = {};
		req.body              = body;
		httplib::Response res = {};
		EXPECT_THROW(Webserver::AllocMemoryCommand::Post(req, res),
		             Webserver::BridgeBusy)
		        << body;
	}
}

TEST_F(GuestMemoryCallersTest, DosInternalsUnderPagingWithEverythingMappedWorks)
{
	EnablePaging();
	Webserver::DosInternalsCommand cmd;
	cmd.Execute();
	EXPECT_FALSE(cmd.NotMappedAt().has_value());
	EXPECT_FALSE(cmd.MemoryMap().empty());
}

TEST_F(GuestMemoryCallersTest, ScreenTextCommandWithAnUnmappedTextBufferNamesIt)
{
	UseTextMode3();
	EnablePaging();
	UnmapPage(0xb8000);

	Webserver::ScreenTextCommand cmd;
	cmd.Execute();

	ASSERT_TRUE(cmd.NotMappedAt().has_value());
	EXPECT_EQ(*cmd.NotMappedAt(), 0xb8000u);
}

} // namespace
