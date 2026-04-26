#pragma once

class ParticleArray;
struct AppDerivedState;
struct CameraContext;
struct ProjectionMapToolState;
struct RenderRuntimeState;

struct DerivedLayerUpdate {
  bool changed = false;
  bool visible = false;
};

struct DerivedRebuildResult {
  DerivedLayerUpdate disk;
  DerivedLayerUpdate ellipsoid;
  DerivedLayerUpdate cube;
  DerivedLayerUpdate line;
  DerivedLayerUpdate lineLayer;
  DerivedLayerUpdate polyhedron;
  DerivedLayerUpdate cuboidAnnotation;
  bool particleLabelsUpdated = false;
};

DerivedRebuildResult RebuildDerivedState(const ParticleArray& particles,
                                         const CameraContext& camera,
                                         AppDerivedState& derived,
                                         const RenderRuntimeState& render,
                                         const ProjectionMapToolState& projection);

void AcknowledgeDerivedRebuild(ParticleArray& particles,
                               AppDerivedState& derived,
                               const DerivedRebuildResult& rebuild);

void ApplyDerivedRenderInvalidation(const DerivedRebuildResult& rebuild,
                                    const CameraContext& camera,
                                    RenderRuntimeState& render);
