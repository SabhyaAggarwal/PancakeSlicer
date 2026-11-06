#pragma once

namespace Slic3r {
class LayerRegion;
class PrintObject;
}

namespace Slic3r::Nonplanar {


bool has_top_texture(const PrintObject &object);


void texture_tops(PrintObject &object);


void order_textured(LayerRegion &region);

}
