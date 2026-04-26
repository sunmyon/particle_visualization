#pragma once

class ParticleArray;
class ProjectionMapGenerator;
struct UnitSystem;
struct NormalizationContext;
struct ProjectionMapRequestState;
struct ProjectionMapToolState;
struct ProjectionPreviewDerivedState;
struct CameraContext;
struct RenderLayerState;

void ExecuteProjectionMapRequests(ProjectionMapRequestState& request,
                                  ProjectionMapToolState& tool,
                                  ProjectionMapGenerator& generator,
                                  ParticleArray& particles,
				  const UnitSystem& units,
                                  const NormalizationContext& normalization,
                                  const CameraContext& camera,
                                  RenderLayerState& cuboidAnnotation,
				  int currentFileIndex,
                                  ProjectionPreviewDerivedState& preview,
				  double time);
