#pragma once

#include <limits>
#include <type_traits>
#include <utility>
#include <tuple>

#include <nlohmann/detail/macro_scope.hpp>

#include <nlohmann/detail/iterators/iterator_traits.hpp>
#include <nlohmann/detail/meta/call_std/begin.hpp>
#include <nlohmann/detail/meta/call_std/end.hpp>
#include <nlohmann/detail/meta/cpp_future.hpp>
#include <nlohmann/detail/meta/detected.hpp>
#include <nlohmann/json_fwd.hpp>

namespace nlohmann
{








namespace detail
{













template<typename> struct is_basic_json : std::false_type {};

NLOHMANN_BASIC_JSON_TPL_DECLARATION
struct is_basic_json<NLOHMANN_BASIC_JSON_TPL> : std::true_type {};





template<typename>
class json_ref;

template<typename>
struct is_json_ref : std::false_type {};

template<typename T>
struct is_json_ref<json_ref<T>> : std::true_type {};





template<typename T>
using mapped_type_t = typename T::mapped_type;

template<typename T>
using key_type_t = typename T::key_type;

template<typename T>
using value_type_t = typename T::value_type;

template<typename T>
using difference_type_t = typename T::difference_type;

template<typename T>
using pointer_t = typename T::pointer;

template<typename T>
using reference_t = typename T::reference;

template<typename T>
using iterator_category_t = typename T::iterator_category;

template<typename T, typename... Args>
using to_json_function = decltype(T::to_json(std::declval<Args>()...));

template<typename T, typename... Args>
using from_json_function = decltype(T::from_json(std::declval<Args>()...));

template<typename T, typename U>
using get_template_function = decltype(std::declval<T>().template get<U>());


template<typename BasicJsonType, typename T, typename = void>
struct has_from_json : std::false_type {};





template <typename BasicJsonType, typename T>
struct is_getable
{
    static constexpr bool value = is_detected<get_template_function, const BasicJsonType&, T>::value;
};

template<typename BasicJsonType, typename T>
struct has_from_json < BasicJsonType, T, enable_if_t < !is_basic_json<T>::value >>
{
    using serializer = typename BasicJsonType::template json_serializer<T, void>;

    static constexpr bool value =
        is_detected_exact<void, from_json_function, serializer,
        const BasicJsonType&, T&>::value;
};



template<typename BasicJsonType, typename T, typename = void>
struct has_non_default_from_json : std::false_type {};

template<typename BasicJsonType, typename T>
struct has_non_default_from_json < BasicJsonType, T, enable_if_t < !is_basic_json<T>::value >>
{
    using serializer = typename BasicJsonType::template json_serializer<T, void>;

    static constexpr bool value =
        is_detected_exact<T, from_json_function, serializer,
        const BasicJsonType&>::value;
};



template<typename BasicJsonType, typename T, typename = void>
struct has_to_json : std::false_type {};

template<typename BasicJsonType, typename T>
struct has_to_json < BasicJsonType, T, enable_if_t < !is_basic_json<T>::value >>
{
    using serializer = typename BasicJsonType::template json_serializer<T, void>;

    static constexpr bool value =
        is_detected_exact<void, to_json_function, serializer, BasicJsonType&,
        T>::value;
};







template<class...> struct conjunction : std::true_type { };
template<class B1> struct conjunction<B1> : B1 { };
template<class B1, class... Bn>
struct conjunction<B1, Bn...>
: std::conditional<bool(B1::value), conjunction<Bn...>, B1>::type {};


template<class B> struct negation : std::integral_constant < bool, !B::value > { };




template <typename T>
struct is_default_constructible : std::is_default_constructible<T> {};

template <typename T1, typename T2>
struct is_default_constructible<std::pair<T1, T2>>
            : conjunction<is_default_constructible<T1>, is_default_constructible<T2>> {};

template <typename T1, typename T2>
struct is_default_constructible<const std::pair<T1, T2>>
            : conjunction<is_default_constructible<T1>, is_default_constructible<T2>> {};

template <typename... Ts>
struct is_default_constructible<std::tuple<Ts...>>
            : conjunction<is_default_constructible<Ts>...> {};

template <typename... Ts>
struct is_default_constructible<const std::tuple<Ts...>>
            : conjunction<is_default_constructible<Ts>...> {};


template <typename T, typename... Args>
struct is_constructible : std::is_constructible<T, Args...> {};

template <typename T1, typename T2>
struct is_constructible<std::pair<T1, T2>> : is_default_constructible<std::pair<T1, T2>> {};

template <typename T1, typename T2>
struct is_constructible<const std::pair<T1, T2>> : is_default_constructible<const std::pair<T1, T2>> {};

template <typename... Ts>
struct is_constructible<std::tuple<Ts...>> : is_default_constructible<std::tuple<Ts...>> {};

template <typename... Ts>
struct is_constructible<const std::tuple<Ts...>> : is_default_constructible<const std::tuple<Ts...>> {};


template<typename T, typename = void>
struct is_iterator_traits : std::false_type {};

template<typename T>
struct is_iterator_traits<iterator_traits<T>>
{
  private:
    using traits = iterator_traits<T>;

