#pragma once

#include <glm/vec3.hpp>

struct Histogram2DContext;
struct Histogram2DRequestState;
struct Histogram2DResultState;
struct SimulationBlock;
struct QuantityState;
struct RadialProfileRequestState;
struct RadialProfileResultState;

void ExecuteRadialProfileRequest(RadialProfileRequestState& request,
                                 RadialProfileResultState& result,
                                 const SimulationBlock& partblock,
                                 const glm::vec3& dataCenter,
                                 QuantityState& quantity);

void ExecuteHistogram2DRequest(Histogram2DRequestState& request,
                               Histogram2DResultState& result,
                               const SimulationBlock& partblock,
                               const Histogram2DContext& ctx);
