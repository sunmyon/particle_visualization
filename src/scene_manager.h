#pragma once
#include "object.h"

struct SceneManagers {
  CubeManager cube;
  DiskManager disk;
  EllipsoidManager ellipsoid;
  LineManager line;
  CuboidAnnotationManager cuboidAnnotation;
  PolyhedronManager polyhedron;
};

