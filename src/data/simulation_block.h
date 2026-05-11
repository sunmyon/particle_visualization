#pragma once

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <array>

#include "simulation_element.h"
#include "data/sample_coordinates.h"
#include "core/input_density_units.h"
#include "core/physics_constants.h"
#include "core/quantity.h"
#include <vector>

struct HeaderInfo;
struct ParticleSelectionOption;

struct SimulationBlock {
  // ---- AoS core ----
  std::vector<SimulationElement> particles;

  // ---- AoS extension (optional) ----
  AoSExtensionBuffer aosExt;

  // ---- SoA fields (optional) ----
  // key = field label (e.g. "Bfield")
  std::unordered_map<std::string, SoAField> soa;
  // FieldKey display names that were actually present in the source/read path.
  std::unordered_set<std::string> loadedFieldNames;
  std::unordered_map<std::string, uint8_t> loadedFieldTypeMask;
  QuantityStorageMetadata quantityStorage;

  std::unordered_map<uint64_t, size_t> id2index;
  bool id2indexDirty = true;
  float worldToRenderScale = 1.0f;
  
  void resize(size_t n) {
    particles.resize(n);

    if (aosExt.stride > 0)
      aosExt.resize(n);

    for (auto &kv : soa)
      kv.second.resize(n);

    id2indexDirty = true;
  }
 
  size_t size() const {
    return particles.size();
  }

  void clear() {
    particles.clear();
    aosExt.bytes.clear();
    soa.clear();
    loadedFieldNames.clear();
    loadedFieldTypeMask.clear();
    id2index.clear();
    id2indexDirty = true;
    worldToRenderScale = 1.0f;
    quantityStorage = QuantityStorageMetadata{};
  }

  bool hasParticleIds() const
  {
    auto it = soa.find(kParticleIdKey);
    return it != soa.end() &&
           it->second.comps == 1 &&
           (it->second.type == DataType::Int32 ||
            it->second.type == DataType::Int64) &&
           it->second.bytes.size() ==
             particles.size() * dataTypeSize(it->second.type);
  }

  void markLoadedField(const char* name)
  {
    if (name && *name) {
      loadedFieldNames.insert(name);
      loadedFieldTypeMask[name] |= 0x3fu;
    }
  }

  void markLoadedFieldForType(const char* name, int ptype)
  {
    if (!name || !*name || ptype < 0 || ptype >= 6) return;
    loadedFieldNames.insert(name);
    loadedFieldTypeMask[name] |= static_cast<uint8_t>(1u << ptype);
  }

  void markLoadedFieldForTypeMask(const char* name, uint8_t typeMask)
  {
    if (!name || !*name || typeMask == 0) return;
    loadedFieldNames.insert(name);
    loadedFieldTypeMask[name] |= static_cast<uint8_t>(typeMask & 0x3fu);
  }

  bool hasLoadedField(const char* name) const
  {
    if (!name || !*name) return false;
    return loadedFieldNames.find(name) != loadedFieldNames.end();
  }

  bool hasLoadedFieldForType(const char* name, int ptype) const
  {
    if (!name || !*name || ptype < 0 || ptype >= 6) return false;
    auto it = loadedFieldTypeMask.find(name);
    if (it == loadedFieldTypeMask.end()) {
      return hasLoadedField(name);
    }
    return (it->second & static_cast<uint8_t>(1u << ptype)) != 0;
  }

  void ensureParticleIdStorage(DataType type = DataType::Int64)
  {
    if (type != DataType::Int32 && type != DataType::Int64) {
      type = DataType::Int64;
    }
    auto& f = soa[kParticleIdKey];
    if (f.type != type || f.comps != 1 ||
        f.bytes.size() != particles.size() * dataTypeSize(type)) {
      f.type = type;
      f.comps = 1;
      f.resize(particles.size());
      id2indexDirty = true;
    }
  }

  void setParticleId(size_t i, uint64_t id)
  {
    if (!hasParticleIds()) {
      ensureParticleIdStorage(DataType::Int64);
    }
    SoAField& f = soa[kParticleIdKey];
    if (i >= particles.size()) return;
    if (f.type == DataType::Int32) {
      int32_t v = static_cast<int32_t>(id);
      std::memcpy(f.ptr(i), &v, sizeof(v));
    } else {
      int64_t v = static_cast<int64_t>(id);
      std::memcpy(f.ptr(i), &v, sizeof(v));
    }
    id2indexDirty = true;
  }

