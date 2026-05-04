#include "data/particle_selection.h"
#include "data/simulation_block.h"
#include "data/header_info.h"
#include "data/quantity_catalog_builder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

namespace {
using RebuildProfileClock = std::chrono::steady_clock;

double RebuildElapsedMs(RebuildProfileClock::time_point start)
{
  return std::chrono::duration<double, std::milli>(
           RebuildProfileClock::now() - start)
    .count();
}

const SoAField* FindSoAField(const SimulationBlock& block,
                             const char* key,
                             int minComps)
{
  auto it = block.soa.find(key);
  if (it == block.soa.end()) return nullptr;

  const SoAField& f = it->second;
  if (f.comps < minComps) return nullptr;
  const size_t bytesPerElement = static_cast<size_t>(f.comps) *
                                 dataTypeSize(f.type);
  if (bytesPerElement == 0 ||
      f.bytes.size() < block.particles.size() * bytesPerElement) {
    return nullptr;
  }
  return &f;
}

float ReadSoAScalar(const SoAField* f, size_t i)
{
  if (!f) return 0.0f;
  const uint8_t* p = f->ptr(i);
  switch (f->type) {
  case DataType::Float:
    return *reinterpret_cast<const float*>(p);
  case DataType::Double:
    return static_cast<float>(*reinterpret_cast<const double*>(p));
  case DataType::Int32:
    return static_cast<float>(*reinterpret_cast<const int32_t*>(p));
  case DataType::Int64:
    return static_cast<float>(*reinterpret_cast<const int64_t*>(p));
  }
  return 0.0f;
}

bool ReadSoAVec3(const SoAField* f, size_t i, float out[3])
{
  if (!f || f->comps < 3) return false;
  const uint8_t* p = f->ptr(i);
  switch (f->type) {
  case DataType::Float: {
    const auto* v = reinterpret_cast<const float*>(p);
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    return true;
  }
  case DataType::Double: {
    const auto* v = reinterpret_cast<const double*>(p);
    out[0] = static_cast<float>(v[0]);
    out[1] = static_cast<float>(v[1]);
    out[2] = static_cast<float>(v[2]);
    return true;
  }
  case DataType::Int32: {
    const auto* v = reinterpret_cast<const int32_t*>(p);
    out[0] = static_cast<float>(v[0]);
    out[1] = static_cast<float>(v[1]);
    out[2] = static_cast<float>(v[2]);
    return true;
  }
  case DataType::Int64: {
    const auto* v = reinterpret_cast<const int64_t*>(p);
    out[0] = static_cast<float>(v[0]);
    out[1] = static_cast<float>(v[1]);
    out[2] = static_cast<float>(v[2]);
    return true;
  }
  }
  return false;
}

struct RebuildQuantityAccessor {
  QuantityId q = QuantityId::Density;
  const SoAField* primary = nullptr;
  const SoAField* bfield = nullptr;
  const SoAField* electron = nullptr;
  const SoAField* h2 = nullptr;
};

RebuildQuantityAccessor MakeRebuildAccessor(const SimulationBlock& block,
                                            QuantityId q)
{
  RebuildQuantityAccessor a;
  a.q = q;
  switch (q) {
  case QuantityId::B:
    a.bfield = FindSoAField(block, kBfieldKey, 3);
    break;
  case QuantityId::Beta:
    a.bfield = FindSoAField(block, kBfieldKey, 3);
    a.electron = FindSoAField(block, kElectronAbundanceKey, 1);
    a.h2 = FindSoAField(block, kH2AbundanceKey, 1);
    break;
  case QuantityId::Metallicity:
    a.primary = FindSoAField(block, kMetallicityKey, 1);
    break;
  case QuantityId::ElectronAbundance:
    a.primary = FindSoAField(block, kElectronAbundanceKey, 1);
    break;
  case QuantityId::H2Abundance:
    a.primary = FindSoAField(block, kH2AbundanceKey, 1);
    break;
  case QuantityId::HDAbundance:
    a.primary = FindSoAField(block, kHDAbundanceKey, 1);
    break;
  case QuantityId::J21:
    a.primary = FindSoAField(block, kJ21Key, 1);
    break;
  case QuantityId::Val:
    a.primary = FindSoAField(block, kVal1Key, 1);
    break;
  case QuantityId::Val2:
    a.primary = FindSoAField(block, kVal2Key, 1);
    break;
  default:
    break;
  }
  return a;
}

float ReadRebuildQuantity(const RebuildQuantityAccessor& a,
                          const SimulationElement& p,
                          size_t i)
{
  switch (a.q) {
  case QuantityId::Density:
    return p.density;
  case QuantityId::Temperature:
    return p.temperature;
  case QuantityId::Mass:
    return p.mass;
  case QuantityId::Hsml:
    return p.supportRadius;
  case QuantityId::PosX:
    return p.position[0];
  case QuantityId::PosY:
    return p.position[1];
  case QuantityId::PosZ:
    return p.position[2];
  case QuantityId::Radius:
    return std::sqrt(p.position[0] * p.position[0] +
                     p.position[1] * p.position[1] +
                     p.position[2] * p.position[2]);
  case QuantityId::B: {
    float b[3];
    if (!ReadSoAVec3(a.bfield, i, b)) return 0.0f;
    return std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
  }
  case QuantityId::Beta: {
    float b[3];
    if (!ReadSoAVec3(a.bfield, i, b)) return 0.0f;
    const float b2 = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
    if (b2 <= 0.0f) return 0.0f;
    const float felec = ReadSoAScalar(a.electron, i);
    const float fH2 = ReadSoAScalar(a.h2, i);
    const double mu = 1.0 / (1.0 + physics_constants::XHe + felec - fH2);
    return static_cast<float>(8.0 * M_PI *
                              physics_constants::boltzmann_cgs *
                              p.temperature * p.density * mu / b2);
  }
  case QuantityId::VRad:
    return 0.0f;
  case QuantityId::Metallicity:
  case QuantityId::ElectronAbundance:
  case QuantityId::H2Abundance:
  case QuantityId::HDAbundance:
  case QuantityId::J21:
  case QuantityId::Val:
  case QuantityId::Val2:
    return ReadSoAScalar(a.primary, i);
  }
  return 0.0f;
}
} // namespace

