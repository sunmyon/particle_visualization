#pragma once

class ParticleArray;
enum class QuantityId : int;

#ifdef ISO_CONTOUR
struct IsoContourGeometryState;
#endif

#ifdef ISO_CONTOUR
void BuildIsoContourGeometry(ParticleArray& part,
                             QuantityId selectedVar,
                             float isoLevel,
                             int max_treelevel,
                             IsoContourGeometryState& iso);
#endif
