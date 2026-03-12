#pragma once
// POSIX I/O 用
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <unordered_map>
#include <filesystem>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <fstream>
#include "PerfTimer.h"

enum class FileFormat {
  Auto,       // 拡張子から自動判別 (従来の動作)
  HDF5,
  Binary,
  Gadget,
  Framed,      // Fortran‐style framed binary
  _Count
};


struct FieldSpec {
  std::string label;        // "position" "velocity" "Bfield" ...
  DataType type;
  int count;                // 1 or 3 etc
  std::string displayName;  // HDF5 dataset 名 or Binary token 名

  inline static const char* candidatePosNames         = "Coordinates";
  inline static const char* candidateVelNames         = "Velocities";
  inline static const char* candidateBfieldNames      = "MagneticField";
  inline static const char* candidateMassNames        = "Masses";
  inline static const char* candidateIDNames          = "ParticleIDs";
  inline static const char* candidateDensityNames     = "Density";
  inline static const char* candidateTemperatureNames = "Temperature";
  inline static const char* candidateElecNames        = "ElectronAbundance";
  inline static const char* candidateH2INames         = "H2IAbundance";
  inline static const char* candidateHDINames         = "HDIAbundance";
  inline static const char* candidateJ21Names         = "LymanWerner";
  inline static const char* candidateGammaNames       = "Gamma";
  inline static const char* candidateInternalEnergyNames = "InternalEnergy";
  inline static const char* candidateMetallicityNames = "Metallicity";
  inline static const char* candidateValNames         = "Metallicity";
  inline static const char* candidateVal2Names        = "ElectronAbundance";
  inline static const char* candidateVolumeNames      = "Volume";
  
  static void SetDefaultDisplayName(FieldSpec& tok) {
    if      (tok.label == "position"        ) tok.displayName = candidatePosNames;
    else if (tok.label == "velocity"        ) tok.displayName = candidateVelNames;
    else if (tok.label == "Bfield"          ) tok.displayName = candidateBfieldNames;
    else if (tok.label == "mass"            ) tok.displayName = candidateMassNames;
    else if (tok.label == "ID"              ) tok.displayName = candidateIDNames;
    else if (tok.label == "density"         ) tok.displayName = candidateDensityNames;
    else if (tok.label == "temperature"     ) tok.displayName = candidateTemperatureNames;
    else if (tok.label == "H2Abundance"     ) tok.displayName = candidateH2INames;
    else if (tok.label == "HDAbundance"     ) tok.displayName = candidateHDINames;    
    else if (tok.label == "ElectronAbundance") tok.displayName = candidateElecNames;
    else if (tok.label == "J21"             ) tok.displayName = candidateJ21Names;
    else if (tok.label == "Gamma"           ) tok.displayName = candidateGammaNames;    
    else if (tok.label == "internalenergy"  ) tok.displayName = candidateInternalEnergyNames;
    else if (tok.label == "Metallicity"     ) tok.displayName = candidateMetallicityNames;
    else if (tok.label == "value"           ) tok.displayName = candidateValNames;
    else if (tok.label == "value2"          ) tok.displayName = candidateVal2Names;
    else if (tok.label == "Volume"          ) tok.displayName = candidateVolumeNames;
    else    tok.displayName = tok.label;    
  }
};

static inline bool isAoSCoreLabel(const std::string& lbl){
  return (lbl == "position" ||
          lbl == "velocity" ||
          lbl == "mass" ||
          lbl == "density" ||
          lbl == "temperature" ||
          lbl == "Hsml" ||
	  lbl == "Volume" ||
          lbl == "type" ||
          lbl == "ID");
}

enum class FieldType {
  Position,
  Velocity,
  Bfield,
  Hsml,
  Volume,
  Mass,
  Density,
  Temperature,
  Value,
  Value2,
  Type,
  ID,
  Dummy,
  InternalEnergy,
  ElectronFraction,
  H2Fraction,
  Gamma,
  Unknown
};


enum class DestKind { AoSCore, AoSExt, SoA, Ignore };

struct FieldLayout{
  FieldSpec spec;
  int offset;

  DestKind dest = DestKind::Ignore;
  size_t aosExtOffset = 0;
  const std::string* soaKey = nullptr;

  bool present = false;
  
  FieldType ftype = FieldType::Unknown;
  void (*store)(ParticleData& p, const uint8_t* src) = nullptr;
};

struct FieldPlanItem {
  DestKind dest = DestKind::Ignore;
  size_t aosExtOffset = 0;          // AoSExt のとき
  std::string soaKey;               // SoA のとき（例 "Bfield"）
  DataType type{};
  int count = 1;
};

struct IOPlan {
  std::unordered_map<std::string, FieldPlanItem> plan; // label->item
  size_t aosExtStride = 0;                             // AoSExt stride
};

static IOPlan buildPlanFromToks(const std::vector<FieldSpec>& toks) {
  IOPlan plan;

  for (const auto& fs : toks) {
    FieldPlanItem item;
    item.type  = fs.type;
    item.count = fs.count;

    // 例：Bfield は SoA
    if (fs.label == "Bfield") {
      item.dest = DestKind::SoA;
      item.soaKey = "Bfield";
    }
    else if (fs.label == "Metallicity") {
      item.dest = DestKind::SoA;
      item.soaKey = "Metallicity";
    }
    else if (fs.label == "ElectronAbundance") {
      item.dest = DestKind::SoA;
      item.soaKey = "ElectronAbundance";
    }
    else if (fs.label == "H2Abundance") {
      item.dest = DestKind::SoA;
      item.soaKey = "H2Abundance";
    }
    else if (fs.label == "HDAbundance") {
      item.dest = DestKind::SoA;
      item.soaKey = "HDAbundance";
    }
    else if (fs.label == "J21") {
      item.dest = DestKind::SoA;
      item.soaKey = "J21";
    }
    else if (fs.label == "value") {
      item.dest = DestKind::SoA;
      item.soaKey = "Val1";
    }
    else if (fs.label == "value2") {
      item.dest = DestKind::SoA;
      item.soaKey = "Val2";
    }
    // それ以外は AoSCore（ParticleData に入れる）か Ignore
    else if (isAoSCoreLabel(fs.label)) {
      item.dest = DestKind::AoSCore;
    }
    else {
      // 将来の未知フィールドは、とりあえず読まない（Ignore）
      // 「未知も保持したい」なら SoA に落とすのが一番簡単
      item.dest = DestKind::Ignore;
    }

    plan.plan[fs.label] = std::move(item);
  }

  return plan;
}

// ---- AoSCoreへの代入（最低限。あなたの assignField をここに集約してもOK）----
template<class T>
static inline void assignCore(ParticleData& p, const std::string& label, const T* v){
  if(label=="position"){
    for(int k=0;k<3;k++){ p.original_pos[k]=float(v[k]);}
  } else if(label=="velocity"){
    for(int k=0;k<3;k++){ p.vel[k]=float(v[k]); }
  } else if(label=="mass"){
    p.mass=float(v[0]);
  } else if(label=="density"){
    p.density=float(v[0]);
  } else if(label=="temperature"){
    p.temperature=float(v[0]);
  } else if(label=="Hsml"){
    p.originalHsml=float(v[0]);
  }else if(label=="Volume"){
    p.originalHsml=float(cbrtf(v[0]));
  } else if(label=="type"){
    p.type=int(v[0]);
  } else if(label=="ID"){
    p.ID=int(v[0]);
  }
}

// ---- helpers (safe load for double/int64 if alignment is uncertain) ----
static inline float load_f32(const uint8_t* p) { float v; std::memcpy(&v, p, sizeof(v)); return v; }
static inline double load_f64(const uint8_t* p) { double v; std::memcpy(&v, p, sizeof(v)); return v; }
static inline int64_t load_i64(const uint8_t* p) { int64_t v; std::memcpy(&v, p, sizeof(v)); return v; }

// position
static inline void store_pos_f32(ParticleData& p, const uint8_t* src){
  const float* v = reinterpret_cast<const float*>(src);
  p.original_pos[0]=v[0]; p.original_pos[1]=v[1]; p.original_pos[2]=v[2];
}
static inline void store_pos_f64(ParticleData& p, const uint8_t* src){
  // 3連続double（安全に memcpy でもOK）
  double v0 = load_f64(src + 0*sizeof(double));
  double v1 = load_f64(src + 1*sizeof(double));
  double v2 = load_f64(src + 2*sizeof(double));
  p.original_pos[0]=v0; p.original_pos[1]=v1; p.original_pos[2]=v2;
}

// velocity
static inline void store_vel_f32(ParticleData& p, const uint8_t* src){
  const float* v = reinterpret_cast<const float*>(src);
  p.vel[0]=v[0]; p.vel[1]=v[1]; p.vel[2]=v[2];
}
static inline void store_vel_f64(ParticleData& p, const uint8_t* src){
  double v0 = load_f64(src + 0*sizeof(double));
  double v1 = load_f64(src + 1*sizeof(double));
  double v2 = load_f64(src + 2*sizeof(double));
  p.vel[0]=(float)v0; p.vel[1]=(float)v1; p.vel[2]=(float)v2;
}

// scalar float/double -> float
static inline void store_mass_f32(ParticleData& p, const uint8_t* src){ p.mass = *reinterpret_cast<const float*>(src); }
static inline void store_mass_f64(ParticleData& p, const uint8_t* src){ p.mass = (float)load_f64(src); }

static inline void store_density_f32(ParticleData& p, const uint8_t* src){ p.density = *reinterpret_cast<const float*>(src); }
static inline void store_density_f64(ParticleData& p, const uint8_t* src){ p.density = (float)load_f64(src); }

static inline void store_temp_f32(ParticleData& p, const uint8_t* src){ p.temperature = *reinterpret_cast<const float*>(src); }
static inline void store_temp_f64(ParticleData& p, const uint8_t* src){ p.temperature = (float)load_f64(src); }

static inline void store_hsml_f32(ParticleData& p, const uint8_t* src){ p.originalHsml = *reinterpret_cast<const float*>(src); }
static inline void store_hsml_f64(ParticleData& p, const uint8_t* src){ p.originalHsml = (float)load_f64(src); }

static inline void store_volume_f32(ParticleData& p, const uint8_t* src)
{
  const float vol = load_f32(src);      // src から float を読む
  p.originalHsml = cbrtf(vol);          // 3乗根
}

static inline void store_volume_f64(ParticleData& p, const uint8_t* src)
{
  const double vol = load_f64(src);     // src から double を読む
  p.originalHsml = (float)cbrt(vol);    // double版cbrt
}

