#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "core/units.h"

static constexpr int kMaxV = 4;
enum class VectorId : int { Pos, OriginalPos, Vel, Bfield};

static constexpr int kCustomScalarQuantityCount = 10;
static constexpr int kDerivedScalarQuantityCount = 10;
static constexpr int kMaxQ = 36;
enum class QuantityId : int {
  Density,
  Temperature,
  Val,
  Val2,
  Mass,
  Hsml,
  PosX,
  PosY,
  PosZ,
  Radius,
  VRad,
  B,
  Beta,
  Metallicity,
  ElectronAbundance,
  H2Abundance,
  HDAbundance,
  J21,
  Custom3,
  Custom4,
  Custom5,
  Custom6,
  Custom7,
  Custom8,
  Custom9,
  Custom10,
  Derived1,
  Derived2,
  Derived3,
  Derived4,
  Derived5,
  Derived6,
  Derived7,
  Derived8,
  Derived9,
  Derived10
};

static constexpr int kNumTypes = 6;
extern QuantityId selectedQuantity[6];

enum class UnitSpace : uint8_t {
  Physical = 0,
  Comoving = 1
};

inline bool IsCustomScalarQuantity(QuantityId q)
{
  switch (q) {
  case QuantityId::Val:
  case QuantityId::Val2:
  case QuantityId::Custom3:
  case QuantityId::Custom4:
  case QuantityId::Custom5:
  case QuantityId::Custom6:
  case QuantityId::Custom7:
  case QuantityId::Custom8:
  case QuantityId::Custom9:
  case QuantityId::Custom10:
    return true;
  default:
    return false;
  }
}

inline int CustomScalarQuantityIndex(QuantityId q)
{
  switch (q) {
  case QuantityId::Val:      return 0;
  case QuantityId::Val2:     return 1;
  case QuantityId::Custom3:  return 2;
  case QuantityId::Custom4:  return 3;
  case QuantityId::Custom5:  return 4;
  case QuantityId::Custom6:  return 5;
  case QuantityId::Custom7:  return 6;
  case QuantityId::Custom8:  return 7;
  case QuantityId::Custom9:  return 8;
  case QuantityId::Custom10: return 9;
  default:                   return -1;
  }
}

inline QuantityId CustomScalarQuantityId(int index)
{
  static constexpr std::array<QuantityId, kCustomScalarQuantityCount> ids = {
    QuantityId::Val,
    QuantityId::Val2,
    QuantityId::Custom3,
    QuantityId::Custom4,
    QuantityId::Custom5,
    QuantityId::Custom6,
    QuantityId::Custom7,
    QuantityId::Custom8,
    QuantityId::Custom9,
    QuantityId::Custom10
  };
  return (index >= 0 && index < kCustomScalarQuantityCount)
    ? ids[index]
    : QuantityId::Density;
}

inline const char* QuantityLabel(QuantityId q) {
  switch (q) {
    case QuantityId::Density:     return "Density";
    case QuantityId::Temperature: return "Temperature";
    case QuantityId::Val:         return "custom1";
    case QuantityId::Val2:        return "custom2";
    case QuantityId::Mass:        return "Mass";
    case QuantityId::Hsml:        return "Hsml";
    case QuantityId::PosX:        return "x";
    case QuantityId::PosY:        return "y";
    case QuantityId::PosZ:        return "z";
    case QuantityId::Radius:      return "r";
    case QuantityId::VRad:        return "vrad";      
    case QuantityId::B:           return "B";
    case QuantityId::Beta:        return "Beta";
    case QuantityId::Metallicity: return "Metallicity";
    case QuantityId::ElectronAbundance: return "felec";
    case QuantityId::H2Abundance: return "fH2";
    case QuantityId::HDAbundance: return "fHD";
    case QuantityId::J21: return "J21";			
    case QuantityId::Custom3:  return "custom3";
    case QuantityId::Custom4:  return "custom4";
    case QuantityId::Custom5:  return "custom5";
    case QuantityId::Custom6:  return "custom6";
    case QuantityId::Custom7:  return "custom7";
    case QuantityId::Custom8:  return "custom8";
    case QuantityId::Custom9:  return "custom9";
    case QuantityId::Custom10: return "custom10";
    case QuantityId::Derived1: return "derived1";
    case QuantityId::Derived2: return "derived2";
    case QuantityId::Derived3: return "derived3";
    case QuantityId::Derived4: return "derived4";
    case QuantityId::Derived5: return "derived5";
    case QuantityId::Derived6: return "derived6";
    case QuantityId::Derived7: return "derived7";
    case QuantityId::Derived8: return "derived8";
    case QuantityId::Derived9: return "derived9";
    case QuantityId::Derived10: return "derived10";
  }
  return "Unknown";
}

