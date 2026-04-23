#pragma once

class ParticleArray;
class ProjectionMapGenerator;
struct NormalizationContext;
struct ProjectionMapToolState;
struct ProjectionPreviewDerivedState;

void ExecuteProjectionMapRequests(ProjectionMapToolState& tool,
                                  ProjectionMapGenerator& generator,
                                  ParticleArray& particles,
                                  const NormalizationContext& normalization,
				  int currentFileIndex,
                                  ProjectionPreviewDerivedState& preview);
