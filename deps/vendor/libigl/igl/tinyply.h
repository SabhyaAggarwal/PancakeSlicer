/*
 * tinyply 2.3.2 (https://github.com/ddiakopoulos/tinyply)
 *
 * A single-header, zero-dependency (except the C++ STL) public domain implementation
 * of the PLY mesh file format. Requires C++11; errors are handled through exceptions.
 *
 * This software is in the public domain. Where that dedication is not
 * recognized, you are granted a perpetual, irrevocable license to copy,
 * distribute, and modify this file as you see fit.
 *
 * Authored by Dimitri Diakopoulos (http://www.dimitridiakopoulos.com)
 *
 * tinyply.h may be included in many files, however in a single compiled file,
 * the implementation must be created with the following defined prior to header inclusion
 * #define TINYPLY_IMPLEMENTATION
 *
 */

////////////////////////
//   tinyply header   //
////////////////////////

#ifndef tinyply_h
#define tinyply_h
#include "igl_inline.h"

#include <vector>
#include <string>
#include <stdint.h>
#include <cstddef>
#include <sstream>
#include <memory>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <functional>
#include <stdint.h>

namespace igl
{
namespace tinyply
{

    enum class Type : uint8_t
    {
        INVALID,
        INT8,
        UINT8,
        INT16,
        UINT16,
        INT32,
        UINT32,
        FLOAT32,
        FLOAT64
    };

    struct PropertyInfo
    {
        PropertyInfo() {};
        PropertyInfo(int stride, std::string str)
            : stride(stride), str(str) {}
        int stride {0};
        std::string str;
    };

    static std::map<Type, PropertyInfo> PropertyTable
    {
        { Type::INT8,    PropertyInfo(1, std::string("char")) },
        { Type::UINT8,   PropertyInfo(1, std::string("uchar")) },
        { Type::INT16,   PropertyInfo(2, std::string("short")) },
        { Type::UINT16,  PropertyInfo(2, std::string("ushort")) },
        { Type::INT32,   PropertyInfo(4, std::string("int")) },
        { Type::UINT32,  PropertyInfo(4, std::string("uint")) },
        { Type::FLOAT32, PropertyInfo(4, std::string("float")) },
        { Type::FLOAT64, PropertyInfo(8, std::string("double")) },
        { Type::INVALID, PropertyInfo(0, std::string("INVALID"))}
    };

    class Buffer
    {
        uint8_t * alias{ nullptr };
        struct delete_array { void operator()(uint8_t * p) { delete[] p; } };
        std::unique_ptr<uint8_t, decltype(Buffer::delete_array())> data;
        size_t size {0};
    public:
        Buffer() {};
        Buffer(const size_t size) : data(new uint8_t[size], delete_array()), size(size) { alias = data.get(); }
        Buffer(uint8_t * ptr): alias(ptr) { }
        uint8_t * get() { return alias; }
        size_t size_bytes() const { return size; }
    };

    struct PlyData
    {
        Type t;
        Buffer buffer;
        size_t count {0};
        bool isList {false};
    };

    struct PlyProperty
    {
        PlyProperty(std::istream & is);
        PlyProperty(Type type, std::string & _name) : name(_name), propertyType(type) {}
        PlyProperty(Type list_type, Type prop_type, std::string & _name, size_t list_count)
            : name(_name), propertyType(prop_type), isList(true), listType(list_type), listCount(list_count) {}
        std::string name;
        Type propertyType{ Type::INVALID };
        bool isList{ false };
        Type listType{ Type::INVALID };
        size_t listCount {0};
    };

    struct PlyElement
    {
        PlyElement(std::istream & istream);
        PlyElement(const std::string & _name, size_t count) : name(_name), size(count) {}
        std::string name;
        size_t size {0};
        std::vector<PlyProperty> properties;
    };

    struct PlyFile
    {
        struct PlyFileImpl;
        std::unique_ptr<PlyFileImpl> impl;

        PlyFile();
        ~PlyFile();







        bool parse_header(std::istream & is);





        void read(std::istream & is);





        void write(std::ostream & os, bool isBinary);





        std::vector<PlyElement> get_elements() const;
        std::vector<std::string> get_info() const;
        std::vector<std::string> & get_comments();
        bool is_binary_file() const;








        std::shared_ptr<PlyData> request_properties_from_element(const std::string & elementKey,
            const std::vector<std::string> propertyKeys, const uint32_t list_size_hint = 0);

        void add_properties_to_element(const std::string & elementKey,
            const std::vector<std::string> propertyKeys,
            const Type type,
            const size_t count,
            uint8_t * data,
            const Type listType,
            const size_t listCount);
    };

}
}

#ifndef IGL_STATIC_LIBRARY

#  include "tinyply.cpp"
#endif


#endif
