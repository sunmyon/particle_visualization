#pragma once
#include "data/data_type.h"

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <array>

enum class FileFormat {
  Auto,       // Infer from extension, preserving legacy behavior.
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
  Custom3,
  Custom4,
  Custom5,
  Custom6,
  Custom7,
  Custom8,
  Custom9,
  Custom10,
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

inline constexpr int kCustomScalarFieldCount = 10;

inline const char* GetCustomScalarFieldDisplayName(int index)
{
  static constexpr std::array<const char*, kCustomScalarFieldCount> names = {
    "custom1", "custom2", "custom3", "custom4", "custom5",
    "custom6", "custom7", "custom8", "custom9", "custom10"
  };
  return (index >= 0 && index < kCustomScalarFieldCount) ? names[index] : "";
}

inline std::array<std::string, kCustomScalarFieldCount>
MakeDefaultCustomScalarFieldLabels()
{
  std::array<std::string, kCustomScalarFieldCount> labels{};
  for (int i = 0; i < kCustomScalarFieldCount; ++i) {
    labels[static_cast<std::size_t>(i)] = GetCustomScalarFieldDisplayName(i);
  }
  return labels;
}

inline bool IsCustomScalarFieldKey(FieldKey key)
{
  switch (key) {
  case FieldKey::Value:
  case FieldKey::Value2:
  case FieldKey::Custom3:
  case FieldKey::Custom4:
  case FieldKey::Custom5:
  case FieldKey::Custom6:
  case FieldKey::Custom7:
  case FieldKey::Custom8:
  case FieldKey::Custom9:
  case FieldKey::Custom10:
    return true;
  default:
    return false;
  }
}

inline int CustomScalarFieldIndex(FieldKey key)
{
  switch (key) {
  case FieldKey::Value:    return 0;
  case FieldKey::Value2:   return 1;
  case FieldKey::Custom3:  return 2;
  case FieldKey::Custom4:  return 3;
  case FieldKey::Custom5:  return 4;
  case FieldKey::Custom6:  return 5;
  case FieldKey::Custom7:  return 6;
  case FieldKey::Custom8:  return 7;
  case FieldKey::Custom9:  return 8;
  case FieldKey::Custom10: return 9;
  default:                 return -1;
  }
}

inline FieldKey CustomScalarFieldKey(int index)
{
  static constexpr std::array<FieldKey, kCustomScalarFieldCount> keys = {
    FieldKey::Value,
    FieldKey::Value2,
    FieldKey::Custom3,
    FieldKey::Custom4,
    FieldKey::Custom5,
    FieldKey::Custom6,
    FieldKey::Custom7,
    FieldKey::Custom8,
    FieldKey::Custom9,
    FieldKey::Custom10
  };
  return (index >= 0 && index < kCustomScalarFieldCount)
    ? keys[index]
    : FieldKey::Unknown;
}

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
  std::uint8_t typeMask = 0x3fu;
};

inline std::uint8_t DefaultFieldTypeMask(FieldKey key)
{
  switch (key) {
  case FieldKey::Position:
  case FieldKey::Velocity:
  case FieldKey::ID:
  case FieldKey::Mass:
    return 0x3fu;
  default:
    return 0x01u;
  }
}

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
    case FieldKey::Value: return GetCustomScalarFieldDisplayName(0);
    case FieldKey::Value2: return GetCustomScalarFieldDisplayName(1);
    case FieldKey::Custom3: return GetCustomScalarFieldDisplayName(2);
    case FieldKey::Custom4: return GetCustomScalarFieldDisplayName(3);
    case FieldKey::Custom5: return GetCustomScalarFieldDisplayName(4);
    case FieldKey::Custom6: return GetCustomScalarFieldDisplayName(5);
    case FieldKey::Custom7: return GetCustomScalarFieldDisplayName(6);
    case FieldKey::Custom8: return GetCustomScalarFieldDisplayName(7);
    case FieldKey::Custom9: return GetCustomScalarFieldDisplayName(8);
    case FieldKey::Custom10: return GetCustomScalarFieldDisplayName(9);
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
  if (name == "custom1" || name == "value" || name == "val" ||
      name == "val1")              return FieldKey::Value;
  if (name == "custom2" || name == "value2" || name == "val2")
                                  return FieldKey::Value2;
  if (name == "custom3")           return FieldKey::Custom3;
  if (name == "custom4")           return FieldKey::Custom4;
  if (name == "custom5")           return FieldKey::Custom5;
  if (name == "custom6")           return FieldKey::Custom6;
  if (name == "custom7")           return FieldKey::Custom7;
  if (name == "custom8")           return FieldKey::Custom8;
  if (name == "custom9")           return FieldKey::Custom9;
  if (name == "custom10")          return FieldKey::Custom10;
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

