#pragma once

class ParticleArray;
class ProjectionMapGenerator;
struct UnitSystem;
struct NormalizationContext;
struct ProjectionMapToolState;
struct ProjectionPreviewDerivedState;

void ExecuteProjectionMapRequests(ProjectionMapToolState& tool,
                                  ProjectionMapGenerator& generator,
                                  ParticleArray& particles,
				  const UnitSystem& units,
                                  const NormalizationContext& normalization,
				  int currentFileIndex,
                                  ProjectionPreviewDerivedState& preview,
				  double time);
