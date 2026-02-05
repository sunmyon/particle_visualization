#ifndef MAIN_H
#define MAIN_H

#define _USE_MATH_DEFINES 
#include <cmath>

#ifndef M_PI
#define M_PI 3.141592653589793
#endif

#include "quantity.h"
#include <vector>
#include <mutex>
#include <array>
#include <string>
#include <iostream>
#include <algorithm>

struct CameraContext;

const double PROTONMASS = 1.67262178e-24;
const double BOLTZMANN = 1.38065e-16;
const double XH  = 0.76;
const double XHe = 0.0625;  
  
extern std::size_t g_totalAllocated;
extern std::mutex g_mutex;

template<typename T>
struct TrackingAllocator {
    using value_type = T;

    TrackingAllocator() = default;
    
    template<typename U>
    constexpr TrackingAllocator(const TrackingAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        std::size_t bytes = n * sizeof(T);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_totalAllocated += bytes;
        }
	if(bytes > 1024. * 1024.)
	  std::cout << "Allocating " << bytes << " bytes. Total allocated: " << g_totalAllocated/1024./1024. << " Mbytes." << std::endl;
        return static_cast<T*>(::operator new(bytes));
    }

    void deallocate(T* p, std::size_t n) noexcept {
        std::size_t bytes = n * sizeof(T);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_totalAllocated -= bytes;
        }
	if(bytes > 1024. * 1024.)
	  std::cout << "Deallocating " << bytes << " bytes. Total allocated: " << g_totalAllocated/1024./1024. << " Mbytes." << std::endl;
        ::operator delete(p);
    }
};

template<typename T, typename U>
bool operator==(const TrackingAllocator<T>&, const TrackingAllocator<U>&) {
    return true;
}

template<typename T, typename U>
bool operator!=(const TrackingAllocator<T>& a, const TrackingAllocator<U>& b) {
    return !(a == b);
}

template<typename T>
using TrackingVector = std::vector<T, TrackingAllocator<T>>;


struct HeaderInfo
{
  int npart;
  double time;            // "time" 属性 (例: Gadget系HDF5でのシミュレーション時刻)
  double cosmic_time;
  double boxSize;         // "BoxSize" 属性 (例)
  int    NumPart_ThisFile[6]; // "NumPart_Total" 属性 (6要素配列想定)
  double Omega0;
  double OmegaLambda;
  double HubbleParam;     // "HubbleParam"
  double massTable[6];    // "MassTable" (6要素配列想定)

  double UnitLength_in_cm;
  double UnitVelocity_in_cm_per_s;
  double UnitMass_in_g;

  bool   flag_comoving;
  bool   flag_hdf5;
};


// ------------------------------
// 粒子データ用構造体
// ------------------------------
// Particle 構造体に、original_pos[3] を追加
class ParticleData {
public:
  ParticleData() noexcept {}
  
  float pos[3];          // 正規化後の座標（描画用）
  float original_pos[3]; // ファイルから読み込んだ元の座標（normalize の基準）
  float vel[3];
  float originalHsml;
  float Hsml;
  float density;
  float temperature;
  float val_show;
  float mass;            // mass
  uint8_t   type;            // 粒子タイプ (0～5)
  uint8_t   flag_stress;
  int   ID;

  float getValue(const std::string &var) const;  
};


struct AoSExtensionBuffer {
  size_t stride = 0;               // bytes per particle
  std::vector<uint8_t> bytes;      // size = stride * N

  void resize(size_t n) {
    bytes.resize(n * stride);
  }

  uint8_t* ptr(size_t i) {
    return bytes.data() + i * stride;
  }

  const uint8_t* ptr(size_t i) const {
    return bytes.data() + i * stride;
  }
};

enum class DataType : uint8_t {
  Float = 0,
  Int32 = 1,
  Int64 = 2,
  Double = 3
};

static inline size_t dataTypeSize(DataType t) {
  switch (t) {
    case DataType::Float:  return 4;
    case DataType::Int32:  return 4;
    case DataType::Int64:  return 8;
    case DataType::Double: return 8;
  }
  return 0;
}

struct SoAField {
  DataType type{};
  int comps = 1;                   // number of components per particle
  std::vector<uint8_t> bytes;      // raw data buffer

  void resize(size_t n) {
    bytes.resize(n * (size_t)comps * dataTypeSize(type));
  }

  uint8_t* ptr(size_t i) {
    return bytes.data() + i * (size_t)comps * dataTypeSize(type);
  }

  const uint8_t* ptr(size_t i) const {
    return bytes.data() + i * (size_t)comps * dataTypeSize(type);
  }
};