static inline void store_type_i32(ParticleData& p, const uint8_t* src){ p.type = *reinterpret_cast<const int32_t*>(src); }
static inline void store_type_i64(ParticleData& p, const uint8_t* src){ p.type = (int)load_i64(src); }

static inline void store_id_i32(ParticleData& p, const uint8_t* src){ p.ID = *reinterpret_cast<const int32_t*>(src); }
static inline void store_id_i64(ParticleData& p, const uint8_t* src){ p.ID = (int)load_i64(src); }

template<class T>
static inline void assignCoreFT(ParticleData& p, FieldType ft, const T* v){
  switch(ft){
    case FieldType::Position:  for(int k=0;k<3;k++){ p.original_pos[k]=float(v[k]); } break;
    case FieldType::Velocity:  for(int k=0;k<3;k++){ p.vel[k]=float(v[k]); } break;
    case FieldType::Mass:      p.mass=float(v[0]); break;
    case FieldType::Density:   p.density=float(v[0]); break;
    case FieldType::Temperature: p.temperature=float(v[0]); break;
    case FieldType::Hsml:      p.originalHsml=float(v[0]); break;
    case FieldType::Volume:    p.originalHsml=float(cbrtf(v[0])); break;
    case FieldType::Type:      p.type=int(v[0]); break;
    case FieldType::ID:        p.ID=int(v[0]); break;
    default: break;
  }
}


struct MaskConfig {
  // sphere
  bool   enableSphere = false;
  double center[3] = {0,0,0};
  double radius = 0.0;

  enum class OutsideMode { Drop, Thin, KeepAll };
  OutsideMode outsideMode = OutsideMode::Drop;
  uint64_t outsideStride = 10;

  // type policy
  enum class ThinPolicy : uint8_t { ReadOff, ReadOn_NoThin, ReadOn_ThinOK };
  ThinPolicy typePolicy[6] = {
    ThinPolicy::ReadOn_ThinOK,
    ThinPolicy::ReadOn_ThinOK,
    ThinPolicy::ReadOn_NoThin,
    ThinPolicy::ReadOn_NoThin,
    ThinPolicy::ReadOn_NoThin,
    ThinPolicy::ReadOn_NoThin
  };

  // max particles (ID thinning)
  bool   enableMaxParticles = false;
  size_t maxParticles = 0;
};

struct CoreSample {
  double   pos[3];
  uint64_t id;
  int      type; // 0..5 (HDF5なら ptype)
};

class ParticleMask {
public:
  explicit ParticleMask(MaskConfig cfg): cfg_(cfg){ rebuildNeeds_(); }

  bool needPos() const { return need_pos_; }
  bool needID()  const { return need_id_; }

  bool typeEnabled(int t) const {
    return cfg_.typePolicy[t] != MaskConfig::ThinPolicy::ReadOff;
  }
  bool typeThinOK(int t) const {
    return cfg_.typePolicy[t] == MaskConfig::ThinPolicy::ReadOn_ThinOK;
  }

  // thin 候補数（= ThinOK の粒子数）から stride を決める
  void prepare(size_t thinCandidates){
    id_stride_ = 1;
    if(cfg_.enableMaxParticles && cfg_.maxParticles>0 && thinCandidates>cfg_.maxParticles){
      id_stride_ = (uint64_t)((thinCandidates + cfg_.maxParticles - 1) / cfg_.maxParticles);
    }
  }

  bool pass(const CoreSample& c) const {
    const int t = c.type;
    if(t<0 || t>5) return false;

    // 完全 on/off
    if(!typeEnabled(t)) return false;

    // sphere
    if(cfg_.enableSphere){
      const double dx = c.pos[0]-cfg_.center[0];
      const double dy = c.pos[1]-cfg_.center[1];
      const double dz = c.pos[2]-cfg_.center[2];
      const double r2 = cfg_.radius*cfg_.radius;
      const bool inside = (dx*dx+dy*dy+dz*dz) <= r2;

      if(!inside){
        if(cfg_.outsideMode == MaskConfig::OutsideMode::Drop) return false;
        if(cfg_.outsideMode == MaskConfig::OutsideMode::Thin){
          if(!keep_by_stride_(c.id, cfg_.outsideStride)) return false;
        }
      }
    }

    // maxParticles thin（ThinOK の type だけ対象）
    if(cfg_.enableMaxParticles && id_stride_>1 && typeThinOK(t)){
      if(!keep_by_stride_(c.id, id_stride_)) return false;
    }

    return true;
  }

  bool active() const {
    if (cfg_.enableSphere) return true;
    if (cfg_.enableMaxParticles && cfg_.maxParticles > 0) return true;
    // type off が一つでもあれば active
    for (int t=0; t<6; ++t) {
      if (cfg_.typePolicy[t] == MaskConfig::ThinPolicy::ReadOff) return true;
    }
    // sphereがONのときのみoutsideModeが意味を持つのでここでは見ない
    return false;
  }
  
  uint64_t idStride() const { return id_stride_; }
  const MaskConfig& config() const { return cfg_; }

private:
  MaskConfig cfg_;
  uint64_t id_stride_ = 1;

  bool need_pos_ = false;
  bool need_id_  = false;

  void rebuildNeeds_(){
    need_pos_ = cfg_.enableSphere;
    const bool need_id_for_outside = (cfg_.enableSphere && cfg_.outsideMode==MaskConfig::OutsideMode::Thin);
    const bool need_id_for_limit   = (cfg_.enableMaxParticles && cfg_.maxParticles>0);
    need_id_ = need_id_for_outside || need_id_for_limit;
  }

  static inline uint64_t splitmix64_(uint64_t x){
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
  }
  static inline bool keep_by_stride_(uint64_t id, uint64_t stride){
    if(stride<=1) return true;
    return (splitmix64_(id) % stride) == 0;
  }
};

class IParticleReader {
public:
  virtual ~IParticleReader() = default;

  virtual bool open(const std::string& path, HeaderInfo& header) = 0;
  virtual void close() = 0;

  virtual size_t particleCount() const = 0;

  virtual bool readRange(ParticleBlock& out,
                         size_t begin, size_t count,
                         const std::vector<FieldSpec>& fields,
                         const IOPlan& plan) = 0;

  // ★追加：mask付き読み込み（デフォルトは未対応）
  virtual bool readRangeMasked(ParticleBlock& out,
                               size_t begin, size_t count,
                               const std::vector<FieldSpec>& fields,
                               const IOPlan& plan,
                               ParticleMask& mask)
  {
    (void)out; (void)begin; (void)count; (void)fields; (void)plan; (void)mask;
    return false;
  }
  
  virtual bool is_binary() { return false; };
  
  // 便利関数：全読み
  bool readAll(ParticleBlock& out,
               const std::vector<FieldSpec>& fields,
               const IOPlan& plan)
  {
    return readRange(out, 0, particleCount(), fields, plan);
  }

  bool tryFixAndCheckBinary(const std::string& fullPath, HeaderInfo& hdr, std::vector<FieldSpec>& formatTokens){
    if(is_binary() == false){
      return true;
    }

    const int iter_max = 20;    
      
    auto testTokens = formatTokens;
    for(int iter=0;iter<iter_max;iter++){	
      IOPlan plan = buildPlanFromToks(testTokens);      
      bool flag_success = check(fullPath, hdr, testTokens, plan);
      if(flag_success){
	formatTokens = std::move(testTokens);
	return true;
      }

      bool has_dummy = false;
      for(size_t i=0;i<testTokens.size();i++){
	if(testTokens[i].label == "dummy"){
	  has_dummy = true;
	  if(iter == 0){
	    testTokens[i].count = 0;
	    break;
	  }else{
	    testTokens[i].count++;
	    break;
	  }	    
	}
      }

      if (!has_dummy) {
	std::cerr << "There is no label named dummy. No room for the adjustment\n";
	return false;
      }

      printf("iter=%d failed to read the file...\n", iter);	
    }

    printf("Too many iterations. Fialed to read the file %s\n", fullPath.c_str());
    return false;    
  }
  
  bool check(const std::string& path, HeaderInfo& header,
             const std::vector<FieldSpec>& fields,
             const IOPlan& plan,
             size_t ncheck = 100)
  {
    if (!open(path, header)) return false;

    const size_t n = std::min(ncheck, particleCount());
    ParticleBlock blk;
    bool ok = readRange(blk, 0, n, fields, plan);

    if (ok) {
      for (size_t i = 0; i < n; ++i) {
        const auto &p = blk.particles[i];
        if (p.type < 0 || p.type > 5) {
          ok = false;
          printf("Why? P[%zu].type = %d\n", i, p.type);
          break;
        }
      }
    }

    close();
    return ok;
  }

};


// 2) ラベル文字列 → FieldType 変換マップ
static const std::unordered_map<std::string,FieldType> labelToField = {
  {"position",         FieldType::Position},
  {"velocity",         FieldType::Velocity},
  {"Bfield",           FieldType::Bfield},
  {"Hsml",             FieldType::Hsml},
  {"Volume",           FieldType::Volume},
  {"mass",             FieldType::Mass},
  {"density",          FieldType::Density},
  {"temperature",      FieldType::Temperature},
  {"value",            FieldType::Value},
  {"value2",           FieldType::Value2},
  {"type",             FieldType::Type},
  {"ID",               FieldType::ID},
  {"dummy",            FieldType::Dummy},
  {"internalenergy",   FieldType::InternalEnergy},
  {"electronfraction", FieldType::ElectronFraction},
  {"H2fraction",       FieldType::H2Fraction},
  {"Gamma",            FieldType::Gamma}
};


// record 全体の配置情報
struct RecordLayout {
  size_t recordSize = 0;
  std::vector<FieldLayout> fields;
};

