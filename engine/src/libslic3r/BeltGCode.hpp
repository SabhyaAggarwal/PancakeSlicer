#pragma once

#include "GCode.hpp"
#include "Point.hpp"
#include "Print.hpp"

namespace Slic3r {

class BeltGCode : public GCode
{
protected:
    void init_belt_writer(Print &print) override;
    void write_belt_header(GCodeOutputStream &file, const Print &print) override;
    void on_set_origin(const PrintObject *obj, const Point &inst_shift) override;
};

}
