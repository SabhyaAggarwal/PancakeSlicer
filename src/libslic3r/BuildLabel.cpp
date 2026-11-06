
#include "BuildLabel.hpp"
#include "git_commit_hash.h"

#include <string>

namespace Slic3r {

std::string nonplanar_build_label()
{
    return "nonplanar-" GIT_COMMIT_HASH GIT_COMMIT_SUFFIX;
}

}
