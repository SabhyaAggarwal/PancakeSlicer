#ifndef slic3r_FileParserError_hpp_
#define slic3r_FileParserError_hpp_

#include "Exception.hpp"
#include "libslic3r.h"

#include <sstream>
#include <string>
#include <boost/filesystem/path.hpp>
#include <stdexcept>

namespace Slic3r {


class file_parser_error: public Slic3r::RuntimeError
{
public:
    file_parser_error(const std::string &msg, const std::string &file, unsigned long line = 0) :
        Slic3r::RuntimeError(format_what(msg, file, line)),
        m_message(msg), m_filename(file), m_line(line) {}
    file_parser_error(const std::string &msg, const boost::filesystem::path &file, unsigned long line = 0) :
        Slic3r::RuntimeError(format_what(msg, file.string(), line)),
        m_message(msg), m_filename(file.string()), m_line(line) {}


    ~file_parser_error() throw() {}


    std::string message() const { return m_message; }

    std::string filename() const { return m_filename; }

    unsigned long line() const { return m_line; }

private:
    std::string     m_message;
    std::string     m_filename;
    unsigned long   m_line;


    static std::string format_what(const std::string &msg, const std::string &file, unsigned long l)
    {
        std::stringstream stream;
        stream << (file.empty() ? "<unspecified file>" : file.c_str());
        if (l > 0)
            stream << '(' << l << ')';
        stream << ": " << msg;
        return stream.str();
    }
};

};

#endif
