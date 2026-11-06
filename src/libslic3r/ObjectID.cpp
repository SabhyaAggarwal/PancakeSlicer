#include "ObjectID.hpp"
#include <cstddef>

namespace Slic3r {

size_t ObjectBase::s_last_id = 0;


ObjectID wipe_tower_object_id()
{
    static ObjectBase mine;
    return mine.id();
}

ObjectID wipe_tower_instance_id()
{
    static ObjectBase mine;
    return mine.id();
}

ObjectWithTimestamp::Timestamp ObjectWithTimestamp::s_last_timestamp = 1;

}
