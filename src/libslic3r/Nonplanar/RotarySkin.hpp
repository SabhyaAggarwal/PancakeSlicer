#pragma once

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct ConformalResult;

bool rotary_skin_claims(const PrintObject &object);
constexpr double SkinClaimArea  = 300.;
constexpr double SkinClaimShare = 0.1;

void build_rotary_skin(PrintObject &object, ConformalResult &result);

}