  int64_t particleIdSigned(size_t i) const
  {
    auto it = soa.find(kParticleIdKey);
    if (it == soa.end() || i >= particles.size()) {
      return static_cast<int64_t>(i);
    }
    const SoAField& f = it->second;
    if (f.comps != 1) return static_cast<int64_t>(i);
    if (f.type == DataType::Int32) {
      int32_t v = 0;
      std::memcpy(&v, f.ptr(i), sizeof(v));
      return static_cast<int64_t>(v);
    }
    if (f.type == DataType::Int64) {
      int64_t v = 0;
      std::memcpy(&v, f.ptr(i), sizeof(v));
      return v;
    }
    return static_cast<int64_t>(i);
  }

  uint64_t particleId(size_t i) const
  {
    return static_cast<uint64_t>(particleIdSigned(i));
  }

  void rebuildIdIndex()
  {
    id2index.clear();
    id2index.reserve(particles.size() * 13 / 10);
    for (size_t i=0;i<particles.size();++i) {
      id2index[particleId(i)] = i;
    }
    id2indexDirty = false;
  }

  inline size_t findIndexByID(uint64_t id) {
    if (id2indexDirty) rebuildIdIndex();
    auto it = id2index.find(id);
    if (it == id2index.end()) return (size_t)-1;
    return it->second;
  }

  template<typename T>
  bool hasSoAAs(const std::string& key, int expectedComps) const {
    auto it = soa.find(key);
    if (it == soa.end()) return false;

    const SoAField& f = it->second;
    if (f.comps != expectedComps) return false;

    const size_t elemSz = dataTypeSize(f.type);
    const size_t expectBytes = particles.size() * size_t(expectedComps) * elemSz;
    if (f.bytes.size() != expectBytes) return false;

    switch (f.type) {
    case DataType::Float:
    case DataType::Double:
    case DataType::Int32:
    case DataType::Int64:
      return true;
    default:
      return false;
    }
  }

  template<typename T, int N>
  bool readSoAAs(const std::string& key, size_t i, T (&out)[N]) const {
    auto it = soa.find(key);
    if (it == soa.end()) return false;

    const SoAField& f = it->second;
    if (f.comps != N) return false;
    if (i >= particles.size()) return false;

    switch (f.type) {
    case DataType::Float: {
      const float* p = reinterpret_cast<const float*>(f.ptr(i));
      for (int k = 0; k < N; ++k) out[k] = static_cast<T>(p[k]);
      return true;
    }
    case DataType::Double: {
      const double* p = reinterpret_cast<const double*>(f.ptr(i));
      for (int k = 0; k < N; ++k) out[k] = static_cast<T>(p[k]);
      return true;
    }
    case DataType::Int32: {
      const int32_t* p = reinterpret_cast<const int32_t*>(f.ptr(i));
      for (int k = 0; k < N; ++k) out[k] = static_cast<T>(p[k]);
      return true;
    }
    case DataType::Int64: {
      const int64_t* p = reinterpret_cast<const int64_t*>(f.ptr(i));
      for (int k = 0; k < N; ++k) out[k] = static_cast<T>(p[k]);
      return true;
    }
    default:
      return false;
    }
  }

  template<typename T>
  bool readSoAAs(const std::string& key, size_t i, T& out) const {
    T tmp[1];
    if (!readSoAAs<T,1>(key, i, tmp)) return false;
    out = tmp[0];
    return true;
  }

  template<typename T, int N>
  bool writeSoAAs(const std::string& key, size_t i, const T (&in)[N]) {
    auto it = soa.find(key);
    if (it == soa.end()) return false;

    SoAField& f = it->second;
    if (f.comps != N) return false;
    if (i >= particles.size()) return false;

    switch (f.type) {
    case DataType::Float: {
      float* p = reinterpret_cast<float*>(f.ptr(i));
      for (int k = 0; k < N; ++k) p[k] = static_cast<float>(in[k]);
      return true;
    }
    case DataType::Double: {
      double* p = reinterpret_cast<double*>(f.ptr(i));
      for (int k = 0; k < N; ++k) p[k] = static_cast<double>(in[k]);
      return true;
    }    case DataType::Int32: {
      int32_t* p = reinterpret_cast<int32_t*>(f.ptr(i));
      for (int k = 0; k < N; ++k) p[k] = static_cast<int32_t>(in[k]);
      return true;
    }
    case DataType::Int64: {
      int64_t* p = reinterpret_cast<int64_t*>(f.ptr(i));
      for (int k = 0; k < N; ++k) p[k] = static_cast<int64_t>(in[k]);
      return true;
    }
    default:
      return false;
    }
  }

