#ifndef SLA_JOBCONTROLLER_HPP
#define SLA_JOBCONTROLLER_HPP

#include <functional>
#include <string>

namespace Slic3r { namespace sla {



struct JobController
{
    using StatusFn = std::function<void(unsigned, const std::string&)>;
    using StopCond = std::function<bool(void)>;
    using CancelFn = std::function<void(void)>;


    StatusFn statuscb = [](unsigned, const std::string&){};


    StopCond stopcondition = [](){ return false; };





    CancelFn cancelfn = [](){};
};

}}

#endif
