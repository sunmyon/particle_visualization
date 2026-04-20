#include <filesystem>
#include <string>
#include <cctype>
#include <cstring>

#include "FileIO/file_io.h"

namespace {
  std::pair<std::string, int> convertFilenameToFormatAndExtractNumber(const std::string& filename)
  {
    size_t dotPos = filename.find_last_of('.');
    std::string basename = (dotPos == std::string::npos) ? filename : filename.substr(0, dotPos);
    std::string extension = (dotPos == std::string::npos) ? "" : filename.substr(dotPos); 

    // 最後の数字がある位置を探す
    size_t pos = basename.find_last_of("0123456789");
    if (pos == std::string::npos)
      return std::make_pair(filename, 0); // 数字が見つからなければそのまま返す

    // 数字部分の開始位置を後ろから辿って求める
    size_t numEnd = pos;
    size_t numStart = pos;
    while (numStart > 0 && std::isdigit(basename[numStart - 1])) {
      numStart--;
    }
    size_t numLen = numEnd - numStart + 1;

    // ファイル名の前半部分（数字部分以前）を取得
    std::string prefix = basename.substr(0, numStart);
    // この例では数字部分以降の文字列は取り除く（必要に応じて拡張子などを扱えます）
    std::string suffix = "";

    // 桁数に合わせたフォーマット指定子を作成（例: 3桁なら "%03d"）
    std::string formatSpecifier = "%" + std::string("0") + std::to_string(numLen) + "d";

    std::string newFormat = prefix + formatSpecifier + suffix + extension;

    // 数字部分を整数に変換
    int fileNumber = -1;
    try {
      fileNumber = std::stoi(basename.substr(numStart, numLen));
    } catch (const std::exception& e) {
      fileNumber = -1;
    }
    
    return std::make_pair(newFormat, fileNumber);
  }
}

void FileInfo::applySelectedFilePath(const char* fullPath) {
  if (!fullPath || fullPath[0] == '\0')
    return;

  auto& src = source;

  std::strncpy(src.filePath, fullPath, sizeof(src.filePath));
  src.filePath[sizeof(src.filePath) - 1] = '\0';

  std::filesystem::path p(src.filePath);

  std::string folder = p.parent_path().string();
  if (!folder.empty()) {
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (folder.back() != sep)
      folder.push_back(sep);

    std::strncpy(src.folderPath, folder.c_str(), sizeof(src.folderPath));
    src.folderPath[sizeof(src.folderPath) - 1] = '\0';
  }

  std::string filename = p.filename().string();

#ifdef HAVE_HDF5
  source.useHDF5 = false;
  std::string ext = p.extension().string();
  if (ext == ".hdf5" || ext == ".h5")
    source.useHDF5 = true;
#endif

  auto res = convertFilenameToFormatAndExtractNumber(filename);

  std::strncpy(src.fileFormat, res.first.c_str(), sizeof(src.fileFormat));
  src.fileFormat[sizeof(src.fileFormat) - 1] = '\0';

  src.initialIndex = res.second;
  src.currentFileIndex = src.initialIndex + src.currentStep * src.skipStep;
}

void FileInfo::generateTestData(ParticleArray *P){
  loader.generateTestData(P);
}

void FileInfo::loadNewSnapshot(int newFileIndex, ParticleArray *P){
  prefetchController.loadNewSnapshot(newFileIndex, P);
  snapshotUpdated = true;
}

void FileInfo::drawDialogs() {
  DrawBinaryFormatDialog(formatDialog, source);
#ifdef HAVE_HDF5
  DrawHDF5FormatDialog(formatDialog, source);
#endif
}
