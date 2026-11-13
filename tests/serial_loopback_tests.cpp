// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
// The soft modem and the nullmodem dial what the guest types; with
// [ethernet] host_loopback off they reach no service on this machine, the
// REST API among them.

#include "hardware/serialport/misc_util.h"

#include "config/setup.h"
#include "dosbox_test_fixture.h"

#include <gtest/gtest.h>

namespace {

TEST(SerialLoopback, KnowsTheAddressesThatMeanThisMachine)
{
	for (const auto* text :
	     {"127.0.0.1", "127.1.2.3", "0.0.0.0", "::1", "::", "::ffff:127.0.0.1"}) {
		EXPECT_TRUE(IsAddressOfThisMachine(asio::ip::make_address(text))) << text;
	}
	for (const auto* text : {"10.0.2.2", "192.168.1.10", "8.8.8.8", "::ffff:10.0.0.1"}) {
		EXPECT_FALSE(IsAddressOfThisMachine(asio::ip::make_address(text))) << text;
	}
}

class SerialLoopbackTest : public DOSBoxTestFixture {};

TEST_F(SerialLoopbackTest, ADialToThisMachineNeedsHostLoopback)
{
	asio::io_context io = {};
	asio::ip::tcp::acceptor listener(io, {asio::ip::make_address("127.0.0.1"), 0});
	const auto port = listener.local_endpoint().port();

	ASSERT_TRUE(get_section("ethernet")->HandleInputLine("host_loopback=off"));
	for (const auto* host : {"127.0.0.1", "localhost", "127.0.0.2"}) {
		const TCPClientSocket refused(host, port);
		EXPECT_FALSE(refused.isopen) << host;
	}

	ASSERT_TRUE(get_section("ethernet")->HandleInputLine("host_loopback=on"));
	const TCPClientSocket allowed("127.0.0.1", port);
	EXPECT_TRUE(allowed.isopen);
	ASSERT_TRUE(get_section("ethernet")->HandleInputLine("host_loopback=off"));
}

} // namespace