  public:
    static constexpr auto value =
        is_detected<value_type_t, traits>::value &&
        is_detected<difference_type_t, traits>::value &&
        is_detected<pointer_t, traits>::value &&
        is_detected<iterator_category_t, traits>::value &&
        is_detected<reference_t, traits>::value;
};

template<typename T>
struct is_range
{
  private:
    using t_ref = typename std::add_lvalue_reference<T>::type;

    using iterator = detected_t<result_of_begin, t_ref>;
    using sentinel = detected_t<result_of_end, t_ref>;




    static constexpr auto is_iterator_begin =
        is_iterator_traits<iterator_traits<iterator>>::value;

  public:
    static constexpr bool value = !std::is_same<iterator, nonesuch>::value && !std::is_same<sentinel, nonesuch>::value && is_iterator_begin;
};

template<typename R>
using iterator_t = enable_if_t<is_range<R>::value, result_of_begin<decltype(std::declval<R&>())>>;

template<typename T>
using range_value_t = value_type_t<iterator_traits<iterator_t<T>>>;





template<typename T, typename = void>
struct is_complete_type : std::false_type {};

template<typename T>
struct is_complete_type<T, decltype(void(sizeof(T)))> : std::true_type {};

template<typename BasicJsonType, typename CompatibleObjectType,
         typename = void>
struct is_compatible_object_type_impl : std::false_type {};

template<typename BasicJsonType, typename CompatibleObjectType>
struct is_compatible_object_type_impl <
    BasicJsonType, CompatibleObjectType,
    enable_if_t < is_detected<mapped_type_t, CompatibleObjectType>::value&&
    is_detected<key_type_t, CompatibleObjectType>::value >>
{
    using object_t = typename BasicJsonType::object_t;


    static constexpr bool value =
        is_constructible<typename object_t::key_type,
        typename CompatibleObjectType::key_type>::value &&
        is_constructible<typename object_t::mapped_type,
        typename CompatibleObjectType::mapped_type>::value;
};

template<typename BasicJsonType, typename CompatibleObjectType>
struct is_compatible_object_type
    : is_compatible_object_type_impl<BasicJsonType, CompatibleObjectType> {};

template<typename BasicJsonType, typename ConstructibleObjectType,
         typename = void>
struct is_constructible_object_type_impl : std::false_type {};

template<typename BasicJsonType, typename ConstructibleObjectType>
struct is_constructible_object_type_impl <
    BasicJsonType, ConstructibleObjectType,
    enable_if_t < is_detected<mapped_type_t, ConstructibleObjectType>::value&&
    is_detected<key_type_t, ConstructibleObjectType>::value >>
{
    using object_t = typename BasicJsonType::object_t;

    static constexpr bool value =
        (is_default_constructible<ConstructibleObjectType>::value &&
         (std::is_move_assignable<ConstructibleObjectType>::value ||
          std::is_copy_assignable<ConstructibleObjectType>::value) &&
         (is_constructible<typename ConstructibleObjectType::key_type,
          typename object_t::key_type>::value &&
          std::is_same <
          typename object_t::mapped_type,
          typename ConstructibleObjectType::mapped_type >::value)) ||
        (has_from_json<BasicJsonType,
         typename ConstructibleObjectType::mapped_type>::value ||
         has_non_default_from_json <
         BasicJsonType,
         typename ConstructibleObjectType::mapped_type >::value);
};

template<typename BasicJsonType, typename ConstructibleObjectType>
struct is_constructible_object_type
    : is_constructible_object_type_impl<BasicJsonType,
      ConstructibleObjectType> {};

template<typename BasicJsonType, typename CompatibleStringType>
struct is_compatible_string_type
{
    static constexpr auto value =
        is_constructible<typename BasicJsonType::string_t, CompatibleStringType>::value;
};

template<typename BasicJsonType, typename ConstructibleStringType>
struct is_constructible_string_type
{
    static constexpr auto value =
        is_constructible<ConstructibleStringType,
        typename BasicJsonType::string_t>::value;
};

template<typename BasicJsonType, typename CompatibleArrayType, typename = void>
struct is_compatible_array_type_impl : std::false_type {};

template<typename BasicJsonType, typename CompatibleArrayType>
struct is_compatible_array_type_impl <
    BasicJsonType, CompatibleArrayType,
    enable_if_t <
    is_detected<iterator_t, CompatibleArrayType>::value&&
    is_iterator_traits<iterator_traits<detected_t<iterator_t, CompatibleArrayType>>>::value&&


