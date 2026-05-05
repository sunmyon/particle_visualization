#pragma once

#include "FileIO/element_reader.h"
#include "FileIO/file_mask.h"
#include "FileIO/file_layout.h"
#include "data/header_info.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

class GadgetBinaryReader final : public IElementReader {
public:
  bool open(const std::string& path, HeaderInfo& header) override
  {
    path_ = path;
    lastGadgetError_.clear();
    file_.open(path, std::ios::binary);
    if (!file_) {
      lastGadgetError_ =
        "Gadget Header block offset=0: open failed: " +
        std::string(std::strerror(errno));
      return false;
    }

    int32_t blockSize = 0;
    std::cerr << "[GadgetBinaryReader] Header block reading head dummy "
              << "offset=0" << std::endl;
    if (!readRaw_(blockSize)) {
      lastGadgetError_ =
        "Gadget Header block offset=0: failed to read leading marker";
      return false;
    }
    std::cerr << "[GadgetBinaryReader] Header block head dummy="
              << blockSize << " expected=256" << std::endl;
    if (blockSize != 256) {
      lastGadgetError_ =
        "Gadget Header block offset=0: marker mismatch: got=" +
        std::to_string(blockSize) +
        " expected=256. If this is HDF5, select Auto/HDF5 instead of Gadget.";
      return false;
    }

    std::array<uint8_t, 256> raw{};
    std::cerr << "[GadgetBinaryReader] Header block reading payload bytes=256 "
              << "offset=4" << std::endl;
    file_.read(reinterpret_cast<char*>(raw.data()), raw.size());
    if (!file_) {
      lastGadgetError_ =
        "Gadget Header block offset=4: failed to read 256-byte payload";
      return false;
    }

    int32_t tail = 0;
    std::cerr << "[GadgetBinaryReader] Header block reading tail dummy "
              << "offset=260" << std::endl;
    if (!readRaw_(tail)) {
      lastGadgetError_ =
        "Gadget Header block offset=260: failed to read trailing marker";
      return false;
    }
    std::cerr << "[GadgetBinaryReader] Header block tail dummy="
              << tail << " expected=" << blockSize << std::endl;
    if (tail != blockSize) {
      lastGadgetError_ =
        "Gadget Header block offset=260: marker mismatch: head=" +
        std::to_string(blockSize) + " tail=" + std::to_string(tail);
      return false;
    }

    parseHeader_(raw.data(), header);
    dataOffset_ = static_cast<size_t>(file_.tellg());
    header.flag_hdf5 = false;
    return true;
  }

  std::string lastError() const override { return lastGadgetError_; }

  void close() override
  {
    file_.close();
    path_.clear();
    npart_ = 0;
    dataOffset_ = 0;
    counts_.fill(0);
    massTable_.fill(0.0);
  }

  size_t elementCount() const override { return npart_; }

  bool is_binary() override { return false; }

  bool readRange(SimulationBlock& out,
                 size_t begin,
                 size_t count,
                 const std::vector<FieldSpec>& fields,
                 ParticleMask* mask = nullptr) override
  {
    if (begin + count > npart_) return false;

    SimulationBlock all;
    if (!readAllParticles_(all, fields)) return false;

    const bool masked = (mask != nullptr) && mask->active();
    initSubsetOutput_(out, count, all);

    if (masked) {
      size_t thinCandidates = 0;
      for (size_t i = begin; i < begin + count; ++i) {
        const SimulationElement& p = all.particles[i];
        if (mask->typeEnabled(p.type) && mask->typeThinOK(p.type)) {
          ++thinCandidates;
        }
      }
      mask->prepare(thinCandidates);
    }

    size_t written = 0;
    for (size_t i = begin; i < begin + count; ++i) {
      if (masked) {
        const SimulationElement& p = all.particles[i];
        CoreSample c;
        c.pos[0] = p.position[0];
        c.pos[1] = p.position[1];
        c.pos[2] = p.position[2];
        c.id = all.particleId(i);
        c.type = p.type;
        if (!mask->pass(c)) continue;
      }

      copyRecord_(out, written, all, i);
      ++written;
    }

    out.resize(written);
    return true;
  }

private:
  std::ifstream file_;
  std::string path_;
  size_t npart_ = 0;
  size_t dataOffset_ = 0;
  std::array<int32_t, 6> counts_ = {0, 0, 0, 0, 0, 0};
  std::array<double, 6> massTable_ = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::string lastGadgetError_;
  std::string currentBlockTraceLabel_ = "unknown";

  enum class GadgetFieldDomain {
    Absolute,
    All,
    Type0,
    Type0And5
  };

  enum class GadgetBinaryBlockKind {
    Position,
    Velocity,
    ID,
    Mass,
    Field,
    Skip
  };

  struct GadgetBlockField {
    FieldSpec spec;
    GadgetFieldDomain domain = GadgetFieldDomain::Type0;
  };

  struct GadgetSkipBlock {
    GadgetFieldDomain domain = GadgetFieldDomain::All;
    DataType type = DataType::Float;
    int componentsPerElement = 1;
    int blockRepeat = 1;
  };

  struct GadgetBinaryBlockSpec {
    GadgetBinaryBlockKind kind = GadgetBinaryBlockKind::Field;
    GadgetBlockField field;
    GadgetSkipBlock skip;
  };

  struct GadgetBinaryFormat {
    std::vector<GadgetBinaryBlockSpec> blocks;
  };

  enum class GadgetFieldBlockReadResult {
    Ok,
    Mismatch,
    MissingOptionalTail
  };

  template <class T>
  bool readRaw_(T& value)
  {
    file_.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(file_);
  }

  static int32_t readI32_(const uint8_t*& p)
  {
    int32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    p += sizeof(v);
    return v;
  }

  static uint32_t readU32_(const uint8_t*& p)
  {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    p += sizeof(v);
    return v;
  }

  static double readF64_(const uint8_t*& p)
  {
    double v = 0.0;
    std::memcpy(&v, p, sizeof(v));
    p += sizeof(v);
    return v;
  }