static inline RecordLayout buildRecordLayout(const std::vector<FieldSpec>& tokens,
                                             const IOPlan& plan, bool flag_hdf5=false) {
  RecordLayout rl;
  rl.fields.reserve(tokens.size());

  size_t off = 0;
  for (const auto& tok : tokens) {
    if (tok.count < 0) throw std::runtime_error("token.count must be >= 0");
    if (tok.count == 0) continue;

    FieldLayout fl;
    fl.offset = (int)off;
    fl.spec   = tok;

    auto it = plan.plan.find(tok.label);
    if (it != plan.plan.end()) {
      fl.dest = it->second.dest;
      fl.aosExtOffset = it->second.aosExtOffset;
      if (it->second.dest == DestKind::SoA) fl.soaKey = &it->second.soaKey;
    } else {
      fl.dest = DestKind::Ignore;
    }
    if(flag_hdf5){
      if(tok.label == "type")
	fl.dest = DestKind::Ignore;
    }

    auto itft = labelToField.find(tok.label);
    fl.ftype = (itft==labelToField.end()) ? FieldType::Unknown : itft->second;

    if (fl.dest == DestKind::AoSCore) {
      const bool isF32 = (tok.type == DataType::Float);
      const bool isF64 = (tok.type == DataType::Double);
      const bool isI32 = (tok.type == DataType::Int32);
      const bool isI64 = (tok.type == DataType::Int64);

      switch (fl.ftype) {
      case FieldType::Position:
	fl.store = isF32 ? &store_pos_f32 : (isF64 ? &store_pos_f64 : nullptr);
	break;
      case FieldType::Velocity:
	fl.store = isF32 ? &store_vel_f32 : (isF64 ? &store_vel_f64 : nullptr);
	break;
      case FieldType::Mass:
	fl.store = isF32 ? &store_mass_f32 : (isF64 ? &store_mass_f64 : nullptr);
	break;
      case FieldType::Density:
	fl.store = isF32 ? &store_density_f32 : (isF64 ? &store_density_f64 : nullptr);
	break;
      case FieldType::Temperature:
	fl.store = isF32 ? &store_temp_f32 : (isF64 ? &store_temp_f64 : nullptr);
	break;
      case FieldType::Hsml:
	fl.store = isF32 ? &store_hsml_f32 : (isF64 ? &store_hsml_f64 : nullptr);
	break;
      case FieldType::Volume:
	fl.store = isF32 ? &store_volume_f32 : (isF64 ? &store_volume_f64 : nullptr);
	break;
      case FieldType::Type:
	fl.store = isI32 ? &store_type_i32 : (isI64 ? &store_type_i64 : nullptr);
	break;
      case FieldType::ID:
	fl.store = isI32 ? &store_id_i32 : (isI64 ? &store_id_i64 : nullptr);
	break;
      default:
	fl.store = nullptr;
	break;
      }
    }
    
    rl.fields.push_back(fl);
    off += dataTypeSize(tok.type) * (size_t)tok.count;
  }

  rl.recordSize = off;
  return rl;
}


template<class T>
static inline void writeToDest(ParticleBlock& out, size_t i,
                               const FieldSpec& fs, const IOPlan& plan,
                               const T* vals)
{
  auto it = plan.plan.find(fs.label);
  if(it==plan.plan.end()) return;
  const auto& pi = it->second;

  if(pi.dest==DestKind::Ignore) return;

  if(pi.dest==DestKind::AoSCore){
    assignCore(out.particles[i], fs.label, vals);
    return;
  }

  if(pi.dest==DestKind::AoSExt){
    if(out.aosExt.stride==0) return;
    uint8_t* dst = out.aosExt.ptr(i) + pi.aosExtOffset;
    std::memcpy(dst, vals, (size_t)fs.count * sizeof(T));
    return;
  }

  if(pi.dest==DestKind::SoA){
    auto &f = out.soa[pi.soaKey];
    if(f.bytes.empty()){
      f.type = fs.type;
      f.comps = fs.count;
      f.resize(out.particles.size());
    }
    std::memcpy(f.ptr(i), vals, (size_t)fs.count * sizeof(T));
    return;
  }
}

template<class T>
static inline void writeToDestFast(ParticleBlock& out, size_t i,
                                   const FieldLayout& fl,
                                   const T* vals)
{
  if(fl.dest == DestKind::Ignore) return;

  const auto& fs = fl.spec;

  if(fl.dest == DestKind::AoSCore){
    assignCoreFT(out.particles[i], fl.ftype, vals);
    return;
  }

  if(fl.dest == DestKind::AoSExt){
    if(out.aosExt.stride==0) return;
    uint8_t* dst = out.aosExt.ptr(i) + fl.aosExtOffset;
    std::memcpy(dst, vals, (size_t)fs.count * sizeof(T));
    return;
  }

  // SoA
  auto &f = out.soa[*fl.soaKey];
  std::memcpy(f.ptr(i), vals, (size_t)fs.count * sizeof(T));
}


class BinaryReader final : public IParticleReader {
  std::ifstream file_;
  std::vector<char> ioBuf_;
  size_t npart_ = 0;
  size_t data_offset_ = 0;
  
public:
  bool open(const std::string& path, HeaderInfo& header) override {
    file_.open(path, std::ios::binary);
    if(!file_) return false;

    ioBuf_.resize(32 * 1024 * 1024);
    file_.rdbuf()->pubsetbuf(ioBuf_.data(),
                             static_cast<std::streamsize>(ioBuf_.size()));
    
    // ---- header 読み ----
    float t;
    int   n;
    file_.read(reinterpret_cast<char*>(&t), sizeof t);
    file_.read(reinterpret_cast<char*>(&n), sizeof n);
    if(!file_) return false;

    header.time  = t;
    header.npart = n;
    header.flag_hdf5 = false;

    npart_ = static_cast<size_t>(n);

    // ---- ここが重要 ----
    // この位置が「粒子 record の先頭」
    data_offset_ = file_.tellg();

    return true;
  }

  void close() override {
    file_.close();
  }

  bool is_binary() override {
    return true;
  }
  
  size_t particleCount() const override {
    return npart_;
  }
  
  bool readRange(ParticleBlock& out,
                 size_t begin, size_t count,
                 const std::vector<FieldSpec>& fields,
                 const IOPlan& plan) override
  {
    TIME_SCOPE("BinaryReader readRange total");
    
    if(begin + count > npart_) return false;
    
    // record layout（FieldSpec順）
    const RecordLayout layout = buildRecordLayout(fields, plan);

    out.aosExt.stride = plan.aosExtStride;
    out.resize(count);    

    // ---- SoA 事前確保 ----
    for (const auto& fl : layout.fields) {
      if (fl.dest != DestKind::SoA) continue;
      auto &f = out.soa[*fl.soaKey];
      f.type  = fl.spec.type;
      f.comps = fl.spec.count;
      f.resize(count);
    }

    // ---- begin 番目の粒子まで seek ----
    const std::streamoff offset =
      data_offset_ + static_cast<std::streamoff>(begin * layout.recordSize);

    file_.seekg(offset, std::ios::beg);
    if(!file_) return false;

    const size_t recSz = layout.recordSize;
    if (recSz == 0) return false;

    // 1回のreadを 8MB 目標（4〜32MBで調整）
    const size_t targetBytes = 32u * 1024u * 1024u;
    
    // recSzに応じてchunk粒子数を決める
    size_t chunkRecs = targetBytes / recSz;
    chunkRecs = std::max<size_t>(chunkRecs, 1024);
    chunkRecs = std::min<size_t>(chunkRecs, 1'000'000);
    
    std::vector<uint8_t> buf(recSz * chunkRecs);

    size_t done = 0;
    while (done < count) {
      const size_t n = std::min(chunkRecs, count - done);
      const size_t bytes = recSz * n;

      {
	TIME_SCOPE("file read");
	file_.read(reinterpret_cast<char*>(buf.data()),
		   static_cast<std::streamsize>(bytes));
      }
      
      if (file_.gcount() != static_cast<std::streamsize>(bytes))
        return false;

      {
	TIME_SCOPE("dispatch fields");
      for (size_t j = 0; j < n; ++j) {
        const size_t i = done + j;
        const uint8_t* rec = buf.data() + j * recSz;
	ParticleData& p = out.particles[i];
	
        for (const auto& fl : layout.fields) {
          const uint8_t* src = rec + fl.offset;

	  if (fl.dest == DestKind::AoSCore && fl.store) {
	    fl.store(p, src);          // ★ここで DataType switch が消える
	    continue;
	  }
	  
          switch (fl.spec.type) {
            case DataType::Float:
              writeToDestFast(out, i, fl, reinterpret_cast<const float*>(src));
              break;
            case DataType::Double:
              writeToDestFast(out, i, fl, reinterpret_cast<const double*>(src));
              break;
            case DataType::Int32:
              writeToDestFast(out, i, fl, reinterpret_cast<const int32_t*>(src));
              break;
            case DataType::Int64:
              writeToDestFast(out, i, fl, reinterpret_cast<const int64_t*>(src));
              break;
          }
        }
      }
      }

      done += n;
    }
    
    
    return true;
  }
};

#ifdef USE_MMAP
#include <sys/mman.h>
class MMapReader final : public IParticleReader {
  int fd_ = -1;
  uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t npart_ = 0;
  size_t data_offset_ = 0;
  
public:
  bool open(const std::string& path, HeaderInfo& header) override {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) return false;

    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
      close();
      return false;
    }
    size_ = static_cast<size_t>(st.st_size);

    data_ = static_cast<uint8_t*>(::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0));
    if (data_ == MAP_FAILED) {
      data_ = nullptr;
      close();
      return false;
    }

    // ---- header 読み（先頭に time(float), npart(int)）----
    if (size_ < sizeof(float) + sizeof(int)) {
      close();
      return false;
    }

    float t;
    int n;
    std::memcpy(&t, data_, sizeof(float));
    std::memcpy(&n, data_ + sizeof(float), sizeof(int));

    header.time  = t;
    header.npart = n;
    header.flag_hdf5 = false;

    if (n < 0) { // 念のため
      close();
      return false;
    }
    npart_ = static_cast<size_t>(n);

    // ---- ここが重要：粒子データはこの位置から始まる ----
    data_offset_ = sizeof(float) + sizeof(int);

    return true;
  }

  void close() override {
    if(data_){ ::munmap(data_, size_); data_=nullptr; }
    if(fd_>=0){ ::close(fd_); fd_=-1; }
    size_=0; npart_=0; data_offset_=0;
  }

  bool is_binary() override {
    return true;
  }
  
  size_t particleCount() const override {
    return npart_;
  }

  bool readRange(ParticleBlock& out,
                 size_t begin, size_t count,
                 const std::vector<FieldSpec>& fields,
                 const IOPlan& plan) override
  {
    TIME_SCOPE("MMapReader readRange total");
   
    const RecordLayout layout = buildRecordLayout(fields, plan);

    if (layout.recordSize == 0) return false;
    if (begin + count > npart_) return false;

    // ファイルサイズ整合性チェック（重要）
    const size_t needBytes = data_offset_ + npart_ * layout.recordSize;
    if (needBytes > size_) {
      // header の npart と実ファイルサイズが矛盾している
      return false;
    }

    {
      TIME_SCOPE("resize + SoA prealloc");

      out.aosExt.stride = plan.aosExtStride;
      out.resize(count);

      // SoA 事前確保
      for(const auto& fs : fields){
	auto it = plan.plan.find(fs.label);
	if(it!=plan.plan.end() && it->second.dest==DestKind::SoA){
	  auto &f = out.soa[it->second.soaKey];
	  f.type = fs.type;
	  f.comps = fs.count;
	  f.resize(count);
	}
      }
    }
    
    {
      TIME_SCOPE("dispatch loop");
      const uint8_t* base = data_ + data_offset_ + begin * layout.recordSize;
      const uint8_t* end = base + count * layout.recordSize;
      if(end > data_ + size_) return false;

      for(size_t i=0;i<count;i++){
	const uint8_t* rec = base + i * layout.recordSize;

	for(const auto& fl : layout.fields){
	  const uint8_t* src = rec + fl.offset;
	  const auto& fs = fl.spec;

	  switch(fs.type){
	  case DataType::Float:
	    writeToDest(out, i, fs, plan, reinterpret_cast<const float*>(src));
	    break;
	  case DataType::Double:
	    writeToDest(out, i, fs, plan, reinterpret_cast<const double*>(src));
	    break;
	  case DataType::Int32:
	    writeToDest(out, i, fs, plan, reinterpret_cast<const int32_t*>(src));
	    break;
	  case DataType::Int64:
	    writeToDest(out, i, fs, plan, reinterpret_cast<const int64_t*>(src));
	    break;
	  }
	}
      }
    }
    }
    return true;
  }
};
#endif