template<typename T>
static inline DataType toDataType();

template<> inline DataType toDataType<float>()  { return DataType::Float; }
template<> inline DataType toDataType<double>() { return DataType::Double; }
template<> inline DataType toDataType<int32_t>(){ return DataType::Int32; }
template<> inline DataType toDataType<int64_t>(){ return DataType::Int64; }

static constexpr const char* kBfieldKey = "Bfield";
static constexpr const char* kMetallicityKey = "Metallicity";
static constexpr const char* kElectronAbundanceKey = "ElectronAbundance";
static constexpr const char* kH2AbundanceKey = "H2Abundance";
static constexpr const char* kHDAbundanceKey = "HDAbundance";
static constexpr const char* kJ21Key = "J21";
static constexpr const char* kVal1Key = "Val1";
static constexpr const char* kVal2Key = "Val2";

struct ParticleBlock {
  // ---- AoS core ----
  TrackingVector<ParticleData> particles;

  // ---- AoS extension (optional) ----
  AoSExtensionBuffer aosExt;

  // ---- SoA fields (optional) ----
  // key = field label (e.g. "Bfield")
  std::unordered_map<std::string, SoAField> soa;

  HeaderInfo header;
  
  void resize(size_t n) {
    particles.resize(n);

    if (aosExt.stride > 0)
      aosExt.resize(n);

    for (auto &kv : soa)
      kv.second.resize(n);
  }
 
  size_t size() const {
    return particles.size();
  }

  void clear() {
    particles.clear();
    aosExt.bytes.clear();
    soa.clear();
  }

  // ---- SoA getter (const) ----
  template<typename T>
  const T* getSoA(const std::string& key, size_t i, int expectedComps) const {
    auto it = soa.find(key);
    if (it == soa.end()) return nullptr;

    const SoAField& f = it->second;
    if (f.type != toDataType<T>()) return nullptr;
    if (f.comps != expectedComps)   return nullptr;
    if (i >= particles.size())      return nullptr;

    return reinterpret_cast<const T*>(f.ptr(i));
  }

  // ---- SoA getter (mutable) ----
  template<typename T>
  T* getSoA(const std::string& key, size_t i, int expectedComps) {
    auto it = soa.find(key);
    if (it == soa.end()) return nullptr;

    SoAField& f = it->second;
    if (f.type != toDataType<T>()) return nullptr;
    if (f.comps != expectedComps)  return nullptr;
    if (i >= particles.size())     return nullptr;

    return reinterpret_cast<T*>(f.ptr(i));
  }

  template<typename T>
  bool setSoA(const std::string& key, size_t i, int expectedComps, const T* src) {
    T* dst = getSoA<T>(key, i, expectedComps);
    if (!dst) return false;
    std::memcpy(dst, src, sizeof(T) * expectedComps);
    return true;
  }

  // ---- SoA setter (scalar) ----
  template<typename T>
  bool setSoA1(const std::string& key, size_t i, const T& x) {
    return setSoA<T>(key, i, /*expectedComps=*/1, &x);
  }

  // ---- SoA setter (3-vector) ----
  template<typename T>
  bool setSoA3(const std::string& key, size_t i, const T x[3]) {
    return setSoA<T>(key, i, /*expectedComps=*/3, x);
  }

  // convenience: std::array
  template<typename T, size_t N>
  bool setSoA(const std::string& key, size_t i, const std::array<T, N>& a) {
    return setSoA<T>(key, i, (int)N, a.data());
  }
  
  template<typename T>
  bool hasSoA(const std::string& key, int expectedComps) const {
    auto it = soa.find(key);
    if (it == soa.end()) return false;

    const SoAField& f = it->second;
    if (f.type  != toDataType<T>()) return false;
    if (f.comps != expectedComps)   return false;

    // サイズ整合（「==」を推奨。余剰を許す設計なら「>=」でもOK）
    const size_t expectBytes = particles.size() * size_t(expectedComps) * sizeof(T);
    if (f.bytes.size() != expectBytes) return false;

    return true;
  }
  
  // ---- Bfield helpers ----
  const float* getBfield(size_t i) const { return getSoA<float>(kBfieldKey, i, 3); }
  float*       getBfield(size_t i)       { return getSoA<float>(kBfieldKey, i, 3); }
  bool setBfield(size_t i, const float *x) { return setSoA3<float>(kBfieldKey, i, x); }
  bool hasBfield() const       { return hasSoA<float>(kBfieldKey, 3); }
  