  template<typename T>
  bool writeSoAAs(const std::string& key, size_t i, T in) {
    T tmp[1] = {in};
    return writeSoAAs<T,1>(key, i, tmp);
  }

  template<typename T, int N>
  bool hasSoAAs(const SoAView<T,N>& view) const {
    return hasSoAAs<T>(view.key, N);
  }

  template<typename T, int N>
  bool readSoAAs(const SoAView<T,N>& view, size_t i, T (&out)[N]) const {
    return readSoAAs<T>(view.key, i, out);
  }

  template<typename T>
  bool readSoAAs(const SoAView<T,1>& view, size_t i, T& out) const {
    return readSoAAs<T>(view.key, i, out);
  }

  template<typename T, int N>
  bool writeSoAAs(const SoAView<T,N>& view, size_t i, const T (&in)[N]) {
    return writeSoAAs<T>(view.key, i, in);
  }

  template<typename T>
  bool writeSoAAs(const SoAView<T,1>& view, size_t i, T in) {
    return writeSoAAs<T>(view.key, i, in);
  }

  static const char* quantitySoAKey(QuantityId q)
  {
    switch (q) {
    case QuantityId::B:
    case QuantityId::Beta:
      return kBfieldKey;
    case QuantityId::Metallicity:
      return kMetallicityKey;
    case QuantityId::ElectronAbundance:
      return kElectronAbundanceKey;
    case QuantityId::H2Abundance:
      return kH2AbundanceKey;
    case QuantityId::HDAbundance:
      return kHDAbundanceKey;
    case QuantityId::J21:
      return kJ21Key;
    case QuantityId::Val:
      return kVal1Key;
    case QuantityId::Val2:
      return kVal2Key;
    default:
      return nullptr;
    }
  }

  static const char* quantityLoadedFieldName(QuantityId q)
  {
    switch (q) {
    case QuantityId::Density:
      return "density";
    case QuantityId::Temperature:
      return "temperature";
    case QuantityId::Mass:
      return "mass";
    case QuantityId::Hsml:
      return "Hsml";
    case QuantityId::B:
    case QuantityId::Beta:
      return kBfieldKey;
    case QuantityId::Metallicity:
      return kMetallicityKey;
    case QuantityId::ElectronAbundance:
      return kElectronAbundanceKey;
    case QuantityId::H2Abundance:
      return kH2AbundanceKey;
    case QuantityId::HDAbundance:
      return kHDAbundanceKey;
    case QuantityId::J21:
      return kJ21Key;
    case QuantityId::Val:
      return "value";
    case QuantityId::Val2:
      return "value2";
    default:
      return nullptr;
    }
  }

  bool hasQuantityForType(QuantityId q, int type) const
  {
    if (type < 0 || type >= kNumTypes) {
      return false;
    }

    switch (q) {
    case QuantityId::Density:
    case QuantityId::Temperature:
    case QuantityId::Mass:
    case QuantityId::Hsml:
    case QuantityId::PosX:
    case QuantityId::PosY:
    case QuantityId::PosZ:
    case QuantityId::Radius:
    case QuantityId::VRad:
      return true;
    default:
      break;
    }

    const char* key = quantitySoAKey(q);
    if (!key) {
      return false;
    }
    if (q == QuantityId::B || q == QuantityId::Beta) {
      if (!hasSoAAs(soa_views::Bfield)) {
        return false;
      }
    } else {
      auto it = soa.find(key);
      if (it == soa.end() || it->second.comps < 1) {
        return false;
      }
    }

    if (loadedFieldTypeMask.empty() && loadedFieldNames.empty()) {
      return true;
    }

    const char* loadedName = quantityLoadedFieldName(q);
    if (loadedName && hasLoadedFieldForType(loadedName, type)) {
      return true;
    }
    return hasLoadedFieldForType(key, type);
  }

  bool hasQuantityAt(size_t i, QuantityId q) const
  {
    if (i >= particles.size()) {
      return false;
    }
    return hasQuantityForType(q, static_cast<int>(particles[i].type));
  }

