/*
* File: gpu_flags
* Project: blok
* Author: Collin Longoria
* Created on: 9/12/2025
*
* Description: Enables bitwise operations for enum class flags.
*/
#ifndef BLOK_GPU_FLAGS_HPP
#define BLOK_GPU_FLAGS_HPP

#include <type_traits>

namespace blok {
template <typename E>
struct is_flags_enum : std::false_type {};

template <typename E>
constexpr auto enable_if_flags(E) -> std::enable_if_t<is_flags_enum<E>::value, E>;

template <typename E>
constexpr E operator| (E a, E b) {
    static_assert(is_flags_enum<E>::value, "Enum not enabled for flags");
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}

template <typename E>
constexpr E operator& (E a, E b) {
    static_assert(is_flags_enum<E>::value, "Enum not enabled for flags");
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}

template <typename E>
constexpr E operator^ (E a, E b) {
    static_assert(is_flags_enum<E>::value, "Enum not enabled for flags");
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b));
}

template <typename E>
constexpr E operator~ (E a) {
    static_assert(is_flags_enum<E>::value, "Enum not enabled for flags");
    using U = std::underlying_type_t<E>;
    return static_cast<E>(~static_cast<U>(a));
}

template <typename E>
E& operator|=(E& a, E b) {
    a = (a | b);
    return a;
}

template <typename E>
E& operator&=(E& a, E b) {
    a = (a & b);
    return a;
}

template <typename E>
E& operator^=(E& a, E b) {
    a = (a ^ b);
    return a;
}
}

#endif //BLOK_GPU_FLAGS_HPP