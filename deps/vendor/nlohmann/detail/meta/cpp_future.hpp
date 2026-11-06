#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include <nlohmann/detail/macro_scope.hpp>

namespace nlohmann
{
namespace detail
{

template<typename T>
using uncvref_t = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

#ifdef JSON_HAS_CPP_14


using std::enable_if_t;
using std::index_sequence;
using std::make_index_sequence;
using std::index_sequence_for;

#else


template<bool B, typename T = void>
using enable_if_t = typename std::enable_if<B, T>::type;

// The following code is taken from https://github.com/abseil/abseil-cpp/blob/10cb35e459f5ecca5b2ff107635da0bfa41011b4/absl/utility/utility.h
// which is part of Google Abseil (https://github.com/abseil/abseil-cpp), licensed under the Apache License 2.0.

//// START OF CODE FROM GOOGLE ABSEIL

// integer_sequence
//
// Class template representing a compile-time integer sequence. An instantiation
// of `integer_sequence<T, Ints...>` has a sequence of integers encoded in its
// type through its template arguments (which is a common need when
// working with C++11 variadic templates). `absl::integer_sequence` is designed
// to be a drop-in replacement for C++14's `std::integer_sequence`.
//
// Example:
//
//   template< class T, T... Ints >
//   void user_function(integer_sequence<T, Ints...>);
//
//   int main()
//   {
//     // user_function's `T` will be deduced to `int` and `Ints...`
//     // will be deduced to `0, 1, 2, 3, 4`.
//     user_function(make_integer_sequence<int, 5>());
//   }
template <typename T, T... Ints>
struct integer_sequence
{
    using value_type = T;
    static constexpr std::size_t size() noexcept
    {
        return sizeof...(Ints);
    }
};






template <size_t... Ints>
using index_sequence = integer_sequence<size_t, Ints...>;

namespace utility_internal
{

template <typename Seq, size_t SeqSize, size_t Rem>
struct Extend;


template <typename T, T... Ints, size_t SeqSize>
struct Extend<integer_sequence<T, Ints...>, SeqSize, 0>
{
    using type = integer_sequence < T, Ints..., (Ints + SeqSize)... >;
};

template <typename T, T... Ints, size_t SeqSize>
struct Extend<integer_sequence<T, Ints...>, SeqSize, 1>
{
    using type = integer_sequence < T, Ints..., (Ints + SeqSize)..., 2 * SeqSize >;
};



template <typename T, size_t N>
struct Gen
{
    using type =
        typename Extend < typename Gen < T, N / 2 >::type, N / 2, N % 2 >::type;
};

template <typename T>
struct Gen<T, 0>
{
    using type = integer_sequence<T>;
};

}








template <typename T, T N>
using make_integer_sequence = typename utility_internal::Gen<T, N>::type;






template <size_t N>
using make_index_sequence = make_integer_sequence<size_t, N>;






template <typename... Ts>
using index_sequence_for = make_index_sequence<sizeof...(Ts)>;



#endif


template<unsigned N> struct priority_tag : priority_tag < N - 1 > {};
template<> struct priority_tag<0> {};


template<typename T>
struct static_const
{
    static constexpr T value{};
};

template<typename T>
constexpr T static_const<T>::value;

}
}