  bool getVector(size_t i, VectorId v, float out[3]) const
  {
    if (i >= particles.size()) {
      return false;
    }
    const SimulationElement& p = particles[i];
    switch (v) {
    case VectorId::OriginalPos:
      out[0] = p.position[0];
      out[1] = p.position[1];
      out[2] = p.position[2];
      return true;
    case VectorId::Pos:
      renderPosition(p, worldToRenderScale, out);
      return true;
    case VectorId::Vel:
      out[0] = p.vel[0];
      out[1] = p.vel[1];
      out[2] = p.vel[2];
      return true;
    case VectorId::Bfield:
      out[0] = out[1] = out[2] = 0.0f;
      if (!hasQuantityAt(i, QuantityId::B)) {
        return false;
      }
      {
        float b[3];
        if (!readSoAAs(soa_views::Bfield, i, b)) {
          return false;
        }
        out[0] = b[0];
        out[1] = b[1];
        out[2] = b[2];
        return true;
      }
    }
    return false;
  }

  bool setVector(size_t i, VectorId v, const float in[3])
  {
    if (i >= particles.size()) {
      return false;
    }
    SimulationElement& p = particles[i];
    switch (v) {
    case VectorId::OriginalPos:
      p.position[0] = in[0];
      p.position[1] = in[1];
      p.position[2] = in[2];
      return true;
    case VectorId::Pos: {
      const float invScale =
        (worldToRenderScale != 0.0f) ? 1.0f / worldToRenderScale : 1.0f;
      p.position[0] = in[0] * invScale;
      p.position[1] = in[1] * invScale;
      p.position[2] = in[2] * invScale;
      return true;
    }
    case VectorId::Vel:
      p.vel[0] = in[0];
      p.vel[1] = in[1];
      p.vel[2] = in[2];
      return true;
    case VectorId::Bfield: {
      float tmp[3] = {in[0], in[1], in[2]};
      return writeSoAAs(soa_views::Bfield, i, tmp);
    }
    }
    return false;
  }

  bool getQuantity(size_t i,
                   QuantityId q,
                   float& out,
                   const float* center = nullptr,
                   const float* vcenter = nullptr) const
  {
    out = 0.0f;
    if (i >= particles.size()) {
      return false;
    }
    const SimulationElement& p = particles[i];

    switch (q) {
    case QuantityId::Density:
      out = p.density;
      return true;
    case QuantityId::Temperature:
      out = p.temperature;
      return true;
    case QuantityId::Mass:
      out = p.mass;
      return true;
    case QuantityId::Hsml:
      out = p.supportRadius;
      return true;
    case QuantityId::PosX:
      out = p.position[0];
      return true;
    case QuantityId::PosY:
      out = p.position[1];
      return true;
    case QuantityId::PosZ:
      out = p.position[2];
      return true;
    case QuantityId::Radius: {
      const float cx = center ? center[0] : 0.0f;
      const float cy = center ? center[1] : 0.0f;
      const float cz = center ? center[2] : 0.0f;
      const float dx = p.position[0] - cx;
      const float dy = p.position[1] - cy;
      const float dz = p.position[2] - cz;
      out = std::sqrt(dx * dx + dy * dy + dz * dz);
      return true;
    }
    case QuantityId::VRad: {
      const float cx = center ? center[0] : 0.0f;
      const float cy = center ? center[1] : 0.0f;
      const float cz = center ? center[2] : 0.0f;
      const float vcx = vcenter ? vcenter[0] : 0.0f;
      const float vcy = vcenter ? vcenter[1] : 0.0f;
      const float vcz = vcenter ? vcenter[2] : 0.0f;
      const float dx = p.position[0] - cx;
      const float dy = p.position[1] - cy;
      const float dz = p.position[2] - cz;
      const float r = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (r <= 0.0f) {
        return false;
      }
      const float dvx = dx * (p.vel[0] - vcx);
      const float dvy = dy * (p.vel[1] - vcy);
      const float dvz = dz * (p.vel[2] - vcz);
      out = (dvx + dvy + dvz) / r;
      return true;
    }
    case QuantityId::B: {
      if (!hasQuantityAt(i, q)) {
        return false;
      }
      float b[3];
      if (!readSoAAs(soa_views::Bfield, i, b)) {
        return false;
      }
      out = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
      return true;
    }
    case QuantityId::Beta: {
      if (!hasQuantityAt(i, q)) {
        return false;
      }
      float b[3];
      if (!readSoAAs(soa_views::Bfield, i, b)) {
        return false;
      }
      const float b2 = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
      if (b2 <= 0.0f) {
        return false;
      }
      float felec = 0.0f;
      readSoAAs(soa_views::ElectronAbundance, i, felec);
      float fH2 = 0.0f;
      readSoAAs(soa_views::H2Abundance, i, fH2);
      const double mu = 1.0 / (1.0 + physics_constants::XHe + felec - fH2);
      out = static_cast<float>(8.0 * M_PI *
                               physics_constants::boltzmann_cgs *
                               p.temperature * p.density * mu / b2);
      return true;
    }
    case QuantityId::Metallicity:
      if (!hasQuantityAt(i, q)) return false;
      return readSoAAs(soa_views::Metallicity, i, out);
    case QuantityId::ElectronAbundance:
      if (!hasQuantityAt(i, q)) return false;
      return readSoAAs(soa_views::ElectronAbundance, i, out);
    case QuantityId::H2Abundance:
      if (!hasQuantityAt(i, q)) return false;
      return readSoAAs(soa_views::H2Abundance, i, out);
    case QuantityId::HDAbundance:
      if (!hasQuantityAt(i, q)) return false;
      return readSoAAs(soa_views::HDAbundance, i, out);
    case QuantityId::J21:
      if (!hasQuantityAt(i, q)) return false;
      return readSoAAs(soa_views::J21, i, out);
    case QuantityId::Val:
      if (!hasQuantityAt(i, q)) return false;
      return readSoAAs(soa_views::Val1, i, out);
    case QuantityId::Val2:
      if (!hasQuantityAt(i, q)) return false;
      return readSoAAs(soa_views::Val2, i, out);
    }
    return false;
  }