  static std::string streamPosToString_(std::streampos pos)
  {
    if (pos == std::streampos(-1)) return "unknown";
    return std::to_string(static_cast<long long>(pos));
  }

  void parseHeader_(const uint8_t* raw, HeaderInfo& header)
  {
    const uint8_t* p = raw;

    npart_ = 0;
    for (int i = 0; i < 6; ++i) {
      counts_[i] = readI32_(p);
      header.NumPart_ThisFile[i] = counts_[i];
      npart_ += static_cast<size_t>(std::max(0, counts_[i]));
    }

    for (int i = 0; i < 6; ++i) {
      massTable_[i] = readF64_(p);
      header.massTable[i] = massTable_[i];
    }

    header.time = readF64_(p);
    header.redshift = readF64_(p);
    header.has_redshift = true;

    p += sizeof(int32_t); // flag_sfr
    p += sizeof(int32_t); // flag_feedback

    uint32_t lowWord[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; ++i) {
      lowWord[i] = readU32_(p);
    }

    p += sizeof(int32_t); // flag_cooling
    p += sizeof(int32_t); // num_files

    header.boxSize = readF64_(p);
    header.Omega0 = readF64_(p);
    header.OmegaLambda = readF64_(p);
    header.HubbleParam = readF64_(p);

    p += sizeof(int32_t); // flag_stellarage
    p += sizeof(int32_t); // flag_metals

    for (int i = 0; i < 6; ++i) {
      (void)readU32_(p);
    }

    header.npart = static_cast<int>(npart_);
  }

  bool readBlock_(std::vector<uint8_t>& bytes)
  {
    const std::streampos headOffset = file_.tellg();
    std::cerr << "[GadgetBinaryReader] " << currentBlockTraceLabel_
              << " reading head dummy offset="
              << streamPosToString_(headOffset) << std::endl;

    int32_t n = 0;
    if (!readRaw_(n)) {
      lastGadgetError_ = "failed to read leading record marker";
      return false;
    }
    std::cerr << "[GadgetBinaryReader] " << currentBlockTraceLabel_
              << " head dummy=" << n << std::endl;
    if (n < 0) {
      lastGadgetError_ = "negative record marker: " + std::to_string(n);
      return false;
    }

    bytes.resize(static_cast<size_t>(n));
    const std::streampos payloadOffset = file_.tellg();
    std::cerr << "[GadgetBinaryReader] " << currentBlockTraceLabel_
              << " reading payload bytes=" << n
              << " offset=" << streamPosToString_(payloadOffset)
              << std::endl;
    file_.read(reinterpret_cast<char*>(bytes.data()), n);
    if (!file_) {
      lastGadgetError_ =
        "failed to read record payload bytes=" + std::to_string(n);
      return false;
    }

    int32_t tail = 0;
    const std::streampos tailOffset = file_.tellg();
    std::cerr << "[GadgetBinaryReader] " << currentBlockTraceLabel_
              << " reading tail dummy offset="
              << streamPosToString_(tailOffset) << std::endl;
    if (!readRaw_(tail)) {
      lastGadgetError_ = "failed to read trailing record marker";
      return false;
    }
    std::cerr << "[GadgetBinaryReader] " << currentBlockTraceLabel_
              << " tail dummy=" << tail
              << " expected=" << n << std::endl;
    if (tail != n) {
      lastGadgetError_ =
        "record marker mismatch head=" + std::to_string(n) +
        " tail=" + std::to_string(tail);
      return false;
    }
    return true;
  }

  void initAllOutput_(SimulationBlock& out, const std::vector<FieldSpec>& fields)
  {
    out.clear();
    out.resize(npart_);

    for (const FieldSpec& spec : fields) {
      const DestKind dest = getDestKind(spec.key);
      if (dest != DestKind::SoA) continue;
      auto& f = out.soa[getSoAKey(spec.key)];
      f.type = spec.type;
      f.comps = spec.count;
      f.resize(npart_);
    }
  }

  static void initSubsetOutput_(SimulationBlock& out,
                                size_t count,
                                const SimulationBlock& source)
  {
    out.clear();
    out.resize(count);
    for (const auto& kv : source.soa) {
      auto& f = out.soa[kv.first];
      f.type = kv.second.type;
      f.comps = kv.second.comps;
      f.resize(count);
    }
  }

  static void copyRecord_(SimulationBlock& dst,
                          size_t dstIndex,
                          const SimulationBlock& src,
                          size_t srcIndex)
  {
    dst.particles[dstIndex] = src.particles[srcIndex];
    for (const auto& kv : src.soa) {
      auto dstIt = dst.soa.find(kv.first);
      if (dstIt == dst.soa.end()) continue;
      const SoAField& srcField = kv.second;
      SoAField& dstField = dstIt->second;
      const size_t bytes =
        static_cast<size_t>(srcField.comps) * dataTypeSize(srcField.type);
      std::memcpy(dstField.ptr(dstIndex), srcField.ptr(srcIndex), bytes);
    }
  }

  bool readAllParticles_(SimulationBlock& out, const std::vector<FieldSpec>& fields)
  {
    lastGadgetError_.clear();
    GadgetBinaryFormat format;
    bool ok = false;
    if (makeGadgetBinaryFormatFromEnv_(fields, format)) {
      ok = readGadgetBinaryFormat_(out, format);
    } else {
      if (!makeGadgetBinaryFormatFromFields_(fields, format)) {
        format = makeDefaultGadgetBinaryFormat_(fields);
      }
      ok = readGadgetBinaryFormat_(out, format);
    }

    if (!ok) {
      if (lastGadgetError_.empty()) {
        lastGadgetError_ = "unknown Gadget binary read failure";
      }
      std::cerr << "[GadgetBinaryReader] failed path=" << path_
                << " particles=" << npart_
                << " dataOffset=" << dataOffset_
                << ": " << lastGadgetError_ << std::endl;
    }
    return ok;
  }

