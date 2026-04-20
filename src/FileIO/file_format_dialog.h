#pragma once

#include "FileIO/snapshot_source.h"
#include "app/ui_state.h"

void OpenBinaryFormatDialog(FileFormatDialogState& state,
                            const SnapshotSource& source);

void DrawBinaryFormatDialog(FileFormatDialogState& state,
                            SnapshotSource& source);

#ifdef HAVE_HDF5
void OpenHDF5FormatDialog(FileFormatDialogState& state,
                          const SnapshotSource& source);

void DrawHDF5FormatDialog(FileFormatDialogState& state,
                          SnapshotSource& source);
#endif
