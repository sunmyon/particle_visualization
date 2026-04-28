#pragma once
#include "core/tracking_vector.h"

// 汎用メッシュ型
// - vertices: [x0,y0,z0, x1,y1,z1, …]
// - indices:  [i0,i1,i2, i3,i4,i5, …]
struct Mesh {
  TrackingVector<float>    vertices;
  TrackingVector<unsigned> indices;
};
