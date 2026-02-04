#pragma once

static constexpr int kMaxQ = 13;
enum class QuantityId : int { Density, Temperature, Val, Val2, Mass, Hsml, PosX, PosY, PosZ, Radius, VRad, B, Metallicity };

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
    case QuantityId::Metallicity: return "Metallicity";
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
