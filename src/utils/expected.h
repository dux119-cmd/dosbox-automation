// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_UTILS_EXPECTED_H
#define DOSBOX_UTILS_EXPECTED_H

#include <cassert>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <version>

#if defined(__cpp_lib_expected) && !defined(DOSBOX_EXPECTED_FALLBACK)
#define DOSBOX_HAS_STD_EXPECTED
#include <expected>
#endif

// Covers the part of std::expected the engine uses, for libraries without
// <expected> (libstdc++ 11). It sits outside the #if so every host tests it.
namespace ExpectedFallback {

template <typename E>
class Unexpected {
public:
	explicit Unexpected(E error) : error_value(std::move(error)) {}

	const E& error() const
	{
		return error_value;
	}

private:
	E error_value;
};

template <typename T, typename E>
class Expected;

template <typename X>
constexpr bool IsUnexpected = false;

template <typename E>
constexpr bool IsUnexpected<Unexpected<E>> = true;

template <typename X>
constexpr bool IsExpected = false;

template <typename T, typename E>
constexpr bool IsExpected<Expected<T, E>> = true;

template <typename T, typename E>
class Expected {
public:
	Expected() : state(std::in_place_index<0>) {}

	template <typename U = std::remove_cv_t<T>>
	        requires(!IsExpected<std::remove_cvref_t<U>> &&
	                 !IsUnexpected<std::remove_cvref_t<U>> &&
	                 std::is_constructible_v<T, U>)
	explicit(!std::is_convertible_v<U, T>) Expected(U&& value)
	        : state(std::in_place_index<0>, std::forward<U>(value))
	{}

	template <typename G>
	        requires std::is_constructible_v<E, const G&>
	explicit(!std::is_convertible_v<const G&, E>)
	        Expected(const Unexpected<G>& unexpected)
	        : state(std::in_place_index<1>, E(unexpected.error()))
	{}

	bool has_value() const
	{
		return state.index() == 0;
	}

	explicit operator bool() const
	{
		return has_value();
	}

	const T& operator*() const
	{
		assert(has_value());
		return std::get<0>(state);
	}

	T& operator*()
	{
		assert(has_value());
		return std::get<0>(state);
	}

	const T* operator->() const
	{
		return &**this;
	}

	T* operator->()
	{
		return &**this;
	}

	template <typename U>
	T value_or(U&& fallback) const
	{
		return has_value() ? std::get<0>(state)
		                   : static_cast<T>(std::forward<U>(fallback));
	}

	const E& error() const
	{
		assert(!has_value());
		return std::get<1>(state);
	}

private:
	std::variant<T, E> state;
};

template <typename E>
class Expected<void, E> {
public:
	Expected() = default;

	template <typename G>
	        requires std::is_constructible_v<E, const G&>
	explicit(!std::is_convertible_v<const G&, E>)
	        Expected(const Unexpected<G>& unexpected)
	        : error_value(E(unexpected.error()))
	{}

	bool has_value() const
	{
		return !error_value.has_value();
	}

	explicit operator bool() const
	{
		return has_value();
	}

	const E& error() const
	{
		assert(!has_value());
		return *error_value;
	}

private:
	std::optional<E> error_value = {};
};

} // namespace ExpectedFallback

#ifdef DOSBOX_HAS_STD_EXPECTED
template <typename T, typename E>
using Expected = std::expected<T, E>;
#else
template <typename T, typename E>
using Expected = ExpectedFallback::Expected<T, E>;
#endif

template <typename E>
auto MakeUnexpected(E error)
{
#ifdef DOSBOX_HAS_STD_EXPECTED
	return std::unexpected<E>(std::move(error));
#else
	return ExpectedFallback::Unexpected<E>(std::move(error));
#endif
}

#endif
