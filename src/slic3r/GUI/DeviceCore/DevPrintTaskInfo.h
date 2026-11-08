#pragma once

#include <string>
#include <vector>

namespace Slic3r
{

struct DevPrintTaskRatingInfo
{
    bool        request_successful;
    int         http_code;
    int         rating_id;
    int         start_count;
    bool        success_printed;
    std::string content;
    std::vector<std::string>  image_url_paths;
};

class DevPrintTaskInfo
{

};

}