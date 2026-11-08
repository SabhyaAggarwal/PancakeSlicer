#pragma once

#include <vector>

namespace Slic3r {
class Model;
class PartPlateList;
namespace GUI {
class ObjectList;

struct BeltPurgeSignature
{
    bool valid          = false;
    int  filament_count = 0;
    long key[14]        = {0};
    bool operator==(const BeltPurgeSignature &o) const
    {
        if (valid != o.valid || filament_count != o.filament_count)
            return false;
        for (int i = 0; i < 14; ++i)
            if (key[i] != o.key[i])
                return false;
        return true;
    }
};

bool ensure_belt_purge_tower(Model &model, PartPlateList &partplate_list, ObjectList *obj_list, std::vector<BeltPurgeSignature> &sigs);

}
}
