#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace Slic3r::Nonplanar {

inline std::string head_box_label(const std::string &name)
{
    std::string out, word;
    int         depth = 0;
    auto        flush = [&out, &word] {
        if (!word.empty() && std::none_of(word.begin(), word.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            out += (out.empty() ? "" : " ") + word;
        word.clear();
    };
    for (const char c : name) {
        if (c == '[') {
            flush();
            ++depth;
        } else if (c == ']')
            depth = std::max(0, depth - 1);
        else if (depth > 0)
            continue;
        else if (c == '_' || c == ' ')
            flush();
        else
            word += c;
    }
    flush();
    return out.empty() ? name : out;
}

}
