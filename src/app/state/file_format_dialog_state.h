#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "FileIO/file_format_types.h"
#include "FileIO/snapshot_extract.h"

#ifdef HAVE_HDF5
struct Hdf5DatasetMetadataPreview {
  std::string sourceName;
  DataType type = DataType::Float;
  int count = 1;
  std::uint8_t typeMask = 0;
};
#endif

struct FileFormatDialogState {
#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
  bool hdf5MetadataScanned = false;
  std::string hdf5MetadataPath;
  std::string hdf5MetadataMessage;
  std::vector<Hdf5DatasetMetadataPreview> hdf5MetadataPreview;
#endif
  bool showOutputFormatDialog = false;
  bool outputFormatShowMissingFields = false;
  bool showFormatDialog = false;
  bool inputFormatDialogInitialized = false;
  bool selectInputFormatTabOnOpen = false;
  FileFormat activeInputFormatTab = FileFormat::Binary;
  std::vector<FieldSpec> formatTokensEdit;
  std::vector<FieldSpec> binaryFormatTokensEdit;
  std::vector<FieldSpec> gadgetFormatTokensEdit;
#ifdef HAVE_HDF5
  std::vector<FieldSpec> hdf5FormatTokensEdit;
#endif
  SnapshotOutputFormatConfig outputFormatEdit;
};
