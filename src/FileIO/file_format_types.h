#pragma once
#include "data/data_type.h"

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

enum class FileFormat {
  Auto,       // 拡張子から自動判別 (従来の動作)
  HDF5,
  Binary,
  Gadget,
  Framed,      // Fortran‐style framed binary
  _Count
};

enum class FieldKey {
  Position,
  Velocity,
  Bfield,
  Hsml,
  Volume,
  Mass,
  Density,
  Temperature,
  Value,
  Value2,
  ID,
  InternalEnergy,
  ElectronAbundance,
  H2Abundance,
  HDAbundance,
  J21,
  Gamma,
  Metallicity,
  Dummy,
  Type,
  Unknown
};

enum class DestKind {
  AoSCore,
  SoA,
  Ignore
};

struct FieldSpec {
  FieldKey key = FieldKey::Unknown;
  DataType type = DataType::Float;
  int count = 1;
  std::string sourceName;
};

inline const char* GetFieldKeyDisplayName(FieldKey key) {
  switch (key) {
    case FieldKey::Position: return "position";
    case FieldKey::Velocity: return "velocity";
    case FieldKey::Bfield: return "Bfield";
    case FieldKey::Hsml: return "Hsml";
    case FieldKey::Volume: return "Volume";
    case FieldKey::Mass: return "mass";
    case FieldKey::Density: return "density";
    case FieldKey::Temperature: return "temperature";
    case FieldKey::Value: return "value";
    case FieldKey::Value2: return "value2";
    case FieldKey::ID: return "ID";
    case FieldKey::InternalEnergy: return "internalenergy";
    case FieldKey::ElectronAbundance: return "ElectronAbundance";
    case FieldKey::H2Abundance: return "H2Abundance";
    case FieldKey::HDAbundance: return "HDAbundance";
    case FieldKey::J21: return "J21";
    case FieldKey::Gamma: return "Gamma";
    case FieldKey::Metallicity: return "Metallicity";
    case FieldKey::Dummy: return "dummy";
    case FieldKey::Type: return "type";
    default: return "unknown";
  }
}

inline FieldKey GetFieldKeyFromDisplayName(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c){ return (char)std::tolower(c); });
  if (name == "position")          return FieldKey::Position;
  if (name == "velocity")          return FieldKey::Velocity;
  if (name == "bfield")            return FieldKey::Bfield;
  if (name == "hsml")              return FieldKey::Hsml;
  if (name == "volume")            return FieldKey::Volume;
  if (name == "mass")              return FieldKey::Mass;
  if (name == "density")           return FieldKey::Density;
  if (name == "temperature")       return FieldKey::Temperature;
  if (name == "value")             return FieldKey::Value;
  if (name == "value2")            return FieldKey::Value2;
  if (name == "id")                return FieldKey::ID;
  if (name == "internalenergy")    return FieldKey::InternalEnergy;
  if (name == "electronabundance") return FieldKey::ElectronAbundance;
  if (name == "h2abundance")       return FieldKey::H2Abundance;
  if (name == "hdabundance")       return FieldKey::HDAbundance;
  if (name == "j21")               return FieldKey::J21;
  if (name == "gamma")             return FieldKey::Gamma;
  if (name == "metallicity")       return FieldKey::Metallicity;
  if (name == "dummy")             return FieldKey::Dummy;
  if (name == "type")              return FieldKey::Type;
  return FieldKey::Unknown;
}

inline const char* GetDefaultHDF5SourceName(FieldKey key) {
  switch (key) {
    case FieldKey::Position:          return "Coordinates";
    case FieldKey::Velocity:          return "Velocities";
    case FieldKey::Bfield:            return "MagneticField";
    case FieldKey::Mass:              return "Masses";
    case FieldKey::ID:                return "ParticleIDs";
    case FieldKey::Density:           return "Density";
    case FieldKey::Temperature:       return "Temperature";
    case FieldKey::ElectronAbundance: return "ElectronAbundance";
    case FieldKey::H2Abundance:       return "H2IAbundance";
    case FieldKey::HDAbundance:       return "HDIAbundance";
    case FieldKey::J21:               return "LymanWerner";
    case FieldKey::Gamma:             return "Gamma";
    case FieldKey::InternalEnergy:    return "InternalEnergy";
    case FieldKey::Metallicity:       return "Metallicity";
    case FieldKey::Value:             return "Metallicity";
    case FieldKey::Value2:            return "ElectronAbundance";
    case FieldKey::Volume:            return "Volume";
    case FieldKey::Hsml:              return "SmoothingLength";
    case FieldKey::Type:              return "ParticleType";    
    case FieldKey::Dummy:             return "dummy";
    default:                          return "unknown";
  }
}

inline void ApplyDefaultFieldSpec(FieldSpec& spec) {
  switch (spec.key) {
    case FieldKey::Position:
    case FieldKey::Velocity:
    case FieldKey::Bfield:
      spec.type = DataType::Float;
      spec.count = 3;
      spec.sourceName = GetDefaultHDF5SourceName(spec.key);
      break;

    case FieldKey::ID:
    case FieldKey::Type:
      spec.type = DataType::Int32;
      spec.count = 1;
      spec.sourceName = GetDefaultHDF5SourceName(spec.key);
      break;

    case FieldKey::Hsml:
    case FieldKey::Volume:
    case FieldKey::Mass:
    case FieldKey::Density:
    case FieldKey::Temperature:
    case FieldKey::InternalEnergy:
    case FieldKey::ElectronAbundance:
    case FieldKey::H2Abundance:
    case FieldKey::HDAbundance:
    case FieldKey::J21:
    case FieldKey::Gamma:
    case FieldKey::Metallicity:
    case FieldKey::Value:
    case FieldKey::Value2:
      spec.type = DataType::Float;
      spec.count = 1;
      spec.sourceName = GetDefaultHDF5SourceName(spec.key);
      break;

    case FieldKey::Dummy:
      spec.type = DataType::Float;
      spec.count = 1;
      spec.sourceName = GetDefaultHDF5SourceName(spec.key);
      break;

    case FieldKey::Unknown:
    default:
      spec.type = DataType::Float;
      spec.count = 1;
      spec.sourceName = "unknown";
      break;
  }
}


inline constexpr FieldKey kAvailableFieldKeys[] = {
  FieldKey::Position,
  FieldKey::Velocity,
  FieldKey::Bfield,
  FieldKey::Hsml,
  FieldKey::Volume,
  FieldKey::Mass,
  FieldKey::Density,
  FieldKey::Temperature,
  FieldKey::Value,
  FieldKey::Value2,
  FieldKey::ID,
  FieldKey::InternalEnergy,
  FieldKey::ElectronAbundance,
  FieldKey::H2Abundance,
  FieldKey::HDAbundance,
  FieldKey::J21,
  FieldKey::Gamma,
  FieldKey::Metallicity,
  FieldKey::Dummy,
  FieldKey::Type
};

inline constexpr int kNumAvailableFieldKeys =
  (int)(sizeof(kAvailableFieldKeys) / sizeof(kAvailableFieldKeys[0]));

class ParticleData;
struct FieldLayout{
  FieldSpec spec;
  int offset;
  DestKind dest = DestKind::Ignore;
  FieldKey ftype = FieldKey::Unknown;

  size_t aosExtOffset = 0;
  const char* soaKey = nullptr;
  void (*store)(ParticleData& p, const uint8_t* src) = nullptr;

  bool present = false;
};

struct BinaryReadLayout {
  size_t recordSize = 0;
  std::vector<FieldLayout> fields;
};
