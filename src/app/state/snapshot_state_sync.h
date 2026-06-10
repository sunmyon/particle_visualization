#pragma once

#include "app/state/runtime_state.h"

#include <algorithm>
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
  RecomputeCurrentFileIndex(rt);
  const int fileIndex = rt.navigation.currentFileIndex;
  char fileNameOnly[255];
  std::snprintf(fileNameOnly,
                sizeof(fileNameOnly),
                rt.input.fileFormat,
                fileIndex,
                fileIndex,
                fileIndex,
                fileIndex);
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

inline bool FindLastDigitRunBefore(const std::string& text,
                                   size_t before,
                                   size_t& start,
                                   size_t& len)
{
  if (before == 0 || before > text.size()) {
    return false;
  }
  size_t pos = before;
  while (pos > 0 && !std::isdigit(static_cast<unsigned char>(text[pos - 1]))) {
    --pos;
  }
  if (pos == 0) {
    return false;
  }
  const size_t end = pos;
  while (pos > 0 && std::isdigit(static_cast<unsigned char>(text[pos - 1]))) {
    --pos;
  }
  start = pos;
  len = end - pos;
  return len > 0;
}

inline std::string ReplaceDigitRunWithFormat(const std::string& text,
                                             size_t start,
                                             size_t len)
{
  return text.substr(0, start) +
         "%0" + std::to_string(len) + "d" +
         text.substr(start + len);
}

inline bool TryApplySplitSnapshotPath(FileNavigationRuntimeState& rt,
                                      const std::filesystem::path& selectedPath)
{
  const std::filesystem::path parent = selectedPath.parent_path();
  const std::filesystem::path root = parent.parent_path();
  if (parent.empty() || root.empty()) {
    return false;
  }

  std::string ext = selectedPath.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (ext != ".hdf5" && ext != ".h5") {
    return false;
  }

  const std::string stem = selectedPath.stem().string(); // e.g. snap_000.0
  const size_t partDot = stem.find_last_of('.');
  if (partDot == std::string::npos || partDot + 1 >= stem.size()) {
    return false;
  }
  const std::string partToken = stem.substr(partDot + 1);
  if (!std::all_of(partToken.begin(), partToken.end(),
                   [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return false;
  }

  const std::string snapshotStem = stem.substr(0, partDot); // e.g. snap_000
  size_t fileNumStart = 0;
  size_t fileNumLen = 0;
  if (!FindLastDigitRunBefore(snapshotStem, snapshotStem.size(), fileNumStart, fileNumLen)) {
    return false;
  }

  int snapshotIndex = -1;
  try {
    snapshotIndex = std::stoi(snapshotStem.substr(fileNumStart, fileNumLen));
  } catch (...) {
    return false;
  }

  const std::string parentName = parent.filename().string(); // e.g. snapdir_000
  size_t dirNumStart = 0;
  size_t dirNumLen = 0;
  if (!FindLastDigitRunBefore(parentName, parentName.size(), dirNumStart, dirNumLen)) {
    return false;
  }

  int dirIndex = -1;
  try {
    dirIndex = std::stoi(parentName.substr(dirNumStart, dirNumLen));
  } catch (...) {
    return false;
  }
  if (dirIndex != snapshotIndex) {
    return false;
  }

  std::string folder = root.string();
#ifdef _WIN32
  const char sep = '\\';
#else
  const char sep = '/';
#endif
  if (!folder.empty() && folder.back() != sep) {
    folder.push_back(sep);
  }

  const std::string dirFormat =
    ReplaceDigitRunWithFormat(parentName, dirNumStart, dirNumLen);
  const std::string fileFormat =
    ReplaceDigitRunWithFormat(snapshotStem, fileNumStart, fileNumLen) +
    ".0" + selectedPath.extension().string();
  const std::string combinedFormat = dirFormat + "/" + fileFormat;

  CopySnapshotCString(rt.input.folderPath, sizeof(rt.input.folderPath), folder.c_str());
  CopySnapshotCString(rt.input.fileFormat, sizeof(rt.input.fileFormat), combinedFormat.c_str());
  rt.navigation.initialIndex = snapshotIndex;
  return true;
}

inline bool TryApplySplitHdf5PartFilePath(FileNavigationRuntimeState& rt,
                                          const std::filesystem::path& selectedPath)
{
  std::string ext = selectedPath.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (ext != ".hdf5" && ext != ".h5") {
    return false;
  }

  const std::string stem = selectedPath.stem().string();
  const size_t partDot = stem.find_last_of('.');
  if (partDot == std::string::npos || partDot + 1 >= stem.size()) {
    return false;
  }

  const std::string partToken = stem.substr(partDot + 1);
  if (!std::all_of(partToken.begin(), partToken.end(),
                   [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return false;
  }

  const std::string snapshotStem = stem.substr(0, partDot);
  size_t fileNumStart = 0;
  size_t fileNumLen = 0;
  if (!FindLastDigitRunBefore(snapshotStem,
                              snapshotStem.size(),
                              fileNumStart,
                              fileNumLen)) {
    return false;
  }

  int snapshotIndex = -1;
  try {
    snapshotIndex = std::stoi(snapshotStem.substr(fileNumStart, fileNumLen));
  } catch (...) {
    return false;
  }

  std::string folder = selectedPath.parent_path().string();
#ifdef _WIN32
  const char sep = '\\';
#else
  const char sep = '/';
#endif
  if (!folder.empty() && folder.back() != sep) {
    folder.push_back(sep);
  }

  const std::string fileFormat =
    ReplaceDigitRunWithFormat(snapshotStem, fileNumStart, fileNumLen) +
    "." + partToken + selectedPath.extension().string();

  CopySnapshotCString(rt.input.folderPath, sizeof(rt.input.folderPath), folder.c_str());
  CopySnapshotCString(rt.input.fileFormat, sizeof(rt.input.fileFormat), fileFormat.c_str());
  rt.navigation.initialIndex = snapshotIndex;
  return true;
}

inline void ApplySelectedSnapshotPath(FileNavigationRuntimeState& rt, const char* fullPath)
{
  if (!fullPath || fullPath[0] == '\0') {
    return;
  }

  CopySnapshotCString(rt.input.filePath, sizeof(rt.input.filePath), fullPath);

  std::filesystem::path p(rt.input.filePath);
  const bool splitSnapshotPathApplied = TryApplySplitSnapshotPath(rt, p);
  if (splitSnapshotPathApplied) {
    RecomputeCurrentFileIndex(rt);
#ifdef HAVE_HDF5
    rt.input.useHDF5 = true;
#endif
    RefreshSnapshotFilePath(rt);
    return;
  }

  const bool splitHdf5PartPathApplied = TryApplySplitHdf5PartFilePath(rt, p);
  if (splitHdf5PartPathApplied) {
    RecomputeCurrentFileIndex(rt);
#ifdef HAVE_HDF5
    rt.input.useHDF5 = true;
#endif
    RefreshSnapshotFilePath(rt);
    return;
  }

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
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  rt.input.useHDF5 = (ext == ".hdf5" || ext == ".h5");
#endif
}
