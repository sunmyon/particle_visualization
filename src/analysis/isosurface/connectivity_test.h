#pragma once
#include "data/spatial/particle_octree.h"
#include "analysis/isosurface/marching_cubes.h"
#include "analysis/isosurface/mesh_data.h"

/// Run debug tests around connectivity.
/// epsCorner: tolerance for comparing vertex values.
/// epsQuant: vertex quantization width used for duplicate detection.
void runConnectivityTests(const ParticleOctree&   tree,
                          const Mesh&             mesh,
                          float                   epsCorner = 1e-5f,
                          float                   epsQuant  = 1e-5f);

void runConnectivityQuickCheck(const ParticleOctree&   tree,
                               const TrackingVector<const ParticleOctree::Node*>& leaves,
			       const Mesh&             mesh,
			       float                   epsCorner = 1e-5f,
			       float                   epsQuant  = 1e-5f);
