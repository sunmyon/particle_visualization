#pragma once

#include "FileIO/file_format_types.h"
#include "FileIO/snapshot_extract.h"
struct FileFormatDialogState {
#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
#endif
  bool showOutputFormatDialog = false;
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
