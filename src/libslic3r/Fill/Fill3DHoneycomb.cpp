#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "FillBase.hpp"
#include "FillCornerSmoothing.hpp"
#include "libslic3r/libslic3r.h"
#include <cstdlib>
#include <vector>
#include <cstddef>
#include "libslic3r/Point.hpp"
#include <cassert>
#include <cmath>
#include <algorithm>
#include "libslic3r/Polyline.hpp"
#include <utility>
#include "Fill3DHoneycomb.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {


template <typename T> int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}














static coordf_t triWave(coordf_t pos, coordf_t gridSize)
{
  float t = (pos / (gridSize * 2.)) + 0.25;
  t = t - (int)t;
  return((1. - abs(t * 8. - 4.)) * (gridSize / 4.) + (gridSize / 4.));
}





static coordf_t troctWave(coordf_t pos, coordf_t gridSize, coordf_t Zpos)
{
  coordf_t Zcycle = triWave(Zpos, gridSize);
  coordf_t perpOffset = Zcycle / 2;
  coordf_t y = triWave(pos, gridSize);
  return((abs(y) > abs(perpOffset)) ?
	 (sgn(y) * perpOffset) :
	 (y * sgn(perpOffset)));
}















 static std::vector<coordf_t> getCriticalPoints(coordf_t Zpos, coordf_t gridSize)
{
  std::vector<coordf_t> res;
  coordf_t perpOffset = abs(triWave(Zpos, gridSize) / 2.);
  coordf_t normalisedOffset = perpOffset / gridSize;
  if(normalisedOffset > 0){
    res.push_back(gridSize * (0. + normalisedOffset));
    res.push_back(gridSize * (1. - normalisedOffset));
    res.push_back(gridSize * (1. + normalisedOffset));
    res.push_back(gridSize * (2. - normalisedOffset));
  }
  return(res);
}



