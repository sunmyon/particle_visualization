#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include "core/units.h"

static constexpr int kMaxV = 4;
enum class VectorId : int { Pos, OriginalPos, Vel, Bfield};

static constexpr int kMaxQ = 18;
enum class QuantityId : int { Density, Temperature, Val, Val2, Mass, Hsml, PosX, PosY, PosZ, Radius, VRad, B, Beta, Metallicity, ElectronAbundance, H2Abundance, HDAbundance, J21};

static constexpr int kNumTypes = 6;
extern QuantityId selectedQuantity[6];

enum class UnitSpace : uint8_t {
  Physical = 0,
  Comoving = 1
};

inline const char* QuantityLabel(QuantityId q) {
  switch (q) {
    case QuantityId::Density:     return "Density";
    case QuantityId::Temperature: return "Temperature";
    case QuantityId::Val:         return "val";
    case QuantityId::Val2:        return "val2";
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
  }
  return "Unknown";
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

struct QuantityState {
  QuantityCatalogState catalog;
  QuantityRangeState range;
  UnitSystem units;
  QuantityConversionState conversion;

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
