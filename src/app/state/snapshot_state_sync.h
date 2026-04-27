#pragma once

#include "app/state/runtime_state.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

inline void CopySnapshotCString(char* dst, size_t dstSize, const char* src)
{
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dstSize);
  dst[dstSize - 1] = '\0';
}

inline void RecomputeCurrentFileIndex(FileNavigationRuntimeState& rt)
{
  auto& nav = rt.navigation;
  if (nav.skipStep <= 0) nav.skipStep = 1;
  nav.currentFileIndex = nav.initialIndex + nav.currentStep * nav.skipStep;
}

inline void RefreshSnapshotFilePath(FileNavigationRuntimeState& rt)
{
  char fileNameOnly[255];
  std::snprintf(fileNameOnly,
                sizeof(fileNameOnly),
                rt.input.fileFormat,
                rt.navigation.initialIndex);
  std::snprintf(rt.input.filePath,
                sizeof(rt.input.filePath),
                "%s/%s",
                rt.input.folderPath,
                fileNameOnly);
}

inline std::pair<std::string, int> ConvertFilenameToFormatAndExtractNumber(const std::string& filename)
{
  const size_t dotPos = filename.find_last_of('.');
  const std::string basename = (dotPos == std::string::npos) ? filename : filename.substr(0, dotPos);
  const std::string extension = (dotPos == std::string::npos) ? "" : filename.substr(dotPos);

  const size_t pos = basename.find_last_of("0123456789");
  if (pos == std::string::npos) {
    return std::make_pair(filename, 0);
  }

  size_t numEnd = pos;
  size_t numStart = pos;
  while (numStart > 0 && std::isdigit(static_cast<unsigned char>(basename[numStart - 1]))) {
    numStart--;
  }
  const size_t numLen = numEnd - numStart + 1;

  const std::string prefix = basename.substr(0, numStart);
  const std::string formatSpecifier = "%" + std::string("0") + std::to_string(numLen) + "d";
  const std::string newFormat = prefix + formatSpecifier + extension;

  int fileNumber = -1;
  try {
    fileNumber = std::stoi(basename.substr(numStart, numLen));
  } catch (...) {
    fileNumber = -1;
  }

  return std::make_pair(newFormat, fileNumber);
}

inline void ApplySelectedSnapshotPath(FileNavigationRuntimeState& rt, const char* fullPath)
{
  if (!fullPath || fullPath[0] == '\0') {
    return;
  }

  CopySnapshotCString(rt.input.filePath, sizeof(rt.input.filePath), fullPath);

  std::filesystem::path p(rt.input.filePath);
  std::string folder = p.parent_path().string();
  if (!folder.empty()) {
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (folder.back() != sep) {
      folder.push_back(sep);
    }
    CopySnapshotCString(rt.input.folderPath, sizeof(rt.input.folderPath), folder.c_str());
  }

  const std::string filename = p.filename().string();
  const auto parsed = ConvertFilenameToFormatAndExtractNumber(filename);
  CopySnapshotCString(rt.input.fileFormat, sizeof(rt.input.fileFormat), parsed.first.c_str());

  rt.navigation.initialIndex = parsed.second;
  RecomputeCurrentFileIndex(rt);

#ifdef HAVE_HDF5
  const std::string ext = p.extension().string();
  rt.input.useHDF5 = (ext == ".hdf5" || ext == ".h5");
#endif
}
