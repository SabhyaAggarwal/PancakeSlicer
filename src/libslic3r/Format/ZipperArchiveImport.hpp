#ifndef ZIPPERARCHIVEIMPORT_HPP
#define ZIPPERARCHIVEIMPORT_HPP

#include <utility>
#include <vector>
#include <string>
#include <cstdint>

#include <boost/property_tree/ptree.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {


struct EntryBuffer
{
    std::vector<uint8_t> buf;
    std::string          fname;
};


struct ZipperArchive
{
    boost::property_tree::ptree profile, config;
    std::vector<EntryBuffer>    entries;
};


const constexpr char *CONFIG_FNAME  = "config.ini";
const constexpr char *PROFILE_FNAME = "prusaslicer.ini";









ZipperArchive read_zipper_archive(const std::string &zipfname,
                                  const std::vector<std::string> &includes,
                                  const std::vector<std::string> &excludes);






std::pair<DynamicPrintConfig, ConfigSubstitutions> extract_profile(
    const ZipperArchive &arch, DynamicPrintConfig &inout);

}

#endif
