// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/programs/label.h"

#include <string>

#include <gtest/gtest.h>

namespace {

using namespace LabelCommand;

TEST(LabelCommand, ADriveAndTextAreSplit)
{
	const auto parts = ParseCommandTail(" a:sales1991");
	EXPECT_EQ(parts.drive, 'A');
	EXPECT_EQ(parts.text, "sales1991");
	EXPECT_FALSE(parts.ask);
}

TEST(LabelCommand, ADriveAloneAsks)
{
	const auto parts = ParseCommandTail(" a:");
	EXPECT_EQ(parts.drive, 'A');
	EXPECT_TRUE(parts.ask);
}

TEST(LabelCommand, NothingAsksForTheCurrentDrive)
{
	const auto parts = ParseCommandTail("");
	EXPECT_EQ(parts.drive, std::nullopt);
	EXPECT_TRUE(parts.ask);
	EXPECT_TRUE(ParseCommandTail("   ").ask);
}

// LABEL.ASM, Find_First_Char: a blank right after the drive means "ask".
TEST(LabelCommand, ABlankAfterTheDriveAsks)
{
	const auto parts = ParseCommandTail(" a: sales");
	EXPECT_EQ(parts.drive, 'A');
	EXPECT_TRUE(parts.ask);
}

TEST(LabelCommand, BlanksInsideTheTextStay)
{
	const auto parts = ParseCommandTail("  sales  1991");
	EXPECT_EQ(parts.drive, std::nullopt);
	EXPECT_EQ(parts.text, "sales  1991");
	EXPECT_FALSE(parts.ask);
}

TEST(LabelCommand, TheLineEndIsNotText)
{
	EXPECT_EQ(ParseCommandTail(" b:disk\r").text, "disk");
	EXPECT_EQ(ParseCommandTail(" b:disk\r").drive, 'B');
}

TEST(LabelCommand, AnyCharacterBeforeTheColonIsTheDrive)
{
	EXPECT_EQ(ParseCommandTail(" 1:x").drive, '1');
}

TEST(LabelCommand, TheTypedLineLosesLeadingBlanksAndItsEnd)
{
	EXPECT_EQ(TextOfInputLine("  sales 1\r\n"), "sales 1");
	EXPECT_EQ(TextOfInputLine("\r\n"), "");
	EXPECT_EQ(TextOfInputLine(""), "");
}

TEST(LabelCommand, TextCheckFollowsCharacterCheck)
{
	EXPECT_EQ(CheckLabelText(""), TextCheck::Empty);
	EXPECT_EQ(CheckLabelText("sales 1991"), TextCheck::Valid);
	EXPECT_EQ(CheckLabelText("a-b_c!#$%&'()@^`{}~"), TextCheck::Valid);
	EXPECT_EQ(CheckLabelText("\x84\x94\xe1"), TextCheck::Valid);
	for (const char c : std::string("*?[]:<|>+=;,/\\.\"")) {
		EXPECT_EQ(CheckLabelText(std::string("ab") + c), TextCheck::Invalid)
		        << c;
	}
	for (int c = 1; c < 0x20; ++c) {
		EXPECT_EQ(CheckLabelText(std::string(1, static_cast<char>(c))),
		          TextCheck::Invalid)
		        << c;
	}
}

// Process_String checks every character but keeps eleven.
TEST(LabelCommand, ABadCharacterPastElevenStillCounts)
{
	EXPECT_EQ(CheckLabelText("abcdefghijklm*"), TextCheck::Invalid);
}

TEST(LabelCommand, TheNameGetsADotAfterEightAndElevenCharacters)
{
	EXPECT_EQ(ToLabelName("sales1991"), "SALES199.1");
	EXPECT_EQ(ToLabelName("disk"), "DISK");
	EXPECT_EQ(ToLabelName("abcdefgh"), "ABCDEFGH");
	EXPECT_EQ(ToLabelName("abcdefghijklmn"), "ABCDEFGH.IJK");
	EXPECT_EQ(ToLabelName("my disk"), "MY DISK");
}

TEST(LabelCommand, AnFcbNameLosesItsPadding)
{
	EXPECT_EQ(LabelFromFcbName("SALES1991  "), "SALES1991");
	EXPECT_EQ(LabelFromFcbName("MY DISK    "), "MY DISK");
	EXPECT_EQ(LabelFromFcbName("           "), "");
}

} // namespace
