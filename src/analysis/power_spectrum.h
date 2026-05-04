#pragma once

#include <string>
#include <vector>

#include "core/quantity.h"
#include "data/simulation_block.h"

struct PowerSpectrumParams {
  int gridSize = 64;
  int fieldKind = 1; // 0: scalar, 1: vector.
  QuantityId scalarQuantity = QuantityId::Density;
  int vectorField = 1; // 0: velocity, 1: B field.
  bool subtractMean = true;
  bool useRegionBox = false;
  float regionCenter[3] = {0.0f, 0.0f, 0.0f};
  float regionSideLength = 1000.0f;
  float axisTiltDegrees[3] = {0.0f, 0.0f, 0.0f};
  float analysisAxis[3] = {0.0f, 0.0f, 1.0f};
};

struct PowerSpectrumResult {
  bool valid = false;
  bool success = false;
  std::string message;

  int gridSize = 0;
  int depositedSamples = 0;
  bool vectorSpectrum = true;
  std::string fieldLabel;
  std::vector<double> k;
  std::vector<double> powerTotal;
  std::vector<double> powerSolenoidal;
  std::vector<double> powerCompressive;
  std::vector<double> powerAxial;
  std::vector<double> powerRadial;
  std::vector<double> powerToroidal;
  std::vector<double> powerPoloidal;
  std::vector<int> nModes;
};

class PowerSpectrumComputer {
public:
  PowerSpectrumResult compute(const SimulationBlock& block,
                              const PowerSpectrumParams& params) const;
};
