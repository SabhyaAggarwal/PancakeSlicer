#ifndef slic3r_enum_bitmask_hpp_
#define slic3r_enum_bitmask_hpp_





#include <type_traits>

namespace Slic3r {


template<class option_type, typename = typename std::enable_if<std::is_enum<option_type>::value>::type>
class enum_bitmask {

    using underlying_type = typename std::underlying_type<option_type>::type;


    static constexpr underlying_type mask_value(option_type o) { return 1 << static_cast<underlying_type>(o); }


    explicit constexpr enum_bitmask(underlying_type o) : m_bits(o) {}

public:

    constexpr enum_bitmask() : m_bits(0) {}




    constexpr enum_bitmask(option_type o) : m_bits(mask_value(o)) {}


    constexpr enum_bitmask operator|(option_type t) const { return enum_bitmask(m_bits | mask_value(t)); }


    constexpr enum_bitmask operator|(enum_bitmask<option_type> t) const { return enum_bitmask(m_bits | t.m_bits); }


    constexpr void operator|=(option_type t) { m_bits = enum_bitmask(m_bits | mask_value(t)); }


    constexpr void operator|=(enum_bitmask<option_type> t) { m_bits = enum_bitmask(m_bits | t.m_bits); }


    constexpr bool operator&(option_type t) const { return m_bits & mask_value(t); }
    constexpr bool has(option_type t) const { return m_bits & mask_value(t); }

    constexpr bool operator==(const enum_bitmask r) const { return m_bits == r.m_bits; }
    constexpr bool operator!=(const enum_bitmask r) const { return m_bits != r.m_bits; }

    constexpr bool lower(const enum_bitmask r) const { return m_bits < r.m_bits; }

private:
    underlying_type m_bits = 0;
};


template<typename Enum> struct is_enum_bitmask_type { static const bool enable = false; };
#define ENABLE_ENUM_BITMASK_OPERATORS(x) template<> struct is_enum_bitmask_type<x> { static const bool enable = true; };
template<class Enum> inline constexpr bool is_enum_bitmask_type_v = is_enum_bitmask_type<Enum>::enable;



template <class option_type>
constexpr std::enable_if_t<is_enum_bitmask_type_v<option_type>, enum_bitmask<option_type>> operator|(option_type lhs, option_type rhs) {
    static_assert(std::is_enum_v<option_type>);
    return enum_bitmask<option_type>{lhs} | rhs;
}

template <class option_type>
constexpr std::enable_if_t<is_enum_bitmask_type_v<option_type>, enum_bitmask<option_type>> operator|(option_type lhs, enum_bitmask<option_type> rhs) {
    static_assert(std::is_enum_v<option_type>);
    return enum_bitmask<option_type>{lhs} | rhs;
}

template <class option_type>
constexpr std::enable_if_t<is_enum_bitmask_type_v<option_type>, enum_bitmask<option_type>> only_if(bool condition, option_type opt) {
    static_assert(std::is_enum_v<option_type>);
    return condition ? enum_bitmask<option_type>{opt} : enum_bitmask<option_type>{};
}

template <class option_type>
constexpr std::enable_if_t<is_enum_bitmask_type_v<option_type>, enum_bitmask<option_type>> only_if(bool condition, enum_bitmask<option_type> opt) {
    static_assert(std::is_enum_v<option_type>);
    return condition ? opt : enum_bitmask<option_type>{};
}

}

#endif