  // ---- Metallicity helpers ----
  const float* getMetallicity(size_t i) const { return getSoA<float>(kMetallicityKey, i, 1); }
  float*       getMetallicity(size_t i)       { return getSoA<float>(kMetallicityKey, i, 1); }
  bool setMetallicity(size_t i, float x) { return setSoA1<float>(kMetallicityKey, i, x); }
  bool hasMetallicity() const  { return hasSoA<float>(kMetallicityKey, 1); }

  // ---- ElectronAbundance helpers ----
  const float* getElectronAbundance(size_t i) const { return getSoA<float>(kElectronAbundanceKey, i, 1); }
  float*       getElectronAbundance(size_t i)       { return getSoA<float>(kElectronAbundanceKey, i, 1); }
  bool setElectronAbundance(size_t i, float x) { return setSoA1<float>(kElectronAbundanceKey, i, x); }
  bool hasElectronAbundance() const  { return hasSoA<float>(kElectronAbundanceKey, 1); }

  // ---- H2Abundance helpers ----
  const float* getH2Abundance(size_t i) const { return getSoA<float>(kH2AbundanceKey, i, 1); }
  float*       getH2Abundance(size_t i)       { return getSoA<float>(kH2AbundanceKey, i, 1); }
  bool setH2Abundance(size_t i, float x) { return setSoA1<float>(kH2AbundanceKey, i, x); }
  bool hasH2Abundance() const  { return hasSoA<float>(kH2AbundanceKey, 1); }
  
  // ---- HDAbundance helpers ----
  const float* getHDAbundance(size_t i) const { return getSoA<float>(kHDAbundanceKey, i, 1); }
  float*       getHDAbundance(size_t i)       { return getSoA<float>(kHDAbundanceKey, i, 1); }
  bool setHDAbundance(size_t i, float x) { return setSoA1<float>(kHDAbundanceKey, i, x); }
  bool hasHDAbundance() const  { return hasSoA<float>(kHDAbundanceKey, 1); }

  // ---- J21 helpers ----
  const float* getJ21(size_t i) const { return getSoA<float>(kJ21Key, i, 1); }
  float*       getJ21(size_t i)       { return getSoA<float>(kJ21Key, i, 1); }
  bool setJ21(size_t i, float x) { return setSoA1<float>(kJ21Key, i, x); }
  bool hasJ21() const  { return hasSoA<float>(kJ21Key, 1); }

  // ---- val1 helpers ----
  const float* getVal1(size_t i) const { return getSoA<float>(kVal1Key, i, 1); }
  float*       getVal1(size_t i)       { return getSoA<float>(kVal1Key, i, 1); }
  bool setVal1(size_t i, float x) { return setSoA1<float>(kVal1Key, i, x); }
  bool hasVal1() const  { return hasSoA<float>(kVal1Key, 1); }

  // ---- val2 helpers ----
  const float* getVal2(size_t i) const { return getSoA<float>(kVal2Key, i, 1); }
  float*       getVal2(size_t i)       { return getSoA<float>(kVal2Key, i, 1); }
  bool setVal2(size_t i, float x) { return setSoA1<float>(kVal2Key, i, x); }
  bool hasVal2() const  { return hasSoA<float>(kVal2Key, 1); }

  int nAllQ = 0;
  int nUIQ  = 0;
  std::array<QuantityId, kMaxQ> allQ;
  std::array<QuantityId, kMaxQ> uiQ;

  void rebuildQuantities() {
    nAllQ = 0; nUIQ = 0;

    auto pushAll = [&](QuantityId q){ allQ[nAllQ++] = q; };
    auto pushUI  = [&](QuantityId q){ uiQ[nUIQ++]  = q; };

    for (auto q : {QuantityId::Density, QuantityId::Temperature,
                   QuantityId::Mass, QuantityId::Hsml}) {
      pushAll(q);
      pushUI(q);
    }

    // --- 内部用（UIに出さないが all には入れる）---
    for (auto q : {QuantityId::PosX, QuantityId::PosY, QuantityId::PosZ, QuantityId::Radius, QuantityId::VRad}) {
      pushAll(q);
    }

    // --- 拡張（存在するときだけ）---
    if (hasBfield()) {
      pushAll(QuantityId::B);
      pushUI(QuantityId::B);
    }
    if (hasMetallicity()) {
      pushAll(QuantityId::Metallicity);
      pushUI(QuantityId::Metallicity);
    }
    if (hasElectronAbundance()) {
      pushAll(QuantityId::ElectronAbundance);
      pushUI(QuantityId::ElectronAbundance);
    }
    if (hasH2Abundance()) {
      pushAll(QuantityId::H2Abundance);
      pushUI(QuantityId::H2Abundance);
    }
    if (hasHDAbundance()) {
      pushAll(QuantityId::HDAbundance);
      pushUI(QuantityId::HDAbundance);
    }
    if (hasJ21()) {
      pushAll(QuantityId::J21);
      pushUI(QuantityId::J21);
    }
    if (hasVal1()) {
      pushAll(QuantityId::Val);
      pushUI(QuantityId::Val);
    }
    if (hasVal2()) {
      pushAll(QuantityId::Val2);
      pushUI(QuantityId::Val2);
    }
  }
};


