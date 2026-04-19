#pragma once

#include <string>
#include "FileIO/file_format_spec.h"

enum class FieldKey {
  Position,
  Velocity,
  Bfield,
  Hsml,
  Volume,
  Mass,
  Density,
  Temperature,
  Value1,
  Value2,
  Type,
  ID,
  Dummy,
  InternalEnergy,
  ElectronFraction,
  H2Fraction,
  HDAbundance,
  J21,
  Metallicity,
  Gamma,
  Unknown
};

struct FieldMapping {
  FieldKey key = FieldKey::Unknown;
  DestKind dest = DestKind::Ignore;
  std::string soaKey;
};

FieldKey parseFieldKey(const std::string& key);
FieldMapping makeFieldMapping(const FieldSpec& spec);
bool isAoSCoreField(FieldKey key);
