#pragma once

#include "FileIO/snapshot_source.h"

struct FileFormatDialogState {
#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
#endif
  bool showFormatDialog = false;
  std::vector<FieldSpec> formatTokensEdit;

  void openBinary(const SnapshotSource& source) {
    showFormatDialog = true;
    formatTokensEdit = source.formatTokens;
  }

#ifdef HAVE_HDF5
  void openHDF5(const SnapshotSource& source) {
    showHDF5MappingDialog = true;
    formatTokensEdit = source.formatTokens_hdf5;
  }
#endif
};

#ifdef HAVE_HDF5
void DrawHDF5FormatDialog(FileFormatDialogState& state,
                          SnapshotSource& source);
#endif

void DrawBinaryFormatDialog(FileFormatDialogState& state,
                            SnapshotSource& source);
