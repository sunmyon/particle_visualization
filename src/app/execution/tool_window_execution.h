#pragma once

#include <glm/glm.hpp>

class SimulationDataset;
class ProjectionMapGenerator;
class HaloStore;
struct CameraContext;
struct Histogram2DContext;
struct Histogram2DRequestState;
struct Histogram2DResultState;
struct Histogram2DUIState;
struct NormalizationContext;
struct SimulationBlock;
struct ProjectionMapUIState;
struct ProjectionFontSelectionRequestState;
struct QuantityState;
struct RadialProfileRequestState;
struct RadialProfileResultState;
struct RadialProfileUIState;
struct SnapshotPostprocessState;
struct MaskRequestState;
struct MaskUIState;
struct HaloesRequestState;
struct HaloesUIState;
struct TopParticlesRequestState;
struct TopParticlesResultState;
struct TopParticlesUIState;
struct TrackingTargetState;

void ExecuteTopParticlesWindowRequests(TopParticlesUIState& ui,
                                       TopParticlesRequestState& req,
                                       TopParticlesResultState& result,
                                       SimulationDataset& particles,
                                       CameraContext& camera,
                                       TrackingTargetState& tracking,
                                       SnapshotPostprocessState& post,
                                       const QuantityState& quantity);

void ExecuteRadialProfileWindowRequests(RadialProfileUIState& ui,
                                        RadialProfileRequestState& request,
                                        RadialProfileResultState& result,
                                        const SimulationBlock& partblock,
                                        const glm::vec3& dataCenter,
                                        QuantityState& quantity);

void ExecuteHistogram2DWindowRequests(Histogram2DUIState& ui,
                                      Histogram2DRequestState& request,
                                      Histogram2DResultState& result,
                                      const SimulationBlock& partblock,
                                      const Histogram2DContext& ctx);

void ExecuteProjectionFontSelectionRequests(ProjectionMapUIState& ui,
                                            ProjectionFontSelectionRequestState& request,
                                            ProjectionMapGenerator& generator);

void ExecuteMaskWindowRequests(MaskUIState& ui,
                               MaskRequestState& request);

void ExecuteHaloesWindowRequests(HaloesUIState& ui,
                                 HaloesRequestState& request,
                                 HaloStore& haloes,
                                 SimulationDataset& particles,
                                 CameraContext& camera,
                                 const NormalizationContext& normalization);
