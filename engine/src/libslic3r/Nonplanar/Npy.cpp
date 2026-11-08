#include "Npy.hpp"

#include "libslic3r/Exception.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <miniz.h>
#include <sstream>
#include <string>
#include <vector>

namespace Slic3r::Nonplanar {

namespace {

[[noreturn]] void fail(const std::string &what) { throw RuntimeError(what); }

std::string shape_text(const std::vector<size_t> &shape)
{
    std::string s = "(";
    for (size_t i = 0; i < shape.size(); ++i)
        s += std::to_string(shape[i]) + (shape.size() == 1 || i + 1 < shape.size() ? "," : "");
    return s + ")";
}

std::string preamble(const std::string &descr, const std::vector<size_t> &shape)
{
    std::string header = "{'descr': '" + descr + "', 'fortran_order': False, 'shape': " + shape_text(shape) + ", }";
    const size_t total = 10 + header.size() + 1;
    header.append((64 - total % 64) % 64, ' ');
    header += '\n';
    std::string out("\x93NUMPY\x01\x00", 8);
    out += char(header.size() & 0xff);
    out += char((header.size() >> 8) & 0xff);
    return out + header;
}

}

size_t NpyArray::count() const
{
    size_t n = 1;
    for (size_t s : shape) {
        if (s != 0 && n > std::numeric_limits<size_t>::max() / s)
            fail("array shape is too large");
        n *= s;
    }
    return n;
}

NpyArray parse_npy(const std::string &name, const unsigned char *data, size_t size)
{
    if (size < 10 || std::memcmp(data, "\x93NUMPY", 6) != 0)
        fail(name + " is not a .npy array");
    const int major = data[6];
    size_t    header_len, offset;
    if (major == 1) {
        header_len = size_t(data[8]) | (size_t(data[9]) << 8);
        offset     = 10;
    } else {
        if (size < 12) fail(name + " is truncated");
        header_len = size_t(data[8]) | (size_t(data[9]) << 8) | (size_t(data[10]) << 16) | (size_t(data[11]) << 24);
        offset     = 12;
    }
    if (header_len > size - offset)
        fail(name + " is truncated");
    const std::string header(reinterpret_cast<const char *>(data + offset), header_len);
    offset += header_len;

    auto value_of = [&](const std::string &key) {
        const size_t k = header.find("'" + key + "'");
        if (k == std::string::npos) fail(name + " has no '" + key + "'");
        const size_t colon = header.find(':', k);
        return header.substr(colon + 1);
    };
    std::string descr = value_of("descr");
    descr             = descr.substr(descr.find('\'') + 1);
    descr             = descr.substr(0, descr.find('\''));
    if (value_of("fortran_order").find("True") < value_of("fortran_order").find(','))
        fail(name + " is in Fortran order");
    std::string shape = value_of("shape");
    shape             = shape.substr(shape.find('(') + 1, shape.find(')') - shape.find('(') - 1);

    NpyArray out;
    std::istringstream dims(shape);
    for (std::string dim; std::getline(dims, dim, ',');)
        if (dim.find_first_not_of(" ") != std::string::npos)
            out.shape.push_back(size_t(std::stoull(dim)));

    if (descr.size() < 3 || descr[0] == '>')
        fail(name + " has unsupported dtype " + descr);
    const char   kind  = descr[1];
    const size_t bytes = size_t(std::stoul(descr.substr(2)));
    const size_t n     = out.count();
    if (kind == 'U') {
        if (n == 0 || bytes == 0 || 4 * bytes > size - offset)
            fail(name + " is truncated");
        out.is_text = true;
        for (size_t i = 0; i < bytes; ++i) {
            uint32_t c;
            std::memcpy(&c, data + offset + 4 * i, 4);
            if (c == 0)
                break;
            out.text += c < 0x80 ? char(c) : '?';
        }
        return out;
    }
    if (bytes == 0 || n > (size - offset) / bytes)
        fail(name + " is truncated");
    out.values.resize(n);
    const unsigned char *p = data + offset;
    for (size_t i = 0; i < n; ++i, p += bytes) {
        double v;
        if (kind == 'f' && bytes == 8)      { double x; std::memcpy(&x, p, 8); v = x; }
        else if (kind == 'f' && bytes == 4) { float x; std::memcpy(&x, p, 4); v = x; }
        else if (kind == 'i' && bytes == 8) { int64_t x; std::memcpy(&x, p, 8); v = double(x); }
        else if (kind == 'i' && bytes == 4) { int32_t x; std::memcpy(&x, p, 4); v = x; }
        else if (kind == 'i' && bytes == 2) { int16_t x; std::memcpy(&x, p, 2); v = x; }
        else if (kind == 'i' && bytes == 1) { v = double(int8_t(*p)); }
        else if ((kind == 'u' || kind == 'b') && bytes == 1) { v = *p; }
        else if (kind == 'u' && bytes == 8) { uint64_t x; std::memcpy(&x, p, 8); v = double(x); }
        else if (kind == 'u' && bytes == 4) { uint32_t x; std::memcpy(&x, p, 4); v = double(x); }
        else fail(name + " has unsupported dtype " + descr);
        out.values[i] = v;
    }
    return out;
}

NpyArray read_npz_array(mz_zip_archive &zip, const std::string &name, bool required, const std::string &what)
{
    size_t size = 0;
    void  *data = mz_zip_reader_extract_file_to_heap(&zip, (name + ".npy").c_str(), &size, 0);
    if (data == nullptr) {
        if (required)
            fail(what + ": missing array '" + name + "'");
        return NpyArray();
    }
    std::unique_ptr<void, decltype(&mz_free)> owner(data, &mz_free);
    return parse_npy(what + ": " + name, static_cast<const unsigned char *>(data), size);
}

std::string npy_f4(const std::vector<size_t> &shape, const float *values)
{
    NpyArray a;
    a.shape = shape;
    return preamble("<f4", shape) + std::string(reinterpret_cast<const char *>(values), a.count() * 4);
}

std::string npy_f8(const std::vector<size_t> &shape, const double *values)
{
    NpyArray a;
    a.shape = shape;
    return preamble("<f8", shape) + std::string(reinterpret_cast<const char *>(values), a.count() * 8);
}

std::string npy_text(const std::string &text)
{
    std::string out = preamble("<U" + std::to_string(std::max<size_t>(1, text.size())), {});
    for (size_t i = 0; i < std::max<size_t>(1, text.size()); ++i) {
        const uint32_t c = i < text.size() ? uint32_t(static_cast<unsigned char>(text[i])) : 0u;
        out.append(reinterpret_cast<const char *>(&c), 4);
    }
    return out;
}

}
