#include "BridgeAdapter.h"
#include <algorithm>
#include <cstring>

namespace bridge {
  bool loadInitialFromAoS(PythonBridge& bridge, const ParticleArray& P) {
    auto& S = bridge.shared();
    
    const size_t srcN = P.particleBlock.particles.size();
    const size_t N    = std::min(static_cast<size_t>(S.N), srcN);
    if (N == 0 || !S.pos) return false;

    for (size_t i = 0; i < N; ++i)
      {
	const ParticleData* src = &P.particleBlock.particles[i];

	float v[3];
	if (S.pos) {
	  getVectorValue(P.particleBlock, *src, i, VectorId::Pos, v);
	  S.pos[3*i+0]=v[0]; S.pos[3*i+1]=v[1]; S.pos[3*i+2]=v[2];
	}
	if (S.origpos) {
	  getVectorValue(P.particleBlock, *src, i, VectorId::OriginalPos, v);
	  S.origpos[3*i+0]=v[0]; S.origpos[3*i+1]=v[1]; S.origpos[3*i+2]=v[2];
	}
	if (S.vel) {
	  getVectorValue(P.particleBlock, *src, i, VectorId::Vel, v);
	  S.vel[3*i+0]=v[0]; S.vel[3*i+1]=v[1]; S.vel[3*i+2]=v[2];
	}
	if (S.B) {
	  getVectorValue(P.particleBlock, *src, i, VectorId::Bfield, v);
	  S.B[3*i+0]=v[0]; S.B[3*i+1]=v[1]; S.B[3*i+2]=v[2];
	}

	if (S.dens) S.dens[i] = getScalarValue(P.particleBlock, *src, (int)i, QuantityId::Density);
	if (S.temp) S.temp[i] = getScalarValue(P.particleBlock, *src, (int)i, QuantityId::Temperature);
	if (S.mass) S.mass[i] = getScalarValue(P.particleBlock, *src, (int)i, QuantityId::Mass);
	if (S.hsml) S.hsml[i] = getScalarValue(P.particleBlock, *src, (int)i, QuantityId::Hsml);
	if (S.val ) S.val[i]  = getScalarValue(P.particleBlock, *src, (int)i, QuantityId::Val);
	if (S.val2) S.val2[i] = getScalarValue(P.particleBlock, *src, (int)i, QuantityId::Val2);
		
	if (S.id)   S.id[i]   = static_cast<uint32_t>(src->ID);
	if (S.type) S.type[i] = static_cast<uint8_t>(src->type);
	if (S.flag) S.flag[i] = static_cast<uint8_t>(src->flag_stress);
      }

    // Visibility mask: 1 = hidden, 0 = visible.
    if (S.mask) {
      if (!P.flag_mask.empty()) {
	const size_t M = std::min(N, P.flag_mask.size());
	std::memcpy(S.mask, P.flag_mask.data(), M);
	if (M < N) std::memset(S.mask + M, 0, N - M); // Fill the rest as visible.
      } else {
	std::memset(S.mask, 0, N); // All visible.
      }
    }
    
    bridge.setSharedValid(true);
    return true;
  }

  void applyFromSharedToAoS(const PythonBridge::Shared& S, ParticleArray& P,
			    const std::vector<FieldId>& dirty) {
    const size_t N = std::min<size_t>(S.N, P.particleBlock.particles.size());
    auto need = [&](FieldId id){
      if (dirty.empty()) return true;
      return std::find(dirty.begin(), dirty.end(), id) != dirty.end();
    };

    if (need(F_POS) && S.pos) {
      for (size_t i=0; i<N; ++i) {
	ParticleData& p = P.particleBlock.particles[i];
	float in[3] = { S.pos[3*i+0], S.pos[3*i+1], S.pos[3*i+2] };
	setVectorValue(P.particleBlock, p, i, VectorId::Pos, in);
      }
      P.particlesDirty = true;
    }

    if (need(F_ORIGPOS) && S.origpos) {
      for (size_t i=0; i<N; ++i) {
	ParticleData& p = P.particleBlock.particles[i];
	float in[3] = { S.origpos[3*i+0], S.origpos[3*i+1], S.origpos[3*i+2] };
	setVectorValue(P.particleBlock, p, i, VectorId::OriginalPos, in);
      }
      P.particlesDirty = true;
    }

    if (need(F_VEL) && S.vel) {
      for (size_t i=0; i<N; ++i) {
	ParticleData& p = P.particleBlock.particles[i];
	float in[3] = { S.vel[3*i+0], S.vel[3*i+1], S.vel[3*i+2] };
	setVectorValue(P.particleBlock, p, i, VectorId::Vel, in);
      }
      P.velocityDirty = true;
    }

    if (need(F_B) && S.B) {
      for (size_t i=0; i<N; ++i) {
	ParticleData& p = P.particleBlock.particles[i];
	float in[3] = { S.B[3*i+0], S.B[3*i+1], S.B[3*i+2] };
	setVectorValue(P.particleBlock, p, i, VectorId::Bfield, in);
      }
    }

    if (need(F_DENS) && S.dens) for (size_t i=0; i<N; ++i) setScalarValue(P.particleBlock, P.particleBlock.particles[i], i, QuantityId::Density, S.dens[i]);
    if (need(F_TEMP) && S.temp) for (size_t i=0; i<N; ++i) setScalarValue(P.particleBlock, P.particleBlock.particles[i], i, QuantityId::Temperature, S.temp[i]);
    if (need(F_MASS) && S.mass) for (size_t i=0; i<N; ++i) setScalarValue(P.particleBlock, P.particleBlock.particles[i], i, QuantityId::Mass, S.mass[i]);
    if (need(F_HSML) && S.hsml) for (size_t i=0; i<N; ++i) setScalarValue(P.particleBlock, P.particleBlock.particles[i], i, QuantityId::Hsml, S.hsml[i]);
    if (need(F_VAL ) && S.val ) for (size_t i=0; i<N; ++i) setScalarValue(P.particleBlock, P.particleBlock.particles[i], i, QuantityId::Val, S.val[i]);
    if (need(F_VAL2) && S.val2) for (size_t i=0; i<N; ++i) setScalarValue(P.particleBlock, P.particleBlock.particles[i], i, QuantityId::Val2, S.val2[i]);
    if ((need(F_DENS) && S.dens) || (need(F_TEMP) && S.temp) ||
        (need(F_MASS) && S.mass) || (need(F_HSML) && S.hsml) ||
        (need(F_VAL) && S.val) || (need(F_VAL2) && S.val2)) {
      P.particlesDirty = true;
    }
    
    if (need(F_ID)   && S.id)   for (size_t i=0; i<N; ++i) P.particleBlock.particles[i].ID   = static_cast<uint32_t>(S.id[i]);
    if (need(F_TYPE) && S.type) for (size_t i=0; i<N; ++i) P.particleBlock.particles[i].type = static_cast<uint8_t>(S.type[i]);
    if (need(F_FLAG) && S.flag) for (size_t i=0; i<N; ++i) P.particleBlock.particles[i].flag_stress = static_cast<uint8_t>(S.flag[i]);
    if ((need(F_ID) && S.id) || (need(F_TYPE) && S.type) ||
        (need(F_FLAG) && S.flag)) {
      P.particlesDirty = true;
    }
    if (need(F_MASK) && S.mask) {
      if (P.flag_mask.size() < N) P.flag_mask.resize(N, 0);
      for (size_t i=0; i<N; ++i) P.flag_mask[i] = static_cast<uint8_t>(S.mask[i]);
      P.particlesDirty = true;
    }
  }

} 
  