#ifdef HAVE_HDF5
#include <H5Cpp.h>
#include "hdf5_utils.h"

class HDF5Reader final : public IParticleReader {
  H5::H5File file_;

  size_t npart_ = 0;
  size_t blockSize_ = 1 << 16; // chunk read

  // Gadget 系を想定：ptype=0..5
  double mass_type_[6]{};   // MassTable
  size_t count_[6]{};       // NumPart_Total/ThisFile
  size_t IndexStart_[7]{};  // prefix sum

  bool   flag_skip_DM_ = false;            // 必要なら
  
  static std::string partPath_(int ptype, const std::string& dsName) {
    return "/PartType" + std::to_string(ptype) + "/" + dsName;
  }

  // ---- attribute helpers ----
  static bool hasAttr_(H5::H5Object& obj, const char* name) {
    // H5Cpp には exists の便利関数が弱いので例外で判定
    try { (void)obj.openAttribute(name); return true; }
    catch (...) { return false; }
  }

  // ---- dataset rank ----
  static int getRank_(const H5::DataSet& ds) {
    H5::DataSpace sp = ds.getSpace();
    return sp.getSimpleExtentNdims();
  }

  // ---- infer particle count from dataset dims ----
  static size_t inferCountFromDataset_(H5::H5File& f, int ptype, const std::string& dsName) {
    try {
      H5::DataSet ds = f.openDataSet(partPath_(ptype, dsName));
      H5::DataSpace sp = ds.getSpace();
      int nd = sp.getSimpleExtentNdims();
      if (nd < 1) return 0;
      std::vector<hsize_t> dims((size_t)nd, 0);
      sp.getSimpleExtentDims(dims.data());
      return (size_t)dims[0];
    } catch (...) {
      return 0;
    }
  }

  inline H5::PredType h5_memtype_from_source(DataType t)
  {
    switch(t){
    case DataType::Float:  return H5::PredType::NATIVE_FLOAT;
    case DataType::Double: return H5::PredType::NATIVE_DOUBLE;
    case DataType::Int32:  return H5::PredType::NATIVE_INT32;
    case DataType::Int64:  return H5::PredType::NATIVE_INT64;   // or NATIVE_LLONG
    }
    // fallback
    return H5::PredType::NATIVE_FLOAT;
  }

private:
  bool flag_position_mask_ = false;
  double mask_center_[3] = {0.,0.,0.}, mask_radius_ = 1.e10;
  double factor_density_ = 1.0;
  double factor_Bfield_ = 1.0;
  double factor_IntEnergy_ = 1.0;
  hid_t dapl_;
  
H5::DataSet openDataSetWithDAPL(const std::string& fullPath) {
  hid_t did = H5Dopen2(file_.getId(), fullPath.c_str(), dapl_);
  if (did < 0) throw H5::DataSetIException("H5Dopen2", "open failed");
  return H5::DataSet(did);
}
  
static DataType mapMetaToDataType(const H5DatasetMeta& m)
{
  if (m.cls == H5T_FLOAT) {
    if (m.bytes == 4) return DataType::Float;
    if (m.bytes == 8) return DataType::Double;
  } else if (m.cls == H5T_INTEGER) {
    // 可視化用途で unsigned を許さないならここで落とす
    // ここが “判断” の場所
    if (m.sign != H5T_SGN_2) {
      // ここを endrun() にしてもいいし例外でもいい
      throw std::runtime_error("unsupported integer sign (unsigned?)");
    }
    if (m.bytes == 4) return DataType::Int32;
    if (m.bytes == 8) return DataType::Int64;
  }
  throw std::runtime_error("unsupported dataset dtype");
}

static int mapMetaToComps(const H5DatasetMeta& m)
{
  if (m.rank == 1) return 1;
  if (m.rank == 2) return (int)m.dims[1];
  throw std::runtime_error("unsupported dataset rank");
}

// AoSCore store の再設定（あなたの buildRecordLayout と同じロジックを再利用）
static void resetStoreFromSpec(FieldLayout& fl)
{
  fl.store = nullptr;
  if (fl.dest != DestKind::AoSCore) return;

  const bool isF32 = (fl.spec.type == DataType::Float);
  const bool isF64 = (fl.spec.type == DataType::Double);
  const bool isI32 = (fl.spec.type == DataType::Int32);
  const bool isI64 = (fl.spec.type == DataType::Int64);

  switch (fl.ftype) {
    case FieldType::Position:   fl.store = isF32 ? &store_pos_f32 : (isF64 ? &store_pos_f64 : nullptr); break;
    case FieldType::Velocity:   fl.store = isF32 ? &store_vel_f32 : (isF64 ? &store_vel_f64 : nullptr); break;
    case FieldType::Mass:       fl.store = isF32 ? &store_mass_f32 : (isF64 ? &store_mass_f64 : nullptr); break;
    case FieldType::Density:    fl.store = isF32 ? &store_density_f32 : (isF64 ? &store_density_f64 : nullptr); break;
    case FieldType::Temperature:fl.store = isF32 ? &store_temp_f32 : (isF64 ? &store_temp_f64 : nullptr); break;
    case FieldType::Hsml:       fl.store = isF32 ? &store_hsml_f32 : (isF64 ? &store_hsml_f64 : nullptr); break;
    case FieldType::Volume:     fl.store = isF32 ? &store_volume_f32 : (isF64 ? &store_volume_f64 : nullptr); break;
    case FieldType::Type:       fl.store = isI32 ? &store_type_i32 : (isI64 ? &store_type_i64 : nullptr); break;
    case FieldType::ID:         fl.store = isI32 ? &store_id_i32 : (isI64 ? &store_id_i64 : nullptr); break;
    default: fl.store = nullptr; break;
  }
}

// これがあなたの欲しい “metaで FieldLayout を上書きする” 本体
static void update_layout_from_hdf5(FieldLayout& fl, const H5::DataSet& ds)
{
  const H5DatasetMeta m = getDatasetMeta(ds);  // util は事実抽出のみ

  // dataset 実体に合わせて上書き（ここが狙い）
  fl.spec.type  = mapMetaToDataType(m);
  fl.spec.count = mapMetaToComps(m);

  // AoSCoreのstore更新
  resetStoreFromSpec(fl);
}
  
public:
  HDF5Reader() = default;

  bool is_binary() override { return false; }
  size_t particleCount() const override { return npart_; }

  bool open(const std::string& path, HeaderInfo& header) override {
    // 初期化
    npart_ = 0;
    for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; IndexStart_[t]=0; }
    IndexStart_[6]=0;

    // open file
    try {
      file_ = H5::H5File(path, H5F_ACC_RDONLY);
    } catch (const H5::FileIException& e) {
      std::cerr << "HDF5 open failed: " << e.getDetailMsg() << "\n";
      return false;
    } catch (const H5::Exception& e) {
      return false;
    } catch (...) {
      return false;
    }

    dapl_ = H5Pcreate(H5P_DATASET_ACCESS);
    
    // rdcc_nslots: ハッシュ表サイズ（chunk数の目安）
    // rdcc_nbytes: キャッシュ総バイト数
    // rdcc_w0: preemption policy（0..1）
    H5Pset_chunk_cache(dapl_, 200003,              // nslots（素数が推奨）
		       512ULL*1024*1024,    // 512MB chunk cache
		       0.75);
    
