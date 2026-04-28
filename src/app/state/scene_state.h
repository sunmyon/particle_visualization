#pragma once
#include "render/scene_objects.h"

struct SceneManagers {
  CubeManager cube;
  DiskManager disk;
  EllipsoidManager ellipsoid;
  LineManager line;
  CuboidAnnotationManager cuboidAnnotation;
  PolyhedronManager polyhedron;
};
