#include "app/execution/analysis_execution.h"

#ifdef POWER_SPECTRUM

#include <utility>

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include "analysis/power_spectrum.h"
#include "data/simulation_dataset.h"
#include "projection/projection_geometry.h"

namespace {
void StoreQuatAsEulerDegrees(const glm::quat& q, float outEuler[3])
{
  const glm::vec3 euler = glm::degrees(glm::eulerAngles(glm::normalize(q)));
  outEuler[0] = euler.x;
  outEuler[1] = euler.y;
  outEuler[2] = euler.z;
}

void UpdatePowerSpectrumRegionPreview(const SimulationDataset& particles,
                                      const PowerSpectrumRequestState& request,
                                      PowerSpectrumResultState& result)
{
  (void)particles;

  result.regionPreviewCpuUpdated = true;
  result.regionPreviewValid =
    request.useRegionBox &&
    request.showRegionBox &&
    request.regionSideLength > 0.0f;

  if (!result.regionPreviewValid) {
    return;
  }

  const float halfSide = 0.5f * request.regionSideLength;

  CubeObject cube;
  cube.center = glm::vec3(request.regionCenter[0],
                          request.regionCenter[1],
                          request.regionCenter[2]);
  cube.halfSize = glm::vec3(halfSide);
  cube.orientation = glm::quat{1, 0, 0, 0};
  cube.color = glm::vec3(0.3f, 0.7f, 1.0f);
  cube.opacity = request.regionOpacity;
  cube.tag = "power_spectrum_region";
  result.regionCube = std::move(cube);
}

void UpdatePowerSpectrumAxisFromAngularMomentum(
  const SimulationDataset& particles,
  PowerSpectrumRequestState& request)
{
  const float side =
    request.regionSideLength > 0.0f ? request.regionSideLength : 1000.0f;
  const float xlen[3] = {
    side,
    side,
    side
  };
  const glm::vec3 center(request.regionCenter[0],
                         request.regionCenter[1],
                         request.regionCenter[2]);

  ProjectionAngularMomentumFrame frame =
    ComputeAngularMomentumFrame(particles.simulationBlock,
                                center,
                                xlen);
  if (!frame.valid) {
    return;
  }

  const glm::vec3 axis = glm::normalize(frame.axis);
  request.analysisAxis[0] = axis.x;
  request.analysisAxis[1] = axis.y;
  request.analysisAxis[2] = axis.z;
  StoreQuatAsEulerDegrees(BuildRotationFromZAxisTo(axis),
                          request.axisTiltDegrees);
}
}  // namespace

void ExecutePowerSpectrumRequest(SimulationDataset& particles,
                                 PowerSpectrumRequestState& request,
                                 PowerSpectrumResultState& result)
{
  if (request.clearRequested) {
    result = PowerSpectrumResultState{};
    result.regionPreviewCpuUpdated = true;
    request.clearRequested = false;
    request.regionUpdateRequested = false;
    return;
  }

  if (request.setAxisFromAngularMomentumRequested) {
    UpdatePowerSpectrumAxisFromAngularMomentum(particles, request);
    request.setAxisFromAngularMomentumRequested = false;
  }

  if (request.regionUpdateRequested) {
    UpdatePowerSpectrumRegionPreview(particles, request, result);
    request.regionUpdateRequested = false;
  }

  if (!request.runRequested) {
    return;
  }
  request.runRequested = false;

  PowerSpectrumParams params;
  params.gridSize = request.gridSize;
  params.fieldKind = request.fieldKind;
  params.scalarQuantity = request.scalarQuantity;
  params.vectorField = request.vectorField;
  params.subtractMean = request.subtractMean;
  params.useRegionBox = request.useRegionBox;
  for (int i = 0; i < 3; ++i) {
    params.regionCenter[i] = request.regionCenter[i];
    params.axisTiltDegrees[i] = request.axisTiltDegrees[i];
    params.analysisAxis[i] = request.analysisAxis[i];
  }
  params.regionSideLength = request.regionSideLength;

  const PowerSpectrumComputer computer;
  result.paramsUsed = params;
  result.result = computer.compute(particles.simulationBlock, params);
  result.computed = result.result.valid;
  ++result.version;
}

#endif
