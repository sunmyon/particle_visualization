#include "data/particle_selection.h"
#include "data/particle_block.h"
#include "data/header_info.h"
#include "data/quantity_catalog_builder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

ParticleBlock::BuildResult ParticleBlock::rebuild(float desiredMax, const QuantityCatalogState& catalog){
  BuildResult result;
  
  if (!particles.empty()) {
    result.originalMax = 0.;
    for (int q = 0; q < catalog.nUIQ; ++q) {
      for (int t = 0; t < kNumTypes; ++t) {
	result.valueMin[q][t] = std::numeric_limits<float>::max();
	result.valueMax[q][t] = -std::numeric_limits<float>::max();
      }
    }

    int npart_type[kNumTypes] = {0,0,0,0,0,0};

#pragma omp parallel
    {
      float localMax = 0.0f;
      float localMin[kMaxQ][kNumTypes], localMaxV[kMaxQ][kNumTypes];
      int   local_npart_type[kNumTypes] = {0};

      // thread-local init
      for (int q = 0; q < catalog.nUIQ; ++q) {
        for (int t = 0; t < kNumTypes; ++t) {
          localMin[q][t]  = std::numeric_limits<float>::max();
          localMaxV[q][t] = -std::numeric_limits<float>::max();
        }
      }

#pragma omp for
      for (int i = 0; i < (int)particles.size(); ++i) {
        ParticleData& p = particles[i];
        int type = p.type;
        const bool validType = (type >= 0 && type < kNumTypes);
        if (!validType) {
          type = 0;
        }
        if (validType) {
          local_npart_type[type]++;
        }

        // maxVal
        float m = std::max(std::fabs(p.original_pos[0]),
			   std::max(std::fabs(p.original_pos[1]), std::fabs(p.original_pos[2])));
        if (m > localMax) localMax = m;

        // initialize pos/Hsml
        for (int k = 0; k < 3; ++k) p.pos[k] = p.original_pos[k];
        p.Hsml = p.originalHsml;
        p.flag_stress = 0;

        // renew min/max
        if (validType) {
          for (int q = 0; q < catalog.nUIQ; ++q) {
            float v = getScalarValue(*this, p, i, catalog.uiQ[q]);
            localMin[q][type]  = std::min(localMin[q][type],  v);
            localMaxV[q][type] = std::max(localMaxV[q][type], v);
          }
        }
      }

#pragma omp critical
      {
        if (localMax > result.originalMax) result.originalMax = localMax;

        for (int t = 0; t < kNumTypes; ++t) npart_type[t] += local_npart_type[t];

        for (int q = 0; q < catalog.nUIQ; ++q)
          for (int t = 0; t < kNumTypes; ++t) {
            result.valueMin[q][t] = std::min(result.valueMin[q][t], localMin[q][t]);
            result.valueMax[q][t] = std::max(result.valueMax[q][t], localMaxV[q][t]);
          }
      }
    } // omp parallel

    // scaling (after maxVal is determined)
    if (result.originalMax > 0.0f) {
      float invMaxVal = desiredMax / result.originalMax;
#pragma omp parallel for
      for (int i = 0; i < (int)particles.size(); ++i) {
        ParticleData& p = particles[i];
        for (int k = 0; k < 3; ++k) p.pos[k] *= invMaxVal;
        p.Hsml *= invMaxVal;
      }
    }

    // 4) set 0 to max/min values if no particle for each particle type
    for (int q = 0; q < catalog.nUIQ; ++q) {
      for (int t = 0; t < kNumTypes; ++t) {
        if (npart_type[t] == 0) {
          result.valueMin[q][t] = result.valueMax[q][t] = 0.0f;
        }
      }
    }
  } // particles not empty

  return result;
}

bool ParticleBlock::ComputeAngularMomentumAxis(const ParticleSelectionOption& op,
                                               glm::vec3& outAxis) const
{
  const float r2max = (op.radius > 0.0f) ? (op.radius * op.radius) : -1.0f;

  glm::dvec3 vcm(0.0);
  double msum = 0.0;

  if (op.flagSubtractBulkVelocity) {
    for (const auto& p : particles) {
      const int t = static_cast<int>(p.type);
      if (t < 0 || t >= 6) continue;
      if (!op.useType[t]) continue;

      glm::dvec3 r = glm::dvec3(p.pos[0], p.pos[1], p.pos[2]) - glm::dvec3(op.center);
      if (r2max > 0.0f && glm::dot(r, r) > r2max) continue;

      vcm += static_cast<double>(p.mass) * glm::dvec3(p.vel[0], p.vel[1], p.vel[2]);
      msum += static_cast<double>(p.mass);
    }
    if (msum > 0.0) vcm /= msum;
  }

  glm::dvec3 L(0.0);
  for (const auto& p : particles) {
    const int t = static_cast<int>(p.type);
    if (t < 0 || t >= 6) continue;
    if (!op.useType[t]) continue;

    glm::dvec3 r = glm::dvec3(p.pos[0], p.pos[1], p.pos[2]) - glm::dvec3(op.center);
    if (r2max > 0.0f && glm::dot(r, r) > r2max) continue;

    glm::dvec3 v = glm::dvec3(p.vel[0], p.vel[1], p.vel[2]) - vcm;
    L += static_cast<double>(p.mass) * glm::cross(r, v);
  }

  const double n2 = glm::dot(L, L);
  if (n2 < 1e-24) return false;

  outAxis = glm::normalize(glm::vec3(L));
  return true;
}


ParticleBlock ParticleBlock::makeTestParticleBlock(HeaderInfo& header)
{
  std::mt19937_64 rng(12345);
  std::uniform_real_distribution<double> ud(-1.0, 1.0);

  const int n_side = 50;
  const double x_min = -50.0;
  const double x_max = 50.0;
  const double xlen  = x_max - x_min;

  const double dx    = xlen / static_cast<double>(n_side);
  const double amp   = 0.001;
  const double Omega = 100.0;

  ParticleBlock block;

  header.npart = n_side * n_side * n_side;

  header.time = 0.0;
  header.boxSize = xlen;
  header.flag_comoving = 0;
  header.flag_hdf5 = 0;
  block.particles.reserve(n_side * n_side * n_side);

  for (int i = 0; i < n_side; i++) {
    const double x = x_min + dx * i;

    for (int j = 0; j < n_side; j++) {
      const double y = x_min + dx * j;

      for (int k = 0; k < n_side; k++) {
        const double z = x_min + dx * k;

        const double rx = ud(rng);
        const double ry = ud(rng);
        const double rz = ud(rng);

        const double x_out = x + amp * rx;
        const double y_out = y + amp * ry;
        const double z_out = z + amp * rz;

        ParticleData p;
        p.pos[0] = x_out; p.pos[1] = y_out; p.pos[2] = z_out;
        p.original_pos[0] = x_out; p.original_pos[1] = y_out; p.original_pos[2] = z_out;

        p.vel[0] = x_out - Omega * y_out;
        p.vel[1] = y_out + Omega * x_out;
        p.vel[2] = z_out;

        p.Hsml = dx;
        p.originalHsml = dx;
        p.mass = 1.0f;
        p.density = 1.0f;
        p.temperature = 1.0f;
        p.type = 0;
        p.ID = (int)block.particles.size();

        block.particles.push_back(p);
      }
    }
  }

  return block;
}