inline const char* GetDefaultHDF5DatasetName(FieldKey key) {
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
    case FieldKey::Value:             return "custom1";
    case FieldKey::Value2:            return "custom2";
    case FieldKey::Custom3:           return "custom3";
    case FieldKey::Custom4:           return "custom4";
    case FieldKey::Custom5:           return "custom5";
    case FieldKey::Custom6:           return "custom6";
    case FieldKey::Custom7:           return "custom7";
    case FieldKey::Custom8:           return "custom8";
    case FieldKey::Custom9:           return "custom9";
    case FieldKey::Custom10:          return "custom10";
    case FieldKey::Volume:            return "Volume";
    case FieldKey::Hsml:              return "SmoothingLength";
    case FieldKey::Type:              return "ParticleType";    
    case FieldKey::Dummy:             return "dummy";
    default:                          return "unknown";
  }
}

inline const char* GetDefaultHDF5SourceName(FieldKey key) {
  return GetDefaultHDF5DatasetName(key);
}

inline void ApplyDefaultFieldSpec(FieldSpec& spec) {
  spec.typeMask = DefaultFieldTypeMask(spec.key);
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
    case FieldKey::Custom3:
    case FieldKey::Custom4:
    case FieldKey::Custom5:
    case FieldKey::Custom6:
    case FieldKey::Custom7:
    case FieldKey::Custom8:
    case FieldKey::Custom9:
    case FieldKey::Custom10:
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

inline std::vector<FieldSpec> MakeDefaultGadgetFormatTokens()
{
  std::vector<FieldSpec> tokens;
  tokens.reserve(8);

  auto push = [&](FieldKey key,
                  DataType type,
                  int count,
                  const char* domainSource,
                  std::uint8_t typeMask) {
    FieldSpec spec;
    spec.key = key;
    spec.type = type;
    spec.count = count;
    spec.sourceName = domainSource;
    spec.typeMask = typeMask;
    tokens.push_back(std::move(spec));
  };

  push(FieldKey::Position,       DataType::Float, 3, "types:1", 0x3fu);
  push(FieldKey::Velocity,       DataType::Float, 3, "types:1", 0x3fu);
  push(FieldKey::ID,             DataType::Int32, 1, "types:1", 0x3fu);
  push(FieldKey::Mass,           DataType::Float, 1, "types:1", 0x3fu);
  push(FieldKey::InternalEnergy, DataType::Float, 1, "types:1", 0x01u);
  push(FieldKey::Density,        DataType::Float, 1, "types:1", 0x01u);
  push(FieldKey::H2Abundance,    DataType::Float, 1, "types:1", 0x01u);
  push(FieldKey::Hsml,           DataType::Float, 1, "types:1", 0x01u);

  return tokens;
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
  FieldKey::Custom3,
  FieldKey::Custom4,
  FieldKey::Custom5,
  FieldKey::Custom6,
  FieldKey::Custom7,
  FieldKey::Custom8,
  FieldKey::Custom9,
  FieldKey::Custom10,
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

class SimulationElement;
struct FieldLayout{
  FieldSpec spec;
  int offset;
  DestKind dest = DestKind::Ignore;
  FieldKey ftype = FieldKey::Unknown;

  size_t aosExtOffset = 0;
  const char* soaKey = nullptr;
  void (*store)(SimulationElement& p, const uint8_t* src) = nullptr;

  bool present = false;
};

struct BinaryReadLayout {
  size_t recordSize = 0;
  std::vector<FieldLayout> fields;
};
