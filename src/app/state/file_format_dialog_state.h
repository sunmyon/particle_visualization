#pragma once

#include "FileIO/file_format_types.h"
#include "FileIO/snapshot_extract.h"
struct FileFormatDialogState {
#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
#endif
  bool showOutputFormatDialog = false;
  bool showFormatDialog = false;
  std::vector<FieldSpec> formatTokensEdit;
  SnapshotOutputFormatConfig outputFormatEdit;
};