    // /Header の属性を読む
    bool hasHeader = false;
    try {
      H5::Group hg = file_.openGroup("/Header");
      hasHeader = true;

      double time = 0.0;
      (void)readAttributeScalar(hg, "Time", time);
      header.time = (float)time;

      printf("time is %g\n", header.time);
      
      // MassTable double[6]
      double mt[6]{};
      if (readAttributeArray(hg, "MassTable", mt)) {
        for (int t=0;t<6;++t) mass_type_[t] = mt[t];
      } else {
        for (int t=0;t<6;++t) mass_type_[t] = 0.0;
      }

      // NumPart_Total (uint32)
      bool okNum = false;
      {
        unsigned int       n32[6]{};
	if (readAttributeArray(hg, "NumPart_Total", n32)) {
          for (int t=0;t<6;++t) count_[t] = (size_t)n32[t];
          okNum = true;
        }
      }

      if (!okNum) {
        unsigned int       n32[6]{};
	if (readAttributeArray(hg, "NumPart_ThisFile", n32)) {
          for (int t=0;t<6;++t) count_[t] = (size_t)n32[t];
          okNum = true;
        }
      }
    } catch (...) {
      hasHeader = false;
      header.time = 0.0f;
      for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; }
    }

    bool hasParam = false;
    try {
      H5::Group param = file_.openGroup("/Parameters");
      hasParam = true;

      double UnitLength_in_cm;
      (void)readAttributeScalar(param, "UnitLength_in_cm", UnitLength_in_cm);
      header.UnitLength_in_cm = UnitLength_in_cm;

      double UnitMass_in_g;
      (void)readAttributeScalar(param, "UnitMass_in_g", UnitMass_in_g);
      header.UnitMass_in_g = UnitMass_in_g;

      double UnitVelocity_in_cm_per_s;
      (void)readAttributeScalar(param, "UnitVelocity_in_cm_per_s", UnitVelocity_in_cm_per_s);
      header.UnitVelocity_in_cm_per_s = UnitVelocity_in_cm_per_s;

      double HubbleParam;
      (void)readAttributeScalar(param, "HubbleParam", HubbleParam);
      header.HubbleParam = HubbleParam;

      bool flag_comoving = false;
      (void)readAttributeScalar(param, "ComovingIntegrationOn", flag_comoving);
      header.flag_comoving = flag_comoving;

      bool flag_density_in_cgs = false;
      (void)readAttributeScalar(param, "FlagDensityInCgs", flag_density_in_cgs);
      header.flag_density_in_cgs = flag_density_in_cgs;

      bool flag_B_in_cgs = false;
      (void)readAttributeScalar(param, "FlagBfieldInCgs", flag_B_in_cgs);
      header.flag_B_in_cgs = flag_B_in_cgs;
    } catch (...) {
      hasParam = false;
      header.flag_comoving = false;
      header.flag_density_in_cgs = false;
      header.flag_B_in_cgs = false;
    }

    // /Header から count が取れていない場合のフォールバック
    bool allZero = true;
    for (int t=0;t<6;++t) if (count_[t] > 0) { allZero=false; break; }

    if (!hasHeader || allZero) {
      for (int t=0;t<6;++t) {
        size_t n = inferCountFromDataset_(file_, t, "Coordinates");
        if (n==0) n = inferCountFromDataset_(file_, t, "Velocities");
        if (n==0) n = inferCountFromDataset_(file_, t, "ParticleIDs");
        count_[t] = n;
      }
    }

    // prefix sum
    IndexStart_[0] = 0;
    for (int t=0;t<6;++t) IndexStart_[t+1] = IndexStart_[t] + count_[t];
    npart_ = IndexStart_[6];

    header.npart     = (int)npart_;
    header.flag_hdf5 = true;
    
    factor_density_ = 1.;
    if(header.flag_density_in_cgs == false){
      const double proton_mass = 1.67e-24;
      factor_density_ = header.HubbleParam * header.HubbleParam * header.UnitMass_in_g / pow(header.UnitLength_in_cm, 3) / proton_mass;
      if(header.flag_comoving)
	factor_density_ /= pow(header.time, 3);
    }

    factor_Bfield_ = 1.;
    if(header.flag_B_in_cgs == false){
      factor_Bfield_ = sqrt(header.UnitMass_in_g / header.UnitLength_in_cm) / (header.UnitLength_in_cm / header.UnitVelocity_in_cm_per_s / header.HubbleParam);
      if(header.flag_comoving)
	factor_Bfield_ /= pow(header.time, 2);
    }

    factor_IntEnergy_ = header.UnitVelocity_in_cm_per_s*header.UnitVelocity_in_cm_per_s * PROTONMASS / BOLTZMANN;    
    
    return true;
  }

  void close() override {
    try { file_.close(); } catch (...) {}
    if (dapl_ != H5P_DEFAULT) {
      H5Pclose(dapl_);
      dapl_ = H5P_DEFAULT;
    }
    
    npart_ = 0;
    for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; IndexStart_[t]=0; }
    IndexStart_[6]=0;
  }

