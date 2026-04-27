#include "FileIO/snapshot_loader.h"

#include "FileIO/hdf5_reader.h"
#include "FileIO/binary_reader.h"
#include "FileIO/gadget_binary_reader.h"
#include "core/PerfTimer.h"
#include "app/state/input_filter_config.h"
#include "data/header_info.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>

namespace {
  struct ReaderSelection {
    std::unique_ptr<IParticleReader> reader;
    std::vector<FieldSpec> format;
    std::string fullPath;
  };

  ReaderSelection makeReaderSelection(const SnapshotLoadParams& params, int fileNumber) {
    ReaderSelection sel;

    char fileName[512];
    std::snprintf(fileName, sizeof(fileName), params.fileFormat, fileNumber);
    sel.fullPath = std::string(params.folderPath) + fileName;

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
      sel.format = params.formatTokens;
      break;

    case FileFormat::Framed:
      break;

    default:
      break;
    }

    return sel;
  }
}

bool SnapshotLoader::readFile(int fileNumber,
                              const SnapshotLoadParams& params,
                              SnapshotReadResult& outResult,
                              const InputFilterConfig& filter) {
  TIME_FUNCTION();

  HeaderInfo& header = outResult.header;
  ParticleBlock& outBlock = outResult.block;
  header = HeaderInfo{};

  header.UnitLength_in_cm         = params.units.length_cm;
  header.UnitMass_in_g            = params.units.mass_g;
  header.UnitVelocity_in_cm_per_s = params.units.velocity_cm_per_s;
  header.HubbleParam              = params.units.hubble;

  ReaderSelection sel = makeReaderSelection(params, fileNumber);
  if (!sel.reader) {
    std::cerr << "Failed to select reader for file #" << fileNumber << "\n";
    return false;
  }

  if (!sel.reader->tryFixAndCheckBinary(sel.fullPath, header, sel.format)) {
    std::cerr << "the format is incorrect\n";
    return false;
  }

  if (!sel.reader->open(sel.fullPath, header)) {
    std::cerr << "failed to open the file: " << sel.fullPath << "\n";
    return false;
  }

  bool ok = false;
  {
    TIME_SCOPE("parse header");

    if (filter.enabled) {
      ParticleMask pmask{filter.mask};
      ok = sel.reader->readRange(outBlock, 0, sel.reader->particleCount(),
                                 sel.format, &pmask);
    }else{
      ok = sel.reader->readAll(outBlock, sel.format);
    }
  }

  sel.reader->close();

  if (!ok) {
    std::cerr << "Failed to read particle data: " << sel.fullPath << "\n";
    return false;
  }

  outResult.fileIndex = fileNumber;
  return true;
}

TrackingVector<int> SnapshotLoader::getStarParticleID(int indexFile,
                                                      const SnapshotLoadParams& params,
                                                      const InputFilterConfig& filter) {
  SnapshotReadResult result;
  if (!readFile(indexFile, params, result, filter)) {
    return {};
  }

  TrackingVector<int> IDs;
  for (auto& p : result.block.particles) {
    if (p.type < 3)
      continue;
    IDs.push_back(p.ID);
  }

  return IDs;
}

void SnapshotLoader::generateTestData(ParticleArray* P,
                                      HeaderInfo& header,
                                      NormalizationContext& normalization,
                                      QuantityState& quantity) {
  ParticleBlock block = ParticleBlock::makeTestParticleBlock(header);
  P->setParticleBlock(std::move(block), nullptr, header, normalization, quantity);
}
