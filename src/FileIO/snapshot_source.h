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

    auto push = [&](FieldKey key, DataType type, int count) {
      FieldSpec token;
      token.key = key;
      token.type = type;
      token.count = count;
      token.typeMask = DefaultFieldTypeMask(key);
      formatTokens.push_back(token);
    };

    push(FieldKey::Position,    DataType::Float, 3);
    push(FieldKey::Velocity,    DataType::Float, 3);
    push(FieldKey::Type,        DataType::Int32, 1);
    push(FieldKey::ID,          DataType::Int32, 1);
    push(FieldKey::Hsml,        DataType::Float, 1);
    push(FieldKey::Density,     DataType::Float, 1);
    push(FieldKey::Temperature, DataType::Float, 1);
    push(FieldKey::Dummy,       DataType::Float, 1);
    push(FieldKey::Value,       DataType::Float, 1);
    push(FieldKey::Value2,      DataType::Float, 1);
    push(FieldKey::Dummy,       DataType::Float, 4);
    push(FieldKey::Mass,        DataType::Float, 1);

    formatTokens_hdf5 = formatTokens;
  }

  SnapshotSource() {
    initDefaultFormatTokens();
  }

private:
  FileFormat readFileFormat = FileFormat::Auto;
};