bool readRange(ParticleBlock& out,
	       size_t begin, size_t count,
	       const std::vector<FieldSpec>& fields,
	       const IOPlan& plan) override
{
  if (begin + count > npart_) return false;

  RecordLayout layout = buildRecordLayout(fields, plan, true);

  // ---------------------------
  // helper: /PartTypeX/Coordinates を chunk で読み keep を作る
  // ---------------------------
  auto build_keep_from_coords_chunk =
    [&](int ptype, size_t localStart, size_t n, std::vector<uint32_t>& keep)->bool
    {
      keep.clear();
      keep.reserve(n);

      // Coordinates dataset open
      H5::DataSet ds;
      try { ds = openDataSetWithDAPL(partPath_(ptype, "Coordinates")); }
      catch (...) { return false; }

      // rank check and comps check
      {
	H5::DataSpace sp = ds.getSpace();
	int rank = sp.getSimpleExtentNdims();
	if (!(rank==2)) return false;         // Coordinates must be (N,3)
	hsize_t dims[2] = {0,0};
	sp.getSimpleExtentDims(dims);
	if ((int)dims[1] != 3) return false;
      }

      // ここは「何で読むか」：ファイルがfloat/doubleどちらでもよいように
      // HDF5 の型変換に任せて float[3] で読む（可視化用途なら十分）
      const H5::PredType memType = H5::PredType::NATIVE_FLOAT;
      const size_t elemSz = sizeof(float);
      const int comps = 3;

      std::vector<uint8_t> buf;
      readHyperslabBytes(ds, memType,
			 (hsize_t)localStart,
			 (hsize_t)n,
			 comps, buf, elemSz);

      const float* xyz = reinterpret_cast<const float*>(buf.data());

      const double cx = mask_center_[0];
      const double cy = mask_center_[1];
      const double cz = mask_center_[2];
      const double r2 = mask_radius_ * mask_radius_;

      for (uint32_t j=0; j<(uint32_t)n; ++j) {
	const double x = (double)xyz[3*(size_t)j + 0] - cx;
	const double y = (double)xyz[3*(size_t)j + 1] - cy;
	const double z = (double)xyz[3*(size_t)j + 2] - cz;
	const double d2 = x*x + y*y + z*z;
	if (d2 <= r2) keep.push_back(j);
      }
      return true;
    };

  // ---------------------------
  // pass1: mask on のとき、保持粒子数 totalKept を数える
  // ---------------------------
  size_t totalKept = 0;
  if (flag_position_mask_) {
    const size_t globalBegin = begin;
    const size_t globalEnd   = begin + count;

    for (int ptype=0; ptype<6; ++ptype) {
      if (flag_skip_DM_ && ptype==1) continue;

      const size_t pBeg = IndexStart_[ptype];
      const size_t pEnd = IndexStart_[ptype+1];
      if (pEnd <= globalBegin || globalEnd <= pBeg) continue;

      const size_t subBegG  = std::max(globalBegin, pBeg);
      const size_t subEndG  = std::min(globalEnd,   pEnd);
      const size_t subCount = subEndG - subBegG;

      const size_t localStart0 = subBegG - pBeg;

      size_t done = 0;
      std::vector<uint32_t> keep;
      while (done < subCount) {
	const size_t n = std::min(blockSize_, subCount - done);
	if (!build_keep_from_coords_chunk(ptype, localStart0 + done, n, keep))
	  return false;
	totalKept += keep.size();
	done += n;
      }
    }

    out.resize(totalKept); // compaction
  } else {
    out.resize(count);     // no mask
  }

  for (const auto& fl : layout.fields) {
    if (fl.dest != DestKind::SoA) continue;
    auto &f = out.soa[*fl.soaKey];
    f.type  = fl.spec.type;
    f.comps = fl.spec.count;
    f.resize(out.particles.size());
  }

  /**************** needed to calculate temperature ***********************/
  bool wantTemp = false, wantU = false, wantE = false, wantH2 = false, wantG = false;
  for(const auto& fl : layout.fields){
    if(fl.dest == DestKind::Ignore) continue;
    switch(fl.ftype){
    case FieldType::Temperature:     wantTemp = true; break;
    case FieldType::InternalEnergy:  wantU    = true; break;
    case FieldType::ElectronFraction: wantE   = true; break;
    case FieldType::H2Fraction:      wantH2   = true; break;
    case FieldType::Gamma:           wantG    = true; break;
    default: break;
    }
  }
  /************************************************************************/

  auto findDSName = [&](FieldType ft)->std::string {
    for (const auto& fl : layout.fields) {
      if (fl.dest == DestKind::Ignore) continue;
      if (fl.ftype != ft) continue;
      const auto& fs = fl.spec;
      return fs.displayName.empty() ? fs.label : fs.displayName;
    }
    return std::string(); // 無ければ空
  };

  auto fill_chunk_scalar =
    [&](std::vector<double>& dst,        // サイズ = nwrite
	const std::vector<uint8_t>& buf, // サイズ = n * elemSz
	size_t nread, size_t elemSz,
	bool masked,
	const std::vector<uint32_t>* keepPtr,
	DataType srcType)
    {
      auto get_double = [&](const uint8_t* p)->double{
	switch(srcType){
	case DataType::Float:  { float v;  std::memcpy(&v, p, sizeof(v)); return (double)v; }
	case DataType::Double: { double v; std::memcpy(&v, p, sizeof(v)); return v; }
	case DataType::Int32:  { int32_t v; std::memcpy(&v, p, sizeof(v)); return (double)v; }
	case DataType::Int64:  { int64_t v; std::memcpy(&v, p, sizeof(v)); return (double)v; }
	}
	return -1.0;
      };

      if(!masked){
	for(size_t j=0;j<nread;++j){
	  dst[j] = get_double(buf.data() + j*elemSz);
	}
      }else{
	for(size_t kk=0; kk<keepPtr->size(); ++kk){
	  const uint32_t j = (*keepPtr)[kk];
	  dst[kk] = get_double(buf.data() + (size_t)j*elemSz);
	}
      }
    };


  size_t outWriteCursor = 0;
  const size_t globalBegin = begin;
  const size_t globalEnd   = begin + count;

  struct OpenedField {
    FieldLayout* fl;
    H5::DataSet  ds;
    size_t elemSz;
    int comps;
    size_t bpp;
    std::vector<uint8_t> buf;   // ★ここに置く（再利用）
  };
  
  for (int ptype=0; ptype<6; ++ptype) {
    if (flag_skip_DM_ && ptype==1) continue;

    /**************** needed to calculate temperature ***********************/
    bool hasTemp=false, hasU=false, hasG=false, hasE=false, hasH2=false;
    if(ptype==0){
      auto tryOpen = [&](const std::string& dsName)->bool{
	if(dsName.empty()) return false;
	try { (void)openDataSetWithDAPL(partPath_(0, dsName)); return true; }
	catch (...) { return false; }
      };

      if(wantTemp) hasTemp = tryOpen(findDSName(FieldType::Temperature));
      if(wantU)    hasU    = tryOpen(findDSName(FieldType::InternalEnergy));
      if(wantE)    hasE    = tryOpen(findDSName(FieldType::ElectronFraction));
      if(wantH2)   hasH2   = tryOpen(findDSName(FieldType::H2Fraction));
      if(wantG)    hasG    = tryOpen(findDSName(FieldType::Gamma));
    }

    const bool needSynth = (ptype==0 && !hasTemp && hasU);
    /************************************************************************/
      
    const size_t pBeg = IndexStart_[ptype];
    const size_t pEnd = IndexStart_[ptype+1];
    if (pEnd <= globalBegin || globalEnd <= pBeg) continue;

    const size_t subBegG  = std::max(globalBegin, pBeg);
    const size_t subEndG  = std::min(globalEnd,   pEnd);
    const size_t subCount = subEndG - subBegG;

    const size_t localStart = subBegG - pBeg;
    const size_t outStart   = subBegG - globalBegin;

    size_t done = 0;      
    std::vector<uint32_t> keep;

    std::vector<OpenedField> opened;
    opened.reserve(layout.fields.size());

    for (auto& fl : layout.fields) {
      if (fl.dest == DestKind::Ignore) continue;

      const std::string dsName = fl.spec.displayName.empty() ? fl.spec.label : fl.spec.displayName;
      try {
	H5SilenceErrors quiet(ptype >= 1);
	
	H5::DataSet ds = openDataSetWithDAPL(partPath_(ptype, dsName)); // ← DAPL版推奨
	update_layout_from_hdf5(fl, ds);                                // ← ここで確定
	OpenedField of;
	of.fl = &fl;
	of.ds = std::move(ds);
	of.elemSz  = dataTypeSize(fl.spec.type);
	of.comps   = fl.spec.count;
	of.bpp     = of.elemSz * (size_t)of.comps;
	opened.push_back(std::move(of));
      } catch (...) {
	// dataset無いならスキップ
      }
    }
      
    while (done < subCount) {
      const size_t n = std::min(blockSize_, subCount - done);

      const bool masked = flag_position_mask_;
      const std::vector<uint32_t>* keepPtr = nullptr;

      size_t outBase = 0; // この chunk の書き込み先先頭

      if (!masked) {
	outBase = outStart + done; // 連結配列（従来通り）
      } else {
	if (!build_keep_from_coords_chunk(ptype, localStart + done, n, keep))
	  return false;
	keepPtr = &keep;
	outBase = outWriteCursor;        // compaction 書き込み先
	outWriteCursor += keep.size();

	if (keep.empty()) {
	  done += n;
	  continue;
	}
      }
			
      const size_t nwrite = masked ? keep.size() : n;
      for (size_t kk=0; kk<nwrite; ++kk) {
	ParticleData& p = out.particles[outBase + kk];
	p.type = ptype;
	if (mass_type_[ptype] > 0.0) p.mass = (float)mass_type_[ptype];
      }	  

      std::vector<double> tmp_u, tmp_e, tmp_h2, tmp_g;
      if(needSynth){
	tmp_u.assign(nwrite, -1.0);
	if(hasE)  tmp_e.assign(nwrite, -1.0);   // hasE は wantE のときだけ true になり得る
	if(hasH2) tmp_h2.assign(nwrite, -1.0);
	if(hasG)  tmp_g.assign(nwrite, -1.0);
      }

      // ------------------------
      // field-first: 各 field を読む
      // ------------------------
      for (auto& of : opened) {
	of.buf.resize(n * of.bpp);
	const H5::PredType memType = h5_memtype_from_source(of.fl->spec.type);
	
	readHyperslabBytes(of.ds, memType,
			   (hsize_t)(localStart + done),
			   (hsize_t)n,
			   of.comps, of.buf, of.elemSz);

	// ------------------------
	// ここが「mask と共存」する本体
	// ------------------------
	if (of.fl->dest == DestKind::AoSCore) {
	  if (!masked) {
	    for (size_t j=0; j<n; ++j) {
	      ParticleData& p = out.particles[outBase + j];
	      const uint8_t* src = of.buf.data() + j*of.bpp;
	      of.fl->store(p, src);
	    }
	  } else {
	    for (size_t kk=0; kk<keepPtr->size(); ++kk) {
	      const uint32_t j = (*keepPtr)[kk];
	      ParticleData& p = out.particles[outBase + kk];
	      const uint8_t* src = of.buf.data() + (size_t)j*of.bpp;
	      of.fl->store(p, src);
	    }
	  }
	}
	else if (of.fl->dest == DestKind::SoA) {
	  auto &f = out.soa[*of.fl->soaKey];

	  // f は上で確保済み想定（そうでない設計ならここで遅延確保でもOK）
	  if (!masked) {
	    std::memcpy(f.bytes.data() + outBase * of.bpp,
			of.buf.data(),
			n * of.bpp);
	  } else {
	    uint8_t* dst0 = f.bytes.data() + outBase * of.bpp;
	    for (size_t kk=0; kk<keepPtr->size(); ++kk) {
	      const uint32_t j = (*keepPtr)[kk];
	      std::memcpy(dst0 + kk*of.bpp,
			  of.buf.data() + (size_t)j*of.bpp,
			  of.bpp);
	    }
	  }
	}
	else {
	  // AoSExt など：maskあり/なしで oi と src を変えるだけ
	  if (!masked) {
	    for (size_t j=0; j<n; ++j) {
	      const size_t oi = outBase + j;
	      const uint8_t* src = of.buf.data() + j*of.bpp;
	      switch (of.fl->spec.type) {
	      case DataType::Float:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const float*>(src));  break;
	      case DataType::Double: writeToDestFast(out, oi, *of.fl, reinterpret_cast<const double*>(src)); break;
	      case DataType::Int32:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const int32_t*>(src)); break;
	      case DataType::Int64:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const int64_t*>(src)); break;
	      }
	    }
	  } else {
	    for (size_t kk=0; kk<keepPtr->size(); ++kk) {
	      const uint32_t j = (*keepPtr)[kk];
	      const size_t oi = outBase + kk;
	      const uint8_t* src = of.buf.data() + (size_t)j*of.bpp;
	      switch (of.fl->spec.type) {
	      case DataType::Float:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const float*>(src));  break;
	      case DataType::Double: writeToDestFast(out, oi, *of.fl, reinterpret_cast<const double*>(src)); break;
	      case DataType::Int32:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const int32_t*>(src)); break;
	      case DataType::Int64:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const int64_t*>(src)); break;
	      }
	    }
	  }
	}
	  
	if(needSynth){
	  if(of.fl->ftype == FieldType::InternalEnergy){
	    fill_chunk_scalar(tmp_u, of.buf, n, of.elemSz, masked, keepPtr, of.fl->spec.type);
	  }else if(hasE && of.fl->ftype == FieldType::ElectronFraction){
	    fill_chunk_scalar(tmp_e, of.buf, n, of.elemSz, masked, keepPtr, of.fl->spec.type);
	  }else if(hasH2 && of.fl->ftype == FieldType::H2Fraction){
	    fill_chunk_scalar(tmp_h2, of.buf, n, of.elemSz, masked, keepPtr, of.fl->spec.type);
	  }else if(hasG && of.fl->ftype == FieldType::Gamma){
	    fill_chunk_scalar(tmp_g, of.buf, n, of.elemSz, masked, keepPtr, of.fl->spec.type);
	  }
	}

	if (of.fl->ftype == FieldType::Bfield && factor_Bfield_ != 1.0){
	  if (of.fl->dest == DestKind::SoA) {
	    auto &f = out.soa[*of.fl->soaKey];          // "Bfield"
	    const int comps = of.comps;                  // たぶん 3
	    const size_t start = outBase;                // このchunkの先頭
	    const size_t nscale = nwrite;                // このchunkで書いた粒子数
	    
	    if (f.type == DataType::Float) {
	      float* p = reinterpret_cast<float*>(f.bytes.data()) + start * comps;
	      const float fac = (float)factor_Bfield_;
	      for (size_t i = 0; i < nscale * (size_t)comps; ++i) p[i] *= fac;
	      
	    } else if (f.type == DataType::Double) {
	      double* p = reinterpret_cast<double*>(f.bytes.data()) + start * comps;
	      const double fac = factor_Bfield_;
	      for (size_t i = 0; i < nscale * (size_t)comps; ++i) p[i] *= fac;	      
	    }	    
	  }
	}	
      } // for field

      if(needSynth){
	for(size_t kk=0; kk<nwrite; ++kk){
	  const double u = tmp_u[kk];
	  if(!(u > 0.0)) continue;

	  double gamma = 5.0/3.0;
	  if(hasG && tmp_g[kk] > 0.0) gamma = tmp_g[kk];

	  double denom = 1.2;
	  if(hasE  && tmp_e[kk]  > 0.0) denom += tmp_e[kk];
	  if(hasH2 && tmp_h2[kk] > 0.0) denom -= tmp_h2[kk];

	  const double T = (gamma - 1.0) * u / denom * factor_IntEnergy_;
	  if(T > 0.0){
	    out.particles[outBase + kk].temperature = (float)T;
	  }
	}
      }

      if (factor_density_ != 1.0) 
	for(size_t kk=0; kk<nwrite; ++kk)
	  out.particles[outBase + kk].density = (float)((double)out.particles[outBase + kk].density * factor_density_);	
      
      done += n;
    }
  }  

  return true;
}