inline int DerivedScalarQuantityIndex(QuantityId q)
{
  switch (q) {
  case QuantityId::Derived1: return 0;
  case QuantityId::Derived2: return 1;
  case QuantityId::Derived3: return 2;
  case QuantityId::Derived4: return 3;
  case QuantityId::Derived5: return 4;
  case QuantityId::Derived6: return 5;
  case QuantityId::Derived7: return 6;
  case QuantityId::Derived8: return 7;
  case QuantityId::Derived9: return 8;
  case QuantityId::Derived10: return 9;
  default:                   return -1;
  }
}

inline bool IsDerivedScalarQuantity(QuantityId q)
{
  return DerivedScalarQuantityIndex(q) >= 0;
}

inline QuantityId DerivedScalarQuantityId(int index)
{
  static constexpr std::array<QuantityId, kDerivedScalarQuantityCount> ids = {
    QuantityId::Derived1,
    QuantityId::Derived2,
    QuantityId::Derived3,
    QuantityId::Derived4,
    QuantityId::Derived5,
    QuantityId::Derived6,
    QuantityId::Derived7,
    QuantityId::Derived8,
    QuantityId::Derived9,
    QuantityId::Derived10
  };
  return (index >= 0 && index < kDerivedScalarQuantityCount)
    ? ids[index]
    : QuantityId::Derived1;
}

inline std::array<std::string, kCustomScalarQuantityCount>
MakeDefaultCustomScalarQuantityLabels()
{
  std::array<std::string, kCustomScalarQuantityCount> labels{};
  for (int i = 0; i < kCustomScalarQuantityCount; ++i) {
    labels[static_cast<std::size_t>(i)] =
      std::string("custom") + std::to_string(i + 1);
  }
  return labels;
}

inline bool QuantityShowInUI(QuantityId q) {
  switch (q) {
    case QuantityId::PosX:
    case QuantityId::PosY:
    case QuantityId::PosZ:
    case QuantityId::VRad:
    case QuantityId::Radius:
      return false;
    default:
      return true;      
  }
}

struct QuantityCatalogState {
  int nAllQ = 0;
  int nUIQ = 0;
  std::array<QuantityId,kMaxQ> allQ{};
  std::array<QuantityId,kMaxQ> uiQ{};
  std::array<int,kNumTypes> nUIQByType{};
  std::array<std::array<QuantityId,kMaxQ>,kNumTypes> uiQByType{};
};

struct QuantityUnitRule {
  double baseFactor = 1.0;
  double expH = 0.0;
  double expAPhysical = 0.0;
  double expAComoving = 0.0;
};

inline QuantityUnitRule MakeDefaultQuantityUnitRule(QuantityId q, const UnitSystem& units)
{
  QuantityUnitRule rule;

  switch (q) {
  case QuantityId::Mass:
    rule.baseFactor = units.mass_msun;
    rule.expH = -1.0;
    break;

  case QuantityId::Density:
    // SimulationElement::density is normalized at load time to nH-like
    // number density, so display and color ranges should use it directly.
    rule.baseFactor = 1.0;
    break;

  case QuantityId::PosX:
  case QuantityId::PosY:
  case QuantityId::PosZ:
  case QuantityId::Radius:
  case QuantityId::Hsml:
    rule.baseFactor = units.length_pc;
    rule.expH = -1.0;
    rule.expAPhysical = 1.0;
    break;

  case QuantityId::VRad:
    // cm/s -> km/s
    rule.baseFactor = units.velocity_cm_per_s * 1.0e-5;
    break;

  default:
    break;
  }

  if (!std::isfinite(rule.baseFactor) || rule.baseFactor <= 0.0) {
    rule.baseFactor = 1.0;
  }
  return rule;
}

struct QuantityUnitConverter {
  std::array<double, kMaxQ> factorPhysical{};
  std::array<double, kMaxQ> factorComoving{};

  QuantityUnitConverter() {
    factorPhysical.fill(1.0);
    factorComoving.fill(1.0);
  }

