// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/render/render_shared.h"
#include "webserver/bridge.h"
#include "webserver/video.h"
#include "webserver/webserver.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "nlohmann/json.hpp"

namespace {

class CountingCommand final : public Webserver::Command {
public:
	void Execute() override
	{
		++runs;
	}
	std::atomic<int> runs = 0;
};

std::string ErrorText(const httplib::Response& res)
{
	return nlohmann::json::parse(res.body).value("error", std::string());
}

} // namespace

// ada-n0vh: the command never runs later, so the client may send it again.
TEST(WebserverBridge, AnUntakenCommandIsBusyAndNeverRuns)
{
	CountingCommand cmd;
	EXPECT_THROW(cmd.WaitForCompletion(20), Webserver::BridgeBusy);
	Webserver::Bridge::Instance().ProcessRequests();
	EXPECT_EQ(cmd.runs, 0);
}

TEST(WebserverBridge, ATakenCommandRunsOnce)
{
	CountingCommand cmd;
	// The other thread may queue it late.
	std::thread emulation([&cmd] {
		const auto deadline = std::chrono::steady_clock::now() +
		                      std::chrono::seconds(5);
		while (cmd.runs == 0 && std::chrono::steady_clock::now() < deadline) {
			Webserver::Bridge::Instance().ProcessRequests();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	});
	EXPECT_NO_THROW(cmd.WaitForCompletion(5000));
	emulation.join();
	EXPECT_EQ(cmd.runs, 1);
}

class SlowCommand final : public Webserver::Command {
public:
	void Execute() override
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		++runs;
	}
	std::atomic<int> runs = 0;
};

// The Bridge holds its lock while a command runs, so a wait that times out
// meanwhile wakes only after the run and sees it done: never busy, one run.
TEST(WebserverBridge, ATakenCommandThatRunsPastTheWaitIsNotBusy)
{
	SlowCommand cmd;
	std::thread emulation([&cmd] {
		const auto deadline = std::chrono::steady_clock::now() +
		                      std::chrono::seconds(5);
		while (cmd.runs == 0 && std::chrono::steady_clock::now() < deadline) {
			Webserver::Bridge::Instance().ProcessRequests();
			std::this_thread::yield();
		}
	});
	EXPECT_NO_THROW(cmd.WaitForCompletion(20));
	emulation.join();
	EXPECT_EQ(cmd.runs, 1);
}

// ada-q50n: a command that runs the machine (a guest page fault, a DOS
// callback) re-enters ProcessRequests on the same thread.
class ReenteringCommand final : public Webserver::Command {
public:
	void Execute() override
	{
		Webserver::Bridge::Instance().ProcessRequests();
		++runs;
	}
	std::atomic<int> runs = 0;
};

TEST(WebserverBridge, ACommandThatReentersTheBridgeDoesNotLockIt)
{
	ReenteringCommand cmd;
	std::thread emulation([&cmd] {
		const auto deadline = std::chrono::steady_clock::now() +
		                      std::chrono::seconds(5);
		while (cmd.runs == 0 && std::chrono::steady_clock::now() < deadline) {
			Webserver::Bridge::Instance().ProcessRequests();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	});
	EXPECT_NO_THROW(cmd.WaitForCompletion(5000));
	emulation.join();
	EXPECT_EQ(cmd.runs, 1);
}

TEST(WebserverErrorResponse, UnmappedGuestPageIs409WithTheAddress)
{
	httplib::Response res;
	Webserver::ErrorResponse(std::make_exception_ptr(
	                                 Webserver::GuestPageNotMapped(0x200000)),
	                         res);
	EXPECT_EQ(res.status, 409);
	const auto j = nlohmann::json::parse(res.body);
	EXPECT_EQ(j.value("address", uint32_t{0}), 0x200000u) << res.body;
	EXPECT_NE(ErrorText(res).find("0x200000"), std::string::npos) << res.body;
}

TEST(WebserverErrorResponse, BusyIs503WithRetryAfter)
{
	httplib::Response res;
	Webserver::ErrorResponse(std::make_exception_ptr(Webserver::BridgeBusy("x")),
	                         res);
	EXPECT_EQ(res.status, 503);
	EXPECT_EQ(res.get_header_value("Retry-After"), "1");
	EXPECT_NE(ErrorText(res).find("busy"), std::string::npos) << res.body;
}

// Before the first frame is drawn the frame routes answer as busy does: a
// client asks again instead of giving up.
TEST(WebserverVideo, NoFrameYetIs503WithRetryAfter)
{
	ASSERT_FALSE(RENDER_HasSharedFrame());
	httplib::Request raw_req;
	raw_req.params.emplace("mode", "raw");
	httplib::Response frame;
	Webserver::VideoHandlers::GetFrame(raw_req, frame);
	EXPECT_EQ(frame.status, 503);
	EXPECT_EQ(frame.get_header_value("Retry-After"), "1");

	httplib::Response info;
	Webserver::VideoHandlers::GetFrameInfo(httplib::Request(), info);
	EXPECT_EQ(info.status, 503);
	EXPECT_EQ(info.get_header_value("Retry-After"), "1");
}

TEST(WebserverErrorResponse, OtherFailuresKeepTheirCodes)
{
	httplib::Response broken;
	Webserver::ErrorResponse(std::make_exception_ptr(std::runtime_error("x")),
	                         broken);
	EXPECT_EQ(broken.status, 500);
	EXPECT_FALSE(broken.has_header("Retry-After"));
	EXPECT_EQ(ErrorText(broken), "Internal server error");

	httplib::Response bad;
	Webserver::ErrorResponse(std::make_exception_ptr(std::invalid_argument("x")),
	                         bad);
	EXPECT_EQ(bad.status, 400);
	EXPECT_EQ(ErrorText(bad), "Invalid request parameter");
}
