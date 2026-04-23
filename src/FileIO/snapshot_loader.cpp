#include "FileIO/snapshot_loader.h"

#include "FileIO/hdf5_reader.h"
#include "FileIO/binary_reader.h"
#include "core/PerfTimer.h"
#include "app/input_filter_config.h"
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

  ReaderSelection makeReaderSelection(const SnapshotSource& source, int fileNumber) {
    ReaderSelection sel;

    char fileName[512];
    std::snprintf(fileName, sizeof(fileName), source.fileFormat, fileNumber);
    sel.fullPath = std::string(source.folderPath) + fileName;

    std::string ext;
    auto pos = sel.fullPath.find_last_of('.');
    if (pos != std::string::npos) {
      ext = sel.fullPath.substr(pos);
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    switch (source.getFormatMode()) {
    case FileFormat::Auto:
#ifdef HAVE_HDF5
      if (ext == ".h5" || ext == ".hdf5") {
        sel.reader = std::make_unique<HDF5Reader>();
        sel.format = source.formatTokens_hdf5;
        break;
      }
#endif
#ifdef USE_MMAP
      sel.reader = std::make_unique<MMapReader>();
#else
      sel.reader = std::make_unique<BinaryReader>();
#endif
      sel.format = source.formatTokens;
      break;

#ifdef HAVE_HDF5
    case FileFormat::HDF5:
      sel.reader = std::make_unique<HDF5Reader>();
      sel.format = source.formatTokens_hdf5;
      break;
#endif

    case FileFormat::Binary:
#ifdef USE_MMAP
      sel.reader = std::make_unique<MMapReader>();
#else
      sel.reader = std::make_unique<BinaryReader>();
#endif
      sel.format = source.formatTokens;
      break;

    case FileFormat::Gadget:
      break;

    case FileFormat::Framed:
      break;

    default:
      break;
    }

    return sel;
  }
}

SnapshotLoader::SnapshotLoader(SnapshotSource& source)
  : source_(source) {}

bool SnapshotLoader::loadSingleFile(int fileNumber, ParticleBlock& outBlock, HeaderInfo& header, const InputFilterConfig& filter) {
  TIME_FUNCTION();

  header.UnitLength_in_cm         = source_.units.length_cm;
  header.UnitMass_in_g            = source_.units.mass_g;
  header.UnitVelocity_in_cm_per_s = source_.units.velocity_cm_per_s;
  header.HubbleParam              = source_.units.hubble;

  ReaderSelection sel = makeReaderSelection(source_, fileNumber);
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

  return true;
}

bool SnapshotLoader::loadFirstFileIntoArray(int targetFile, ParticleArray* P, HeaderInfo& header, NormalizationContext& normalization, const InputFilterConfig& filter) {
  ParticleBlock newBlock;
  if (!loadSingleFile(targetFile, newBlock, header, filter)) {
    P->particleBlock.clear();
    std::cerr << "Failed to load first file: " << targetFile << std::endl;
    return false;
  }

  ParticleBlock oldBlock;
  P->setParticleBlock(std::move(newBlock), &oldBlock, header, normalization);
  return true;
}

TrackingVector<int> SnapshotLoader::getStarParticleID(int indexFile, const InputFilterConfig& filter) {
  ParticleBlock p_block;
  HeaderInfo header;
  loadSingleFile(indexFile, p_block, header, filter);

  TrackingVector<int> IDs;
  for (auto& p : p_block.particles) {
    if (p.type < 3)
      continue;
    IDs.push_back(p.ID);
  }

  return IDs;
}

void SnapshotLoader::generateTestData(ParticleArray* P, HeaderInfo& header, NormalizationContext& normalization) {
  ParticleBlock block = ParticleBlock::makeTestParticleBlock(header);
  P->setParticleBlock(std::move(block), nullptr, header, normalization);
}