    !std::is_same<CompatibleArrayType, detected_t<range_value_t, CompatibleArrayType>>::value >>
{
    static constexpr bool value =
        is_constructible<BasicJsonType,
        range_value_t<CompatibleArrayType>>::value;
};

template<typename BasicJsonType, typename CompatibleArrayType>
struct is_compatible_array_type
    : is_compatible_array_type_impl<BasicJsonType, CompatibleArrayType> {};

template<typename BasicJsonType, typename ConstructibleArrayType, typename = void>
struct is_constructible_array_type_impl : std::false_type {};

template<typename BasicJsonType, typename ConstructibleArrayType>
struct is_constructible_array_type_impl <
    BasicJsonType, ConstructibleArrayType,
    enable_if_t<std::is_same<ConstructibleArrayType,
    typename BasicJsonType::value_type>::value >>
            : std::true_type {};

template<typename BasicJsonType, typename ConstructibleArrayType>
struct is_constructible_array_type_impl <
    BasicJsonType, ConstructibleArrayType,
    enable_if_t < !std::is_same<ConstructibleArrayType,
    typename BasicJsonType::value_type>::value&&
    !is_compatible_string_type<BasicJsonType, ConstructibleArrayType>::value&&
    is_default_constructible<ConstructibleArrayType>::value&&
(std::is_move_assignable<ConstructibleArrayType>::value ||
 std::is_copy_assignable<ConstructibleArrayType>::value)&&
is_detected<iterator_t, ConstructibleArrayType>::value&&
is_iterator_traits<iterator_traits<detected_t<iterator_t, ConstructibleArrayType>>>::value&&
is_detected<range_value_t, ConstructibleArrayType>::value&&


!std::is_same<ConstructibleArrayType, detected_t<range_value_t, ConstructibleArrayType>>::value&&
        is_complete_type <
        detected_t<range_value_t, ConstructibleArrayType >>::value >>
{
    using value_type = range_value_t<ConstructibleArrayType>;

    static constexpr bool value =
        std::is_same<value_type,
        typename BasicJsonType::array_t::value_type>::value ||
        has_from_json<BasicJsonType,
        value_type>::value ||
        has_non_default_from_json <
        BasicJsonType,
        value_type >::value;
};

template<typename BasicJsonType, typename ConstructibleArrayType>
struct is_constructible_array_type
    : is_constructible_array_type_impl<BasicJsonType, ConstructibleArrayType> {};

template<typename RealIntegerType, typename CompatibleNumberIntegerType,
         typename = void>
struct is_compatible_integer_type_impl : std::false_type {};

template<typename RealIntegerType, typename CompatibleNumberIntegerType>
struct is_compatible_integer_type_impl <
    RealIntegerType, CompatibleNumberIntegerType,
    enable_if_t < std::is_integral<RealIntegerType>::value&&
    std::is_integral<CompatibleNumberIntegerType>::value&&
    !std::is_same<bool, CompatibleNumberIntegerType>::value >>
{

    using RealLimits = std::numeric_limits<RealIntegerType>;
    using CompatibleLimits = std::numeric_limits<CompatibleNumberIntegerType>;

    static constexpr auto value =
        is_constructible<RealIntegerType,
        CompatibleNumberIntegerType>::value &&
        CompatibleLimits::is_integer &&
        RealLimits::is_signed == CompatibleLimits::is_signed;
};

template<typename RealIntegerType, typename CompatibleNumberIntegerType>
struct is_compatible_integer_type
    : is_compatible_integer_type_impl<RealIntegerType,
      CompatibleNumberIntegerType> {};

template<typename BasicJsonType, typename CompatibleType, typename = void>
struct is_compatible_type_impl: std::false_type {};

template<typename BasicJsonType, typename CompatibleType>
struct is_compatible_type_impl <
    BasicJsonType, CompatibleType,
    enable_if_t<is_complete_type<CompatibleType>::value >>
{
    static constexpr bool value =
        has_to_json<BasicJsonType, CompatibleType>::value;
};

template<typename BasicJsonType, typename CompatibleType>
struct is_compatible_type
    : is_compatible_type_impl<BasicJsonType, CompatibleType> {};

template<typename T1, typename T2>
struct is_constructible_tuple : std::false_type {};

template<typename T1, typename... Args>
struct is_constructible_tuple<T1, std::tuple<Args...>> : conjunction<is_constructible<T1, Args>...> {};



template <typename T>
struct is_ordered_map
{
    using one = char;

    struct two
    {
        char x[2]; // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
    };

    template <typename C> static one test( decltype(&C::capacity) ) ;
    template <typename C> static two test(...);

    enum { value = sizeof(test<T>(nullptr)) == sizeof(char) }; // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
};


template < typename T, typename U, enable_if_t < !std::is_same<T, U>::value, int > = 0 >
T conditional_static_cast(U value)
{
    return static_cast<T>(value);
}

template<typename T, typename U, enable_if_t<std::is_same<T, U>::value, int> = 0>
T conditional_static_cast(U value)
{
    return value;
}

}
}