  bool readPositions_(SimulationBlock& out)
  {
    std::vector<uint8_t> block;
    if (!readBlock_(block)) return false;
    const size_t comps = npart_ * 3;
    if (block.size() == comps * sizeof(float)) {
      const float* v = reinterpret_cast<const float*>(block.data());
      for (size_t i = 0; i < npart_; ++i) {
        out.particles[i].position[0] = v[3 * i + 0];
        out.particles[i].position[1] = v[3 * i + 1];
        out.particles[i].position[2] = v[3 * i + 2];
      }
      return true;
    }
    if (block.size() == comps * sizeof(double)) {
      const double* v = reinterpret_cast<const double*>(block.data());
      for (size_t i = 0; i < npart_; ++i) {
        out.particles[i].position[0] = static_cast<float>(v[3 * i + 0]);
        out.particles[i].position[1] = static_cast<float>(v[3 * i + 1]);
        out.particles[i].position[2] = static_cast<float>(v[3 * i + 2]);
      }
      return true;
    }
    lastGadgetError_ =
      "Position block size mismatch: got=" + std::to_string(block.size()) +
      " expectedFloat=" + std::to_string(comps * sizeof(float)) +
      " expectedDouble=" + std::to_string(comps * sizeof(double));
    return false;
  }

  bool readVelocities_(SimulationBlock& out)
  {
    std::vector<uint8_t> block;
    if (!readBlock_(block)) return false;
    const size_t comps = npart_ * 3;
    if (block.size() == comps * sizeof(float)) {
      const float* v = reinterpret_cast<const float*>(block.data());
      for (size_t i = 0; i < npart_; ++i) {
        out.particles[i].vel[0] = v[3 * i + 0];
        out.particles[i].vel[1] = v[3 * i + 1];
        out.particles[i].vel[2] = v[3 * i + 2];
      }
      return true;
    }
    if (block.size() == comps * sizeof(double)) {
      const double* v = reinterpret_cast<const double*>(block.data());
      for (size_t i = 0; i < npart_; ++i) {
        out.particles[i].vel[0] = static_cast<float>(v[3 * i + 0]);
        out.particles[i].vel[1] = static_cast<float>(v[3 * i + 1]);
        out.particles[i].vel[2] = static_cast<float>(v[3 * i + 2]);
      }
      return true;
    }
    lastGadgetError_ =
      "Velocity block size mismatch: got=" + std::to_string(block.size()) +
      " expectedFloat=" + std::to_string(comps * sizeof(float)) +
      " expectedDouble=" + std::to_string(comps * sizeof(double));
    return false;
  }

  bool readIds_(SimulationBlock& out)
  {
    std::vector<uint8_t> block;
    if (!readBlock_(block)) return false;
    if (block.size() == npart_ * sizeof(uint32_t)) {
      const uint32_t* ids = reinterpret_cast<const uint32_t*>(block.data());
      out.ensureParticleIdStorage(DataType::Int32);
      for (size_t i = 0; i < npart_; ++i) {
        out.setParticleId(i, static_cast<uint64_t>(ids[i]));
      }
      return true;
    }
    if (block.size() == npart_ * sizeof(uint64_t)) {
      const uint64_t* ids = reinterpret_cast<const uint64_t*>(block.data());
      out.ensureParticleIdStorage(DataType::Int64);
      for (size_t i = 0; i < npart_; ++i) {
        out.setParticleId(i, ids[i]);
      }
      return true;
    }
    lastGadgetError_ =
      "ID block size mismatch: got=" + std::to_string(block.size()) +
      " expectedInt32=" + std::to_string(npart_ * sizeof(uint32_t)) +
      " expectedInt64=" + std::to_string(npart_ * sizeof(uint64_t));
    return false;
  }

  bool readMasses_(SimulationBlock& out)
  {
    const size_t massBlockCount = massBlockParticleCount_();
    std::vector<uint8_t> block;
    if (massBlockCount > 0) {
      if (!readBlock_(block)) return false;
      if (block.size() != massBlockCount * sizeof(float) &&
          block.size() != massBlockCount * sizeof(double)) {
        lastGadgetError_ =
          "Mass block size mismatch: got=" + std::to_string(block.size()) +
          " expectedFloat=" +
          std::to_string(massBlockCount * sizeof(float)) +
          " expectedDouble=" +
          std::to_string(massBlockCount * sizeof(double)) +
          " massBlockParticles=" + std::to_string(massBlockCount);
        return false;
      }
    }

    size_t global = 0;
    size_t massCursor = 0;
    for (int type = 0; type < 6; ++type) {
      for (int j = 0; j < counts_[type]; ++j, ++global) {
        SimulationElement& p = out.particles[global];
        p.type = static_cast<uint8_t>(type);
        if (massTable_[type] > 0.0) {
          p.mass = static_cast<float>(massTable_[type]);
        } else if (massBlockCount > 0) {
          if (block.size() == massBlockCount * sizeof(float)) {
            p.mass = reinterpret_cast<const float*>(block.data())[massCursor];
          } else {
            p.mass = static_cast<float>(
              reinterpret_cast<const double*>(block.data())[massCursor]);
          }
          ++massCursor;
        } else {
          p.mass = 0.0f;
        }
      }
    }
    return true;
  }

  size_t massBlockParticleCount_() const
  {
    size_t n = 0;
    for (int type = 0; type < 6; ++type) {
      if (counts_[type] > 0 && massTable_[type] == 0.0) {
        n += static_cast<size_t>(counts_[type]);
      }
    }
    return n;
  }

