// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "utils/expected.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ExpectedFallback::Expected;
using ExpectedFallback::Unexpected;

Expected<void, uint32_t> Refuse(const uint32_t address)
{
	return Unexpected<uint32_t>(address);
}

TEST(ExpectedFallback, AVoidResultIsASuccessByDefault)
{
	const Expected<void, uint32_t> result = {};
	EXPECT_TRUE(result.has_value());
	EXPECT_TRUE(static_cast<bool>(result));
}

TEST(ExpectedFallback, AVoidResultCarriesItsError)
{
	const auto result = Refuse(0x1000);
	EXPECT_FALSE(result.has_value());
	EXPECT_FALSE(static_cast<bool>(result));
	EXPECT_EQ(result.error(), 0x1000u);
}

TEST(ExpectedFallback, AVoidResultKeepsItsErrorWhenCopied)
{
	const auto result = Refuse(0x2000);
	const auto copy   = result;
	ASSERT_FALSE(copy.has_value());
	EXPECT_EQ(copy.error(), 0x2000u);
}

TEST(ExpectedFallback, AnEmptyBraceResultHoldsADefaultValue)
{
	const Expected<std::string, uint32_t> result = {};
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(*result, "");
}

TEST(ExpectedFallback, AValueResultGivesItsValue)
{
	Expected<std::vector<int>, uint32_t> result = std::vector<int>{1, 2, 3};
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->size(), 3u);
	EXPECT_EQ(result->at(2), 3);
	EXPECT_EQ(*result, (std::vector<int>{1, 2, 3}));

	result->push_back(4);
	EXPECT_EQ(result->size(), 4u);
}

TEST(ExpectedFallback, AValueResultTestsTrueAndAnErrorFalse)
{
	const Expected<std::string, uint32_t> value = std::string("text");
	const Expected<std::string, uint32_t> error = Unexpected<uint32_t>(0x10);

	EXPECT_TRUE(static_cast<bool>(value));
	EXPECT_FALSE(!value);
	EXPECT_FALSE(static_cast<bool>(error));
	EXPECT_TRUE(!error);
}

TEST(ExpectedFallback, AConstValueResultReadsThroughTheArrow)
{
	const Expected<std::string, uint32_t> result = std::string("C:\\>");
	EXPECT_EQ(result->size(), 4u);
	EXPECT_EQ(result->front(), 'C');
}

struct ExplicitError {
	explicit ExplicitError(const uint32_t address) : address(address) {}
	uint32_t address = 0;
};

template <template <typename, typename> class Ex, template <typename> class Unex>
void ExpectConversionsLikeTheStandard()
{
	using Text  = Ex<std::string, uint32_t>;
	using Bytes = Ex<std::vector<int>, uint32_t>;
	using Done  = Ex<void, uint32_t>;

	EXPECT_TRUE((std::is_convertible_v<const char*, Text>));
	EXPECT_TRUE((std::is_convertible_v<std::string, Text>));
	EXPECT_FALSE((std::is_convertible_v<int, Bytes>));
	EXPECT_TRUE((std::is_constructible_v<Bytes, int>));
	EXPECT_FALSE((std::is_constructible_v<Text, double>));
	EXPECT_FALSE((std::is_convertible_v<uint32_t, Done>));

	using ExplicitDone = Ex<void, ExplicitError>;
	using ExplicitText = Ex<std::string, ExplicitError>;

	EXPECT_TRUE((std::is_convertible_v<Unex<uint16_t>, Done>));
	EXPECT_FALSE((std::is_convertible_v<Unex<uint32_t>, ExplicitDone>));
	EXPECT_TRUE((std::is_constructible_v<ExplicitDone, Unex<uint32_t>>));
	EXPECT_FALSE((std::is_convertible_v<Unex<uint32_t>, ExplicitText>));
	EXPECT_TRUE((std::is_constructible_v<ExplicitText, Unex<uint32_t>>));
}

