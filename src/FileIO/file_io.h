#pragma once
#include "FileIO/snapshot_source.h"
#include "FileIO/snapshot_loader.h"
#include "FileIO/snapshot_prefetch_controller.h"

struct NormalizationContext;
struct InputFilterConfig;
struct HeaderInfo;

class FileInfo{
private:
  SnapshotSource source;
  SnapshotLoader loader;
  SnapshotPrefetchController prefetchController;
    
public:
  FileInfo() : loader(source), prefetchController(source, loader) {}

  SnapshotSource& editSource() { return source; }
  const SnapshotSource& getSource() const { return source; }
  
  bool snapshotUpdated = false;
  void clearSnapshotUpdated() {
    snapshotUpdated = false;
  }
  
  void loadNewSnapshot(int newindex, ParticleArray* P, HeaderInfo& header, NormalizationContext& normalization, const InputFilterConfig& filter);
  void generateTestData(ParticleArray *P, HeaderInfo& header, NormalizationContext& normalization);
  
  void applySelectedFilePath(const char* fullPath);
  
  void setUnit(UnitSystem& units_input){
    source.setUnit(units_input);
  }

  TrackingVector<int> getStarParticleID(int indexFile, const InputFilterConfig& filter){
    return loader.getStarParticleID(indexFile, filter);
  }

  void setFormatMode(FileFormat form){
    source.setFormatMode(form);
  }

  int getFormatMode_int(void) const {
    return source.getFormatMode_int();
  }
 
  FileFormat getFormatMode(void) const {
    return source.getFormatMode();
  }

  bool isLoading() const {
    return prefetchController.isLoading();
  }
};
