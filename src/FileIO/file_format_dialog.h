#pragma once

#include "FileIO/snapshot_source.h"

#ifdef HAVE_HDF5
void DrawHDF5FormatDialog(bool& showDialog,
                          std::vector<FieldSpec>& formatTokensEdit,
                          SnapshotSource& source);
#endif

void DrawBinaryFormatDialog(bool& showDialog,
                            std::vector<FieldSpec>& formatTokensEdit,
                            SnapshotSource& source);
