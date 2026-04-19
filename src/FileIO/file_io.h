#pragma once
// POSIX I/O 用
#include "data/particle_array.h"
#include "data/header_info.h"
#include "core/PerfTimer.h"
#include "core/units.h"
#include "app/ui_state.h"

#include "FileIO/file_format_types.h"
#include "FileIO/prefetch_cache.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <filesystem>


#include <future>
#include <atomic>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

// ------------------------------
// 連番ファイル読み込み用グローバル変数
// ------------------------------
class FileInfo{
public:
  int initialIndex = 0;
  int currentFileIndex;
  int batchSize = 1;
  int skipStep = 1;
  int currentStep = 0;      // **現在のステップ（n）**
  
  char fileFormat[255] = "output_%04d.dat"; // 例: "output_%04d.dat"
  char folderPath[255] = "./example/";              // 末尾に "/" を付加する
  char filePath[512] = "./example/output_0000.dat";              // 末尾に "/" を付加する

  bool isLoading = false;   // **ロード中かどうか**
  
  // ------------------------------
  // データフォーマット編集用ダイアログ用変数
  // ------------------------------

#ifdef HAVE_HDF5
  bool useHDF5 = false;
#endif
  std::vector<FieldSpec> formatTokens;
  std::vector<FieldSpec> formatTokens_hdf5;

  void setFormatMode(FileFormat form){
    readFileFormat = form;
  };

  int getFormatMode_int(void) const {
    return static_cast<int>(readFileFormat);
  };
  
  FileFormat getFormatMode(void) const {
    return readFileFormat;
  };
  
private:
  FileFormat readFileFormat = FileFormat::Auto;

  PrefetchCache prefetchCache;
  
  std::future<void> prefetchFuture;
  std::atomic<bool> prefetchRunning{false};
  std::atomic<int> prefetchGeneration{0};
  
#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
#endif

  bool showFormatDialog = false;
  std::vector<FieldSpec> formatTokensEdit; // 編集用一時コピー
  
  // 更新タイミングでのみ使うmutex
  std::mutex g_dataMutex;
    
  bool syncLoadFirstFile(int targetFile, ParticleArray *P);
  void asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep, int generation);

  bool loadSingleFile(int fileNumber, ParticleBlock& particles);

#ifdef HAVE_HDF5
  TrackingVector<ParticleData> loadParticlesFromHDF5(const std::string& filename, HeaderInfo& hdr);
#endif
  
  void initDefaultFormatTokens();
  UnitSystem units;

  MaskUIState currentMaskConfig;
  bool       enableMask = false; // mask を使うか（UIでON/OFF）
  
public:
  FileInfo(){
    initDefaultFormatTokens();
  }

  bool snapshotUpdated = false;
  void clearSnapshotUpdated() {
    snapshotUpdated = false;
  }
  
  void setUnit(UnitSystem& units_input){
    units = units_input;
  }
 
  void loadNewSnapshot(int newindex, ParticleArray* P);
  void loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray *P);
  void generateTestData(ParticleArray *P);
  
#ifdef HAVE_HDF5
  void ShowHDF5FieldMappingDialog();
  void showHDF5Dialog(void){    
    showHDF5MappingDialog = true;
    formatTokensEdit = formatTokens_hdf5;
  };
#endif
  
  void DrawFormatDialog();
  void showDialog(void){
    showFormatDialog = true;
    formatTokensEdit = formatTokens;
  };

#ifdef HAVE_HDF5
  HaloCatalog readHaloCatalogFromHDF5(char *fname, bool loadIDs /*=true*/);
#endif

  void setMaskConfig(const MaskUIState& cfg){
    currentMaskConfig = cfg;
    enableMask = true;
  } 
  
  TrackingVector<int> getStarParticleID(int indexFile);
};
