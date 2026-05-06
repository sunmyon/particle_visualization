#pragma once

#include <cstdint>
#include <cstring>

// ---- helpers (safe load for double/int64 if alignment is uncertain) ----
static inline float load_f32(const uint8_t* p) { float v; std::memcpy(&v, p, sizeof(v)); return v; }
static inline double load_f64(const uint8_t* p) { double v; std::memcpy(&v, p, sizeof(v)); return v; }
static inline int64_t load_i64(const uint8_t* p) { int64_t v; std::memcpy(&v, p, sizeof(v)); return v; }

// position
static inline void store_pos_f32(SimulationElement& p, const uint8_t* src){
  const float* v = reinterpret_cast<const float*>(src);
  p.position[0]=v[0]; p.position[1]=v[1]; p.position[2]=v[2];
}
static inline void store_pos_f64(SimulationElement& p, const uint8_t* src){
  double v0 = load_f64(src + 0*sizeof(double));
  double v1 = load_f64(src + 1*sizeof(double));
  double v2 = load_f64(src + 2*sizeof(double));
  p.position[0]=v0; p.position[1]=v1; p.position[2]=v2;
}
// velocity
static inline void store_vel_f32(SimulationElement& p, const uint8_t* src){
  const float* v = reinterpret_cast<const float*>(src);
  p.vel[0]=v[0]; p.vel[1]=v[1]; p.vel[2]=v[2];
}
static inline void store_vel_f64(SimulationElement& p, const uint8_t* src){
  double v0 = load_f64(src + 0*sizeof(double));
  double v1 = load_f64(src + 1*sizeof(double));
  double v2 = load_f64(src + 2*sizeof(double));
  p.vel[0]=(float)v0; p.vel[1]=(float)v1; p.vel[2]=(float)v2;
}
// scalar float/double -> float
static inline void store_mass_f32(SimulationElement& p, const uint8_t* src){ p.mass = *reinterpret_cast<const float*>(src); }
static inline void store_mass_f64(SimulationElement& p, const uint8_t* src){ p.mass = (float)load_f64(src); }
static inline void store_density_f32(SimulationElement& p, const uint8_t* src){ p.density = *reinterpret_cast<const float*>(src); }
static inline void store_density_f64(SimulationElement& p, const uint8_t* src){ p.density = (float)load_f64(src); }
static inline void store_temp_f32(SimulationElement& p, const uint8_t* src){ p.temperature = *reinterpret_cast<const float*>(src); }
static inline void store_temp_f64(SimulationElement& p, const uint8_t* src){ p.temperature = (float)load_f64(src); }
static inline void store_hsml_f32(SimulationElement& p, const uint8_t* src){ p.supportRadius = *reinterpret_cast<const float*>(src); }
static inline void store_hsml_f64(SimulationElement& p, const uint8_t* src){ p.supportRadius = (float)load_f64(src); }
static inline void store_volume_f32(SimulationElement& p, const uint8_t* src)
{
  const float vol = load_f32(src);
  p.supportRadius = cbrtf(vol);
}
static inline void store_volume_f64(SimulationElement& p, const uint8_t* src)
{
  const double vol = load_f64(src);
  p.supportRadius = (float)cbrt(vol);
}
static inline void store_type_i32(SimulationElement& p, const uint8_t* src){ p.type = *reinterpret_cast<const int32_t*>(src); }
static inline void store_type_i64(SimulationElement& p, const uint8_t* src){ p.type = (int)load_i64(src); }

static inline bool isAoSCoreFieldKey(FieldKey key) {
  switch (key) {
    case FieldKey::Position:
    case FieldKey::Velocity:
    case FieldKey::Mass:
    case FieldKey::Density:
    case FieldKey::Temperature:
    case FieldKey::Hsml:
    case FieldKey::Volume:
    case FieldKey::Type:
      return true;
    default:
      return false;
  }
}

static inline DestKind getDestKind(FieldKey key, bool flag_hdf5 = false) {
  if (key == FieldKey::Dummy)
    return DestKind::Ignore;

  if (flag_hdf5 && key == FieldKey::Type)
    return DestKind::Ignore;

  switch (key) {
    case FieldKey::Bfield:
    case FieldKey::Metallicity:
    case FieldKey::ElectronAbundance:
    case FieldKey::H2Abundance:
    case FieldKey::HDAbundance:
    case FieldKey::J21:
    case FieldKey::Gamma:
    case FieldKey::Value:
    case FieldKey::Value2:
    case FieldKey::ID:
      return DestKind::SoA;

    default:
      return isAoSCoreFieldKey(key) ? DestKind::AoSCore : DestKind::Ignore;
  }
}

