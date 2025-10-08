#include "BridgeAdapter.h"
#include <cstring>
#include <mutex>
#include "main.h"

namespace bridge {
  bool loadInitialFromAoS(PythonBridge& bridge, const ParticleArray& P, size_t stride_bytes) {
    auto& S = bridge.shared();
    
    const size_t srcN = P.particles.size();
    const size_t N    = std::min(static_cast<size_t>(S.N), srcN);
    if (N == 0) return false;
    
    const bool contiguous = (stride_bytes == sizeof(ParticleData));

    for (size_t i = 0; i < N; ++i)
      {
	const ParticleData* src;
	if (contiguous) {
	  src = &P.particles[i];
	} else {
	  const uint8_t* base = reinterpret_cast<const uint8_t*>(P.particles.data());
	  src = reinterpret_cast<const ParticleData*>(base + i * stride_bytes);
	}

	// SoA へ展開
	if (S.pos) {
	  S.pos[3*i+0] = src->original_pos[0];
	  S.pos[3*i+1] = src->original_pos[1];
	  S.pos[3*i+2] = src->original_pos[2];
	}
	if (S.pos_scaled) {
	  S.pos_scaled[3*i+0] = src->pos[0];
	  S.pos_scaled[3*i+1] = src->pos[1];
	  S.pos_scaled[3*i+2] = src->pos[2];
	}
	if (S.vel) {
	  S.vel[3*i+0] = src->vel[0];
	  S.vel[3*i+1] = src->vel[1];
	  S.vel[3*i+2] = src->vel[2];
	}

	if (S.dens) S.dens[i] = src->density;
	if (S.temp) S.temp[i] = src->temperature;
	if (S.mass) S.mass[i] = src->mass;
	if (S.hsml) S.hsml[i] = src->Hsml;
	if (S.val ) S.val[i]  = src->val;
	if (S.val2) S.val2[i] = src->val2;

	if (S.id)   S.id[i]   = static_cast<uint32_t>(src->ID);
	if (S.type) S.type[i] = static_cast<uint8_t>(src->type);
	if (S.flag) S.flag[i] = static_cast<uint8_t>(src->flag_stress);
      }

    // 可視マスク（1=非表示 / 0=表示）
    if (S.mask) {
      if (!P.flag_mask.empty()) {
	const size_t M = std::min(N, P.flag_mask.size());
	std::memcpy(S.mask, P.flag_mask.data(), M);
	if (M < N) std::memset(S.mask + M, 0, N - M); // 残りは表示(0)で埋める
      } else {
	std::memset(S.mask, 0, N); // 全表示
      }
    }
    
    bridge.setSharedValid(true);
    return true;
  }

  void applyFromSharedToAoS(const PythonBridge::Shared& S, ParticleArray& P,
			    const std::vector<FieldId>& dirty) {
    const size_t N = std::min<size_t>(S.N, P.particles.size());
    auto need = [&](FieldId id){
      if (dirty.empty()) return true;
      return std::find(dirty.begin(), dirty.end(), id) != dirty.end();
    };

    if (need(F_POS) && S.pos) {
      for (size_t i=0; i<N; ++i) {
	P.particles[i].pos[0] = S.pos[3*i+0];
	P.particles[i].pos[1] = S.pos[3*i+1];
	P.particles[i].pos[2] = S.pos[3*i+2];
      }
      P.particlesDirty = true;
    }

    if (need(F_VEL) && S.vel) {
      for (size_t i=0; i<N; ++i) {
	P.particles[i].vel[0] = S.vel[3*i+0];
	P.particles[i].vel[1] = S.vel[3*i+1];
	P.particles[i].vel[2] = S.vel[3*i+2];
      }
      P.velocityDirty = true;
    }

    if (need(F_DENS) && S.dens) for (size_t i=0; i<N; ++i) P.particles[i].density     = S.dens[i];
    if (need(F_TEMP) && S.temp) for (size_t i=0; i<N; ++i) P.particles[i].temperature = S.temp[i];
    if (need(F_MASS) && S.mass) for (size_t i=0; i<N; ++i) P.particles[i].mass        = S.mass[i];
    if (need(F_HSML) && S.hsml) for (size_t i=0; i<N; ++i) P.particles[i].Hsml        = S.hsml[i];
    if (need(F_VAL ) && S.val ) for (size_t i=0; i<N; ++i) P.particles[i].val         = S.val[i];
    if (need(F_VAL2)&& S.val2) for (size_t i=0; i<N; ++i) P.particles[i].val2        = S.val2[i];
    if (need(F_ID)   && S.id)   for (size_t i=0; i<N; ++i) P.particles[i].ID   = static_cast<uint32_t>(S.id[i]);
    if (need(F_TYPE) && S.type) for (size_t i=0; i<N; ++i) P.particles[i].type = static_cast<uint8_t>(S.type[i]);

    if (need(F_FLAG) && S.flag) for (size_t i=0; i<N; ++i) P.particles[i].flag_stress = static_cast<uint8_t>(S.flag[i]);
    if (need(F_MASK) && S.mask) for (size_t i=0; i<N; ++i) P.flag_mask[i] = static_cast<uint8_t>(S.mask[i]);
  }

} 
  

