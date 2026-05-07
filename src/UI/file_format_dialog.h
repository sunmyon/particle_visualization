#pragma once

#include <vector>

#include "FileIO/file_format_types.h"
#include "app/state/file_format_dialog_state.h"

void DrawBinaryFormatDialog(FileFormatDialogState& state,
                            std::vector<FieldSpec>& formatTokens,
                            FileFormat readFormat);

void DrawInputFormatDialog(FileFormatDialogState& state,
                           std::vector<FieldSpec>& binaryFormatTokens,
                           std::vector<FieldSpec>& gadgetFormatTokens,
#ifdef HAVE_HDF5
                           std::vector<FieldSpec>& hdf5FormatTokens,
#endif
                           FileFormat& readFormat);

#ifdef HAVE_HDF5
void DrawHDF5FormatDialog(FileFormatDialogState& state,
                          std::vector<FieldSpec>& formatTokens);
#endif

void DrawOutputFormatDialog(FileFormatDialogState& state,
                            SnapshotOutputFormatConfig& outputFormat);
