#include "FileIO/snapshot_loader.h"

#include "FileIO/hdf5_reader.h"
#include "FileIO/binary_reader.h"
#include "FileIO/gadget_binary_reader.h"
#include "core/PerfTimer.h"
#include "app/state/input_filter_config.h"
#include "data/header_info.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iterator>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>

namespace {

  void applyInputDensityInterpretation(const HeaderInfo& header,
                                       SimulationBlock& outBlock)
  {
    if (header.flag_hdf5) {
      return;
    }

    const double factor =
      InputDensityToInternalNHFactor(header.input_density_unit,
                                     header.UnitMass_in_g,
                                     header.UnitLength_in_cm,
                                     header.HubbleParam,
                                     header.time,
                                     header.flag_comoving);
    if (!std::isfinite(factor) || factor <= 0.0 || factor == 1.0) {
      return;
    }

    for (auto& p : outBlock.particles) {
      p.density = static_cast<float>(static_cast<double>(p.density) * factor);
    }
  }

  void applyInputTemperatureInterpretation(const HeaderInfo& header,
                                           SimulationBlock& outBlock)
  {
    if (header.flag_hdf5) {
      return;
    }

    const double factor =
      InputInternalEnergyToTemperatureFactor(header.input_temperature_unit,
                                             header.UnitVelocity_in_cm_per_s);
    if (!std::isfinite(factor) || factor <= 0.0 || factor == 1.0) {
      return;
    }

    for (auto& p : outBlock.particles) {
      p.temperature =
        static_cast<float>(static_cast<double>(p.temperature) * factor);
    }
  }

  void applyInputMagneticFieldInterpretation(const HeaderInfo& header,
                                             SimulationBlock& outBlock)
  {
    if (header.flag_hdf5) {
      return;
    }

    const double factor =
      InputMagneticFieldToGaussFactor(header.input_magnetic_field_unit,
                                      header.UnitMass_in_g,
                                      header.UnitLength_in_cm,
                                      header.UnitVelocity_in_cm_per_s,
                                      header.HubbleParam,
                                      header.time,
                                      header.flag_comoving);
    if (!std::isfinite(factor) || factor <= 0.0 || factor == 1.0) {
      return;
    }

    auto it = outBlock.soa.find(kBfieldKey);
    if (it == outBlock.soa.end()) {
      return;
    }
    SoAField& f = it->second;
    const size_t nvalues = outBlock.particles.size() * static_cast<size_t>(f.comps);
    if (f.type == DataType::Float) {
      float* values = reinterpret_cast<float*>(f.bytes.data());
      const float fac = static_cast<float>(factor);
      for (size_t i = 0; i < nvalues; ++i) {
        values[i] *= fac;
      }
    } else if (f.type == DataType::Double) {
      double* values = reinterpret_cast<double*>(f.bytes.data());
      for (size_t i = 0; i < nvalues; ++i) {
        values[i] *= factor;
      }
    }
  }

  void finalizeQuantityStorageMetadata(const HeaderInfo& header,
                                       SimulationBlock& outBlock)
  {
    outBlock.quantityStorage.position = StoredQuantityUnit::CodeUnit;
    outBlock.quantityStorage.velocity = StoredQuantityUnit::CodeUnit;
    outBlock.quantityStorage.mass = StoredQuantityUnit::CodeUnit;
    outBlock.quantityStorage.density = StoredQuantityUnit::InternalStandard;
    outBlock.quantityStorage.temperature = StoredQuantityUnit::InternalStandard;
    outBlock.quantityStorage.magneticField = StoredQuantityUnit::InternalStandard;
    outBlock.quantityStorage.inputDensityUnit = header.input_density_unit;
    outBlock.quantityStorage.inputTemperatureUnit = header.input_temperature_unit;
    outBlock.quantityStorage.inputMagneticFieldUnit =
      header.input_magnetic_field_unit;
    outBlock.quantityStorage.inputComoving = header.flag_comoving;
    outBlock.quantityStorage.densityToInternalFactor =
      InputDensityToInternalNHFactor(header.input_density_unit,
                                     header.UnitMass_in_g,
                                     header.UnitLength_in_cm,
                                     header.HubbleParam,
                                     header.time,
                                     header.flag_comoving);
    outBlock.quantityStorage.temperatureToInternalFactor =
      InputInternalEnergyToTemperatureFactor(header.input_temperature_unit,
                                             header.UnitVelocity_in_cm_per_s);
    outBlock.quantityStorage.magneticFieldToInternalFactor =
      InputMagneticFieldToGaussFactor(header.input_magnetic_field_unit,
                                      header.UnitMass_in_g,
                                      header.UnitLength_in_cm,
                                      header.UnitVelocity_in_cm_per_s,
                                      header.HubbleParam,
                                      header.time,
                                      header.flag_comoving);
  }
  using SnapshotLoadClock = std::chrono::steady_clock;

