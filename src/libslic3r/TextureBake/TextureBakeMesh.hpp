#pragma once

#include "TextureBakeIndex.hpp"
#include "../TriangleMesh.hpp"
#include <vector>
#include <cstdint>

namespace Slic3r {
namespace TextureBake {

TriSoup to_soup(const indexed_triangle_set &its, const std::vector<uint8_t> &face_excluded = {});

indexed_triangle_set to_indexed_triangle_set(const TriSoup &soup, std::vector<int> *face_color = nullptr);

}
}