SimulationBlock::BuildResult SimulationBlock::rebuild(float desiredMax, const QuantityCatalogState& catalog){
  const auto totalStart = RebuildProfileClock::now();
  BuildResult result;
  worldToRenderScale = 1.0f;
  
  if (!particles.empty()) {
    std::array<RebuildQuantityAccessor, kMaxQ> accessors;
    for (int q = 0; q < catalog.nUIQ; ++q) {
      accessors[static_cast<size_t>(q)] =
        MakeRebuildAccessor(*this, catalog.uiQ[q]);
    }

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
        SimulationElement& p = particles[i];
        int type = p.type;
        const bool validType = (type >= 0 && type < kNumTypes);
        if (!validType) {
          type = 0;
        }
        if (validType) {
          local_npart_type[type]++;
        }

        // maxVal
        float m = std::max(std::fabs(p.position[0]),
			   std::max(std::fabs(p.position[1]), std::fabs(p.position[2])));
        if (m > localMax) localMax = m;

        // renew min/max
        if (validType) {
          for (int q = 0; q < catalog.nUIQ; ++q) {
            float v = ReadRebuildQuantity(accessors[static_cast<size_t>(q)],
                                           p,
                                           static_cast<size_t>(i));
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
      worldToRenderScale = desiredMax / result.originalMax;
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

  std::fprintf(stderr,
               "[Dataset] SimulationBlock::rebuild n=%zu uiQuantities=%d total=%.3f ms\n",
               particles.size(),
               catalog.nUIQ,
               RebuildElapsedMs(totalStart));

  return result;
}

bool SimulationBlock::ComputeAngularMomentumAxis(const ParticleSelectionOption& op,
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

      glm::dvec3 r = glm::dvec3(renderPosition(p, worldToRenderScale)) -
                     glm::dvec3(op.center);
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

    glm::dvec3 r = glm::dvec3(renderPosition(p, worldToRenderScale)) -
                   glm::dvec3(op.center);
    if (r2max > 0.0f && glm::dot(r, r) > r2max) continue;

    glm::dvec3 v = glm::dvec3(p.vel[0], p.vel[1], p.vel[2]) - vcm;
    L += static_cast<double>(p.mass) * glm::cross(r, v);
  }

  const double n2 = glm::dot(L, L);
  if (n2 < 1e-24) return false;

  outAxis = glm::normalize(glm::vec3(L));
  return true;
}


SimulationBlock SimulationBlock::makeTestSimulationBlock(HeaderInfo& header)
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

  SimulationBlock block;

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

        SimulationElement p;
        p.position[0] = x_out; p.position[1] = y_out; p.position[2] = z_out;

        p.vel[0] = x_out - Omega * y_out;
        p.vel[1] = y_out + Omega * x_out;
        p.vel[2] = z_out;

        p.supportRadius = dx;
        p.mass = 1.0f;
        p.density = 1.0f;
        p.temperature = 1.0f;
        p.type = 0;

        block.particles.push_back(p);
      }
    }
  }

  block.ensureParticleIdStorage(DataType::Int64);
  for (size_t i = 0; i < block.particles.size(); ++i) {
    block.setParticleId(i, static_cast<uint64_t>(i));
  }

  return block;
}