  struct ReaderSelection {
    std::unique_ptr<IElementReader> reader;
    std::vector<FieldSpec> format;
    std::string fullPath;
  };

  struct SplitPartReadResult {
    bool ok = false;
    size_t partIndex = 0;
    std::string path;
    std::string errorMessage;
    HeaderInfo header{};
    SimulationBlock block{};
  };

  double elapsedMs(SnapshotLoadClock::time_point start)
  {
    return std::chrono::duration<double, std::milli>(
             SnapshotLoadClock::now() - start)
      .count();
  }

  std::string formatSnapshotFileNameWithRepeatedIndex(const char* pattern, int fileNumber)
  {
    char fileName[512];
    // Extra printf arguments are ignored. Passing the snapshot index several
    // times lets patterns such as "snapdir_%03d/snap_%03d.0.hdf5" work.
    std::snprintf(fileName,
                  sizeof(fileName),
                  pattern,
                  fileNumber,
                  fileNumber,
                  fileNumber,
                  fileNumber);
    return std::string(fileName);
  }

  int countPrintfIntegerConversions(const char* pattern)
  {
    int count = 0;
    for (const char* p = pattern; *p; ++p) {
      if (*p != '%') continue;
      ++p;
      if (*p == '%') continue;
      while (*p && std::strchr("-+ #0", *p)) ++p;
      while (*p && std::isdigit(static_cast<unsigned char>(*p))) ++p;
      if (*p == '.') {
        ++p;
        while (*p && std::isdigit(static_cast<unsigned char>(*p))) ++p;
      }
      while (*p && std::strchr("hljztL", *p)) ++p;
      if (*p == 'd' || *p == 'i' || *p == 'u') {
        ++count;
      }
      if (!*p) break;
    }
    return count;
  }

  std::string formatSnapshotFileName(const SnapshotLoadParams& params, int fileNumber)
  {
    const int conversions = countPrintfIntegerConversions(params.fileFormat);
    if (conversions != 1) {
      return formatSnapshotFileNameWithRepeatedIndex(params.fileFormat, fileNumber);
    }

    const std::string snapshotName =
      formatSnapshotFileNameWithRepeatedIndex(params.fileFormat, fileNumber);
    const std::string part0Name =
      formatSnapshotFileNameWithRepeatedIndex(params.fileFormat, 0);

    const std::filesystem::path snapshotPath =
      std::filesystem::path(params.folderPath) / snapshotName;
    const std::filesystem::path part0Path =
      std::filesystem::path(params.folderPath) / part0Name;

    // If the requested index does not exist but part 0 does, this is most
    // likely a fixed snapshot directory with a part-number pattern such as
    // "snap_000.%01d.hdf5". Use part 0 as the anchor for split discovery.
    if (!std::filesystem::exists(snapshotPath) &&
        std::filesystem::exists(part0Path)) {
      return part0Name;
    }

    return snapshotName;
  }

  bool hasHdf5Extension(const std::filesystem::path& path)
  {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".h5" || ext == ".hdf5";
  }

