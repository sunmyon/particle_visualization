#include "compute_radial_profile.h"

#include <imgui.h>
#include "implot.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdio>
#include <vector>

std::tuple<TrackingVector<float>, TrackingVector<float>>
radialProfile::computeRadialProfile(const ParticleBlock& partblock,
                                    bool useOriginal, int bins,
                                    XAxisMode xmode,
                                    bool isMDot, QuantityId var1,
                                    bool autorange,
                                    float& xmin, float& xmax, float& ymin, float& ymax, float& rmax)
{
  TrackingVector<float> profile(bins, 0.0f);   // Σ(m*val) or Σ(m*v_r) for mdot
  TrackingVector<float> x_coord(bins, 0.0f);
  TrackingVector<int>   counts(bins, 0);
  TrackingVector<float> masses(bins, 0.0f);    // Σ(m)

  // --- center (normalized/original conversion: あなたの元コード踏襲) ---
  float normalizationFactor = 1.0f;
  if (partblock.particles.size() > 0) {
    const auto& p0 = partblock.particles[0];
    if (p0.original_pos[0] != 0.0f) normalizationFactor = p0.pos[0] / p0.original_pos[0];
  }
  glm::vec3 center = useOriginal ? (camCenter / normalizationFactor) : camCenter;

  auto getPos = [&](const ParticleData& p)->glm::vec3{
    return useOriginal ? glm::vec3(p.original_pos[0], p.original_pos[1], p.original_pos[2])
                       : glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
  };

  // --- items (type==0 only, rmax cut always by radius) ---
  struct Item {
    int idx;        // ipart
    float mass;
    glm::vec3 dpos; // pos-center
    float r;        // |dpos|
    float xval;     // X-axis value
    float Mcum;     // cumulative mass vs r (only meaningful when needed)
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

  // --- need v_center? (v_rad or mdot) ---
  const bool needVcenter = isMDot || (var1 == QuantityId::VRad);

  if (needVcenter) {
    float dmin = std::numeric_limits<float>::max();
    for (const auto& it : items) {
      if (it.r < dmin) {
        dmin = it.r;
        const ParticleData& p = partblock.particles[it.idx];
        v_center = glm::vec3(p.vel[0], p.vel[1], p.vel[2]);
      }
    }
    // printf("v_center=%g %g %g\n", v_center.x, v_center.y, v_center.z);
  }

  // --- X-axis fill ---
  if (xmode == XAxisMode::EnclosedMass) {
    // sort by r and build Mcum and xval=Mcum
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b){ return a.r < b.r; });

    double runM = 0.0;
    for (auto& it : items) {
      runM += (double)it.mass;
      it.Mcum = (float)runM;
      it.xval = it.Mcum; // X = M(<r)
    }
  } else {
    for (auto& it : items) {
      switch (xmode) {
        case XAxisMode::Radius: it.xval = it.r;      break;
        case XAxisMode::PosX:   it.xval = it.dpos.x; break;
        case XAxisMode::PosY:   it.xval = it.dpos.y; break;
        case XAxisMode::PosZ:   it.xval = it.dpos.z; break;
        default:                it.xval = it.r;      break;
      }
    }
  }

  // --- autorange based on xval ---
  if (autorange) {
    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();

    if (plotXAxisLog) {
      for (const auto& it : items) {
        if (it.xval > 0.0f) {
          minX = std::min(minX, it.xval);
          maxX = std::max(maxX, it.xval);
        }
      }
      if (minX == std::numeric_limits<float>::max()) { // no positive
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

    if (plotXAxisLog) {
      if (xmin <= 0.0f) xmin = 1.e-6f * xmax;
      if (xmin <  1.e-6f * xmax) xmin = 1.e-6f * xmax;
    }
  }

  // --- bin width ---
  const double dx = (xmax - xmin) / (double)bins;
  double dln_x = 0.0;
  if (plotXAxisLog) dln_x = std::log((double)xmax / (double)xmin) / (double)bins;

  // --- cumulative Y? (Mass means enclosed mass in profile context) ---
  const bool flag_cumulativeY = (!isMDot && var1 == QuantityId::Mass);

  // --- helper: r(M) for mdot when X is enclosed mass ---
  auto radiusAtEnclosedMass = [&](double Medge)->double{
    if (items.empty()) return 0.0;

    // items are r-sorted ONLY when xmode==EnclosedMass
    // if not, mdot on EnclosedMass should be disabled by UI
    double M0 = (double)items.front().Mcum;
    double M1 = (double)items.back().Mcum;

    if (Medge <= M0) return (double)items.front().r;
    if (Medge >= M1) return (double)items.back().r;

    int lo = 0, hi = (int)items.size() - 1;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if ((double)items[mid].Mcum < Medge) lo = mid + 1;
      else hi = mid;
    }
    int j = lo;
    int i = j - 1;
    double Ma = (double)items[i].Mcum, Mb = (double)items[j].Mcum;
    double ra = (double)items[i].r,    rb = (double)items[j].r;
    if (Mb == Ma) return rb;
    double t = (Medge - Ma) / (Mb - Ma);
    return ra + t * (rb - ra);
  };

  // --- accumulation ---
  const float* cptr = glm::value_ptr(center);
  const float* vptr = glm::value_ptr(v_center);

  for (const auto& it : items) {
    float x = it.xval;

    if (x > xmax) continue;
    if (!flag_cumulativeY && x < xmin) continue;
    if (plotXAxisLog && x <= 0.0f) continue;

    int bin = 0;
    if (!plotXAxisLog) {
      bin = (dx > 0.0) ? int((x - xmin) / dx) : 0;
    } else {
      bin = (dln_x > 0.0) ? int(std::log((double)x / (double)xmin) / dln_x) : 0;
    }
    if (bin >= bins) bin = bins - 1;
    if (bin < 0)     bin = 0;

    const ParticleData& p = partblock.particles[it.idx];

    // Mass as Y: just accumulate mass per bin, later cumulative sum
    if (flag_cumulativeY) {
      masses[bin] += p.mass;
      counts[bin] += 1;
      continue;
    }

    if (isMDot) {
      // profile stores Σ(m * v_rad)
      float vr = getScalarValue(partblock, p, it.idx, QuantityId::VRad, cptr, vptr);
      profile[bin] += vr * p.mass;
      masses[bin]  += p.mass;
      counts[bin]  += 1;
    } else {
      float val = getScalarValue(partblock, p, it.idx, var1, cptr, vptr);
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
  } else if (isMDot) {

    // mdot is defined as Σ(m v_rad) / dr_shell (dr_shell is radial width)
    if (xmode == XAxisMode::Radius) {
      // r-bin edges from xmin..xmax
      double r_old = (double)xmin;
      for (int i = 0; i < bins; ++i) {
        double r_edge = (!plotXAxisLog)
          ? ((double)xmin + dx * (i + 1))
          : ((double)xmin * std::exp(dln_x * (i + 1)));

        double dr_shell = r_edge - r_old;
        double mv = (double)profile[i];
        double mdot = (dr_shell != 0.0) ? (mv / dr_shell) : 0.0;

        profile[i] = (float)(mdot * UnitMass_in_msolar / UnitTime_in_yr);
        r_old = r_edge;

        if (flagAbsolute && profile[i] < 0) profile[i] = -profile[i];
      }

    } else if (xmode == XAxisMode::EnclosedMass) {
      // M-bin edges -> r edges via r(M)
      for (int i = 0; i < bins; ++i) {
        double M_lo = (!plotXAxisLog)
          ? ((double)xmin + dx * i)
          : ((double)xmin * std::exp(dln_x * i));

        double M_hi = (!plotXAxisLog)
          ? ((double)xmin + dx * (i + 1))
          : ((double)xmin * std::exp(dln_x * (i + 1)));

        double r_lo = radiusAtEnclosedMass(M_lo);
        double r_hi = radiusAtEnclosedMass(M_hi);

        double dr_shell = r_hi - r_lo;
        double mv = (double)profile[i];
        double mdot = (dr_shell != 0.0) ? (mv / dr_shell) : 0.0;

        profile[i] = (float)(mdot * UnitMass_in_msolar / UnitTime_in_yr);

        if (flagAbsolute && profile[i] < 0) profile[i] = -profile[i];
      }
    } else {
      // x/y/z では dr_shell が定義できないので 0（UIで隠すのが基本）
      for (int i = 0; i < bins; ++i) profile[i] = 0.0f;
    }

  } else {
    // mass-weighted mean
    for (int i = 0; i < bins; ++i) {
      if (masses[i] > 0.0f) profile[i] /= masses[i];
      else profile[i] = 0.0f;

      if (flagAbsolute && profile[i] < 0) profile[i] = -profile[i];
    }
  }

  // --- x_coord (bin centers) ---
  for (int i = 0; i < bins; ++i) {
    if (!plotXAxisLog) {
      x_coord[i] = xmin + (float)(dx * (i + 0.5));
    } else {
      x_coord[i] = (float)((double)xmin * std::exp(dln_x * (i + 0.5)));
    }
  }

  // --- ymin/ymax autorange ---
  if (autorange) {
    double ymax0 = -std::numeric_limits<double>::max();
    double ymin0 =  std::numeric_limits<double>::max();

    for (int i = 0; i < bins; ++i) {
      // empty bins skip for non-cumulative
      if (!flag_cumulativeY && counts[i] == 0) continue;

      ymax0 = std::max(ymax0, (double)profile[i]);
      ymin0 = std::min(ymin0, (double)profile[i]);
    }
    ymax = (float)ymax0;
    ymin = (float)ymin0;
  }

  return { std::move(x_coord), std::move(profile) };
}



void radialProfile::ShowRadialProfileUI(const ParticleBlock& partblock,
                                        double unitmass_in_g, double unitlength_in_cm, double unittime_in_s)
{
  if (!showWindowRadialProfile) return;

  setUnits(unitmass_in_g, unitlength_in_cm, unittime_in_s);

  ImGui::SetNextWindowSize(ImVec2(650, 430), ImGuiCond_Appearing);
  ImGui::Begin("Radial Profile", &showWindowRadialProfile, ImGuiWindowFlags_None);

  // ---- X axis ----
  static int selectedXAxis = 0; // 0:r,1:x,2:y,3:z,4:M(<r)
  const char* xaxes[] = { "r", "x", "y", "z", "M(<r)" };
  ImGui::Combo("X Axis", &selectedXAxis, xaxes, IM_ARRAYSIZE(xaxes));
  XAxisMode xmode = (XAxisMode)selectedXAxis;

  // ---- Quantity (base = partblock.uiQ, derived = mdot if allowed) ----
  static int selectedVarIdx = 0;

  const int baseCount = partblock.nUIQ;

  // mdot is allowed for r-axis and M(<r)-axis
  const bool allowMDot = (xmode == XAxisMode::Radius || xmode == XAxisMode::EnclosedMass);
  const int derivedCount = allowMDot ? 1 : 0;
  const int totalCount = baseCount + derivedCount;

  if (selectedVarIdx < 0 || selectedVarIdx >= totalCount) selectedVarIdx = 0;

  auto labelAt = [&](int idx)->const char*{
    if (idx < baseCount) return QuantityLabel(partblock.uiQ[idx]);
    return "mdot";
  };

  bool isMDot = false;
  static QuantityId var1 = QuantityId::Density;

  if (ImGui::BeginCombo("Quantity", labelAt(selectedVarIdx))) {
    for (int i = 0; i < baseCount; ++i) {
      bool sel = (selectedVarIdx == i);
      if (ImGui::Selectable(QuantityLabel(partblock.uiQ[i]), sel)) selectedVarIdx = i;
      if (sel) ImGui::SetItemDefaultFocus();
    }
    if (allowMDot) {
      ImGui::Separator();
      int mdotIdx = baseCount;
      bool sel = (selectedVarIdx == mdotIdx);
      if (ImGui::Selectable("mdot", sel)) selectedVarIdx = mdotIdx;
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (selectedVarIdx < baseCount) {
    var1 = partblock.uiQ[selectedVarIdx];
    isMDot = false;
  } else {
    isMDot = true;
  }

  // ---- other options ----
  ImGui::InputInt("Number of Bins", &radialProfileBins);
  ImGui::Checkbox("Use Original Coordinates", &radialProfileUseOriginal);
  ImGui::Checkbox("Log X Axis", &plotXAxisLog);
  ImGui::Checkbox("Log Y Axis", &plotYAxisLog);

  ImGui::Checkbox("Auto Range", &autoRange);
  ImGui::Checkbox("Take absolute value", &flagAbsolute);

  if (!autoRange) {
    ImGui::InputFloat("X Axis Min", &xmin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("X Axis Max", &xmax, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Min", &ymin, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Y Axis Max", &ymax, 0.0f, 0.0f, "%g");
  }

  // radius cut (still meaningful even for M(<r) axis)
  ImGui::InputFloat("Maximum Radius (cut)", &rmax, 0.0f, 0.0f, "%g");

  if (ImGui::Button("Compute profile")) {
    std::tie(radialProfileDataCoord, radialProfileDataValue)
      = computeRadialProfile(partblock,
                             radialProfileUseOriginal, radialProfileBins,
                             xmode, isMDot, var1,
                             autoRange, xmin, xmax, ymin, ymax, rmax);
    radialProfileComputed = true;
  }

  if (radialProfileComputed) {
    if (ImPlot::BeginPlot("Profile", ImVec2(-1, 300))) {
      // axis labels
      const char* ylabel = isMDot ? "mdot" : QuantityLabel(var1);
      ImPlot::SetupAxes(XAxisLabel(xmode), ylabel);

      if (plotXAxisLog) ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
      if (plotYAxisLog) ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

      ImPlot::SetupAxisLimits(ImAxis_X1, xmin, xmax, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, ymin, ymax, ImGuiCond_Always);

      ImPlot::PlotLine("Profile",
                       radialProfileDataCoord.data(),
                       radialProfileDataValue.data(),
                       (int)radialProfileDataValue.size());
      ImPlot::EndPlot();
    }
  }

  ImGui::End();
}
