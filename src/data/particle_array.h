#pragma once
#include "particle_block.h"
#include "halo_data.h"
#include "core/tracking_vector.h"
#include "interaction/camera.h"
#include "core/units.h"

struct NormalizationContext;

class ParticleArray {
private:
  int particleBlock_index; //current index in batch file list
  
public:
  ParticleArray() = default;

  void rescalePositions(NormalizationContext& ctx);  
  
  bool particlesDirty = true;
  bool velocityDirty = true;
  bool flagParticleIndexDirty = true;
  float originalMax = 1.;
  
  std::array<std::array<float, kNumTypes>, kMaxQ> particleValueMin;
  std::array<std::array<float, kNumTypes>, kMaxQ> particleValueMax;

  UnitSystem units;
  
  void setUnits(){
    units.updateDerived();
  }
  
  ParticleBlock particleBlock;
    
  TrackingVector<uint8_t> flag_mask;
  
  TrackingVector<HaloData> Haloes;
  std::vector<std::vector<uint64_t>> haloIDs;   // size = Haloes.size() or empty
  std::vector<uint8_t> haloChecked;            // 0/1
  bool haloIDsLoaded = false;

  void ApplyHaloStress(int iHalo, bool on)
  {
    if (!haloIDsLoaded) return;
    if (iHalo < 0 || iHalo >= (int)haloIDs.size()) return;
    
    const uint8_t v = on ? 1 : 0;
    
    for (uint64_t pid : haloIDs[iHalo]) {
      size_t ip = particleBlock.findIndexByID(pid);
      if (ip == (size_t)-1) continue;
      particleBlock.particles[ip].flag_stress = v;
    }
    particlesDirty = true;
  }

  //void recomputeHaloPositionsFromParticles(bool useMassWeight, bool useOriginalPos, int  minParticles);
  void recomputeHaloPositionsFromParticles(bool useMassWeight,
					   bool useOriginalPos,
					   int  minParticles)
  {
    if (!haloIDsLoaded) return;
    if (haloIDs.size() != Haloes.size()) return;

    // ID辞書が必要
    if (particleBlock.id2indexDirty) particleBlock.rebuildIdIndex();

    const size_t nHalos = Haloes.size();

    for (size_t ih = 0; ih < nHalos; ++ih) {
      const auto& ids = haloIDs[ih];
      if ((int)ids.size() < minParticles) continue;

      double sx = 0.0, sy = 0.0, sz = 0.0;
      double sw = 0.0;

      for (uint64_t pid : ids) {
	const size_t ip = particleBlock.findIndexByID(pid);
	if (ip == (size_t)-1) continue;

	const ParticleData& p = particleBlock.particles[ip];

	const float* x = useOriginalPos ? p.original_pos : p.pos;

	double w = 1.0;
	if (useMassWeight) 
	  w = (p.mass > 0.0f) ? (double)p.mass : 1.0;      

	sx += w * (double)x[0];
	sy += w * (double)x[1];
	sz += w * (double)x[2];
	sw += w;
      }

      if (sw <= 0.0) continue;

      const float cx = (float)(sx / sw);
      const float cy = (float)(sy / sw);
      const float cz = (float)(sz / sw);

      Haloes[ih].GroupPos[0] = cx;
      Haloes[ih].GroupPos[1] = cy;
      Haloes[ih].GroupPos[2] = cz;
    }
  };
  
  bool findParticleID(int ID, float *pos)
  {
    size_t ip = particleBlock.findIndexByID((uint64_t)(int64_t)ID);
    if (ip == (size_t)-1) return false;
    
    const auto &p = particleBlock.particles[ip];
    pos[0] = p.pos[0]; pos[1] = p.pos[1]; pos[2] = p.pos[2];
    return true;
  }
    
  bool setParticleBlock(ParticleBlock&& newBlock, ParticleBlock* oldBlock, NormalizationContext& ctx);
  void computeStellarDensity(const std::array<bool,6>& selType, bool flag_overwirte_hsml, const NormalizationContext& ctx);
};
