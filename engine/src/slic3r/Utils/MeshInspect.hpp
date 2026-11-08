#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace Slic3r {

class Model;

namespace MeshInspect {

void inspect_to_json(const Model &model, const std::vector<std::string> &source_paths, std::ostream &out, size_t max_planes = 8);

}
}