bool readRangeMasked(ParticleBlock& out,
                     size_t begin, size_t count,
                     const std::vector<FieldSpec>& fields,
                     const IOPlan& plan,
                     ParticleMask& mask) override
{
  if (begin + count > npart_) return false;

  RecordLayout layout = buildRecordLayout(fields, plan, true);

  if(!mask.active()){
    return readRange(out, begin, count, fields, plan);
  }
 
  const size_t globalBegin = begin;
  const size_t globalEnd   = begin + count;

  // ---------------------------
  // pass0: Thin候補数（maxParticles用）
  // ---------------------------
  if(mask.config().enableMaxParticles && mask.config().maxParticles>0){
    size_t thinCandidates = 0;
    for(int ptype=0; ptype<6; ++ptype){
      if(!mask.typeEnabled(ptype)) continue;
      if(!mask.typeThinOK(ptype)) continue; // ThinOKのみ候補

      const size_t pBeg = IndexStart_[ptype];
      const size_t pEnd = IndexStart_[ptype+1];
      if (pEnd <= globalBegin || globalEnd <= pBeg) continue;
      const size_t subBegG  = std::max(globalBegin, pBeg);
      const size_t subEndG  = std::min(globalEnd,   pEnd);
      thinCandidates += (subEndG - subBegG);
    }
    mask.prepare(thinCandidates);
  }else{
    mask.prepare(0);
  }

  // ---------------------------
  // pass1: totalKept を数える（keepはchunkごと）
  // ---------------------------
  size_t totalKept = 0;
  {
    std::vector<uint32_t> keep;
    for(int ptype=0; ptype<6; ++ptype){
      if(!mask.typeEnabled(ptype)) continue;

      const size_t pBeg = IndexStart_[ptype];
      const size_t pEnd = IndexStart_[ptype+1];
      if (pEnd <= globalBegin || globalEnd <= pBeg) continue;

      const size_t subBegG  = std::max(globalBegin, pBeg);
      const size_t subEndG  = std::min(globalEnd,   pEnd);
      const size_t subCount = subEndG - subBegG;

      const size_t localStart0 = subBegG - pBeg;

      size_t done = 0;
      while(done < subCount){
        const size_t n = std::min(blockSize_, subCount - done);
        if(!build_keep_chunk_(ptype, localStart0 + done, n, mask, keep)) return false;
        totalKept += keep.size();
        done += n;
      }
    }
  }
  
  out.clear();
  out.resize(totalKept);  

  for (auto& fl : layout.fields) {
    if (fl.dest == DestKind::Ignore && !isTempSynthField(fl.ftype)) continue;
    
    const std::string dsName = fl.spec.displayName.empty() ? fl.spec.label : fl.spec.displayName;
    
    // find first ptype that has this dataset
    bool found = false;
    for (int ptype = 0; ptype < 6; ++ptype) {
      if (flag_skip_DM_ && ptype == 1) continue;
      try {
	H5SilenceErrors quiet(ptype >= 1);
	H5::DataSet ds = openDataSetWithDAPL(partPath_(ptype, dsName));
	update_layout_from_hdf5(fl, ds);   // <-- finalize fl.spec.type/count here
	found = true;
	break;                             // dataset found -> done
      } catch (...) {
	// try next ptype
      }
    }

    fl.present = found;   // ★ FieldLayout に bool present を1個足すのが簡単
  }
  
  for (const auto& fl : layout.fields) {
    if (fl.dest != DestKind::SoA) continue;
    if (!fl.present) continue; 
    
    auto &f = out.soa[*fl.soaKey];
    f.type  = fl.spec.type;
    f.comps = fl.spec.count;
    f.resize(out.particles.size());
  }

  /**************** needed to calculate temperature ***********************/
  bool wantTemp = false, wantU = false, wantE = false, wantH2 = false, wantG = false;
  for(const auto& fl : layout.fields){
    if(fl.dest == DestKind::Ignore && !isTempSynthField(fl.ftype)) continue;

    switch(fl.ftype){
    case FieldType::Temperature:     wantTemp = true; break;
    case FieldType::InternalEnergy:  wantU    = true; break;
    case FieldType::ElectronFraction: wantE   = true; break;
    case FieldType::H2Fraction:      wantH2   = true; break;
    case FieldType::Gamma:           wantG    = true; break;
    default: break;
    }
  }
  /************************************************************************/

  auto findDSName = [&](FieldType ft)->std::string {
    for (const auto& fl : layout.fields) {
      if (fl.dest == DestKind::Ignore && !isTempSynthField(fl.ftype)) continue;

      if (fl.ftype != ft) continue;
      const auto& fs = fl.spec;
      return fs.displayName.empty() ? fs.label : fs.displayName;
    }
    return std::string(); // 無ければ空
  };

  auto fill_chunk_scalar =
    [&](std::vector<double>& dst,        // サイズ = nwrite
	const std::vector<uint8_t>& buf, // サイズ = n * elemSz
	size_t nread, size_t elemSz,
	const std::vector<uint32_t>* keepPtr,
	DataType srcType)
    {
      auto get_double = [&](const uint8_t* p)->double{
	switch(srcType){
	case DataType::Float:  { float v;  std::memcpy(&v, p, sizeof(v)); return (double)v; }
	case DataType::Double: { double v; std::memcpy(&v, p, sizeof(v)); return v; }
	case DataType::Int32:  { int32_t v; std::memcpy(&v, p, sizeof(v)); return (double)v; }
	case DataType::Int64:  { int64_t v; std::memcpy(&v, p, sizeof(v)); return (double)v; }
	}
	return -1.0;
      };

      for(size_t kk=0; kk<keepPtr->size(); ++kk){
	const uint32_t j = (*keepPtr)[kk];
	dst[kk] = get_double(buf.data() + (size_t)j*elemSz);
      }      
    };


  size_t outWriteCursor = 0;

  struct OpenedField {
    FieldLayout* fl;
    H5::DataSet  ds;
    size_t elemSz;
    int comps;
    size_t bpp;
    std::vector<uint8_t> buf;   // ★ここに置く（再利用）
  };
  
  for (int ptype=0; ptype<6; ++ptype) {
    if (flag_skip_DM_ && ptype==1) continue;

    /**************** needed to calculate temperature ***********************/
    bool hasTemp=false, hasU=false, hasG=false, hasE=false, hasH2=false;
    if(ptype==0){
      auto tryOpen = [&](const std::string& dsName)->bool{
	if(dsName.empty()) return false;
	try { (void)openDataSetWithDAPL(partPath_(0, dsName)); return true; }
	catch (...) { return false; }
      };

      if(wantTemp) hasTemp = tryOpen(findDSName(FieldType::Temperature));
      if(wantU)    hasU    = tryOpen(findDSName(FieldType::InternalEnergy));
      if(wantE)    hasE    = tryOpen(findDSName(FieldType::ElectronFraction));
      if(wantH2)   hasH2   = tryOpen(findDSName(FieldType::H2Fraction));
      if(wantG)    hasG    = tryOpen(findDSName(FieldType::Gamma));
    }

    const bool needSynth = (ptype==0 && !hasTemp && hasU);
    if(ptype == 0){
      printf("ptype=%d wantT=%d wantU=%d hasT=%d hasU=%d needS=%d\n", ptype, wantTemp, wantU, hasTemp, hasU, needSynth);
      fflush(stdout);
    }
    /************************************************************************/
      
    const size_t pBeg = IndexStart_[ptype];
    const size_t pEnd = IndexStart_[ptype+1];
    if (pEnd <= globalBegin || globalEnd <= pBeg) continue;

    const size_t subBegG  = std::max(globalBegin, pBeg);
    const size_t subEndG  = std::min(globalEnd,   pEnd);
    const size_t subCount = subEndG - subBegG;

    const size_t localStart = subBegG - pBeg;
    const size_t outStart   = subBegG - globalBegin;

    size_t done = 0;      
    std::vector<uint32_t> keep;

    std::vector<OpenedField> opened;
    opened.reserve(layout.fields.size());

    for (auto& fl : layout.fields) {
      if (fl.dest == DestKind::Ignore && !isTempSynthField(fl.ftype)) continue;

      const std::string dsName = fl.spec.displayName.empty() ? fl.spec.label : fl.spec.displayName;
      try {
	H5SilenceErrors quiet(ptype >= 1);
	
	H5::DataSet ds = openDataSetWithDAPL(partPath_(ptype, dsName)); // ← DAPL版推奨
	update_layout_from_hdf5(fl, ds); 
	
	OpenedField of;
	of.fl = &fl;
	of.ds = std::move(ds);
	of.elemSz  = dataTypeSize(fl.spec.type);
	of.comps   = fl.spec.count;
	of.bpp     = of.elemSz * (size_t)of.comps;
	opened.push_back(std::move(of));
      } catch (...) {
	// dataset無いならスキップ
      }
    }
      
    while (done < subCount) {
      const size_t n = std::min(blockSize_, subCount - done);

      const std::vector<uint32_t>* keepPtr = nullptr;

      size_t outBase = 0; // この chunk の書き込み先先頭

      if (!build_keep_chunk_(ptype, localStart + done, n, mask, keep))
	return false;
      keepPtr = &keep;
      outBase = outWriteCursor;        // compaction 書き込み先
      outWriteCursor += keep.size();

      if (keep.empty()) {
	done += n;
	continue;
      }
      
			
      const size_t nwrite = keep.size();
      for (size_t kk=0; kk<nwrite; ++kk) {
	ParticleData& p = out.particles[outBase + kk];
	p.type = ptype;
	if (mass_type_[ptype] > 0.0) p.mass = (float)mass_type_[ptype];
      }	  

      std::vector<double> tmp_u, tmp_e, tmp_h2, tmp_g;
      if(needSynth){
	tmp_u.assign(nwrite, -1.0);
	if(hasE)  tmp_e.assign(nwrite, -1.0);   // hasE は wantE のときだけ true になり得る
	if(hasH2) tmp_h2.assign(nwrite, -1.0);
	if(hasG)  tmp_g.assign(nwrite, -1.0);
      }

      // ------------------------
      // field-first: 各 field を読む
      // ------------------------
      for (auto& of : opened) {
	of.buf.resize(n * of.bpp);
	const H5::PredType memType = h5_memtype_from_source(of.fl->spec.type);
	
	readHyperslabBytes(of.ds, memType,
			   (hsize_t)(localStart + done),
			   (hsize_t)n,
			   of.comps, of.buf, of.elemSz);

	// ------------------------
	// ここが「mask と共存」する本体
	// ------------------------
	if (of.fl->dest == DestKind::AoSCore) {
	  for (size_t kk=0; kk<keepPtr->size(); ++kk) {
	    const uint32_t j = (*keepPtr)[kk];
	    ParticleData& p = out.particles[outBase + kk];
	    const uint8_t* src = of.buf.data() + (size_t)j*of.bpp;
	    of.fl->store(p, src);
	  }	  
	}
	else if (of.fl->dest == DestKind::SoA) {
	  auto &f = out.soa[*of.fl->soaKey];
	  // f は上で確保済み想定（そうでない設計ならここで遅延確保でもOK）
	  uint8_t* dst0 = f.bytes.data() + outBase * of.bpp;
	  for (size_t kk=0; kk<keepPtr->size(); ++kk) {
	    const uint32_t j = (*keepPtr)[kk];
	    std::memcpy(dst0 + kk*of.bpp,
			of.buf.data() + (size_t)j*of.bpp,
			of.bpp);
	  }	  
	}
	else {
	  // AoSExt など：maskあり/なしで oi と src を変えるだけ
	  for (size_t kk=0; kk<keepPtr->size(); ++kk) {
	    const uint32_t j = (*keepPtr)[kk];
	    const size_t oi = outBase + kk;
	    const uint8_t* src = of.buf.data() + (size_t)j*of.bpp;
	    switch (of.fl->spec.type) {
	    case DataType::Float:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const float*>(src));  break;
	    case DataType::Double: writeToDestFast(out, oi, *of.fl, reinterpret_cast<const double*>(src)); break;
	    case DataType::Int32:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const int32_t*>(src)); break;
	    case DataType::Int64:  writeToDestFast(out, oi, *of.fl, reinterpret_cast<const int64_t*>(src)); break;
	    }
	  }	  
	}
	  
	if(needSynth){
	  if(of.fl->ftype == FieldType::InternalEnergy){
	    fill_chunk_scalar(tmp_u, of.buf, n, of.elemSz, keepPtr, of.fl->spec.type);
	  }else if(hasE && of.fl->ftype == FieldType::ElectronFraction){
	    fill_chunk_scalar(tmp_e, of.buf, n, of.elemSz, keepPtr, of.fl->spec.type);
	  }else if(hasH2 && of.fl->ftype == FieldType::H2Fraction){
	    fill_chunk_scalar(tmp_h2, of.buf, n, of.elemSz, keepPtr, of.fl->spec.type);
	  }else if(hasG && of.fl->ftype == FieldType::Gamma){
	    fill_chunk_scalar(tmp_g, of.buf, n, of.elemSz, keepPtr, of.fl->spec.type);
	  }
	}

	if (of.fl->ftype == FieldType::Bfield && factor_Bfield_ != 1.0){
	  if (of.fl->dest == DestKind::SoA) {
	    auto &f = out.soa[*of.fl->soaKey];          // "Bfield"
	    const int comps = of.comps;                  // たぶん 3
	    const size_t start = outBase;                // このchunkの先頭
	    const size_t nscale = nwrite;                // このchunkで書いた粒子数
	    
	    if (f.type == DataType::Float) {
	      float* p = reinterpret_cast<float*>(f.bytes.data()) + start * comps;
	      const float fac = (float)factor_Bfield_;
	      for (size_t i = 0; i < nscale * (size_t)comps; ++i) p[i] *= fac;
	      
	    } else if (f.type == DataType::Double) {
	      double* p = reinterpret_cast<double*>(f.bytes.data()) + start * comps;
	      const double fac = factor_Bfield_;
	      for (size_t i = 0; i < nscale * (size_t)comps; ++i) p[i] *= fac;	      
	    }	    
	  }
	}	
      } // for field
      
      if(needSynth){
	for(size_t kk=0; kk<nwrite; ++kk){
	  const double u = tmp_u[kk];
	  if(!(u > 0.0)) continue;

	  double gamma = 5.0/3.0;
	  if(hasG && tmp_g[kk] > 0.0) gamma = tmp_g[kk];

	  double denom = 1.2;
	  if(hasE  && tmp_e[kk]  > 0.0) denom += tmp_e[kk];
	  if(hasH2 && tmp_h2[kk] > 0.0) denom -= tmp_h2[kk];

	  const double T = (gamma - 1.0) * u / denom * factor_IntEnergy_;
	  if(T > 0.0){
	    out.particles[outBase + kk].temperature = (float)T;
	  }
	}
      }

      if (factor_density_ != 1.0) 
	for(size_t kk=0; kk<nwrite; ++kk)
	  out.particles[outBase + kk].density = (float)((double)out.particles[outBase + kk].density * factor_density_);	
      
      done += n;
    }
  }

  if (outWriteCursor != totalKept) {
    fprintf(stderr, "BUG: outWriteCursor(%zu) != totalKept(%zu)\n", outWriteCursor, totalKept);
    return false;
  }
  
  return true;
}

