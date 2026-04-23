#pragma once

#include <array>

static constexpr int kMaxV = 4;
enum class VectorId : int { Pos, OriginalPos, Vel, Bfield};

static constexpr int kMaxQ = 18;
enum class QuantityId : int { Density, Temperature, Val, Val2, Mass, Hsml, PosX, PosY, PosZ, Radius, VRad, B, Beta, Metallicity, ElectronAbundance, H2Abundance, HDAbundance, J21};

static constexpr int kNumTypes = 6;
extern QuantityId selectedQuantity[6];

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
  int nAllQ;
  int nUIQ;
  std::array<QuantityId,kMaxQ> allQ;
  std::array<QuantityId,kMaxQ> uiQ;
};
