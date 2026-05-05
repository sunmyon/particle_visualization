#pragma once

#include <vector>

#include "FileIO/file_format_types.h"
#include "core/units.h"

struct SnapshotLoadParams {
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

  FileFormat readFormat = FileFormat::Auto;
  std::vector<FieldSpec> formatTokens;
  std::vector<FieldSpec> formatTokensHdf5;
  std::vector<FieldSpec> formatTokensGadget;
  UnitSystem units;
};
