#pragma once

#include <glm/vec3.hpp>

struct Histogram2DContext;
struct Histogram2DRequestState;
struct Histogram2DResultState;
struct NormalizationContext;
struct ParticleBlock;
struct QuantityState;
struct RadialProfileRequestState;
struct RadialProfileResultState;

void ExecuteRadialProfileRequest(RadialProfileRequestState& request,
                                 RadialProfileResultState& result,
                                 const ParticleBlock& partblock,
                                 const glm::vec3& camCenter,
                                 NormalizationContext& normalization,
                                 QuantityState& quantity);

void ExecuteHistogram2DRequest(Histogram2DRequestState& request,
                               Histogram2DResultState& result,
                               const ParticleBlock& partblock,
                               const Histogram2DContext& ctx);
