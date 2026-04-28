#pragma once

#include "core/tracking_vector.h"

struct IsoContourGeometryState {
  TrackingVector<float> verts;
  TrackingVector<unsigned> inds;

  void clear() {
    verts.clear();
    inds.clear();
  }
};
