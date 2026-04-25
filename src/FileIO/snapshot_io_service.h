#pragma once

#include <vector>

#include "FileIO/file_format_types.h"
#include "FileIO/snapshot_loader.h"
#include "FileIO/snapshot_prefetch_controller.h"
#include "FileIO/snapshot_read_result.h"
#include "FileIO/snapshot_source.h"
#include "core/units.h"

struct InputFilterConfig;

struct SnapshotLoadParams {
  int initialIndex = 0;
  int currentFileIndex = 0;
  int batchSize = 1;
  int skipStep = 1;
  int currentStep = 0;

  char fileFormat[255] = "output_%04d.dat";
  char folderPath[255] = "./example/";
  char filePath[512]   = "./example/output_0000.dat";

#ifdef HAVE_HDF5
  bool useHDF5 = false;
#endif

  FileFormat readFormat = FileFormat::Auto;
  std::vector<FieldSpec> formatTokens;
  std::vector<FieldSpec> formatTokensHdf5;
  UnitSystem units;
};

class SnapshotIOService {
public:
  SnapshotIOService();

  bool loadNewSnapshot(int newFileIndex,
                       const SnapshotLoadParams& params,
                       SnapshotReadResult& outResult,
                       const InputFilterConfig& filter);

  TrackingVector<int> getStarParticleID(int indexFile,
                                        const SnapshotLoadParams& params,
                                        const InputFilterConfig& filter);

  bool isLoading() const;

private:
  void applyParams(const SnapshotLoadParams& params);

private:
  SnapshotSource source_;
  SnapshotLoader loader_;
  SnapshotPrefetchController prefetch_;
};

