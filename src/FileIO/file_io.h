#pragma once
#include "FileIO/snapshot_source.h"
#include "FileIO/snapshot_loader.h"
#include "FileIO/snapshot_prefetch_controller.h"

class FileInfo{
private:
  SnapshotSource source;
  SnapshotLoader loader;
  SnapshotPrefetchController prefetchController;
  
#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
#endif
  bool showFormatDialog = false;
  std::vector<FieldSpec> formatTokensEdit;
    
public:
  FileInfo() : loader(source), prefetchController(source, loader) {}

  SnapshotSource& editSource() { return source; }
  const SnapshotSource& getSource() const { return source; }
  
  bool snapshotUpdated = false;
  void clearSnapshotUpdated() {
    snapshotUpdated = false;
  }
  
  void loadNewSnapshot(int newindex, ParticleArray* P);
  void generateTestData(ParticleArray *P);
  
#ifdef HAVE_HDF5
  void showHDF5Dialog(void){    
    showHDF5MappingDialog = true;
    formatTokensEdit = source.formatTokens_hdf5;
  };
#endif
  
  void showDialog(void){
    showFormatDialog = true;
    formatTokensEdit = source.formatTokens;
  };
  void applySelectedFilePath(const char* fullPath);
  void drawDialogs();
  
#ifdef HAVE_HDF5
  HaloCatalog readHaloCatalogFromHDF5(char *fname, bool loadIDs /*=true*/);
#endif

  void setUnit(UnitSystem& units_input){
    source.setUnit(units_input);
  }

  void setMaskConfig(const MaskUIState& cfg){
    source.setMaskConfig(cfg);
  }

  TrackingVector<int> getStarParticleID(int indexFile){
    return loader.getStarParticleID(indexFile);
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