  static std::string normalizedToken_(std::string value)
  {
    value.erase(std::remove_if(value.begin(),
                               value.end(),
                               [](unsigned char c) {
                                 return std::isspace(c) || c == '_' ||
                                        c == '-' || c == '/';
                               }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    return value;
  }

  static FieldSpec requestedOrDefaultSpec_(const std::vector<FieldSpec>& fields,
                                           FieldKey key)
  {
    for (const FieldSpec& field : fields) {
      if (field.key == key) return field;
    }

    FieldSpec fallback;
    fallback.key = key;
    ApplyDefaultFieldSpec(fallback);
    return fallback;
  }

  static bool requestedSpec_(const std::vector<FieldSpec>& fields,
                             FieldKey key,
                             FieldSpec& out)
  {
    for (const FieldSpec& field : fields) {
      if (field.key == key) {
        out = field;
        return true;
      }
    }
    return false;
  }

  static FieldSpec requestedThermalSpec_(const std::vector<FieldSpec>& fields)
  {
    for (const FieldSpec& field : fields) {
      if (field.key == FieldKey::InternalEnergy) return field;
    }
    for (const FieldSpec& field : fields) {
      if (field.key == FieldKey::Temperature) return field;
    }

    FieldSpec fallback;
    fallback.key = FieldKey::InternalEnergy;
    ApplyDefaultFieldSpec(fallback);
    return fallback;
  }

  static void appendSimpleBlock_(GadgetBinaryFormat& format,
                                 GadgetBinaryBlockKind kind)
  {
    GadgetBinaryBlockSpec block;
    block.kind = kind;
    format.blocks.push_back(block);
  }

  static void appendFieldBlock_(GadgetBinaryFormat& format,
                                FieldSpec spec,
                                GadgetFieldDomain domain)
  {
    GadgetBinaryBlockSpec block;
    block.kind = GadgetBinaryBlockKind::Field;
    block.field.spec = std::move(spec);
    block.field.domain = domain;
    format.blocks.push_back(std::move(block));
  }

  static void appendSkipBlock_(GadgetBinaryFormat& format,
                               GadgetFieldDomain domain,
                               DataType type,
                               int componentsPerElement,
                               int blockRepeat)
  {
    GadgetBinaryBlockSpec block;
    block.kind = GadgetBinaryBlockKind::Skip;
    block.skip.domain = domain;
    block.skip.type = type;
    block.skip.componentsPerElement = std::max(1, componentsPerElement);
    block.skip.blockRepeat = std::max(1, blockRepeat);
    format.blocks.push_back(block);
  }

  static bool parseDomainToken_(const std::string& token,
                                GadgetFieldDomain& domain)
  {
    const std::string t = normalizedToken_(token);
    if (t == "all" || t == "npart" || t == "particles") {
      domain = GadgetFieldDomain::All;
      return true;
    }
    if (t == "absolute" || t == "block" || t == "global") {
      domain = GadgetFieldDomain::Absolute;
      return true;
    }
    if (t == "gas" || t == "type0" || t == "ptype0") {
      domain = GadgetFieldDomain::Type0;
      return true;
    }
    if (t == "gasstar" || t == "type0and5" || t == "ptype0and5" ||
        t == "0and5" || t == "type05") {
      domain = GadgetFieldDomain::Type0And5;
      return true;
    }
    return false;
  }

  static bool parseDataTypeToken_(const std::string& token,
                                  DataType& type)
  {
    const std::string t = normalizedToken_(token);
    if (t == "float" || t == "float32" || t == "f32") {
      type = DataType::Float;
      return true;
    }
    if (t == "double" || t == "float64" || t == "f64") {
      type = DataType::Double;
      return true;
    }
    if (t == "int" || t == "int32" || t == "i32") {
      type = DataType::Int32;
      return true;
    }
    if (t == "int64" || t == "i64" || t == "long") {
      type = DataType::Int64;
      return true;
    }
    return false;
  }

  static bool parsePositiveIntToken_(const std::string& token, int& value)
  {
    if (token.empty()) return false;
    char* end = nullptr;
    const long parsed = std::strtol(token.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed <= 0 || parsed > 1000000) {
      return false;
    }
    value = static_cast<int>(parsed);
    return true;
  }

  static bool appendSkipBlockToken_(GadgetBinaryFormat& format,
                                    const std::string& token)
  {
    std::stringstream ss(token);
    std::string part;
    std::vector<std::string> parts;
    while (std::getline(ss, part, ':')) {
      parts.push_back(part);
    }

    if (parts.size() < 4 || parts.size() > 5) return false;
    const std::string kind = normalizedToken_(parts[0]);
    if (kind != "skip" && kind != "dummy") return false;

    GadgetFieldDomain domain = GadgetFieldDomain::All;
    DataType type = DataType::Float;
    int componentsPerElement = 1;
    int blockRepeat = 1;
    if (!parseDomainToken_(parts[1], domain)) return false;
    if (!parseDataTypeToken_(parts[2], type)) return false;
    if (!parsePositiveIntToken_(parts[3], componentsPerElement)) return false;
    if (parts.size() == 5 &&
        !parsePositiveIntToken_(parts[4], blockRepeat)) {
      return false;
    }

    appendSkipBlock_(format,
                     domain,
                     type,
                     componentsPerElement,
                     blockRepeat);
    return true;
  }

  static GadgetFieldDomain defaultGadgetDomainForField_(FieldKey key)
  {
    switch (key) {
    case FieldKey::Position:
    case FieldKey::Velocity:
    case FieldKey::ID:
    case FieldKey::Mass:
      return GadgetFieldDomain::All;
    default:
      return GadgetFieldDomain::Type0;
    }
  }

  static bool parseGadgetSourceName_(const std::string& text,
                                     GadgetFieldDomain fallbackDomain,
                                     bool allowAbsolute,
                                     GadgetFieldDomain& domain,
                                     int& blockRepeat)
  {
    domain = fallbackDomain;
    blockRepeat = 1;

    const std::string normalized = normalizedToken_(text);
    if (text.empty() ||
        normalized == "unknown" ||
        normalized == "dummy" ||
        normalized == normalizedToken_(GetDefaultHDF5SourceName(FieldKey::Unknown))) {
      return true;
    }

    std::stringstream ss(text);
    std::string domainPart;
    std::string repeatPart;
    if (!std::getline(ss, domainPart, ':')) {
      return true;
    }

    GadgetFieldDomain parsedDomain = fallbackDomain;
    if (!parseDomainToken_(domainPart, parsedDomain)) {
      // Older configs stored HDF5 dataset names in sourceName. Treat those as
      // absent Gadget domain metadata rather than rejecting the whole format.
      return true;
    }
    if (parsedDomain == GadgetFieldDomain::Absolute && !allowAbsolute) {
      parsedDomain = fallbackDomain;
    }
    domain = parsedDomain;

    if (std::getline(ss, repeatPart, ':')) {
      int parsedRepeat = 1;
      if (parsePositiveIntToken_(repeatPart, parsedRepeat)) {
        blockRepeat = parsedRepeat;
      }
    }
    return true;
  }

  static bool appendGadgetFieldSpecBlock_(GadgetBinaryFormat& format,
                                          const FieldSpec& spec)
  {
    switch (spec.key) {
    case FieldKey::Position:
      appendSimpleBlock_(format, GadgetBinaryBlockKind::Position);
      return true;
    case FieldKey::Velocity:
      appendSimpleBlock_(format, GadgetBinaryBlockKind::Velocity);
      return true;
    case FieldKey::ID:
      appendSimpleBlock_(format, GadgetBinaryBlockKind::ID);
      return true;
    case FieldKey::Mass:
      appendSimpleBlock_(format, GadgetBinaryBlockKind::Mass);
      return true;
    case FieldKey::Dummy: {
      GadgetFieldDomain domain = GadgetFieldDomain::All;
      int blockRepeat = 1;
      parseGadgetSourceName_(spec.sourceName,
                             GadgetFieldDomain::All,
                             true,
                             domain,
                             blockRepeat);
      appendSkipBlock_(format,
                       domain,
                       spec.type,
                       std::max(1, spec.count),
                       blockRepeat);
      return true;
    }
    case FieldKey::Type:
    case FieldKey::Unknown:
      return true;
    case FieldKey::Bfield:
    case FieldKey::Hsml: {
      GadgetFieldDomain domain = defaultGadgetDomainForField_(spec.key);
      int ignoredRepeat = 1;
      parseGadgetSourceName_(spec.sourceName,
                             domain,
                             false,
                             domain,
                             ignoredRepeat);
      appendFieldBlock_(format, spec, domain);
      return true;
    }
    default:
      if (!isGadgetGasScalarField_(spec.key)) return true;
      GadgetFieldDomain domain = defaultGadgetDomainForField_(spec.key);
      int ignoredRepeat = 1;
      parseGadgetSourceName_(spec.sourceName,
                             domain,
                             false,
                             domain,
                             ignoredRepeat);
      appendFieldBlock_(format, spec, domain);
      return true;
    }
  }

  static bool makeGadgetBinaryFormatFromFields_(
    const std::vector<FieldSpec>& fields,
    GadgetBinaryFormat& format)
  {
    GadgetBinaryFormat parsed;
    for (const FieldSpec& spec : fields) {
      if (!appendGadgetFieldSpecBlock_(parsed, spec)) {
        return false;
      }
    }

    if (parsed.blocks.empty()) return false;
    format = std::move(parsed);
    return true;
  }

  static bool appendNamedGadgetBlock_(GadgetBinaryFormat& format,
                                      const std::vector<FieldSpec>& fields,
                                      std::string name)
  {
    if (appendSkipBlockToken_(format, name)) return true;

    name = normalizedToken_(std::move(name));
    if (name.empty()) return true;

    if (name == "pos" || name == "position" || name == "positions" ||
        name == "coordinates") {
      appendSimpleBlock_(format, GadgetBinaryBlockKind::Position);
      return true;
    }
    if (name == "vel" || name == "velocity" || name == "velocities") {
      appendSimpleBlock_(format, GadgetBinaryBlockKind::Velocity);
      return true;
    }
    if (name == "id" || name == "ids" || name == "particleid" ||
        name == "particleids") {
      appendSimpleBlock_(format, GadgetBinaryBlockKind::ID);
      return true;
    }
    if (name == "splitid" || name == "splitids") {
      appendSkipBlock_(format,
                       GadgetFieldDomain::All,
                       DataType::Int32,
                       1,
                       1);
      return true;
    }
    if (name == "mass" || name == "masses") {
      appendSimpleBlock_(format, GadgetBinaryBlockKind::Mass);
      return true;
    }
    if (name == "u" || name == "ie" || name == "internalenergy" ||
        name == "temperature" || name == "temp") {
      appendFieldBlock_(format,
                        requestedThermalSpec_(fields),
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "rho" || name == "density") {
      appendFieldBlock_(format,
                        requestedOrDefaultSpec_(fields, FieldKey::Density),
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "electronabundance" || name == "elec" || name == "ne") {
      appendFieldBlock_(
        format,
        requestedOrDefaultSpec_(fields, FieldKey::ElectronAbundance),
        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "h2" || name == "h2abundance" || name == "h2iabundance") {
      appendFieldBlock_(format,
                        requestedOrDefaultSpec_(fields, FieldKey::H2Abundance),
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "hd" || name == "hdabundance" || name == "hdiabundance") {
      appendFieldBlock_(format,
                        requestedOrDefaultSpec_(fields, FieldKey::HDAbundance),
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "j21" || name == "lw" || name == "lymanwerner") {
      appendFieldBlock_(format,
                        requestedOrDefaultSpec_(fields, FieldKey::J21),
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "gamma") {
      appendFieldBlock_(format,
                        requestedOrDefaultSpec_(fields, FieldKey::Gamma),
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "metallicity" || name == "metals" || name == "value") {
      FieldSpec valueSpec;
      if (!requestedSpec_(fields, FieldKey::Value, valueSpec)) {
        valueSpec = requestedOrDefaultSpec_(fields, FieldKey::Metallicity);
      }
      appendFieldBlock_(format,
                        valueSpec,
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "value2" || name == "val2") {
      appendFieldBlock_(format,
                        requestedOrDefaultSpec_(fields, FieldKey::Value2),
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "b" || name == "bfield" || name == "magneticfield") {
      appendFieldBlock_(format,
                        requestedOrDefaultSpec_(fields, FieldKey::Bfield),
                        GadgetFieldDomain::Type0);
      return true;
    }
    if (name == "hsml" || name == "smoothinglength") {
      appendFieldBlock_(format,
                        requestedOrDefaultSpec_(fields, FieldKey::Hsml),
                        GadgetFieldDomain::Type0);
      return true;
    }

    return false;
  }

  static bool makeGadgetBinaryFormatFromEnv_(const std::vector<FieldSpec>& fields,
                                             GadgetBinaryFormat& format)
  {
    const char* env = std::getenv("PARTICLE_VIS_GADGET_BLOCKS");
    if (!env || !*env) return false;

    GadgetBinaryFormat parsed;
    std::stringstream ss(env);
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (!appendNamedGadgetBlock_(parsed, fields, token)) {
        return false;
      }
    }

    if (parsed.blocks.empty()) return false;
    format = std::move(parsed);
    return true;
  }

  static GadgetBinaryFormat
  makeDefaultGadgetBinaryFormat_(const std::vector<FieldSpec>& fields)
  {
    GadgetBinaryFormat format;
    appendSimpleBlock_(format, GadgetBinaryBlockKind::Position);
    appendSimpleBlock_(format, GadgetBinaryBlockKind::Velocity);
    appendSimpleBlock_(format, GadgetBinaryBlockKind::ID);
    appendSimpleBlock_(format, GadgetBinaryBlockKind::Mass);

    const std::vector<GadgetBlockField> fieldPlan =
      makeGadgetBlockPlan_(fields);
    for (const GadgetBlockField& field : fieldPlan) {
      appendFieldBlock_(format, field.spec, field.domain);
    }

    return format;
  }

  static const char* gadgetDomainName_(GadgetFieldDomain domain)
  {
    switch (domain) {
    case GadgetFieldDomain::Absolute: return "absolute";
    case GadgetFieldDomain::All: return "all";
    case GadgetFieldDomain::Type0: return "type0";
    case GadgetFieldDomain::Type0And5: return "type0+type5";
    }
    return "unknown";
  }

  static const char* gadgetBlockKindName_(GadgetBinaryBlockKind kind)
  {
    switch (kind) {
    case GadgetBinaryBlockKind::Position: return "Position";
    case GadgetBinaryBlockKind::Velocity: return "Velocity";
    case GadgetBinaryBlockKind::ID: return "ID";
    case GadgetBinaryBlockKind::Mass: return "Mass";
    case GadgetBinaryBlockKind::Field: return "Field";
    case GadgetBinaryBlockKind::Skip: return "Skip";
    }
    return "unknown";
  }

  static std::string describeGadgetBlock_(const GadgetBinaryBlockSpec& block)
  {
    std::ostringstream os;
    os << gadgetBlockKindName_(block.kind);
    if (block.kind == GadgetBinaryBlockKind::Field) {
      os << "(" << GetFieldKeyDisplayName(block.field.spec.key)
         << ", domain=" << gadgetDomainName_(block.field.domain)
         << ", comps=" << block.field.spec.count << ")";
    } else if (block.kind == GadgetBinaryBlockKind::Skip) {
      os << "(domain=" << gadgetDomainName_(block.skip.domain)
         << ", type=" << GetDataTypeDisplayName(block.skip.type)
         << ", comps=" << block.skip.componentsPerElement
         << ", repeat=" << block.skip.blockRepeat << ")";
    }
    return os.str();
  }

  void prefixGadgetBlockError_(size_t blockIndex,
                               const GadgetBinaryBlockSpec& block,
                               std::streampos offset)
  {
    std::ostringstream os;
    os << "block#" << blockIndex << " "
       << describeGadgetBlock_(block)
       << " offset=";
    if (offset == std::streampos(-1)) {
      os << "unknown";
    } else {
      os << static_cast<long long>(offset);
    }
    os << ": "
       << (lastGadgetError_.empty() ? "unknown block read failure"
                                    : lastGadgetError_);
    lastGadgetError_ = os.str();
  }

  bool finishWithOptionalGadgetWarning_(size_t blockIndex,
                                        const GadgetBinaryBlockSpec& block,
                                        std::streampos offset)
  {
    prefixGadgetBlockError_(blockIndex, block, offset);
    std::cerr
      << "[GadgetBinaryReader] optional block ignored after core blocks "
      << "(Position/Velocity/ID/Mass) were read; continuing with partial data: "
      << lastGadgetError_ << std::endl;
    file_.clear();
    return true;
  }

  bool readGadgetBinaryFormat_(SimulationBlock& out,
                               const GadgetBinaryFormat& format)
  {
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(dataOffset_), std::ios::beg);
    if (!file_) {
      lastGadgetError_ =
        "failed to seek to first Gadget data block offset=" +
        std::to_string(dataOffset_);
      return false;
    }

    initAllOutput_(out, formatFields_(format));

    bool coreBlocksComplete = false;
    for (size_t blockIndex = 0; blockIndex < format.blocks.size();
         ++blockIndex) {
      const GadgetBinaryBlockSpec& block = format.blocks[blockIndex];
      const std::streampos offset = file_.tellg();
      {
        std::ostringstream label;
        label << "block#" << blockIndex << " "
              << describeGadgetBlock_(block);
        currentBlockTraceLabel_ = label.str();
      }
      std::cerr << "[GadgetBinaryReader] " << currentBlockTraceLabel_
                << " begin offset=" << streamPosToString_(offset)
                << std::endl;
      switch (block.kind) {
      case GadgetBinaryBlockKind::Position:
        if (!readPositions_(out)) {
          prefixGadgetBlockError_(blockIndex, block, offset);
          return false;
        }
        break;
      case GadgetBinaryBlockKind::Velocity:
        if (!readVelocities_(out)) {
          prefixGadgetBlockError_(blockIndex, block, offset);
          return false;
        }
        break;
      case GadgetBinaryBlockKind::ID:
        if (!readIds_(out)) {
          prefixGadgetBlockError_(blockIndex, block, offset);
          return false;
        }
        break;
      case GadgetBinaryBlockKind::Mass:
        if (!readMasses_(out)) {
          prefixGadgetBlockError_(blockIndex, block, offset);
          return false;
        }
        coreBlocksComplete = true;
        break;
      case GadgetBinaryBlockKind::Skip:
        if (!skipGadgetBlock_(block.skip)) {
          if (coreBlocksComplete) {
            return finishWithOptionalGadgetWarning_(blockIndex, block, offset);
          }
          prefixGadgetBlockError_(blockIndex, block, offset);
          return false;
        }
        break;
      case GadgetBinaryBlockKind::Field: {
        const GadgetFieldBlockReadResult status =
          readGadgetFieldBlock_(out, block.field);
        if (status == GadgetFieldBlockReadResult::Ok) break;
        if (status == GadgetFieldBlockReadResult::MissingOptionalTail) {
          if (!coreBlocksComplete) {
            prefixGadgetBlockError_(blockIndex, block, offset);
            return false;
          }
          prefixGadgetBlockError_(blockIndex, block, offset);
          std::cerr
            << "[GadgetBinaryReader] optional tail is missing after core "
            << "blocks were read; continuing with partial data: "
            << lastGadgetError_ << std::endl;
          file_.clear();
          return true;
        }
        if (coreBlocksComplete) {
          return finishWithOptionalGadgetWarning_(blockIndex, block, offset);
        }
        prefixGadgetBlockError_(blockIndex, block, offset);
        return false;
      }
      }
    }

    return true;
  }

  static std::vector<FieldSpec> formatFields_(const GadgetBinaryFormat& format)
  {
    std::vector<FieldSpec> fields;
    fields.reserve(format.blocks.size());
    for (const GadgetBinaryBlockSpec& block : format.blocks) {
      switch (block.kind) {
      case GadgetBinaryBlockKind::Position: {
        FieldSpec f;
        f.key = FieldKey::Position;
        f.type = DataType::Float;
        f.count = 3;
        fields.push_back(f);
        break;
      }
      case GadgetBinaryBlockKind::Velocity: {
        FieldSpec f;
        f.key = FieldKey::Velocity;
        f.type = DataType::Float;
        f.count = 3;
        fields.push_back(f);
        break;
      }
      case GadgetBinaryBlockKind::ID: {
        FieldSpec f;
        f.key = FieldKey::ID;
        f.type = DataType::Int64;
        f.count = 1;
        fields.push_back(f);
        break;
      }
      case GadgetBinaryBlockKind::Mass: {
        FieldSpec f;
        f.key = FieldKey::Mass;
        f.type = DataType::Float;
        f.count = 1;
        fields.push_back(f);
        break;
      }
      case GadgetBinaryBlockKind::Field:
        fields.push_back(block.field.spec);
        break;
      case GadgetBinaryBlockKind::Skip:
        break;
      }
    }
    return fields;
  }

  bool skipGadgetBlock_(const GadgetSkipBlock& skip)
  {
    const size_t domainCount = fieldDomainElementCount_(skip.domain);
    const size_t expectedBytes =
      domainCount *
      static_cast<size_t>(std::max(1, skip.componentsPerElement)) *
      dataTypeSize(skip.type);

    for (int i = 0; i < std::max(1, skip.blockRepeat); ++i) {
      std::vector<uint8_t> block;
      if (!readBlock_(block)) return false;
      if (block.size() != expectedBytes) {
        lastGadgetError_ =
          "skip/dummy block size mismatch: repeatIndex=" +
          std::to_string(i) +
          " got=" + std::to_string(block.size()) +
          " expected=" + std::to_string(expectedBytes) +
          " domain=" + gadgetDomainName_(skip.domain) +
          " domainCount=" + std::to_string(domainCount) +
          " componentsPerElement=" +
          std::to_string(std::max(1, skip.componentsPerElement)) +
          " type=" + GetDataTypeDisplayName(skip.type);
        return false;
      }
    }
    return true;
  }

  static bool isFixedGadgetBlock_(FieldKey key)
  {
    return key == FieldKey::Position ||
           key == FieldKey::Velocity ||
           key == FieldKey::ID ||
           key == FieldKey::Mass ||
           key == FieldKey::Type;
  }

  static bool isGadgetGasScalarField_(FieldKey key)
  {
    switch (key) {
    case FieldKey::InternalEnergy:
    case FieldKey::Temperature:
    case FieldKey::Density:
    case FieldKey::ElectronAbundance:
    case FieldKey::H2Abundance:
    case FieldKey::HDAbundance:
    case FieldKey::J21:
    case FieldKey::Gamma:
    case FieldKey::Metallicity:
    case FieldKey::Value:
    case FieldKey::Value2:
      return true;
    default:
      return false;
    }
  }

  static int gadgetCanonicalRank_(FieldKey key)
  {
    switch (key) {
    case FieldKey::InternalEnergy:    return 0;
    case FieldKey::Temperature:       return 0;
    case FieldKey::Density:           return 1;
    case FieldKey::ElectronAbundance: return 2;
    case FieldKey::H2Abundance:       return 3;
    case FieldKey::HDAbundance:       return 4;
    case FieldKey::J21:               return 5;
    case FieldKey::Gamma:             return 6;
    case FieldKey::Metallicity:       return 7;
    case FieldKey::Value:             return 8;
    case FieldKey::Value2:            return 9;
    case FieldKey::Bfield:            return 10;
    case FieldKey::Hsml:              return 11;
    default:                          return 1000;
    }
  }

  static std::vector<GadgetBlockField>
  makeGadgetBlockPlan_(const std::vector<FieldSpec>& fields)
  {
    std::vector<GadgetBlockField> plan;
    plan.reserve(fields.size());

    bool haveThermalBlock = false;
    for (const FieldSpec& spec : fields) {
      if (isFixedGadgetBlock_(spec.key) || spec.key == FieldKey::Dummy) {
        continue;
      }

      GadgetBlockField field;
      field.spec = spec;

      if (spec.key == FieldKey::Hsml ||
          isGadgetGasScalarField_(spec.key) ||
          spec.key == FieldKey::Bfield) {
        field.domain = GadgetFieldDomain::Type0;
      } else {
        continue;
      }

      int ignoredRepeat = 1;
      parseGadgetSourceName_(spec.sourceName,
                             field.domain,
                             false,
                             field.domain,
                             ignoredRepeat);

      if (spec.key == FieldKey::InternalEnergy ||
          spec.key == FieldKey::Temperature) {
        if (haveThermalBlock) continue;
        haveThermalBlock = true;
      }

      plan.push_back(field);
    }

    std::stable_sort(plan.begin(), plan.end(),
                     [](const GadgetBlockField& a,
                        const GadgetBlockField& b) {
                       return gadgetCanonicalRank_(a.spec.key) <
                              gadgetCanonicalRank_(b.spec.key);
                     });
    return plan;
  }

  size_t particleOffsetForType_(int type) const
  {
    size_t offset = 0;
    for (int t = 0; t < type && t < 6; ++t) {
      offset += static_cast<size_t>(std::max(0, counts_[t]));
    }
    return offset;
  }

  size_t fieldDomainElementCount_(GadgetFieldDomain domain) const
  {
    switch (domain) {
    case GadgetFieldDomain::Absolute:
      return 1;
    case GadgetFieldDomain::All:
      return npart_;
    case GadgetFieldDomain::Type0:
      return static_cast<size_t>(std::max(0, counts_[0]));
    case GadgetFieldDomain::Type0And5:
      return static_cast<size_t>(std::max(0, counts_[0])) +
             static_cast<size_t>(std::max(0, counts_[5]));
    }
    return 0;
  }

  size_t outputIndexForDomain_(GadgetFieldDomain domain, size_t localIndex) const
  {
    const size_t gasCount = static_cast<size_t>(std::max(0, counts_[0]));
    switch (domain) {
    case GadgetFieldDomain::Absolute:
      return localIndex;
    case GadgetFieldDomain::All:
      return localIndex;
    case GadgetFieldDomain::Type0:
      return localIndex;
    case GadgetFieldDomain::Type0And5:
      if (localIndex < gasCount) return localIndex;
      return particleOffsetForType_(5) + (localIndex - gasCount);
    }
    return localIndex;
  }

  static FieldSpec makeWritableGadgetSpec_(const FieldSpec& spec)
  {
    FieldSpec actual = spec;
    actual.count = std::max(1, actual.count);

    // Old Gadget snapshots store internal energy, not temperature. The app has
    // no separate core slot for U, so keep the raw thermal block in the
    // temperature slot for visualization unless the user maps it differently.
    if (actual.key == FieldKey::InternalEnergy) {
      actual.key = FieldKey::Temperature;
    }

    return actual;
  }

  GadgetFieldBlockReadResult readGadgetFieldBlock_(SimulationBlock& out,
                                                   const GadgetBlockField& field)
  {
    const size_t domainCount = fieldDomainElementCount_(field.domain);
    if (domainCount == 0) return GadgetFieldBlockReadResult::Ok;

    std::streampos before = file_.tellg();
    std::vector<uint8_t> block;
    if (!readBlock_(block)) {
      file_.clear();
      file_.seekg(before, std::ios::beg);
      return GadgetFieldBlockReadResult::MissingOptionalTail;
    }

    FieldSpec actual = makeWritableGadgetSpec_(field.spec);
    const size_t comps = static_cast<size_t>(actual.count);
    const size_t nvalues = domainCount * comps;

    if (block.size() != nvalues * sizeof(float) &&
        block.size() != nvalues * sizeof(double) &&
        block.size() != nvalues * sizeof(int32_t) &&
        block.size() != nvalues * sizeof(int64_t)) {
      file_.clear();
      file_.seekg(before, std::ios::beg);
      lastGadgetError_ =
        "field block size mismatch: field=" +
        std::string(GetFieldKeyDisplayName(field.spec.key)) +
        " domain=" + gadgetDomainName_(field.domain) +
        " domainCount=" + std::to_string(domainCount) +
        " comps=" + std::to_string(comps) +
        " got=" + std::to_string(block.size()) +
        " expectedFloat=" + std::to_string(nvalues * sizeof(float)) +
        " expectedDouble=" + std::to_string(nvalues * sizeof(double)) +
        " expectedInt32=" + std::to_string(nvalues * sizeof(int32_t)) +
        " expectedInt64=" + std::to_string(nvalues * sizeof(int64_t));
      return GadgetFieldBlockReadResult::Mismatch;
    }

    if (block.size() == nvalues * sizeof(double)) actual.type = DataType::Double;
    else if (block.size() == nvalues * sizeof(int32_t)) actual.type = DataType::Int32;
    else if (block.size() == nvalues * sizeof(int64_t)) actual.type = DataType::Int64;
    else actual.type = DataType::Float;

    FieldLayout fl;
    fl.spec = actual;
    fl.ftype = actual.key;
    fl.dest = getDestKind(actual.key);
    if (fl.dest == DestKind::SoA) {
      fl.soaKey = getSoAKey(actual.key);
      auto& f = out.soa[fl.soaKey];
      f.type = actual.type;
      f.comps = actual.count;
      f.resize(npart_);
    }

    for (size_t i = 0; i < domainCount; ++i) {
      const size_t outIndex = outputIndexForDomain_(field.domain, i);
      if (outIndex >= npart_) {
        lastGadgetError_ =
          "field output index out of range: field=" +
          std::string(GetFieldKeyDisplayName(field.spec.key)) +
          " domain=" + gadgetDomainName_(field.domain) +
          " localIndex=" + std::to_string(i) +
          " outputIndex=" + std::to_string(outIndex) +
          " npart=" + std::to_string(npart_);
        return GadgetFieldBlockReadResult::Mismatch;
      }

      const uint8_t* src =
        block.data() + i * comps * dataTypeSize(actual.type);
      switch (actual.type) {
      case DataType::Float:
        writeFieldToSimulationBlock(out,
                                    outIndex,
                                    fl,
                                    reinterpret_cast<const float*>(src));
        break;
      case DataType::Double:
        writeFieldToSimulationBlock(out,
                                    outIndex,
                                    fl,
                                    reinterpret_cast<const double*>(src));
        break;
      case DataType::Int32:
        writeFieldToSimulationBlock(out,
                                    outIndex,
                                    fl,
                                    reinterpret_cast<const int32_t*>(src));
        break;
      case DataType::Int64:
        writeFieldToSimulationBlock(out,
                                    outIndex,
                                    fl,
                                    reinterpret_cast<const int64_t*>(src));
        break;
      }
    }

    return GadgetFieldBlockReadResult::Ok;
  }
};
