#ifndef SLA_RASTERBASE_HPP
#define SLA_RASTERBASE_HPP

#include <cstddef>
#include <functional>
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include <ostream>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <utility>
#include <cstdint>

#include <libslic3r/ExPolygon.hpp>

namespace Slic3r {

namespace sla {

class EncodedRaster {
protected:
    std::vector<uint8_t> m_buffer;
    std::string m_ext;
public:
    EncodedRaster() = default;
    explicit EncodedRaster(std::vector<uint8_t> &&buf, std::string ext)
        : m_buffer(std::move(buf)), m_ext(std::move(ext))
    {}
    
    size_t size() const { return m_buffer.size(); }
    const void * data() const { return m_buffer.data(); }
    const char * extension() const { return m_ext.c_str(); }
};

struct Resolution {
    size_t width_px = 0;
    size_t height_px = 0;

    Resolution() = default;
    Resolution(size_t w, size_t h) : width_px(w), height_px(h) {}
    size_t pixels() const { return width_px * height_px; }
};

struct PixelDim {
    double w_mm = 1.;
    double h_mm = 1.;

    PixelDim() = default;
    PixelDim(double px_width_mm, double px_height_mm)
        : w_mm(px_width_mm), h_mm(px_height_mm)
    {}
};

using RasterEncoder =
    std::function<EncodedRaster(const void *ptr, size_t w, size_t h, size_t num_components)>;

class RasterBase {
public:
    
    enum Orientation { roLandscape, roPortrait };
    
    using TMirroring = std::array<bool, 2>;
    static const constexpr TMirroring NoMirror = {false, false};
    static const constexpr TMirroring MirrorX  = {true, false};
    static const constexpr TMirroring MirrorY  = {false, true};
    static const constexpr TMirroring MirrorXY = {true, true};
    
    struct Trafo {
        bool mirror_x = false, mirror_y = false, flipXY = false;
        coord_t center_x = 0, center_y = 0;
        
        Trafo(Orientation o = roLandscape, const TMirroring &mirror = NoMirror)
            : mirror_x(o == roPortrait ? !mirror[0] : mirror[0])
            , mirror_y(!mirror[1])
            , flipXY(o == roPortrait)
        {}
        
        TMirroring get_mirror() const { return { (roPortrait ? !mirror_x : mirror_x), mirror_y}; }
        Orientation get_orientation() const { return flipXY ? roPortrait : roLandscape; }
        Point get_center() const { return {center_x, center_y}; }
    };
    
    virtual ~RasterBase() = default;
    
    virtual void draw(const ExPolygon& poly) = 0;
    
    virtual Trafo      trafo() const = 0;
    
    virtual EncodedRaster encode(RasterEncoder encoder) const = 0;
};

struct PNGRasterEncoder {
    EncodedRaster operator()(const void *ptr, size_t w, size_t h, size_t num_components);
};

struct PPMRasterEncoder {
    EncodedRaster operator()(const void *ptr, size_t w, size_t h, size_t num_components);
};

std::ostream& operator<<(std::ostream &stream, const EncodedRaster &bytes);

std::unique_ptr<RasterBase> create_raster_grayscale_aa(
    const Resolution        &res,
    const PixelDim          &pxdim,
    double                   gamma = 1.0,
    const RasterBase::Trafo &tr    = {});

}}

#endif