  float getQuantityOr(size_t i,
                      QuantityId q,
                      float fallback = 0.0f,
                      const float* center = nullptr,
                      const float* vcenter = nullptr) const
  {
    float out = fallback;
    return getQuantity(i, q, out, center, vcenter) ? out : fallback;
  }

  bool setQuantity(size_t i, QuantityId q, float value)
  {
    if (i >= particles.size()) {
      return false;
    }
    SimulationElement& p = particles[i];
    switch (q) {
    case QuantityId::Density:
      p.density = value;
      return true;
    case QuantityId::Temperature:
      p.temperature = value;
      return true;
    case QuantityId::Mass:
      p.mass = value;
      return true;
    case QuantityId::Hsml:
      p.supportRadius = value;
      return true;
    case QuantityId::Val:
      return writeSoAAs(soa_views::Val1, i, value);
    case QuantityId::Val2:
      return writeSoAAs(soa_views::Val2, i, value);
    default:
      return false;
    }
  }
  
  void rebuildQuantities();
  
public:
  struct BuildResult {
    float valueMin[kMaxQ][kNumTypes];
    float valueMax[kMaxQ][kNumTypes];
    float originalMax = 0.0f;
  };
  
  BuildResult rebuild(float desiredMax, const QuantityCatalogState& catalog);  
  static SimulationBlock makeTestSimulationBlock(HeaderInfo& header);

  bool ComputeAngularMomentumAxis(const ParticleSelectionOption& op,
                                  glm::vec3& outAxis) const;
};


inline bool getVectorValue(const SimulationBlock& blk, const SimulationElement& p, size_t ipart, VectorId v, float out[3]) {
  (void)p;
  return blk.getVector(ipart, v, out);
}

inline void setVectorValue(SimulationBlock& blk, SimulationElement& p, size_t ipart, VectorId v, const float in[3]) {
  (void)p;
  blk.setVector(ipart, v, in);
}

inline float getScalarValue(const SimulationBlock& blk, const SimulationElement& p, int ipart, QuantityId q, const float* center = nullptr, const float* vcenter = nullptr) {
  (void)p;
  float out = 0.0f;
  blk.getQuantity(static_cast<size_t>(ipart), q, out, center, vcenter);
  return out;
}

inline void setScalarValue(SimulationBlock& blk, SimulationElement& p, size_t ipart, QuantityId q, float x) {
  (void)p;
  blk.setQuantity(ipart, q, x);
}
