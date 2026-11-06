#pragma once

#include <string>
#include <nlohmann/detail/macro_scope.hpp>

namespace nlohmann
{
namespace detail
{














inline void replace_substring(std::string& s, const std::string& f,
                              const std::string& t)
{
    JSON_ASSERT(!f.empty());
    for (auto pos = s.find(f);
            pos != std::string::npos;
            s.replace(pos, f.size(), t),
            pos = s.find(f, pos + t.size()))
    {}
}








inline std::string escape(std::string s)
{
    replace_substring(s, "~", "~0");
    replace_substring(s, "/", "~1");
    return s;
}








static void unescape(std::string& s)
{
    replace_substring(s, "~1", "/");
    replace_substring(s, "~0", "~");
}

}
}