static inline const char* getSoAKey(FieldKey key) {
  switch (key) {
    case FieldKey::Bfield:            return "Bfield";
    case FieldKey::Metallicity:       return "Metallicity";
    case FieldKey::ElectronAbundance: return "ElectronAbundance";
    case FieldKey::H2Abundance:       return "H2Abundance";
    case FieldKey::HDAbundance:       return "HDAbundance";
    case FieldKey::J21:               return "J21";
    case FieldKey::Gamma:             return "Gamma";
    case FieldKey::Value:             return "Val1";
    case FieldKey::Value2:            return "Val2";
    case FieldKey::ID:                return kParticleIdKey;
    default:                          return nullptr;
  }
}

template<class T>
static inline void assignCore(SimulationElement& p, FieldKey ft, const T* v){
  switch(ft){
    case FieldKey::Position:  for(int k=0;k<3;k++){ p.position[k]=float(v[k]); } break;
    case FieldKey::Velocity:  for(int k=0;k<3;k++){ p.vel[k]=float(v[k]); } break;
    case FieldKey::Mass:      p.mass=float(v[0]); break;
    case FieldKey::Density:   p.density=float(v[0]); break;
    case FieldKey::Temperature: p.temperature=float(v[0]); break;
    case FieldKey::Hsml:      p.supportRadius=float(v[0]); break;
    case FieldKey::Volume:    p.supportRadius=float(cbrtf(v[0])); break;
    case FieldKey::Type:      p.type=int(v[0]); break;
    default: break;
  }
}

static inline BinaryReadLayout buildBinaryReadLayout(const std::vector<FieldSpec>& tokens,
                                                     bool flag_hdf5 = false) {
  BinaryReadLayout rl;
  rl.fields.reserve(tokens.size());

  size_t off = 0;
  for (const auto& tok : tokens) {
    if (tok.count < 0) throw std::runtime_error("token.count must be >= 0");
    if (tok.count == 0) continue;

    FieldLayout fl;
    fl.offset = static_cast<int>(off);
    fl.spec   = tok;
    fl.ftype  = tok.key;
    fl.dest   = getDestKind(tok.key, flag_hdf5);

    if (fl.dest == DestKind::SoA) {
      fl.soaKey = getSoAKey(tok.key);
    }

    if (fl.dest == DestKind::AoSCore) {
      const bool isF32 = (tok.type == DataType::Float);
      const bool isF64 = (tok.type == DataType::Double);
      const bool isI32 = (tok.type == DataType::Int32);
      const bool isI64 = (tok.type == DataType::Int64);

      switch (tok.key) {
        case FieldKey::Position:
          fl.store = isF32 ? &store_pos_f32 : (isF64 ? &store_pos_f64 : nullptr);
          break;
        case FieldKey::Velocity:
          fl.store = isF32 ? &store_vel_f32 : (isF64 ? &store_vel_f64 : nullptr);
          break;
        case FieldKey::Mass:
          fl.store = isF32 ? &store_mass_f32 : (isF64 ? &store_mass_f64 : nullptr);
          break;
        case FieldKey::Density:
          fl.store = isF32 ? &store_density_f32 : (isF64 ? &store_density_f64 : nullptr);
          break;
        case FieldKey::Temperature:
          fl.store = isF32 ? &store_temp_f32 : (isF64 ? &store_temp_f64 : nullptr);
          break;
        case FieldKey::Hsml:
          fl.store = isF32 ? &store_hsml_f32 : (isF64 ? &store_hsml_f64 : nullptr);
          break;
        case FieldKey::Volume:
          fl.store = isF32 ? &store_volume_f32 : (isF64 ? &store_volume_f64 : nullptr);
          break;
        case FieldKey::Type:
          fl.store = isI32 ? &store_type_i32 : (isI64 ? &store_type_i64 : nullptr);
          break;
        default:
          fl.store = nullptr;
          break;
      }
    }

    rl.fields.push_back(fl);
    off += dataTypeSize(tok.type) * static_cast<size_t>(tok.count);
  }

  rl.recordSize = off;
  return rl;
}



template<class T>
static inline void writeFieldToSimulationBlock(SimulationBlock& out, size_t i,
					     const FieldLayout& fl,
					     const T* vals)
{
  if(fl.dest == DestKind::Ignore) return;

  const auto& fs = fl.spec;

  if(fl.dest == DestKind::AoSCore){
    assignCore(out.particles[i], fl.ftype, vals);
    return;
  }

  // reserved for future extended AoS fields
  /*if(fl.dest == DestKind::AoSExt){
    if(out.aosExt.stride==0) return;
    uint8_t* dst = out.aosExt.ptr(i) + fl.aosExtOffset;
    std::memcpy(dst, vals, (size_t)fs.count * sizeof(T));
    return;
    }*/

  // SoA
  auto &f = out.soa[fl.soaKey];
  std::memcpy(f.ptr(i), vals, (size_t)fs.count * sizeof(T));
}
