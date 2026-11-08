#include <string>

namespace Slic3r {
    std::string make_temp_stl_with_modelio(const std::string &input_file);

    void delete_temp_file(const std::string &temp_file);
}
