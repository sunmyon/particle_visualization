#pragma once
#include <vector>

// Generic mesh type.
// - vertices: [x0,y0,z0, x1,y1,z1, …]
// - indices:  [i0,i1,i2, i3,i4,i5, …]
struct Mesh {
  std::vector<float>    vertices;
  std::vector<unsigned> indices;
};
