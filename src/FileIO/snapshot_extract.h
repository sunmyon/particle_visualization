#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "FileIO/file_format_types.h"

struct SimulationBlock;

enum class SnapshotExtractRegionKind {
  Box = 0,
  Sphere = 1
};

enum class SnapshotExtractUnitMode {
  PreserveRaw = 0
};

enum class SnapshotExtractComovingMode {
  Preserve = 0,
  ComovingToPhysical = 1,
  PhysicalToComoving = 2
};

struct SnapshotExtractUnitConversion {
  bool enabled = false;
  double sourceUnitLengthCm = 3.0856775814913673e18;
  double sourceUnitMassG = 1.98847e33;
  double sourceUnitVelocityCmPerS = 1.0e5;
  double sourceHubbleParam = 1.0;
  double sourceScaleFactor = 1.0;
  double unitLengthCm = 3.0856775814913673e18;
  double unitMassG = 1.98847e33;
  double unitVelocityCmPerS = 1.0e5;
  double hubbleParam = 1.0;
  double scaleFactor = 1.0;
  SnapshotExtractComovingMode comovingMode = SnapshotExtractComovingMode::Preserve;
};

struct SnapshotExtractRegion {
  SnapshotExtractRegionKind kind = SnapshotExtractRegionKind::Box;
  std::array<double, 3> center = {0.0, 0.0, 0.0};
  std::array<double, 3> halfSize = {1.0, 1.0, 1.0};
  double radius = 1.0;
};

struct SnapshotBackgroundGridConfig {
  bool enabled = false;
  int cellsPerAxis = 16;
  double density = 1.0e-30;
};

struct SnapshotParticleIdTransform {
  bool offsetEnabled = false;
  std::uint64_t offset = 0;
};

enum class SnapshotOutputMissingPolicy {
  Omit = 0,
  FillDefault = 1,
  Require = 2
};

struct SnapshotOutputFieldSpec {
  FieldKey key = FieldKey::Unknown;
  DataType type = DataType::Double;
  int count = 1;
  std::string outputName;
  SnapshotOutputMissingPolicy missingPolicy = SnapshotOutputMissingPolicy::Omit;
  std::vector<double> defaultValues;
  unsigned int typeMask = 0x3fu;
};

struct SnapshotOutputFormatConfig {
  bool enabled = false;
  std::vector<SnapshotOutputFieldSpec> fields;
};

struct SnapshotExtractJob {
  std::string inputPath;
  std::string outputPath;
  SnapshotExtractRegion region;
  SnapshotExtractUnitMode unitMode = SnapshotExtractUnitMode::PreserveRaw;
  SnapshotExtractUnitConversion unitConversion;
  SnapshotBackgroundGridConfig backgroundGrid;
  SnapshotParticleIdTransform particleIdTransform;
  std::vector<FieldSpec> fields;
  SnapshotOutputFormatConfig outputFormat;
  bool copyHeader = true;
  bool copyParameters = true;
};

struct SnapshotExtractReport {
  bool ok = false;
  std::string message;
  std::array<std::size_t, 6> sourceCounts = {0, 0, 0, 0, 0, 0};
  std::array<std::size_t, 6> selectedCounts = {0, 0, 0, 0, 0, 0};
  std::array<std::size_t, 6> extractedCounts = {0, 0, 0, 0, 0, 0};
  std::size_t sourceParticles = 0;
  std::size_t selectedParticles = 0;
  std::size_t extractedParticles = 0;
  std::size_t backgroundParticles = 0;
  double suggestedMeanVolume = 0.0;
  double suggestedReferenceGasPartMass = 0.0;
  std::size_t copiedDatasets = 0;
  std::size_t skippedDatasets = 0;
};

struct SnapshotLoadedExtractMetadata {
  double time = 1.0;
  double redshift = 0.0;
  double boxSize = 0.0;
  double omega0 = 0.0;
  double omegaLambda = 0.0;
  double omegaBaryon = 0.0;
  double hubbleParam = 1.0;
  double unitLengthCm = 3.0856775814913673e18;
  double unitMassG = 1.98847e33;
  double unitVelocityCmPerS = 1.0e5;
  bool comoving = false;
};

std::vector<FieldSpec> MakeDefaultSnapshotExtractFields();
std::vector<SnapshotOutputFieldSpec> MakeDefaultSnapshotOutputFields();

SnapshotExtractReport ExtractHdf5SnapshotRegion(const SnapshotExtractJob& job);
SnapshotExtractReport ExtractLoadedSnapshotRegionToHdf5(
  const SnapshotExtractJob& job,
  const SimulationBlock& block,
  const SnapshotLoadedExtractMetadata& metadata);
