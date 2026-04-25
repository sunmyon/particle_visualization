#pragma once

#include <vector>

#include "FileIO/file_format_types.h"
#include "app/file_format_dialog_state.h"

void OpenBinaryFormatDialog(FileFormatDialogState& state,
                            const std::vector<FieldSpec>& formatTokens);

void DrawBinaryFormatDialog(FileFormatDialogState& state,
                            std::vector<FieldSpec>& formatTokens);

#ifdef HAVE_HDF5
void OpenHDF5FormatDialog(FileFormatDialogState& state,
                          const std::vector<FieldSpec>& formatTokens);

void DrawHDF5FormatDialog(FileFormatDialogState& state,
                          std::vector<FieldSpec>& formatTokens);
#endif