  std::vector<std::string> discoverSplitHdf5Parts(const std::string& part0Path)
  {
    namespace fs = std::filesystem;

    const fs::path selected(part0Path);
    if (!hasHdf5Extension(selected)) {
      return {part0Path};
    }

    const fs::path parent = selected.parent_path();
    const std::string filename = selected.filename().string();
    const std::string suffix = selected.extension().string();
    const size_t suffixPos = filename.size() - suffix.size();
    const size_t partDot = filename.rfind('.', suffixPos > 0 ? suffixPos - 1 : 0);
    if (partDot == std::string::npos || partDot >= suffixPos) {
      return {part0Path};
    }

    const std::string partToken = filename.substr(partDot + 1, suffixPos - partDot - 1);
    if (partToken.empty() ||
        !std::all_of(partToken.begin(), partToken.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
      return {part0Path};
    }

    const std::string prefix = filename.substr(0, partDot + 1);
    std::map<int, std::string> parts;
    try {
      for (const auto& entry : fs::directory_iterator(parent)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.size() <= prefix.size() + suffix.size()) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;

        const std::string token =
          name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        if (token.empty() ||
            !std::all_of(token.begin(), token.end(),
                         [](unsigned char c) { return std::isdigit(c) != 0; })) {
          continue;
        }
        parts[std::stoi(token)] = entry.path().string();
      }
    } catch (const fs::filesystem_error&) {
      return {part0Path};
    }

    if (parts.size() <= 1 || parts.begin()->first != 0) {
      return {part0Path};
    }

    std::vector<std::string> out;
    out.reserve(parts.size());
    int expected = 0;
    for (const auto& [partIndex, path] : parts) {
      if (partIndex != expected) {
        std::cerr << "Split HDF5 snapshot is missing part " << expected
                  << " near " << part0Path << "\n";
        return {};
      }
      out.push_back(path);
      ++expected;
    }
    return out;
  }