template <typename E>
using AliasUnexpected = decltype(MakeUnexpected(std::declval<E>()));

TEST(ExpectedFallback, ConvertsLikeTheStandardType)
{
	ExpectConversionsLikeTheStandard<ExpectedFallback::Expected,
	                                 ExpectedFallback::Unexpected>();
}

TEST(Expected, TheAliasConvertsLikeTheStandardType)
{
	ExpectConversionsLikeTheStandard<::Expected, AliasUnexpected>();
}

TEST(ExpectedFallback, AnotherExpectedNeverPassesAsAValue)
{
	using Flag  = ExpectedFallback::Expected<bool, uint32_t>;
	using Count = ExpectedFallback::Expected<int, uint32_t>;

	EXPECT_FALSE((std::is_constructible_v<Flag, Count>));
	EXPECT_FALSE((std::is_constructible_v<Flag, const Count&>));
	EXPECT_FALSE((std::is_constructible_v<Count, Flag>));
}

TEST(ExpectedFallback, ValueOrGivesTheValueWhenThereIsOne)
{
	const Expected<std::string, uint32_t> result = std::string("C:\\>");
	EXPECT_EQ(result.value_or(std::string{}), "C:\\>");
}

TEST(ExpectedFallback, ValueOrGivesTheFallbackOnAnError)
{
	const Expected<std::string, uint32_t> result = Unexpected<uint32_t>(0x3000);
	EXPECT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), 0x3000u);
	EXPECT_EQ(result.value_or(std::string("none")), "none");
}

TEST(ExpectedFallback, AnErrorOfAnotherTypeConvertsToTheErrorType)
{
	const Expected<void, uint32_t> result = Unexpected<uint16_t>(0xbeef);
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), 0xbeefu);
}

TEST(ExpectedFallback, AValueAndAnErrorOfTheSameTypeStayApart)
{
	const Expected<uint32_t, uint32_t> value = uint32_t{7};
	const Expected<uint32_t, uint32_t> error = Unexpected<uint32_t>(7);

	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(*value, 7u);
	ASSERT_FALSE(error.has_value());
	EXPECT_EQ(error.error(), 7u);
}

TEST(ExpectedFallback, AVoidErrorPassesThroughToAValueResult)
{
	const auto passed = [](const Expected<void, uint32_t>& checked)
	        -> Expected<std::string, uint32_t> {
		if (!checked) {
			return Unexpected<uint32_t>(checked.error());
		}
		return std::string("read");
	};
	EXPECT_EQ(*passed({}), "read");
	EXPECT_EQ(passed(Refuse(0x4000)).error(), 0x4000u);
}

#ifndef NDEBUG
TEST(ExpectedFallbackDeathTest, ReadingTheValueOfAnErrorAsserts)
{
	const Expected<std::string, uint32_t> result = Unexpected<uint32_t>(1);
	EXPECT_DEATH((void)*result, "");
}

TEST(ExpectedFallbackDeathTest, ReadingTheErrorOfASuccessAsserts)
{
	const Expected<void, uint32_t> result = {};
	EXPECT_DEATH((void)result.error(), "");
}
#endif

TEST(Expected, TheAliasIsTheStandardTypeWhereTheLibraryHasIt)
{
#ifdef DOSBOX_HAS_STD_EXPECTED
	EXPECT_TRUE(
	        (std::is_same_v<::Expected<int, uint32_t>, std::expected<int, uint32_t>>));
#else
	EXPECT_TRUE((std::is_same_v<::Expected<int, uint32_t>,
	                            ExpectedFallback::Expected<int, uint32_t>>));
#endif
}

TEST(Expected, MakeUnexpectedBuildsAnErrorForTheAlias)
{
	const ::Expected<void, uint32_t> result = MakeUnexpected(uint32_t{0x5000});
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), 0x5000u);
}

} // namespace
