#ifndef slic3r_PaintCLI_hpp_
#define slic3r_PaintCLI_hpp_

#include <iosfwd>
#include <string>
#include <vector>

namespace Slic3r {
class Model;

namespace PaintCLI {

void inspect_to_json(const Model &model, const std::vector<std::string> &source_paths,
                     std::ostream &out);

}
}

#endif