  ReaderSelection makeReaderSelectionForPath(const SnapshotLoadParams& params,
                                             std::string fullPath) {
    ReaderSelection sel;
    sel.fullPath = std::move(fullPath);

    std::string ext;
    auto pos = sel.fullPath.find_last_of('.');
    if (pos != std::string::npos) {
      ext = sel.fullPath.substr(pos);
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    switch (params.readFormat) {
    case FileFormat::Auto:
#ifdef HAVE_HDF5
      if (ext == ".h5" || ext == ".hdf5") {
        sel.reader = std::make_unique<HDF5Reader>();
        sel.format = params.formatTokensHdf5;
        break;
      }
#endif
#ifdef USE_MMAP
      sel.reader = std::make_unique<MMapReader>();
#else
      sel.reader = std::make_unique<BinaryReader>();
#endif
      sel.format = params.formatTokens;
      break;

#ifdef HAVE_HDF5
    case FileFormat::HDF5:
      sel.reader = std::make_unique<HDF5Reader>();
      sel.format = params.formatTokensHdf5;
      break;
#endif

    case FileFormat::Binary:
#ifdef USE_MMAP
      sel.reader = std::make_unique<MMapReader>();
#else
      sel.reader = std::make_unique<BinaryReader>();
#endif
      sel.format = params.formatTokens;
      break;

    case FileFormat::Gadget:
      sel.reader = std::make_unique<GadgetBinaryReader>();
      sel.format = params.formatTokensGadget;
      break;

    case FileFormat::Framed:
      break;

    default:
      break;
    }

    return sel;
  }

  ReaderSelection makeReaderSelection(const SnapshotLoadParams& params, int fileNumber) {
    const std::string fileName = formatSnapshotFileName(params, fileNumber);
    return makeReaderSelectionForPath(params, std::string(params.folderPath) + fileName);
  }

  bool readSelectionIntoBlock(ReaderSelection& sel,
                              const SnapshotLoadParams& params,
                              HeaderInfo& header,
                              SimulationBlock& outBlock,
                              const InputFilterConfig& filter,
                              std::string* errorMessage = nullptr)
  {
    header.UnitLength_in_cm         = params.units.length_cm;
    header.UnitMass_in_g            = params.units.mass_g;
    header.UnitVelocity_in_cm_per_s = params.units.velocity_cm_per_s;
    header.HubbleParam              = params.units.hubble;
    header.flag_comoving            = params.units.useComovingCoordinate;
    header.input_density_unit       = params.inputDensityUnit;
    header.input_temperature_unit   = params.inputTemperatureUnit;
    header.input_magnetic_field_unit = params.inputMagneticFieldUnit;

    if (!sel.reader) {
      if (errorMessage) {
        *errorMessage = "No snapshot reader is available for this file format";
      }
      return false;
    }

    if (!sel.reader->tryFixAndCheckBinary(sel.fullPath, header, sel.format)) {
      std::cerr << "the format is incorrect\n";
      if (errorMessage) {
        const std::string reason = sel.reader->lastError();
        *errorMessage = "Binary format check failed: " +
                        (reason.empty() ? std::string("field layout does not match the file")
                                        : reason);
      }
      return false;
    }

    if (!sel.reader->open(sel.fullPath, header)) {
      std::cerr << "failed to open the file: " << sel.fullPath << "\n";
      const std::string reason = sel.reader->lastError();
      if (!reason.empty()) {
        std::cerr << "  reason: " << reason << "\n";
      }
      if (errorMessage) {
        *errorMessage = "Failed to open snapshot: " +
                        (reason.empty() ? sel.fullPath : reason);
      }
      return false;
    }

    bool ok = false;
    if (filter.enabled) {
      ParticleMask pmask{filter.mask};
      ok = sel.reader->readRange(outBlock, 0, sel.reader->elementCount(),
                                 sel.format, &pmask);
    } else {
      ok = sel.reader->readAll(outBlock, sel.format);
    }

    const std::string readError = ok ? std::string{} : sel.reader->lastError();
    sel.reader->close();
    if (ok) {
      applyInputDensityInterpretation(header, outBlock);
      applyInputTemperatureInterpretation(header, outBlock);
      applyInputMagneticFieldInterpretation(header, outBlock);
      finalizeQuantityStorageMetadata(header, outBlock);
    }
    if (!ok) {
      std::cerr << "Failed to read particle data: " << sel.fullPath << "\n";
      if (!readError.empty()) {
        std::cerr << "  reason: " << readError << "\n";
      }
      if (errorMessage) {
        *errorMessage = "Failed to read particle data: " +
                        (readError.empty() ? sel.fullPath : readError);
      }
    }
    return ok;
  }

  bool appendBlock(SimulationBlock& dst, SimulationBlock&& src)
  {
    const size_t base = dst.particles.size();
    const size_t nsrc = src.particles.size();

    dst.particles.insert(dst.particles.end(),
                         std::make_move_iterator(src.particles.begin()),
                         std::make_move_iterator(src.particles.end()));
    dst.loadedFieldNames.insert(src.loadedFieldNames.begin(),
                                src.loadedFieldNames.end());
    for (const auto& kv : src.loadedFieldTypeMask) {
      dst.loadedFieldTypeMask[kv.first] |= kv.second;
    }

    if (src.aosExt.stride > 0) {
      if (dst.aosExt.stride == 0) {
        dst.aosExt.stride = src.aosExt.stride;
        dst.aosExt.bytes.resize(base * dst.aosExt.stride);
      }
      if (dst.aosExt.stride != src.aosExt.stride) {
        std::cerr << "Cannot merge split snapshot: AoS extension stride mismatch\n";
        return false;
      }
      dst.aosExt.bytes.insert(dst.aosExt.bytes.end(),
                              src.aosExt.bytes.begin(),
                              src.aosExt.bytes.end());
    } else if (dst.aosExt.stride > 0) {
      dst.aosExt.bytes.resize(dst.aosExt.bytes.size() + nsrc * dst.aosExt.stride);
    }

    std::unordered_set<std::string> srcKeys;
    srcKeys.reserve(src.soa.size());
    for (const auto& kv : src.soa) {
      srcKeys.insert(kv.first);
    }

    for (auto& kv : dst.soa) {
      if (srcKeys.find(kv.first) == srcKeys.end()) {
        kv.second.bytes.resize(kv.second.bytes.size() +
                               nsrc * static_cast<size_t>(kv.second.comps) *
                                 dataTypeSize(kv.second.type));
      }
    }

    for (auto& kv : src.soa) {
      const std::string& key = kv.first;
      SoAField& srcField = kv.second;
      const size_t elemBytes =
        static_cast<size_t>(srcField.comps) * dataTypeSize(srcField.type);
      if (srcField.bytes.size() != nsrc * elemBytes) {
        std::cerr << "Cannot merge split snapshot: malformed SoA field " << key << "\n";
        return false;
      }

      auto [it, inserted] = dst.soa.try_emplace(key);
      SoAField& dstField = it->second;
      if (inserted) {
        dstField.type = srcField.type;
        dstField.comps = srcField.comps;
        dstField.bytes.resize(base * elemBytes);
      } else if (dstField.type != srcField.type ||
                 dstField.comps != srcField.comps) {
        std::cerr << "Cannot merge split snapshot: SoA field mismatch for " << key << "\n";
        return false;
      }

      dstField.bytes.insert(dstField.bytes.end(),
                            srcField.bytes.begin(),
                            srcField.bytes.end());
    }

    dst.id2index.clear();
    dst.id2indexDirty = true;
    return true;
  }

  void updateHeaderCountsFromBlock(HeaderInfo& header, const SimulationBlock& block)
  {
    int counts[6] = {};
    for (const SimulationElement& p : block.particles) {
      if (p.type < 6) {
        ++counts[p.type];
      }
    }
    header.npart = static_cast<int>(block.particles.size());
    for (int t = 0; t < 6; ++t) {
      header.NumPart_ThisFile[t] = counts[t];
    }
  }

  bool hdf5LibraryIsThreadSafe()
  {
#ifdef HAVE_HDF5
    static const bool checked = [] {
      hbool_t threadSafe = 0;
      if (H5is_library_threadsafe(&threadSafe) < 0) {
        return false;
      }
      return threadSafe > 0;
    }();
    return checked;
#else
    return false;
#endif
  }

  bool envValueIsTrue(const char* value)
  {
    if (!value) return false;
    std::string v(value);
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v == "1" || v == "on" || v == "true" || v == "yes";
  }

  bool envValueIsFalse(const char* value)
  {
    if (!value) return false;
    std::string v(value);
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v == "0" || v == "off" || v == "false" || v == "no";
  }

  bool shouldReadSplitPartsInParallel()
  {
    const char* env = std::getenv("PARTICLE_VIS_PARALLEL_HDF5_SPLIT");
    if (envValueIsFalse(env)) return false;
    if (envValueIsTrue(env)) return true;
    return hdf5LibraryIsThreadSafe();
  }

  SplitPartReadResult readSplitPart(size_t partIndex,
                                    const std::string& partPath,
                                    const SnapshotLoadParams& params,
                                    const InputFilterConfig& filter)
  {
    SplitPartReadResult result;
    result.partIndex = partIndex;
    result.path = partPath;

    ReaderSelection partSel = makeReaderSelectionForPath(params, partPath);
    result.ok =
      readSelectionIntoBlock(partSel,
                             params,
                             result.header,
                             result.block,
                             filter,
                             &result.errorMessage);
    return result;
  }
}

bool SnapshotLoader::readFile(int fileNumber,
                              const SnapshotLoadParams& params,
                              SnapshotReadResult& outResult,
                              const InputFilterConfig& filter) {
  TIME_FUNCTION();

  HeaderInfo& header = outResult.header;
  SimulationBlock& outBlock = outResult.block;
  header = HeaderInfo{};

  header.UnitLength_in_cm         = params.units.length_cm;
  header.UnitMass_in_g            = params.units.mass_g;
  header.UnitVelocity_in_cm_per_s = params.units.velocity_cm_per_s;
  header.HubbleParam              = params.units.hubble;
  header.flag_comoving            = params.units.useComovingCoordinate;
  header.input_density_unit       = params.inputDensityUnit;
  header.input_temperature_unit   = params.inputTemperatureUnit;
  header.input_magnetic_field_unit = params.inputMagneticFieldUnit;

  ReaderSelection sel = makeReaderSelection(params, fileNumber);
  if (!sel.reader) {
    std::cerr << "Failed to select reader for file #" << fileNumber << "\n";
    outResult.errorMessage =
      "Failed to select snapshot reader for file #" + std::to_string(fileNumber);
    return false;
  }

  const std::vector<std::string> splitParts = discoverSplitHdf5Parts(sel.fullPath);
  if (splitParts.empty()) {
    outResult.errorMessage = "Split HDF5 snapshot has missing parts near " + sel.fullPath;
    return false;
  }

  if (splitParts.size() > 1) {
    const bool parallelSplitRead = shouldReadSplitPartsInParallel();
    std::fprintf(stderr,
                 "[SnapshotLoader] split snapshot fileIndex=%d parts=%zu mode=%s "
                 "hdf5ThreadSafe=%s first=%s\n",
                 fileNumber,
                 splitParts.size(),
                 parallelSplitRead ? "parallel" : "serial",
                 hdf5LibraryIsThreadSafe() ? "yes" : "no",
                 splitParts.front().c_str());

    TIME_SCOPE("read split snapshot parts");
    const auto splitReadStart = SnapshotLoadClock::now();
    SimulationBlock merged;
    HeaderInfo mergedHeader = header;

    auto mergePart = [&](SplitPartReadResult&& part) {
      if (!part.ok) {
        outResult.errorMessage =
          part.errorMessage.empty()
            ? ("Failed to read split snapshot part: " + part.path)
            : part.errorMessage;
        return false;
      }
      if (part.partIndex == 0) {
        mergedHeader = part.header;
        merged = std::move(part.block);
        return true;
      }
      return appendBlock(merged, std::move(part.block));
    };

    if (parallelSplitRead) {
      std::vector<std::future<SplitPartReadResult>> futures;
      futures.reserve(splitParts.size());
      for (size_t partNumber = 0; partNumber < splitParts.size(); ++partNumber) {
        std::fprintf(stderr,
                     "[SnapshotLoader] split part %zu/%zu path=%s\n",
                     partNumber + 1,
                     splitParts.size(),
                     splitParts[partNumber].c_str());
        futures.push_back(std::async(std::launch::async,
                                     readSplitPart,
                                     partNumber,
                                     splitParts[partNumber],
                                     params,
                                     filter));
      }

      std::vector<SplitPartReadResult> parts(splitParts.size());
      for (auto& future : futures) {
        SplitPartReadResult part;
        try {
          part = future.get();
        } catch (const std::exception& e) {
          std::cerr << "Split HDF5 parallel read failed: " << e.what() << "\n";
          outResult.errorMessage =
            std::string("Split HDF5 parallel read failed: ") + e.what();
          return false;
        } catch (...) {
          std::cerr << "Split HDF5 parallel read failed with an unknown exception\n";
          outResult.errorMessage =
            "Split HDF5 parallel read failed with an unknown exception";
          return false;
        }
        if (part.partIndex >= parts.size()) {
          std::cerr << "Split HDF5 parallel read returned an invalid part index\n";
          outResult.errorMessage =
            "Split HDF5 parallel read returned an invalid part index";
          return false;
        }
        parts[part.partIndex] = std::move(part);
      }

      for (auto& part : parts) {
        if (!mergePart(std::move(part))) {
          return false;
        }
      }
    } else {
      size_t partNumber = 0;
      for (const std::string& partPath : splitParts) {
        std::fprintf(stderr,
                     "[SnapshotLoader] split part %zu/%zu path=%s\n",
                     partNumber + 1,
                     splitParts.size(),
                     partPath.c_str());
        SplitPartReadResult part;
        {
          TIME_SCOPE("read particle data");
          part = readSplitPart(partNumber, partPath, params, filter);
        }
        if (!mergePart(std::move(part))) {
          return false;
        }
        ++partNumber;
      }
    }

    updateHeaderCountsFromBlock(mergedHeader, merged);
    header = mergedHeader;
    outBlock = std::move(merged);
    outResult.fileIndex = fileNumber;
    std::fprintf(stderr,
                 "[SnapshotLoader] split snapshot merged parts=%zu particles=%zu "
                 "elapsed=%.3f ms\n",
                 splitParts.size(),
                 outBlock.particles.size(),
                 elapsedMs(splitReadStart));
    return true;
  }

  std::fprintf(stderr,
               "[SnapshotLoader] fileIndex=%d path=%s formatFields=%zu\n",
               fileNumber,
               sel.fullPath.c_str(),
               sel.format.size());

  bool ok = false;
  {
    TIME_SCOPE("read particle data");
    ok = readSelectionIntoBlock(sel,
                                params,
                                header,
                                outBlock,
                                filter,
                                &outResult.errorMessage);
  }

  if (!ok) {
    std::cerr << "Failed to read particle data: " << sel.fullPath << "\n";
    if (outResult.errorMessage.empty()) {
      outResult.errorMessage = "Failed to read particle data: " + sel.fullPath;
    }
    return false;
  }

  outResult.fileIndex = fileNumber;
  return true;
}

std::vector<int64_t> SnapshotLoader::getStarParticleID(int indexFile,
                                                       const SnapshotLoadParams& params,
                                                       const InputFilterConfig& filter) {
  SnapshotReadResult result;
  if (!readFile(indexFile, params, result, filter)) {
    return {};
  }

  std::vector<int64_t> IDs;
  for (size_t i = 0; i < result.block.particles.size(); ++i) {
    const auto& p = result.block.particles[i];
    if (p.type < 3)
      continue;
    IDs.push_back(result.block.particleIdSigned(i));
  }

  return IDs;
}

void SnapshotLoader::generateTestData(SimulationDataset* P,
                                      HeaderInfo& header,
                                      NormalizationContext& normalization,
                                      QuantityState& quantity) {
  SimulationBlock block = SimulationBlock::makeTestSimulationBlock(header);
  P->setSimulationBlock(std::move(block), nullptr, header, normalization, quantity);
}
