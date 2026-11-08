#ifndef slic3r_CrealityHostDiscovery_hpp_
#define slic3r_CrealityHostDiscovery_hpp_

#include <string>
#include <vector>

namespace Slic3r {

struct CrealityHost
{
    std::string ip;
    std::string service_name;
    std::string hostname;
    std::string model_code;
    std::string model_name;
    std::string mac;
    bool        cfs_capable = false;
};

class CrealityHostDiscovery
{
public:
    static std::vector<CrealityHost> scan(bool probe_info = true);
};

}

#endif