  void rebuild(const UnitSystem& units, double scaleFactor)
  {
    const double a = (std::isfinite(scaleFactor) && scaleFactor > 0.0)
      ? scaleFactor
      : 1.0;
    const double h = (std::isfinite(units.hubble) && units.hubble > 0.0)
      ? units.hubble
      : 1.0;

    auto powSafe = [](double base, double exp) {
      if (!std::isfinite(exp) || std::abs(exp) < 1.0e-12) {
        return 1.0;
      }
      return std::pow(base, exp);
    };

    for (int i = 0; i < kMaxQ; ++i) {
      const QuantityId q = static_cast<QuantityId>(i);
      const QuantityUnitRule rule = MakeDefaultQuantityUnitRule(q, units);
      const double hFactor = powSafe(h, rule.expH);
      factorPhysical[i] = rule.baseFactor * hFactor * powSafe(a, rule.expAPhysical);
      factorComoving[i] = rule.baseFactor * hFactor * powSafe(a, rule.expAComoving);
    }
  }

  double factor(QuantityId q, UnitSpace space) const
  {
    const int idx = static_cast<int>(q);
    return (space == UnitSpace::Comoving) ? factorComoving[idx] : factorPhysical[idx];
  }

  double convert(QuantityId q, double codeValue, UnitSpace space) const
  {
    return codeValue * factor(q, space);
  }
};

struct QuantityConversionState {
  QuantityUnitConverter converter;
  double scaleFactor = 1.0;
  UnitSpace displaySpace = UnitSpace::Physical;
};

struct QuantityRangeState {
  float originalMax = 0.0f;
  std::array<std::array<float, kNumTypes>, kMaxQ> valueMin{};
  std::array<std::array<float, kNumTypes>, kMaxQ> valueMax{};
};

enum class DerivedScalarInstructionKind : uint8_t {
  Constant,
  Quantity,
  Add,
  Sub,
  Mul,
  Div,
  Pow,
  Neg,
  Abs,
  Sqrt,
  Cbrt,
  Log,
  Log10,
  Exp,
  Min,
  Max
};

struct DerivedScalarInstruction {
  DerivedScalarInstructionKind kind = DerivedScalarInstructionKind::Constant;
  double value = 0.0;
  QuantityId quantity = QuantityId::Density;
};

struct DerivedScalarQuantitySpec {
  bool enabled = false;
  std::string label;
  std::string expression;
  bool compiled = false;
  std::string compiledExpression;
  std::string compileError;
  std::vector<DerivedScalarInstruction> program;
};

struct QuantityState {
  QuantityCatalogState catalog;
  QuantityRangeState range;
  UnitSystem units;
  QuantityConversionState conversion;
  std::array<std::string, kCustomScalarQuantityCount> customScalarLabels =
    MakeDefaultCustomScalarQuantityLabels();
  std::array<DerivedScalarQuantitySpec, kDerivedScalarQuantityCount>
    derivedScalars{};

  void rebuildConversion(double scaleFactor)
  {
    conversion.scaleFactor = (std::isfinite(scaleFactor) && scaleFactor > 0.0)
      ? scaleFactor
      : 1.0;
    conversion.converter.rebuild(units, conversion.scaleFactor);
  }

  double toDisplay(QuantityId q, double codeValue) const
  {
    return conversion.converter.convert(q, codeValue, conversion.displaySpace);
  }

  double displayFactor(QuantityId q) const
  {
    return conversion.converter.factor(q, conversion.displaySpace);
  }
};

inline const char* QuantityDisplayLabel(const QuantityState& state,
                                        QuantityId q)
{
  const int derivedIndex = DerivedScalarQuantityIndex(q);
  if (derivedIndex >= 0 && derivedIndex < kDerivedScalarQuantityCount) {
    const std::string& label =
      state.derivedScalars[static_cast<std::size_t>(derivedIndex)].label;
    if (!label.empty()) {
      return label.c_str();
    }
  }
  const int customIndex = CustomScalarQuantityIndex(q);
  if (customIndex >= 0 && customIndex < kCustomScalarQuantityCount) {
    const std::string& label =
      state.customScalarLabels[static_cast<std::size_t>(customIndex)];
    if (!label.empty()) {
      return label.c_str();
    }
  }
  return QuantityLabel(q);
}
