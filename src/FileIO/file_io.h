#pragma once
#include "FileIO/snapshot_source.h"
#include "FileIO/snapshot_loader.h"
#include "FileIO/snapshot_prefetch_controller.h"
#include "FileIO/file_format_dialog.h"

class FileInfo{
private:
  SnapshotSource source;
  SnapshotLoader loader;
  SnapshotPrefetchController prefetchController;
  FileFormatDialogState formatDialog;
    
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
    formatDialog.openHDF5(source);
  };
#endif

  void showDialog(void){
    formatDialog.openBinary(source);
  };
  void applySelectedFilePath(const char* fullPath);
  void drawDialogs();
  
#ifdef HAVE_HDF5
  HaloCatalog readHaloCatalogFromHDF5(char *fname, bool loadIDs /*=true*/);
#endif

  void setUnit(UnitSystem& units_input){
    source.setUnit(units_input);
  }

  void setMaskConfig(const ParticleMaskConfig& cfg){
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
