#pragma once
#include "app/ui_state.h"

struct CoreSample {
  double   pos[3];
  uint64_t id;
  int      type; // 0..5 (HDF5なら ptype)
};

class ParticleMask {
public:
  explicit ParticleMask(ParticleMaskConfig cfg): cfg_(cfg){ rebuildNeeds_(); }

  bool needPos() const { return need_pos_; }
  bool needID()  const { return need_id_; }

  bool typeEnabled(int t) const {
    return cfg_.typeMode[t] != ParticleMaskConfig::TypeMode::Off;
  }
  bool typeThinOK(int t) const {
    return cfg_.typeMode[t] == ParticleMaskConfig::TypeMode::On_ThinOK;
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
        if(cfg_.outsideMode == ParticleMaskConfig::OutsideMode::Drop) return false;
        if(cfg_.outsideMode == ParticleMaskConfig::OutsideMode::Thin){
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
      if (cfg_.typeMode[t] == ParticleMaskConfig::TypeMode::Off) return true;
    }
    // sphereがONのときのみoutsideModeが意味を持つのでここでは見ない
    return false;
  }
  
  uint64_t idStride() const { return id_stride_; }
  const ParticleMaskConfig& config() const { return cfg_; }

private:
  ParticleMaskConfig cfg_;
  uint64_t id_stride_ = 1;

  bool need_pos_ = false;
  bool need_id_  = false;

  void rebuildNeeds_(){
    need_pos_ = cfg_.enableSphere;
    const bool need_id_for_outside = (cfg_.enableSphere && cfg_.outsideMode==ParticleMaskConfig::OutsideMode::Thin);
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
