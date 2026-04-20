#pragma once

#include "FileIO/file_format_types.h"
struct FileFormatDialogState {
#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
#endif
  bool showFormatDialog = false;
  std::vector<FieldSpec> formatTokensEdit;
};
