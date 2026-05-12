#pragma once

class SimulationDataset;
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
  DerivedLayerUpdate scaleGuide;
  DerivedLayerUpdate lineLayer;
  DerivedLayerUpdate polyhedron;
  DerivedLayerUpdate cuboidAnnotation;
  bool particleLabelsUpdated = false;
};

DerivedRebuildResult RebuildDerivedState(const SimulationDataset& particles,
                                         const CameraContext& camera,
                                         AppDerivedState& derived,
                                         const RenderRuntimeState& render,
                                         const ProjectionMapToolState& projection);

void AcknowledgeDerivedRebuild(SimulationDataset& particles,
                               AppDerivedState& derived,
                               const DerivedRebuildResult& rebuild);

void ApplyDerivedRenderInvalidation(const DerivedRebuildResult& rebuild,
                                    RenderRuntimeState& render);
