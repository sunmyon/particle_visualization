#pragma once

#include "FileIO/particle_reader.h"
#include "FileIO/file_mask.h"
#include "FileIO/file_layout.h"
#include "data/header_info.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

class GadgetBinaryReader final : public IParticleReader {
public:
  bool open(const std::string& path, HeaderInfo& header) override
  {
    path_ = path;
    file_.open(path, std::ios::binary);
    if (!file_) return false;

    int32_t blockSize = 0;
    if (!readRaw_(blockSize) || blockSize != 256) return false;

    std::array<uint8_t, 256> raw{};
    file_.read(reinterpret_cast<char*>(raw.data()), raw.size());
    if (!file_) return false;

    int32_t tail = 0;
    if (!readRaw_(tail) || tail != blockSize) return false;

    parseHeader_(raw.data(), header);
    dataOffset_ = static_cast<size_t>(file_.tellg());
    header.flag_hdf5 = false;
    return true;
  }

  void close() override
  {
    file_.close();
    path_.clear();
    npart_ = 0;
    dataOffset_ = 0;
    counts_.fill(0);
    massTable_.fill(0.0);
  }

  size_t particleCount() const override { return npart_; }

  bool is_binary() override { return false; }

  bool readRange(ParticleBlock& out,
                 size_t begin,
                 size_t count,
                 const std::vector<FieldSpec>& fields,
                 ParticleMask* mask = nullptr) override
  {
    if (begin + count > npart_) return false;

    ParticleBlock all;
    if (!readAllParticles_(all, fields)) return false;

    const bool masked = (mask != nullptr) && mask->active();
    initSubsetOutput_(out, count, all);

    if (masked) {
      size_t thinCandidates = 0;
      for (size_t i = begin; i < begin + count; ++i) {
        const ParticleData& p = all.particles[i];
        if (mask->typeEnabled(p.type) && mask->typeThinOK(p.type)) {
          ++thinCandidates;
        }
      }
      mask->prepare(thinCandidates);
    }

    size_t written = 0;
    for (size_t i = begin; i < begin + count; ++i) {
      if (masked) {
        const ParticleData& p = all.particles[i];
        CoreSample c;
        c.pos[0] = p.original_pos[0];
        c.pos[1] = p.original_pos[1];
        c.pos[2] = p.original_pos[2];
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
    int32_t n = 0;
    if (!readRaw_(n) || n < 0) return false;

    bytes.resize(static_cast<size_t>(n));
    file_.read(reinterpret_cast<char*>(bytes.data()), n);
    if (!file_) return false;

    int32_t tail = 0;
    if (!readRaw_(tail) || tail != n) return false;
    return true;
  }

  void initAllOutput_(ParticleBlock& out, const std::vector<FieldSpec>& fields)
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

  static void initSubsetOutput_(ParticleBlock& out,
                                size_t count,
                                const ParticleBlock& source)
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

  static void copyRecord_(ParticleBlock& dst,
                          size_t dstIndex,
                          const ParticleBlock& src,
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

  bool readAllParticles_(ParticleBlock& out, const std::vector<FieldSpec>& fields)
  {
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(dataOffset_), std::ios::beg);
    if (!file_) return false;

    initAllOutput_(out, fields);

    if (!readPositions_(out)) return false;
    if (!readVelocities_(out)) return false;
    if (!readIds_(out)) return false;
    if (!readMasses_(out)) return false;

    std::vector<FieldSpec> extraGasFields;
    for (const FieldSpec& f : fields) {
      if (f.key == FieldKey::Position ||
          f.key == FieldKey::Velocity ||
          f.key == FieldKey::ID ||
          f.key == FieldKey::Mass ||
          f.key == FieldKey::Type ||
          f.key == FieldKey::Dummy) {
        continue;
      }
      extraGasFields.push_back(f);
    }

    for (const FieldSpec& f : extraGasFields) {
      if (!readGasFieldBlock_(out, f)) break;
    }

    return true;
  }

  bool readPositions_(ParticleBlock& out)
  {
    std::vector<uint8_t> block;
    if (!readBlock_(block)) return false;
    const size_t comps = npart_ * 3;
    if (block.size() == comps * sizeof(float)) {
      const float* v = reinterpret_cast<const float*>(block.data());
      for (size_t i = 0; i < npart_; ++i) {
        out.particles[i].original_pos[0] = v[3 * i + 0];
        out.particles[i].original_pos[1] = v[3 * i + 1];
        out.particles[i].original_pos[2] = v[3 * i + 2];
      }
      return true;
    }
    if (block.size() == comps * sizeof(double)) {
      const double* v = reinterpret_cast<const double*>(block.data());
      for (size_t i = 0; i < npart_; ++i) {
        out.particles[i].original_pos[0] = static_cast<float>(v[3 * i + 0]);
        out.particles[i].original_pos[1] = static_cast<float>(v[3 * i + 1]);
        out.particles[i].original_pos[2] = static_cast<float>(v[3 * i + 2]);
      }
      return true;
    }
    return false;
  }

  bool readVelocities_(ParticleBlock& out)
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
    return false;
  }

  bool readIds_(ParticleBlock& out)
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
    return false;
  }

  bool readMasses_(ParticleBlock& out)
  {
    const size_t massBlockCount = massBlockParticleCount_();
    std::vector<uint8_t> block;
    if (massBlockCount > 0) {
      if (!readBlock_(block)) return false;
      if (block.size() != massBlockCount * sizeof(float) &&
          block.size() != massBlockCount * sizeof(double)) {
        return false;
      }
    }

    size_t global = 0;
    size_t massCursor = 0;
    for (int type = 0; type < 6; ++type) {
      for (int j = 0; j < counts_[type]; ++j, ++global) {
        ParticleData& p = out.particles[global];
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

  bool readGasFieldBlock_(ParticleBlock& out, const FieldSpec& spec)
  {
    if (counts_[0] <= 0) return true;

    std::streampos before = file_.tellg();
    std::vector<uint8_t> block;
    if (!readBlock_(block)) {
      file_.clear();
      file_.seekg(before, std::ios::beg);
      return false;
    }

    const size_t gasCount = static_cast<size_t>(counts_[0]);
    const size_t comps = std::max(1, spec.count);
    const size_t nvalues = gasCount * comps;

    if (block.size() != nvalues * sizeof(float) &&
        block.size() != nvalues * sizeof(double) &&
        block.size() != nvalues * sizeof(int32_t) &&
        block.size() != nvalues * sizeof(int64_t)) {
      file_.clear();
      file_.seekg(before, std::ios::beg);
      return false;
    }

    FieldSpec actual = spec;
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

    for (size_t i = 0; i < gasCount; ++i) {
      const uint8_t* src =
        block.data() + i * comps * dataTypeSize(actual.type);
      switch (actual.type) {
      case DataType::Float:
        writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const float*>(src));
        break;
      case DataType::Double:
        writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const double*>(src));
        break;
      case DataType::Int32:
        writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const int32_t*>(src));
        break;
      case DataType::Int64:
        writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const int64_t*>(src));
        break;
      }
    }

    return true;
  }
};
