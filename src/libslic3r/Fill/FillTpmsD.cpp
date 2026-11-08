#include <cmath>
#include <algorithm>
#include <utility>
#include <math.h>
#include <cstddef>
#include <vector>

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"
#include "FillTpmsD.hpp"
#include "FillTpmsAdaptive.hpp"

namespace Slic3r {

static double scaled_floor(double x,double scale){
	return std::floor(x/scale)*scale;
}

static float schwarz_d(float x, float y, float z)
{
    return std::sin(x) * std::sin(y) * std::sin(z) - std::cos(x) * std::cos(y) * std::cos(z);
}

static Polylines make_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height)
{
    const double scaleFactor = scale_(line_spacing) / density_adjusted;

    const double tolerance = std::min(line_spacing / 2, FillTpmsD::PatternTolerance) / unscale<double>(scaleFactor);

    const double z = gridZ / scaleFactor;
    Polylines result;

	double a=sin(z)-cos(z);
	double b=sin(z)+cos(z);
	double minU=0-height;
	double maxU=width-0;
	double minV=0+0;
	double maxV=width+height;
	const bool swapUV=(std::abs(a)>std::abs(b));
	if(swapUV) {
		std::swap(a,b);
		std::swap(minU,minV);
		std::swap(maxU,maxV);
	}
	std::vector<std::pair<double,double>> wave;
	{
		const auto v=[&](double u){return acos(a/b*cos(u));};
		const int initialSegments=16;
		for(int c=0;c<=initialSegments;++c){
			const double u=minU+2*M_PI*c/initialSegments;
			wave.emplace_back(u,v(u));
		}
		{
			int current=0;
			while(current+1<int(wave.size())){
				const double u1=wave[current].first;
				const double u2=wave[current+1].first;
				const double middleU=(u1+u2)/2;
				const double v1=wave[current].second;
				const double v2=wave[current+1].second;
				const double middleV=v((u1+u2)/2);
				if(std::abs(middleV-(v1+v2)/2)>tolerance)
					wave.emplace(wave.begin()+current+1,middleU,middleV);
				else
					++current;
			}
		}
		for(int c=1;c<int(wave.size()) && wave.back().first<maxU;++c)
			wave.emplace_back(wave[c].first+2*M_PI,wave[c].second);
	}
	for(double vShift=scaled_floor(minV,2*M_PI);vShift<maxV+2*M_PI;vShift+=2*M_PI) {
		for(bool forwardRoot:{false,true}) {
			result.emplace_back();
			for(const auto &pair:wave) {
				const double u=pair.first;
				double v=pair.second;
				v=(forwardRoot?v:-v)+vShift;
				const double x=(u+v)/2;
				const double y=(v-u)/2*(swapUV?-1:1);
				result.back().points.emplace_back(x*scaleFactor,y*scaleFactor);
			}
		}
	}
    return result;
}

constexpr double FillTpmsD::PatternTolerance;

void FillTpmsD::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon, 
    Polylines                       &polylines_out)
{
    if (params.tpms_adaptive == TpmsAdaptiveMode::SteppedShells && this->tpms_radial_field != nullptr) {
        fill_tpms_shells(*this->tpms_radial_field, expolygon, this->z - 0.5 * params.layer_height, params, this->spacing,
                         [&](const FillParams &shell_params, const ExPolygon &shell) {
                             this->_fill_surface_single(shell_params, thickness_layers, direction, shell, polylines_out);
                         });
        return;
    }

    auto infill_angle = float(this->angle + (CorrectionAngle * 2*M_PI) / 360.);
    if(std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    Polylines polylines;
    if (params.tpms_adaptive != TpmsAdaptiveMode::Disabled && this->tpms_radial_field != nullptr) {
        auto frequency = [&params, this](double density) { return density * DensityAdjust / (params.multiline * this->spacing); };
        BoundingBox bbox = expolygon.contour.bounding_box();
        bbox.offset(scale_((params.multiline + 1) * this->spacing));
        polylines = make_adaptive_tpms({schwarz_d, frequency(params.density), frequency(params.tpms_interior_density), params.tpms_adaptive_gradient},
                                       *this->tpms_radial_field, bbox, this->z, params.layer_height, this->spacing, infill_angle);
    } else {
        BoundingBox bb = expolygon.contour.bounding_box();
        double      density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline);
        coord_t     distance = coord_t(scale_(this->spacing)  / density_adjusted);

        bb.merge(align_to_grid(bb.min, Point(2*M_PI*distance, 2*M_PI*distance)));

        polylines = make_waves(
            scale_(this->z),
            density_adjusted,
            this->spacing,
            ceil(bb.size()(0) / distance) + 1.,
            ceil(bb.size()(1) / distance) + 1.);

        for (Polyline &pl : polylines)
            pl.translate(bb.min);
    }

    multiline_fill(polylines, params, spacing);

	polylines = intersection_pl(std::move(polylines), expolygon);

    if (! polylines.empty()) {
        const double minlength = scale_(0.8 * this->spacing);
		polylines.erase(
			std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return pl.length() < minlength; }),
			polylines.end());
    }

	if (! polylines.empty()) {
		size_t polylines_out_first_idx = polylines_out.size();
		if (params.dont_connect())
        	append(polylines_out, chain_polylines(polylines));
        else
            this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

        if (std::abs(infill_angle) >= EPSILON) {
	        for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++ it)
	        	it->rotate(infill_angle);
	    }
    }
}

}