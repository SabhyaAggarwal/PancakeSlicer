#ifndef slic3r_LocalesUtils_hpp_
#define slic3r_LocalesUtils_hpp_

#include <cstddef>
#include <string>
#include <clocale>
#include <iomanip>
#include <cassert>
#include <string_view>

#ifdef __APPLE__
#include <xlocale.h>
#endif

namespace Slic3r {



class CNumericLocalesSetter {
public:
    CNumericLocalesSetter();
    ~CNumericLocalesSetter();

    CNumericLocalesSetter(const CNumericLocalesSetter&) = delete;
    CNumericLocalesSetter& operator=(const CNumericLocalesSetter&) = delete;

private:

    bool m_nested { false };
#ifdef _WIN32
    std::string m_orig_numeric_locale;
#else
    locale_t m_original_locale;
    locale_t m_new_locale;
#endif

};



bool is_decimal_separator_point();






std::string float_to_string_decimal_point(double value, int precision = -1);

double string_to_double_decimal_point(const std::string_view str, size_t* pos = nullptr);


double atof_decimal_point(std::string_view str);

}

#endif
