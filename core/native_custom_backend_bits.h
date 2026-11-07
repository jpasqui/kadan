#pragma once
// Exact C++17 equivalents of the two C++20 integer operations used by the existing compiler.
#include <cstdint>
#include <type_traits>
namespace nvasm { namespace portable {
template<class T> inline int countr_zero(T Value) {
    static_assert(std::is_unsigned<T>::value, "unsigned bit operation required");
    if (!Value) return int(sizeof(T) * 8);
    int Count = 0; while (!(Value & T(1))) { ++Count; Value >>= 1; } return Count;
}
template<class T> inline int popcount(T Value) {
    static_assert(std::is_unsigned<T>::value, "unsigned bit operation required");
    int Count = 0; while (Value) { Value &= Value - 1; ++Count; } return Count;
}
} }