//-----------------------------------------------------------
// HaloData : haloの情報を保持する構造体 (ID, 質量, 中心座標, 半径など)
//-----------------------------------------------------------
class HaloData {
public:
  int   id;

  int   GroupLen;
  int   GroupLenType[6];
  float GroupMass;
  float GroupMassType[6];
  
  float GroupPos[3];
  float GroupVel[3];
  float radius;
  
  float GroupMetallicity[2];

  float getHaloValue(const std::string &var) const;
};


class ClumpData {
public:
  int clumpID = -1;
  int nextClumpID = -1;

  int count = 0;
  int offset = 0;

  float originalPos[3] = {0.,0.,0.};
  float Pos[3] = {0.,0.,0.};
  float density = 0.;
  float temperature = 0.;
  float mass = 0.;

  int stellar_count = 0;
  int stellar_id = -1;
  float stellar_mass = 0.;
  
  TrackingVector<int> IDs;
  
  float getClumpValue(const std::string &var) const;

  void get_next_clump_position(const TrackingVector<ClumpData>& clump_in_next_snapshot, float *next_pos){
    struct ParticleInfo {
      int ID;
      int clumpID;
    };

    TrackingVector<struct ParticleInfo> particleIDs;

    for(size_t ic=0;ic< clump_in_next_snapshot.size();ic++){
      for(auto id : clump_in_next_snapshot[ic].IDs){
	struct ParticleInfo temp;
	temp.ID = id;
	temp.clumpID = ic;
	
	particleIDs.push_back(temp);      
      }
    }
  
    std::sort(particleIDs.begin(), particleIDs.end(),
	      [](const auto &a, const auto &b) {
		return a.ID < b.ID;
	      });
    
    TrackingVector<int> counts(clump_in_next_snapshot.size(), 0);    
    size_t i1=0, i2=0;
    while(i1 < IDs.size() && i2 < particleIDs.size()){
      if(IDs[i1] < particleIDs[i2].ID){
	i1++;
      }
      else if(IDs[i1] > particleIDs[i2].ID){
	i2++;
      }
      else {
	int clumpID = particleIDs[i2].clumpID;
	counts[clumpID]++;	
	i1++; i2++;
      }
    }
      
    int max_count = -1;
    int max_index = -1;
    for(size_t i=0;i < clump_in_next_snapshot.size();i++){
      if(counts[i] > max_count){
	max_count = counts[i];
	max_index = static_cast<int>(i);
      }
    }

    next_pos[0] = clump_in_next_snapshot[max_index].Pos[0];
    next_pos[1] = clump_in_next_snapshot[max_index].Pos[1];
    next_pos[2] = clump_in_next_snapshot[max_index].Pos[2];
  }

  float getValue(const std::string &var) const;  
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
      const float* B = blk.getBfield((size_t)ipart);
      if (!B){ out[0] = out[1] = out[2] = 0.;}
      else{ out[0]=B[0];out[1]=B[1];out[2]=B[2];}
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
    case VectorId::Bfield:
      if (blk.hasBfield()) blk.setBfield(ipart, in);
      return;
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
      const float* B = blk.getBfield((size_t)ipart);
      if (!B) return 0.0f;
      return std::sqrt(B[0]*B[0] + B[1]*B[1] + B[2]*B[2]); // 例: |B|
    }

    case QuantityId::Metallicity: {
      const float* Z = blk.getMetallicity((size_t)ipart);
      if (!Z) return 0.0f;
      return Z[0];
    }

    case QuantityId::ElectronAbundance: {
      const float* Z = blk.getElectronAbundance((size_t)ipart);
      if (!Z) return 0.0f;
      return Z[0];
    }

    case QuantityId::H2Abundance: {
      const float* Z = blk.getH2Abundance((size_t)ipart);
      if (!Z) return 0.0f;
      return Z[0];
    }

    case QuantityId::HDAbundance: {
      const float* Z = blk.getHDAbundance((size_t)ipart);
      if (!Z) return 0.0f;
      return Z[0];
    }

    case QuantityId::J21: {
      const float* Z = blk.getJ21((size_t)ipart);
      if (!Z) return 0.0f;
      return Z[0];
    }

    case QuantityId::Val: {
      const float* Z = blk.getVal1((size_t)ipart);
      if (!Z) return 0.0f;
      return Z[0];
    }
      
    case QuantityId::Val2: {
      const float* Z = blk.getVal2((size_t)ipart);
      if (!Z) return 0.0f;
      return Z[0];
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
      if (blk.hasVal1()) blk.setVal1(ipart, x);
      return;
    case QuantityId::Val2:
      if (blk.hasVal2()) blk.setVal2(ipart, x);
      return;
    default:
      // Radius/VRad/B など “派生量” は set 不可でOK（Bridgeでdirty対象にしない）
      return;
  }
}

