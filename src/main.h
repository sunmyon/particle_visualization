#ifndef MAIN_H
#define MAIN_H

#define _USE_MATH_DEFINES 
#include <cmath>

#ifndef M_PI
#define M_PI 3.141592653589793
#endif

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
  float pos[3];          // 正規化後の座標（描画用）
  float original_pos[3]; // ファイルから読み込んだ元の座標（normalize の基準）
  float vel[3];
  float originalHsml;
  float Hsml;
  float density;
  float temperature;
  float val;             // 物理量（0～1）
  float val2;             // 物理量（0～1）
#ifdef SAVE_GPU_MEMORY
  float val_show;
#endif
  float mass;            // mass
  uint8_t   type;            // 粒子タイプ (0～5)
  uint8_t   flag_stress;
  int   ID;

  float getValue(const std::string &var) const;  
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



class ParticleArray {
private:
  bool showWindowHaloes = false;
  CameraContext& camCtx;  
  int particles_index; //current index in batch file list
  
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
  
  std::array<std::array<float, 6>, 4> particleValueMin;
  std::array<std::array<float, 6>, 4> particleValueMax;

  HeaderInfo Header;
  
  double UnitLength_in_pc = 1.;
  double UnitMass_in_msolar = 1.;
  double Hubble = 1.;

  double GravConst_internal = 6.6743e-8;
  bool useComovingCorrdinate = true;

  double UnitVelocity_in_cgs = 1.e5; // km/s
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
  
  TrackingVector<ParticleData> particles;
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
      
      for (ParticleData &p : particles) {
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
  
  void swap_particles(TrackingVector<TrackingVector<ParticleData>>& batchP, int ibatch, HeaderInfo header, int flag_reset);
  void computeStellarDensity(int type, bool flag_overwirte_hsml);

  void ShowHaloesUI();
  void showWindowHaloList(){
    showWindowHaloes = true;
  };

  int readClumpData(int snapshotIndex);

  void setUnits(){
    UnitLength_in_pc = UnitLength_in_cm / pc_in_cm;
    UnitMass_in_msolar = UnitMass_in_g / msolar_in_g;    
    UnitTime_in_s = UnitLength_in_cm / UnitVelocity_in_cgs;
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