private:
bool read_coords_chunk_(int ptype, size_t localStart, size_t n,
                        std::vector<uint8_t>& buf)
{
  try{
    H5::DataSet ds = openDataSetWithDAPL(partPath_(ptype, "Coordinates"));
    const H5::PredType memType = H5::PredType::NATIVE_FLOAT;
    const size_t elemSz = sizeof(float);
    const int comps = 3;
    readHyperslabBytes(ds, memType, (hsize_t)localStart, (hsize_t)n, comps, buf, elemSz);
    return true;
  }catch(...){
    return false;
  }
}

bool read_ids_chunk_u64_(int ptype, size_t localStart, size_t n,
                         std::vector<uint64_t>& ids)
{
  ids.resize(n);
  try{
    H5::DataSet ds = openDataSetWithDAPL(partPath_(ptype, "ParticleIDs"));

    H5::DataSpace fsp = ds.getSpace();
    hsize_t start[1] = { (hsize_t)localStart };
    hsize_t cnt[1]   = { (hsize_t)n };
    fsp.selectHyperslab(H5S_SELECT_SET, cnt, start);

    H5::DataSpace msp(1, cnt);
    ds.read(ids.data(), H5::PredType::NATIVE_ULLONG, msp, fsp);
    return true;
  }catch(...){
    // fallback（最悪時）
    for(size_t j=0;j<n;++j){
      ids[j] = (uint64_t)(IndexStart_[ptype] + localStart + j + 1);
    }
    return true;
  }
}

bool build_keep_chunk_(int ptype, size_t localStart, size_t n,
                       ParticleMask& mask,
                       std::vector<uint32_t>& keep)
{
  keep.clear();
  keep.reserve(n);

  std::vector<uint8_t> coordBuf;
  std::vector<uint64_t> ids;

  const bool needPos = mask.needPos();
  const bool needID  = mask.needID();

  const float* xyz = nullptr;
  if(needPos){
    if(!read_coords_chunk_(ptype, localStart, n, coordBuf)) return false;
    xyz = reinterpret_cast<const float*>(coordBuf.data());
  }
  if(needID){
    if(!read_ids_chunk_u64_(ptype, localStart, n, ids)) return false;
  }

  for(uint32_t j=0; j<(uint32_t)n; ++j){
    CoreSample c;
    c.type = ptype;

    if(needPos){
      c.pos[0] = xyz[3*(size_t)j + 0];
      c.pos[1] = xyz[3*(size_t)j + 1];
      c.pos[2] = xyz[3*(size_t)j + 2];
    }else{
      c.pos[0]=c.pos[1]=c.pos[2]=0.0;
    }

    c.id = needID ? ids[j] : (uint64_t)(IndexStart_[ptype] + localStart + j + 1);

    if(mask.pass(c)) keep.push_back(j);
  }
  return true;
}

static inline bool isTempSynthField(FieldType ft){
  switch(ft){
    case FieldType::InternalEnergy:
    case FieldType::ElectronFraction:
    case FieldType::H2Fraction:
    case FieldType::Gamma:
      return true;
    default:
      return false;
  }
}

};


#endif


// ------------------------------
// 連番ファイル読み込み用グローバル変数
// ------------------------------
class FileInfo{
public:
  int initialIndex = 0;
  int currentFileIndex;
  int batchSize = 1;
  int skipStep = 1;
  int currentStep = 0;      // **現在のステップ（n）**
  bool isLoading = false;   // **ロード中かどうか**
  
  char fileFormat[255] = "output_%04d.dat"; // 例: "output_%04d.dat"
  char folderPath[255] = "./example/";              // 末尾に "/" を付加する
  char filePath[512] = "./example/output_0000.dat";              // 末尾に "/" を付加する

  int currentBatchStart = initialIndex;  // 現在のバッチの開始ファイル番号  
  
  // ------------------------------
  // データフォーマット編集用ダイアログ用変数
  // ------------------------------

#ifdef HAVE_HDF5
  bool useHDF5 = false;
#endif
  std::vector<FieldSpec> formatTokens;

  void setFormatMode(FileFormat form){
    readFileFormat = form;
  };

  int getFormatMode(void){
    return static_cast<int>(readFileFormat);
  };
  
private:
  CameraContext& camCtx;
  FileFormat readFileFormat = FileFormat::Auto;
  
  // バッチ管理用変数
  TrackingVector<ParticleBlock> batchParticleBlocks; // バッチ内の各ファイルの粒子データ

#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
#endif
  
  bool showFormatDialog = false;
  std::vector<FieldSpec> formatTokensEdit; // 編集用一時コピー
  
  // 更新タイミングでのみ使うmutex
  std::mutex g_dataMutex;
    
  void syncLoadFirstFile(int targetFile, ParticleArray *P);
  void asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep);

  bool loadSingleFile(int fileNumber, ParticleBlock& particles);

#ifdef HAVE_HDF5
  TrackingVector<ParticleData> loadParticlesFromHDF5(const std::string& filename, HeaderInfo& hdr);
#endif
  
  void initDefaultFormatTokens();

  double UnitLength_in_cm;
  double UnitMass_in_g;
  double UnitVelocity_in_cm_per_s;
  double Hubble;

  MaskConfig currentMaskConfig;
  uint64_t   currentMaskRevision = 0;
  bool       enableMask = false; // mask を使うか（UIでON/OFF）
  
public:
  FileInfo(CameraContext& cam):
    camCtx(cam)
  {
    initDefaultFormatTokens();
  }

  void setUnit(ParticleArray *P){
    UnitLength_in_cm = P->UnitLength_in_cm;
    UnitMass_in_g = P->UnitMass_in_g;
    UnitVelocity_in_cm_per_s = P->UnitVelocity_in_cm_per_s;
    Hubble = P->Hubble;
  }
  
  void loadNewSnapshot(int newindex, ParticleArray* P);
  void loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray *P);
  void generateTestData(ParticleArray *P);
  
#ifdef HAVE_HDF5
  void ShowHDF5FieldMappingDialog();
  void showHDF5Dialog(void){    
    showHDF5MappingDialog = true;
    formatTokensEdit = formatTokens;
  };
#endif
  
  void DrawFormatDialog();
  void showDialog(void){
    showFormatDialog = true;
    formatTokensEdit = formatTokens;
  };

#ifdef HAVE_HDF5
  HaloCatalog readHaloCatalogFromHDF5(char *fname, bool loadIDs /*=true*/);
  
  bool groupExists(const H5::H5File &file, const std::string &groupPath)
  {
    // file.getId() でC APIの hid_t が得られるので、H5Lexists などを呼べる
    herr_t status = H5Lexists(file.getId(), groupPath.c_str(), H5P_DEFAULT);
    // status > 0 なら存在、0 なら存在しない、 <0 ならエラー
    return (status > 0);
  };
#endif

  void setMaskConfig(const MaskConfig& cfg, uint64_t rev){
    currentMaskConfig = cfg;
    currentMaskRevision = rev;
    enableMask = true;
  } 
  
  TrackingVector<int> getStarParticleID(int indexFile);
};

extern FileInfo *gFileInfo;
