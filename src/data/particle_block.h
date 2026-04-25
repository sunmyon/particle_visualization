#pragma once

#include <unordered_map>
#include <array>

#include "particle_data.h"
#include "core/physics_constants.h"
#include "core/quantity.h"
#include "core/tracking_vector.h"

struct HeaderInfo;
struct ParticleSelectionOption;

struct ParticleBlock {
  // ---- AoS core ----
  TrackingVector<ParticleData> particles;

  // ---- AoS extension (optional) ----
  AoSExtensionBuffer aosExt;

  // ---- SoA fields (optional) ----
  // key = field label (e.g. "Bfield")
  std::unordered_map<std::string, SoAField> soa;

  std::unordered_map<uint64_t, size_t> id2index;
  bool id2indexDirty = true;
  
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
    id2index.clear();
    id2indexDirty = true;
  }

  void rebuildIdIndex()
  {
    id2index.clear();
    id2index.reserve(particles.size() * 13 / 10);
    for (size_t i=0;i<particles.size();++i) {
      // ParticleData::ID は int なので uint64 化
      id2index[(uint64_t)(int64_t)particles[i].ID] = i;
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
  
  void rebuildQuantities();
  
public:
  struct BuildResult {
    float valueMin[kMaxQ][kNumTypes];
    float valueMax[kMaxQ][kNumTypes];
    float originalMax = 0.0f;
  };
  
  BuildResult rebuild(float desiredMax, const QuantityCatalogState& catalog);  
  static ParticleBlock makeTestParticleBlock(HeaderInfo& header);

  bool ComputeAngularMomentumAxis(const ParticleSelectionOption& op,
                                  glm::vec3& outAxis) const;
};


inline bool getVectorValue(const ParticleBlock& blk, const ParticleData& p, size_t ipart, VectorId v, float out[3]) {
  switch (v) {
    case VectorId::OriginalPos: {
      out[0]=p.original_pos[0]; out[1]=p.original_pos[1]; out[2]=p.original_pos[2]; return true;
    }
    case VectorId::Pos: {
      out[0]=p.pos[0]; out[1]=p.pos[1]; out[2]=p.pos[2]; return true;
    }
    case VectorId::Vel: {
      out[0]=p.vel[0]; out[1]=p.vel[1]; out[2]=p.vel[2]; return true;
    }
    case VectorId::Bfield: {
      float B[3];
      out[0] = out[1] = out[2] = 0.;
      if(blk.readSoAAs(soa_views::Bfield, (size_t)ipart, B)){
	out[0]=B[0];out[1]=B[1];out[2]=B[2];
      }
      return true;
    }
  }
  return false;
}

inline void setVectorValue(ParticleBlock& blk, ParticleData& p, size_t ipart, VectorId v, const float in[3]) {
  switch (v) {
  case VectorId::OriginalPos:
    p.original_pos[0]=in[0]; p.original_pos[1]=in[1]; p.original_pos[2]=in[2]; 
    return;
  case VectorId::Pos:
    p.pos[0]=in[0]; p.pos[1]=in[1]; p.pos[2]=in[2]; 
    return;
  case VectorId::Vel:
    p.vel[0]=in[0]; p.vel[1]=in[1]; p.vel[2]=in[2];
    return;
  case VectorId::Bfield: {
    float tmp[3] = {in[0], in[1], in[2]};
    blk.writeSoAAs(soa_views::Bfield, ipart, tmp);
    return;
  }
  }
}

inline float getScalarValue(const ParticleBlock& blk, const ParticleData& p, int ipart, QuantityId q, const float* center = nullptr, const float* vcenter = nullptr) {
  switch (q) {
  case QuantityId::Density:     return p.density;
  case QuantityId::Temperature: return p.temperature;
  case QuantityId::Mass:
    return p.mass;  // ←あなたの ParticleData に合わせて修正

  case QuantityId::Hsml:
    return p.Hsml;

  case QuantityId::PosX:        return p.original_pos[0]; // or p.original_pos[0]
  case QuantityId::PosY:        return p.original_pos[1];
  case QuantityId::PosZ:        return p.original_pos[2];

  case QuantityId::Radius: {
    const float cx = center ? center[0] : 0.0f;
    const float cy = center ? center[1] : 0.0f;
    const float cz = center ? center[2] : 0.0f;

    const float dx = p.original_pos[0] - cx;   // 必要なら original_pos に
    const float dy = p.original_pos[1] - cy;
    const float dz = p.original_pos[2] - cz;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
  }

  case QuantityId::VRad: {
    const float cx = center ? center[0] : 0.0f;
    const float cy = center ? center[1] : 0.0f;
    const float cz = center ? center[2] : 0.0f;

    const float vcx = vcenter ? vcenter[0] : 0.0f;
    const float vcy = vcenter ? vcenter[1] : 0.0f;
    const float vcz = vcenter ? vcenter[2] : 0.0f;

    const float dx = p.original_pos[0] - cx;
    const float dy = p.original_pos[1] - cy;
    const float dz = p.original_pos[2] - cz;
      
    const float dvx = (p.original_pos[0] - cx) * (p.vel[0] - vcx);
    const float dvy = (p.original_pos[1] - cy) * (p.vel[1] - vcy);
    const float dvz = (p.original_pos[2] - cz) * (p.vel[2] - vcz);
    return (dvx + dvy + dvz)/sqrt(dx*dx + dy*dy + dz*dz);
  }

  case QuantityId::B: {
    float B[3];
    if (!blk.readSoAAs(soa_views::Bfield, (size_t)ipart, B)) return 0.0f;
    return std::sqrt(B[0]*B[0] + B[1]*B[1] + B[2]*B[2]);
  }
  case QuantityId::Beta: {
    float B[3];
    if (!blk.readSoAAs(soa_views::Bfield, (size_t)ipart, B)) return 0.0f;

    const float B2 = B[0]*B[0] + B[1]*B[1] + B[2]*B[2];
    if (B2 <= 0.0f) return 0.0f;

    float felec = 0.0f;
    blk.readSoAAs(soa_views::ElectronAbundance, (size_t)ipart, felec);

    float fH2 = 0.0f;
    blk.readSoAAs(soa_views::H2Abundance, (size_t)ipart, fH2);

    const double mu = 1.0 / (1.0 + physics_constants::XHe + felec - fH2);
    const float beta = (float)(
			       8.0 * M_PI * physics_constants::boltzmann_cgs * p.temperature * p.density * mu / B2
			       );
    return beta;
  }
  case QuantityId::Metallicity: {
    float z;
    if (!blk.readSoAAs(soa_views::Metallicity, (size_t)ipart, z)) return 0.0f;
    return z;
  }
  case QuantityId::ElectronAbundance: {
    float z;
    if (!blk.readSoAAs(soa_views::ElectronAbundance, (size_t)ipart, z)) return 0.0f;
    return z;
  }
  case QuantityId::H2Abundance: {
    float z;
    if (!blk.readSoAAs(soa_views::H2Abundance, (size_t)ipart, z)) return 0.0f;
    return z;
  }
  case QuantityId::HDAbundance: {
    float z;
    if (!blk.readSoAAs(soa_views::HDAbundance, (size_t)ipart, z)) return 0.0f;
    return z;
  }
  case QuantityId::J21: {
    float z;
    if (!blk.readSoAAs(soa_views::J21, (size_t)ipart, z)) return 0.0f;
    return z;
  }
  case QuantityId::Val: {
    float z;
    if (!blk.readSoAAs(soa_views::Val1, (size_t)ipart, z)) return 0.0f;
    return z;
  }
  case QuantityId::Val2: {
    float z;
    if (!blk.readSoAAs(soa_views::Val2, (size_t)ipart, z)) return 0.0f;
    return z;
  }
      
  }
  return 0.0f;
}

inline void setScalarValue(ParticleBlock& blk, ParticleData& p, size_t ipart, QuantityId q, float x) {
  switch (q) {
    case QuantityId::Density:
      p.density = x;
      return;
    case QuantityId::Temperature:
      p.temperature = x;
      return;
    case QuantityId::Mass:
      p.mass = x;
      return;
    case QuantityId::Hsml:
      p.Hsml = x;
      return;
    case QuantityId::Val:
      blk.writeSoAAs(soa_views::Val1, ipart, x);
      return;
    case QuantityId::Val2:
      blk.writeSoAAs(soa_views::Val2, ipart, x);
      return;
    default:
      // Radius/VRad/B など “派生量” は set 不可でOK（Bridgeでdirty対象にしない）
      return;
  }
}
