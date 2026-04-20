#pragma once

#include "data/header_info.h"
#include "core/units.h"

#include "FileIO/file_format_types.h"

#include <vector>
#include <cstring>

struct SnapshotSource {
public:
  int initialIndex = 0;
  int currentFileIndex = 0;
  int batchSize = 1;
  int skipStep = 1;
  int currentStep = 0;

  char fileFormat[255] = "output_%04d.dat";
  char folderPath[255] = "./example/";
  char filePath[512]   = "./example/output_0000.dat";

#ifdef HAVE_HDF5
  bool useHDF5 = false;
#endif

  std::vector<FieldSpec> formatTokens;
  std::vector<FieldSpec> formatTokens_hdf5;

  UnitSystem units;

  void setFormatMode(FileFormat form) {
    readFileFormat = form;
  }

  int getFormatMode_int() const {
    return static_cast<int>(readFileFormat);
  }

  FileFormat getFormatMode() const {
    return readFileFormat;
  }

  void setUnit(const UnitSystem& units_input) {
    units = units_input;
  }

  void initDefaultFormatTokens() {
    formatTokens.clear();

    FieldSpec token;
    token.key = FieldKey::Position;    token.type = DataType::Float; token.count = 3;
    formatTokens.push_back(token);
    token.key = FieldKey::Velocity;    token.type = DataType::Float; token.count = 3;
    formatTokens.push_back(token);
    token.key = FieldKey::Type;        token.type = DataType::Int32; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::ID;          token.type = DataType::Int32; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Hsml;        token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Density;     token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Temperature; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Dummy;       token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Value;       token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Value2;      token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Dummy;       token.type = DataType::Float; token.count = 4;
    formatTokens.push_back(token);
    token.key = FieldKey::Mass;        token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);

    formatTokens_hdf5 = formatTokens;
  }

  SnapshotSource() {
    initDefaultFormatTokens();
  }

private:
  FileFormat readFileFormat = FileFormat::Auto;
};