static constexpr int kNumTypes = 6;
extern QuantityId selectedQuantity[6];

class ParticleArray {
private:
  bool showWindowHaloes = false;
  CameraContext& camCtx;  
  int particleBlock_index; //current index in batch file list
  
public:
  ParticleArray(CameraContext& cam):
    camCtx(cam)
  {};

  float originalMax = 0.0f;
  float desiredMax = 1.0f;         // ユーザー指定の目標値（例: 1, 10, 100, ...）
  float normalizationFactor = 1.0f;  // 適用された倍率
  
  bool particlesDirty = true;
  bool velocityDirty = true;
  bool flagParticleIndexDirty = true;

  std::array<std::array<float, kNumTypes>, kMaxQ> particleValueMin;
  std::array<std::array<float, kNumTypes>, kMaxQ> particleValueMax;

  double UnitLength_in_pc = 1.;
  double UnitMass_in_msolar = 1.;
  double Hubble = 1.;

  double GravConst_internal = 6.6743e-8;
  bool useComovingCorrdinate = true;

  double UnitVelocity_in_cm_per_s = 1.e5; // km/s
  double UnitLength_in_cm = 3.08e18; //pc
  double UnitMass_in_g = 1.98e33; //Msun
  double UnitTime_in_s = 3.08e13;
  double UnitTime_in_yr = 0.97e6;

  static constexpr double yr_in_sec = 3.15576e7;
  static constexpr double msolar_in_g = 1.989e33;
  static constexpr double au_in_cm = 1.49598e13;
  static constexpr double pc_in_cm = 3.085678e18;
  static constexpr double kpc_in_cm = 3.085678e21;
  static constexpr double Mpc_in_cm = 3.085678e24;
  static constexpr double GravConst = 6.6743e-8;

  ParticleBlock particleBlock;
    
  TrackingVector<uint8_t> flag_mask;
  
  TrackingVector<HaloData> Haloes;
  TrackingVector<ClumpData> Clumps;
  std::string fname_clump_file;
  bool flag_follow_clump_center = false;
  bool flag_renew_clumpList = false;
  int TargetClumpID;

  bool flag_follow_particle_ID = false;
  int TargetParticleID;

  bool findParticleID(int ID, float *pos);
  
  void rescalePositions(){
    if (originalMax > 0) {
      float scale = desiredMax / originalMax;
      
      for (ParticleData &p : particleBlock.particles) {
	p.pos[0] = p.original_pos[0] * scale;
	p.pos[1] = p.original_pos[1] * scale;
	p.pos[2] = p.original_pos[2] * scale;
	p.Hsml = p.originalHsml * scale;
      }
      
      normalizationFactor = scale;
    } else {
      normalizationFactor = 1.0f;
    }

    particlesDirty = true;  // グローバルなフラグをtrueに設定
  };
  
  void swap_particles(TrackingVector<ParticleBlock>& batchP, int ibatch, int flag_reset);
  void computeStellarDensity(int type, bool flag_overwirte_hsml);

  void ShowHaloesUI();
  void showWindowHaloList(){
    showWindowHaloes = true;
  };

  int readClumpData(int snapshotIndex);

  void setUnits(){
    UnitLength_in_pc = UnitLength_in_cm / pc_in_cm;
    UnitMass_in_msolar = UnitMass_in_g / msolar_in_g;    
    UnitTime_in_s = UnitLength_in_cm / UnitVelocity_in_cm_per_s;
    UnitTime_in_yr = UnitTime_in_s / yr_in_sec;
    
    GravConst_internal = GravConst / std::pow(UnitLength_in_cm, 3) * UnitMass_in_g * std::pow(UnitTime_in_s, 2);
  }
};


// CGALに依存しない抽象インターフェース
class IConvexHull {
public:
    virtual ~IConvexHull() = default;
    // 3次元の点が凸包内部にあるかどうかを判定するメソッド
    virtual bool isInside(const std::array<double, 3>& pt) const = 0;
};
#endif
