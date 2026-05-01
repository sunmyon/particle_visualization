#include "analysis/radial_profile.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <limits>
#include <cmath>
#include <vector>

#include "core/physics_constants.h"
#include "data/particle_coordinates.h"

RadialProfileResult
RadialProfileComputer::compute(const ParticleBlock& partblock,
			       double scaleToPhysical,
                               const RadialProfileParams& params,
			       const glm::vec3& cam_center)
{
  RadialProfileResult result;
  result.valid = false;

  const int bins = params.bins;
  if (bins <= 0) return result;

  std::vector<float> profile(bins, 0.0f);
  std::vector<float> x_coord(bins, 0.0f);
  std::vector<int>   counts(bins, 0);
  std::vector<float> masses(bins, 0.0f);

  float xmin = params.xmin;
  float xmax = params.xmax;
  float ymin = params.ymin;
  float ymax = params.ymax;
  float rmax = params.rmax;

  // --- center ---
  float normalizationFactor = static_cast<float>(scaleToPhysical);
  if (!std::isfinite(normalizationFactor) || normalizationFactor <= 0.0f) {
    normalizationFactor = 1.0f;
  }
  glm::vec3 center = cam_center;
  const float normalizedScale = 1.0f / normalizationFactor;

  auto getPos = [&](const ParticleData& p) -> glm::vec3 {
    return params.useOriginal
      ? glm::vec3(p.original_pos[0], p.original_pos[1], p.original_pos[2])
      : normalizedParticlePosition(p, normalizedScale);
  };

  struct Item {
    int idx;
    float mass;
    glm::vec3 dpos;
    float r;
    float xval;
    float Mcum;
  };

  std::vector<Item> items;
  items.reserve(partblock.particles.size());

  for (int i = 0; i < (int)partblock.particles.size(); ++i) {
    const ParticleData& p = partblock.particles[i];
    if (p.type != 0) continue;

    glm::vec3 pos  = getPos(p);
    glm::vec3 dpos = pos - center;
    float r = glm::length(dpos);

    if (rmax > 0.0f && r > rmax) continue;

    Item it;
    it.idx  = i;
    it.mass = p.mass;
    it.dpos = dpos;
    it.r    = r;
    it.xval = 0.0f;
    it.Mcum = 0.0f;
    items.push_back(it);
  }

  const bool needVcenter = params.isMDot || (params.var1 == QuantityId::VRad);

  if (needVcenter) {
    float dmin = std::numeric_limits<float>::max();
    for (const auto& it : items) {
      if (it.r < dmin) {
        dmin = it.r;
        const ParticleData& p = partblock.particles[it.idx];
        v_center = glm::vec3(p.vel[0], p.vel[1], p.vel[2]);
      }
    }
  }

  // --- X-axis fill ---
  if (params.xmode == XAxisMode::EnclosedMass) {
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.r < b.r; });

    double runM = 0.0;
    for (auto& it : items) {
      runM += (double)it.mass;
      it.Mcum = (float)runM;
      it.xval = it.Mcum;
    }
  } else {
    for (auto& it : items) {
      switch (params.xmode) {
        case XAxisMode::Radius: it.xval = it.r;      break;
        case XAxisMode::PosX:   it.xval = it.dpos.x; break;
        case XAxisMode::PosY:   it.xval = it.dpos.y; break;
        case XAxisMode::PosZ:   it.xval = it.dpos.z; break;
        case XAxisMode::EnclosedMass:
        default:                it.xval = it.r;      break;
      }
    }
  }

  // --- autorange based on xval ---
  if (params.autorange) {
    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();

    if (params.plotXAxisLog) {
      for (const auto& it : items) {
        if (it.xval > 0.0f) {
          minX = std::min(minX, it.xval);
          maxX = std::max(maxX, it.xval);
        }
      }
      if (minX == std::numeric_limits<float>::max()) {
        minX = 1e-6f;
        maxX = 1.0f;
      }
    } else {
      for (const auto& it : items) {
        minX = std::min(minX, it.xval);
        maxX = std::max(maxX, it.xval);
      }
    }

    xmin = minX;
    xmax = maxX;

    if (params.plotXAxisLog) {
      if (xmin <= 0.0f) xmin = 1.e-6f * xmax;
      if (xmin < 1.e-6f * xmax) xmin = 1.e-6f * xmax;
    }
  }

  const double dx = (xmax - xmin) / (double)bins;
  double dln_x = 0.0;
  if (params.plotXAxisLog) {
    dln_x = std::log((double)xmax / (double)xmin) / (double)bins;
  }

  const bool flag_cumulativeY = (!params.isMDot && params.var1 == QuantityId::Mass);

  auto radiusAtEnclosedMass = [&](double Medge) -> double {
    if (items.empty()) return 0.0;

    double M0 = (double)items.front().Mcum;
    double M1 = (double)items.back().Mcum;

    if (Medge <= M0) return (double)items.front().r;
    if (Medge >= M1) return (double)items.back().r;

    int lo = 0;
    int hi = (int)items.size() - 1;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if ((double)items[mid].Mcum < Medge) lo = mid + 1;
      else hi = mid;
    }

    int j = lo;
    int i = j - 1;
    double Ma = (double)items[i].Mcum;
    double Mb = (double)items[j].Mcum;
    double ra = (double)items[i].r;
    double rb = (double)items[j].r;

    if (Mb == Ma) return rb;
    double t = (Medge - Ma) / (Mb - Ma);
    return ra + t * (rb - ra);
  };

  const float* cptr = glm::value_ptr(center);
  const float* vptr = glm::value_ptr(v_center);

  for (const auto& it : items) {
    float x = it.xval;

    if (x > xmax) continue;
    if (!flag_cumulativeY && x < xmin) continue;
    if (params.plotXAxisLog && x <= 0.0f) continue;

    int bin = 0;
    if (!params.plotXAxisLog) {
      bin = (dx > 0.0) ? int((x - xmin) / dx) : 0;
    } else {
      bin = (dln_x > 0.0) ? int(std::log((double)x / (double)xmin) / dln_x) : 0;
    }

    if (bin >= bins) bin = bins - 1;
    if (bin < 0)     bin = 0;

    const ParticleData& p = partblock.particles[it.idx];

    if (flag_cumulativeY) {
      masses[bin] += p.mass;
      counts[bin] += 1;
      continue;
    }

    if (params.isMDot) {
      float vr = getScalarValue(partblock, p, it.idx, QuantityId::VRad, cptr, vptr);
      profile[bin] += vr * p.mass;
      masses[bin]  += p.mass;
      counts[bin]  += 1;
    } else {
      float val = getScalarValue(partblock, p, it.idx, params.var1, cptr, vptr);
      profile[bin] += val * p.mass;
      masses[bin]  += p.mass;
      counts[bin]  += 1;
    }
  }

  // --- post process ---
  if (flag_cumulativeY) {
    double run = 0.0;
    for (int i = 0; i < bins; ++i) {
      run += (double)masses[i];
      profile[i] = (float)run;
    }
  } else if (params.isMDot) {
    if (params.xmode == XAxisMode::Radius) {
      double r_old = (double)xmin;
      for (int i = 0; i < bins; ++i) {
        double r_edge = (!params.plotXAxisLog)
          ? ((double)xmin + dx * (i + 1))
          : ((double)xmin * std::exp(dln_x * (i + 1)));

        double dr_shell = r_edge - r_old;
        double mv = (double)profile[i];
        double mdot = (dr_shell != 0.0) ? (mv / dr_shell) : 0.0;

        profile[i] = (float)(mdot * units_.mdot_in_msun_per_yr());
        r_old = r_edge;

        if (params.flagAbsolute && profile[i] < 0.0f) profile[i] = -profile[i];
      }

    } else if (params.xmode == XAxisMode::EnclosedMass) {
      for (int i = 0; i < bins; ++i) {
        double M_lo = (!params.plotXAxisLog)
          ? ((double)xmin + dx * i)
          : ((double)xmin * std::exp(dln_x * i));

        double M_hi = (!params.plotXAxisLog)
          ? ((double)xmin + dx * (i + 1))
          : ((double)xmin * std::exp(dln_x * (i + 1)));

        double r_lo = radiusAtEnclosedMass(M_lo);
        double r_hi = radiusAtEnclosedMass(M_hi);

        double dr_shell = r_hi - r_lo;
        double mv = (double)profile[i];
        double mdot = (dr_shell != 0.0) ? (mv / dr_shell) : 0.0;

        profile[i] = (float)(mdot * units_.mdot_in_msun_per_yr());

        if (params.flagAbsolute && profile[i] < 0.0f) profile[i] = -profile[i];
      }
    } else {
      for (int i = 0; i < bins; ++i) profile[i] = 0.0f;
    }

  } else {
    for (int i = 0; i < bins; ++i) {
      if (masses[i] > 0.0f) profile[i] /= masses[i];
      else profile[i] = 0.0f;

      if (params.flagAbsolute && profile[i] < 0.0f) profile[i] = -profile[i];
    }
  }

  for (int i = 0; i < bins; ++i) {
    if (!params.plotXAxisLog) {
      x_coord[i] = xmin + (float)(dx * (i + 0.5));
    } else {
      x_coord[i] = (float)((double)xmin * std::exp(dln_x * (i + 0.5)));
    }
  }

  if (params.autorange) {
    double ymax0 = -std::numeric_limits<double>::max();
    double ymin0 =  std::numeric_limits<double>::max();

    for (int i = 0; i < bins; ++i) {
      if (!flag_cumulativeY && counts[i] == 0) continue;

      ymax0 = std::max(ymax0, (double)profile[i]);
      ymin0 = std::min(ymin0, (double)profile[i]);
    }

    ymax = (float)ymax0;
    ymin = (float)ymin0;
  }

  result.x = std::move(x_coord);
  result.y = std::move(profile);
  result.xmin = xmin;
  result.xmax = xmax;
  result.ymin = ymin;
  result.ymax = ymax;
  result.valid = true;

  return result;
}