static Polylines addTops(coordf_t Zpos, coordf_t gridSize, coordf_t lengthX, coordf_t lengthY, coordf_t spacing,
                         size_t multiline_count, size_t topDistance)
{
  coordf_t zCycle = fmod(Zpos + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  coordf_t zHalfCycle = fmod(zCycle, 0.5) * 2.;
  bool printVert = zCycle < 0.5;
  coordf_t offsetX = multiline_count;
  coordf_t offsetY = multiline_count;
  coordf_t perpOffset = abs(triWave(Zpos, gridSize) / 2.);
  coordf_t gridPoint = gridSize * (0. + perpOffset / gridSize);
  coordf_t topOffset = gridSize / 2.0 - abs(troctWave(gridPoint, gridSize, Zpos));
  coordf_t multilineAdjust = (sqrt(2) - 1.0) / 2.;
  Polylines lines;
  size_t pointCount = 0;
  coordf_t gridStartL = gridSize * 0.5 - topOffset;
  coordf_t gridEndL = gridSize * 0.5 + topOffset;
  if((topDistance == 0) && (multiline_count == 1)){

    gridStartL -= spacing;
    gridEndL += spacing;
  } else if(multiline_count > 1) {

    gridStartL -= spacing * multiline_count * multilineAdjust;
    gridEndL += spacing * multiline_count * multilineAdjust;
  }

  coordf_t gridStartP = gridSize * 0.5 - topOffset + spacing * multiline_count / 2. + spacing / 2.;
  coordf_t gridEndP = gridSize * 0.5 + topOffset - spacing * multiline_count / 2. - spacing / 2.;
  coordf_t x, y;
  int xm, ym;

  if((topDistance % 2) == 0){
    std::swap(gridStartL, gridStartP);
    std::swap(gridEndL, gridEndP);
  }


  coordf_t region_count = floor((gridEndP - gridStartP) / (spacing / sqrt(2)));
  if(region_count <= 0){
    return lines;
  }
  spacing = (gridEndP - gridStartP) / region_count;
  for (x = offsetX, xm = 0; x <= (lengthX); x+= gridSize, xm = xm ^ 1) {
    for (y = offsetY, ym = 0; y <= (lengthY); y += gridSize, ym = ym ^ 1) {
      if(((xm ^ ym) == 1) == printVert){
        continue;
      }




      Polyline newPoints;
      int dirMod = xm ^ ym;
      if(printVert == (topDistance % 2)){
        if(y < (lengthY - spacing * multiline_count * 1.5)){
          coordf_t endPMod = std::min(lengthX - (multiline_count * (spacing + 1) / 2.), x + gridEndP) - x;
          coordf_t endLMod = std::min(lengthY - (multiline_count * (spacing + 1) / 2.), y + gridEndL) - y;
          for(coordf_t xi = gridStartP; xi < (endPMod + EPSILON); xi += spacing, dirMod = dirMod ^ 1){
            newPoints.points.push_back((dirMod == 0) ? Point(x + xi, y + gridStartL) : Point(x + xi, y + endLMod));
            newPoints.points.push_back((dirMod == 0) ? Point(x + xi, y + endLMod) : Point(x + xi, y + gridStartL));
            pointCount += 2;
          }
        }
      } else {
        if(x < (lengthX - spacing * multiline_count * 1.5)){
          coordf_t endPMod = std::min(lengthY - (multiline_count * (spacing + 1) / 2.), y + gridEndP) - y;
          coordf_t endLMod = std::min(lengthX - (multiline_count * (spacing + 1) / 2.), x + gridEndL) - x;
          for(coordf_t yi = gridStartP; yi < (endPMod + EPSILON); yi += spacing, dirMod = dirMod ^ 1){
            newPoints.points.push_back((dirMod == 0) ? Point(x + gridStartL, y + yi) : Point(x + endLMod, y + yi));
            newPoints.points.push_back((dirMod == 0) ? Point(x + endLMod, y + yi) : Point(x + gridStartL, y + yi));
            pointCount += 2;
          }
        }
      }
      lines.push_back(newPoints);
    }
  }
  return lines;
}



static Polylines makeEndPoints(const coordf_t Zpos, coordf_t gridSize, std::vector<coordf_t> critPoints,
                               coordf_t lengthX, coordf_t lengthY, coordf_t spacing, size_t multiline_count)
{
  Polylines lines;
  coordf_t zCycle = fmod(Zpos + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  bool printVert = zCycle < 0.5;
  bool printHoriz = zCycle >= 0.5;
  int zFlipOffset = ((sgn(fmod(zCycle, 0.5) - 0.25) > 0) == printVert) ? 0 : 1;

  Polylines startLines, endLines;
  for(size_t li = 0; li < multiline_count; li++){
    coordf_t oAdj = (li - ((multiline_count - 1) / 2.)) * spacing;
    coordf_t dAdj = oAdj * sqrt(2);
    Polyline startLine, endLine;

    startLine.points.push_back(printHoriz ? Point(oAdj, -dAdj) : Point(-dAdj, oAdj));

    endLine.points.push_back(printHoriz ? Point(-oAdj, -dAdj) : Point(-dAdj, -oAdj));
    for(size_t pi = 0; pi < 2; pi++){
      int pDir = pi * 2 - 1;
      coordf_t pAdj = pDir * (sqrt(2) - 1) * oAdj;
      coordf_t troctOffset = abs(troctWave(critPoints[pi], gridSize, Zpos));
      startLine.points.push_back(printHoriz ?
                                 Point(-troctOffset, critPoints[pi] + pAdj) :
                                 Point(critPoints[pi] + pAdj, -troctOffset));
      endLine.points.push_back(printHoriz ?
                               Point(troctOffset, critPoints[pi] + pAdj) :
                               Point(critPoints[pi] + pAdj,  troctOffset));
    }

    startLine.points.push_back(printHoriz ? Point(oAdj, gridSize + dAdj) : Point(gridSize + dAdj, oAdj));

    endLine.points.push_back(printHoriz ? Point(-oAdj, gridSize + dAdj) : Point(gridSize + dAdj, -oAdj));
    startLines.push_back(startLine);
    endLines.push_back(endLine);
  }
  coordf_t gridMaxX = ceil((lengthX - EPSILON) / gridSize) * gridSize;
  coordf_t gridMaxY = ceil((lengthY - EPSILON) / gridSize) * gridSize;
  for(size_t li = 0; li < multiline_count; li++){
    coordf_t mlFactor = (li - ((multiline_count - 1) / 2.)) * spacing;
    for (coordf_t cLoc = zFlipOffset * gridSize; cLoc < ((printHoriz ? gridMaxY : gridMaxX) - EPSILON); cLoc += gridSize * 2) {
      Polyline tsLine(startLines[li]);
      Polyline teLine(endLines[li]);
      tsLine.translate(printVert ? Point(cLoc, -mlFactor) : Point(-mlFactor, cLoc));
      teLine.translate(printVert ? Point(cLoc, gridMaxY + mlFactor) : Point(gridMaxX + mlFactor, cLoc));
      lines.push_back(tsLine);
      lines.push_back(teLine);
    }
  }
  return lines;
}




static Polyline patternPoints(const coordf_t Zpos, coordf_t gridSize, std::vector<coordf_t> critPoints,
                              coordf_t gridLength, coordf_t perpDir, int print_dir, coordf_t oAdj)
{
  Polyline line;
  coordf_t dAdj = oAdj * (sqrt(2) - 1);
  coordf_t zCycle = fmod(Zpos + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  int zFlipDirection = sgn(fmod(zCycle, 0.5) - 0.25);
  bool hitEnd = false;
  int endPi = -1;
  size_t pi = 0;
  size_t piOfs = 0;
  line.points.push_back((print_dir == 1) ? Point(dAdj, 0.) : Point(0., dAdj));
  coordf_t gridMax = ceil((gridLength - EPSILON) / gridSize) * gridSize;
  for (coordf_t cLoc = 0; cLoc < gridMax; cLoc += gridSize, piOfs = (piOfs + 2) % 4) {
    for(pi = piOfs; pi < (piOfs + 2); pi++){
      coordf_t offset = troctWave(critPoints[pi], gridSize, Zpos);
      coordf_t offsetFlip = sgn(offset);
      coordf_t posFlip = floor(((pi + 1) % 4) / 2) * 2 - 1;
      coordf_t posLin = cLoc - (piOfs * gridSize / 2.) + critPoints[pi];
      coordf_t posPerp = offset * perpDir;
      line.points.push_back((print_dir == 1) ?
                            Point(posPerp, posLin + posFlip * dAdj * perpDir * zFlipDirection * print_dir) :
                            Point(posLin + posFlip * dAdj * perpDir * zFlipDirection * print_dir, posPerp));
    }
  }
  line.points.push_back((print_dir == 1) ? Point(dAdj, gridMax) : Point(gridMax, dAdj));
  return line;
}



  static Polylines makeZigZag(coordf_t Zpos, coordf_t gridSize, coordf_t lengthX, coordf_t lengthY,
                              coordf_t spacing, size_t multiline_count)
{
  Polylines lines;
  std::vector<coordf_t> critPoints = getCriticalPoints(Zpos, gridSize);
  coordf_t zCycle = fmod(Zpos + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  bool printVert = zCycle < 0.5;
  BoundingBox extents;
  int perpDir = -1;
  int perpDirPattern = -1;
  coordf_t gridMax = ceil(((printVert ? lengthX : lengthY) - EPSILON) / gridSize) * gridSize;
  for (coordf_t pPos = 0; pPos < gridMax; pPos += gridSize, perpDirPattern *= -1) {
    for (size_t li = 0; li < multiline_count; li++){
      coordf_t oAdj = (li - ((multiline_count - 1) / 2.)) * spacing;
      Polyline newPoints;
      newPoints = patternPoints(Zpos, gridSize, critPoints,
                                printVert ? lengthY : lengthX,
                                perpDirPattern, printVert ? 1 : -1, oAdj);
      if (perpDir == 1)
        std::reverse(newPoints.points.begin(), newPoints.points.end());
      newPoints.translate(printVert ? Point(pPos + oAdj, 0.) : Point(0., pPos + oAdj));
      extents.merge(newPoints.points);
      lines.push_back(newPoints);
      perpDir *= -1;
    }
  }
  return lines;
}








static Polylines makeGrid(coordf_t z, coordf_t zLast, coordf_t gridSize,
                          coordf_t lengthX, coordf_t lengthY,
                          bool completeTops, coordf_t spacing, size_t multiline_count, size_t layer_count)
{
  coordf_t zCycle = fmod(z + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  bool printVert = zCycle < 0.5;
  coordf_t zCycleLast = fmod(zLast + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  bool printVertLast = zCycleLast < 0.5;
  Polylines result;
  Polylines polyZag = makeZigZag(z, gridSize, lengthX, lengthY, spacing, multiline_count);
  result.insert(result.end(), polyZag.begin(), polyZag.end());

  std::vector<coordf_t> critPoints = getCriticalPoints(z, gridSize);
  Polylines endPoints = makeEndPoints(z, gridSize, critPoints, lengthX, lengthY, spacing, multiline_count);
  result.insert(result.end(), endPoints.begin(), endPoints.end());

  if(completeTops && (printVert != printVertLast)){
    coordf_t layerHeight = (z - zLast) / (multiline_count * layer_count);
    size_t top_distance = 0;
    for(coordf_t zCheck = z; zCheck >= (zLast + EPSILON); zCheck -= layerHeight * layer_count, top_distance++){
      coordf_t zCheckCycle = fmod(zCheck + gridSize/2, gridSize * 2.) / (gridSize * 2.);
      if(printVert != (zCheckCycle < 0.5)){
        break;
      }
    }
    Polylines polytops = addTops(z, gridSize, lengthX, lengthY, spacing, multiline_count, top_distance);
    result.insert(result.end(), polytops.begin(), polytops.end());
  }
  return result;
}










void Fill3DHoneycomb::_fill_surface_single(
    const FillParams                &params,
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{

    auto infill_angle   = float(this->angle);
    if (std::abs(infill_angle) >= EPSILON) expolygon.rotate(-infill_angle);
    BoundingBox bb = expolygon.contour.bounding_box();


    coord_t expandSize = 5. * scale_(this->spacing);
    bb.offset(expandSize);


    size_t layersPerSlice = 1;
    if(thickness_layers > 0){
      layersPerSlice = thickness_layers;
    }



    coordf_t zScale = sqrt(2);








    coordf_t gridSize = (scale_(this->spacing) *
                         ((zScale + 1.) / 2.) * params.multiline  / params.density);
    coordf_t layerHeight = scale_(params.layer_height);
    coordf_t layersPerModule = floor((gridSize * 2) / (zScale * layerHeight) + 0.05);

    if((params.density > 0.42) || (layersPerModule < 2)){
      layersPerModule = 2;


      gridSize = (scale_(this->spacing) * 1.1 * params.multiline  / params.density);

      zScale = (gridSize * 2) / (layersPerModule * layerHeight);
    }



    bb.merge(align_to_grid(bb.min, Point(gridSize * 2., gridSize * 2.)));


    coordf_t startOffset = gridSize / 2. + scale_(params.layer_height / 2.);

    Polylines polylines =
      makeGrid(
	       scale_(this->z) * zScale + startOffset,
	       scale_(this->z - (params.layer_height * params.multiline * layersPerSlice)) * zScale + startOffset,
	       gridSize, bb.size()(0), bb.size()(1),
	       params.infill_complete_top,
               scale_(this->spacing),
               params.multiline,
               layersPerSlice);


    for (Polyline &pl : polylines){
      pl.translate(bb.min);
      pl.simplify(5 * spacing);


      smooth_polyline_corners(pl, params.smooth_factor, scaled<double>(params.resolution));
    }





    polylines = intersection_pl(std::move(polylines), to_polygons(expolygon));

    if (! polylines.empty()) {


    const double minlength = scale_(0.8 * this->spacing);
    polylines.erase(
	std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return pl.length() < minlength; }),
	polylines.end());
    }


    if (!polylines.empty()) {
        int infill_start_idx = polylines_out.size();

        chain_or_connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);


        if (std::abs(infill_angle) >= EPSILON) {
          for (auto it = polylines_out.begin() + infill_start_idx; it != polylines_out.end(); ++it)
            it->rotate(infill_angle);
        }
    }
}

}
