#pragma once

#include <cmath>

#include "core/physics_constants.h"

enum class InputDensityUnit : int {
  CodeMassDensity = 0,
  NumberDensityNH = 1,
  MassDensityCgs = 2,
};

enum class InputTemperatureUnit : int {
  Kelvin = 0,
  CodeInternalEnergy = 1,
};

enum class InputMagneticFieldUnit : int {
  Gauss = 0,
  CodeMagneticField = 1,
};

enum class StoredQuantityUnit : int {
  Unknown = 0,
  CodeUnit = 1,
  InternalStandard = 2,
};

struct QuantityStorageMetadata {
  StoredQuantityUnit position = StoredQuantityUnit::CodeUnit;
  StoredQuantityUnit velocity = StoredQuantityUnit::CodeUnit;
  StoredQuantityUnit mass = StoredQuantityUnit::CodeUnit;
  StoredQuantityUnit density = StoredQuantityUnit::Unknown;
  StoredQuantityUnit temperature = StoredQuantityUnit::Unknown;
  StoredQuantityUnit magneticField = StoredQuantityUnit::Unknown;

  InputDensityUnit inputDensityUnit = InputDensityUnit::CodeMassDensity;
  InputTemperatureUnit inputTemperatureUnit =
    InputTemperatureUnit::CodeInternalEnergy;
  InputMagneticFieldUnit inputMagneticFieldUnit =
    InputMagneticFieldUnit::CodeMagneticField;
  bool inputComoving = false;
  double densityToInternalFactor = 1.0;
  double temperatureToInternalFactor = 1.0;
  double magneticFieldToInternalFactor = 1.0;
};

inline const char* GetInputDensityUnitDisplayName(InputDensityUnit unit)
{
  switch (unit) {
  case InputDensityUnit::CodeMassDensity:
    return "code mass density";
  case InputDensityUnit::NumberDensityNH:
    return "nH [cm^-3]";
  case InputDensityUnit::MassDensityCgs:
    return "mass density [g cm^-3]";
  }
  return "code mass density";
}

inline const char* GetInputTemperatureUnitDisplayName(InputTemperatureUnit unit)
{
  switch (unit) {
  case InputTemperatureUnit::Kelvin:
    return "temperature [K]";
  case InputTemperatureUnit::CodeInternalEnergy:
    return "code internal energy";
  }
  return "code internal energy";
}

inline const char* GetInputMagneticFieldUnitDisplayName(InputMagneticFieldUnit unit)
{
  switch (unit) {
  case InputMagneticFieldUnit::Gauss:
    return "B [Gauss]";
  case InputMagneticFieldUnit::CodeMagneticField:
    return "code magnetic field";
  }
  return "code magnetic field";
}

inline double InputDensityToInternalNHFactor(InputDensityUnit unit,
                                             double unitMassG,
                                             double unitLengthCm,
                                             double hubbleParam,
                                             double scaleFactor,
                                             bool comoving)
{
  switch (unit) {
  case InputDensityUnit::NumberDensityNH:
    return 1.0;
  case InputDensityUnit::MassDensityCgs:
    return 1.0 / physics_constants::proton_mass_cgs;
  case InputDensityUnit::CodeMassDensity:
    break;
  }

  if (!std::isfinite(unitMassG) || unitMassG <= 0.0 ||
      !std::isfinite(unitLengthCm) || unitLengthCm <= 0.0 ||
      !std::isfinite(hubbleParam) || hubbleParam <= 0.0) {
    return 1.0;
  }

  double factor =
    hubbleParam * hubbleParam * unitMassG /
    std::pow(unitLengthCm, 3) /
    physics_constants::proton_mass_cgs;

  if (comoving) {
    const double a = (std::isfinite(scaleFactor) && scaleFactor > 0.0)
      ? scaleFactor
      : 1.0;
    factor /= std::pow(a, 3);
  }

  return factor;
}

inline double InputInternalEnergyToTemperatureFactor(InputTemperatureUnit unit,
                                                     double unitVelocityCmPerS)
{
  if (unit == InputTemperatureUnit::Kelvin) {
    return 1.0;
  }
  if (!std::isfinite(unitVelocityCmPerS) || unitVelocityCmPerS <= 0.0) {
    return 1.0;
  }

  // Default neutral primordial mean-particle factor used when no composition
  // fields are available. Rich HDF5 paths can still synthesize T with H2/e-/gamma.
  constexpr double gammaMinusOne = 2.0 / 3.0;
  constexpr double defaultDenom = 1.2;
  return gammaMinusOne / defaultDenom *
         unitVelocityCmPerS * unitVelocityCmPerS *
         physics_constants::proton_mass_cgs /
         physics_constants::boltzmann_cgs;
}

inline double InputMagneticFieldToGaussFactor(InputMagneticFieldUnit unit,
                                              double unitMassG,
                                              double unitLengthCm,
                                              double unitVelocityCmPerS,
                                              double hubbleParam,
                                              double scaleFactor,
                                              bool comoving)
{
  if (unit == InputMagneticFieldUnit::Gauss) {
    return 1.0;
  }
  if (!std::isfinite(unitMassG) || unitMassG <= 0.0 ||
      !std::isfinite(unitLengthCm) || unitLengthCm <= 0.0 ||
      !std::isfinite(unitVelocityCmPerS) || unitVelocityCmPerS <= 0.0 ||
      !std::isfinite(hubbleParam) || hubbleParam <= 0.0) {
    return 1.0;
  }

  double factor =
    std::sqrt(unitMassG / unitLengthCm) /
    (unitLengthCm / unitVelocityCmPerS / hubbleParam);
  if (comoving) {
    const double a = (std::isfinite(scaleFactor) && scaleFactor > 0.0)
      ? scaleFactor
      : 1.0;
    factor /= std::pow(a, 2);
  }
  return factor;
}
