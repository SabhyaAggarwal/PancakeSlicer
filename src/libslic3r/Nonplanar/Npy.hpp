#pragma once



#include <miniz.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r::Nonplanar {

struct NpyArray
{
    std::vector<size_t> shape;
    std::vector<double> values;
    std::string         text;
    bool                is_text = false;
    size_t              count() const;
};


NpyArray parse_npy(const std::string &what, const unsigned char *data, size_t size);


NpyArray read_npz_array(mz_zip_archive &zip, const std::string &name, bool required, const std::string &what);


std::string npy_f4(const std::vector<size_t> &shape, const float *values);
std::string npy_f8(const std::vector<size_t> &shape, const double *values);
std::string npy_text(const std::string &text);

}
