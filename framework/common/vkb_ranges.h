/* Copyright (c) 2024, Huawei Technologies Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <algorithm>
#include <iterator>

/**
 * @brief Polyfill for std::ranges algorithms for platforms with incomplete C++20 support (like some OHOS SDKs).
 *
 * Usage: use vkb::ranges::any_of(...) instead of std::ranges::any_of(...)
 */
namespace vkb
{
namespace ranges
{
template <typename C, typename P>
auto any_of(const C &container, P predicate)
{
	return std::any_of(std::begin(container), std::end(container), predicate);
}

template <typename C, typename P>
auto none_of(const C &container, P predicate)
{
	return std::none_of(std::begin(container), std::end(container), predicate);
}

template <typename C, typename P>
auto all_of(const C &container, P predicate)
{
	return std::all_of(std::begin(container), std::end(container), predicate);
}

template <typename C, typename P>
auto find_if(C &container, P predicate)
{
	return std::find_if(std::begin(container), std::end(container), predicate);
}

template <typename C, typename P>
auto find_if(const C &container, P predicate)
{
	return std::find_if(std::begin(container), std::end(container), predicate);
}

template <typename C, typename T>
auto find(C &container, const T &value)
{
	return std::find(std::begin(container), std::end(container), value);
}

template <typename C, typename T>
auto find(const C &container, const T &value)
{
	return std::find(std::begin(container), std::end(container), value);
}

template <typename C, typename P>
auto count_if(const C &container, P predicate)
{
	return std::count_if(std::begin(container), std::end(container), predicate);
}

template <typename C, typename F>
auto for_each(C &container, F func)
{
	return std::for_each(std::begin(container), std::end(container), func);
}

template <typename C, typename O, typename F>
auto transform(const C &container, O result, F func)
{
	return std::transform(std::begin(container), std::end(container), result, func);
}

template <typename C>
auto max_element(const C &container)
{
	return std::max_element(std::begin(container), std::end(container));
}
}        // namespace ranges
}        // namespace vkb
