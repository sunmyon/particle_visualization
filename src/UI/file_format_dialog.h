#pragma once

#include <array>
#include <string>
#include <vector>

#include "FileIO/file_format_types.h"
#include "app/state/file_format_dialog_state.h"
#include "core/input_density_units.h"
#include "core/units.h"

void DrawBinaryFormatDialog(FileFormatDialogState& state,
                            std::vector<FieldSpec>& formatTokens,
                            FileFormat readFormat);

void DrawInputFormatDialog(FileFormatDialogState& state,
                           std::vector<FieldSpec>& binaryFormatTokens,
                           std::vector<FieldSpec>& gadgetFormatTokens,
#ifdef HAVE_HDF5
                           std::vector<FieldSpec>& hdf5FormatTokens,
                           const char* hdf5MetadataPath,
#endif
                           FileFormat& readFormat,
                           InputDensityUnit& inputDensityUnit,
                           InputTemperatureUnit& inputTemperatureUnit,
                           InputMagneticFieldUnit& inputMagneticFieldUnit,
                           std::array<std::string, kCustomScalarFieldCount>& customScalarLabels,
                           UnitSystem& unitsDraft,
                           const UnitSystem& currentUnits,
                           bool& unitsDraftDirty,
                           bool& applyUnitsRequested,
                           bool& unitConversionRebuildRequested);

#ifdef HAVE_HDF5
void DrawHDF5FormatDialog(FileFormatDialogState& state,
                          std::vector<FieldSpec>& formatTokens);
#endif

void DrawOutputFormatDialog(FileFormatDialogState& state,
                            SnapshotOutputFormatConfig& outputFormat,
                            const char* hdf5MetadataPath);
