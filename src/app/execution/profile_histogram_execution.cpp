#include "app/execution/profile_histogram_execution.h"

#include "app/state/analysis_state.h"
#include "app/state/normalization_config.h"
#include "app/state/runtime_state.h"
#include "app/state/tool_window_state.h"
#include "analysis/histogram2d.h"
#include "analysis/radial_profile.h"
#include "data/particle_block.h"

void ExecuteRadialProfileRequest(RadialProfileRequestState& request,
                                 RadialProfileResultState& result,
                                 const ParticleBlock& partblock,
                                 const glm::vec3& camCenter,
                                 NormalizationContext& normalization,
                                 QuantityState& quantity)
{
  if (!request.runRequested) {
    return;
  }

  static RadialProfileComputer computer;
  computer.setUnits(quantity.units);

  result.result = computer.compute(partblock,
                                   normalization.toPhysicalScale(),
                                   request.params,
                                   camCenter);
  result.computed = result.result.valid;

  if (request.params.autorange && result.result.valid) {
    request.params.xmin = result.result.xmin;
    request.params.xmax = result.result.xmax;
    request.params.ymin = result.result.ymin;
    request.params.ymax = result.result.ymax;
  }

  result.paramsUsed = request.params;
  ++result.version;
  request.runRequested = false;
}

void ExecuteHistogram2DRequest(Histogram2DRequestState& request,
                               Histogram2DResultState& result,
                               const ParticleBlock& partblock,
                               const Histogram2DContext& ctx)
{
  if (!request.runRequested) {
    return;
  }

  static Histogram2DComputer computer;
  result.result = computer.compute(partblock, request.params, ctx);
  result.computed = result.result.valid;

  if (result.result.valid) {
    request.params.range1_min = result.result.range1_min;
    request.params.range1_max = result.result.range1_max;
    request.params.range2_min = result.result.range2_min;
    request.params.range2_max = result.result.range2_max;
  }

  result.paramsUsed = request.params;
  ++result.version;
  request.runRequested = false;
}
