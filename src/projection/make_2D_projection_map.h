#ifndef PROJECTION_MAP_GENERATOR_H
#define PROJECTION_MAP_GENERATOR_H

#ifdef USE_LUA
#include <lua.hpp>
#endif

#include "image/bitmap_font_renderer.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>

// GLM 関連
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/tracking_vector.h"
#include "render/colormap_defs.h"

class ParticleArray;
class ParticleData;
struct ProjectionMapParams;
struct ProjectionMapContext;
struct RgbImage;
struct FluxSettings;
struct UnitSystem;

ProjectionMapContext BuildProjectionMapContext(const ProjectionMapParams& params,
                                               double scaleToPhysical,
                                               double time);

extern const float jetMap[];
extern const float viridisMap[];
extern const float plasmaMap[];

struct pos_val {
  float pos[3];
  float val;
  float density;
  float mass;
  float hsml;
};

class ProjectionMapGenerator {
private:
  BitmapFontRenderer fontRenderer_;
  
  struct ProjectionMap {
    int npixel = 0, npixel_x = 0, npixel_y = 0, npixel_z = 0;
    float xlen[3] = {0.f, 0.f, 0.f}, xmin[3] = {0.f, 0.f, 0.f};
    float dx = 0.f, dy = 0.f, dz = 0.f;
    float cell_size = 0.f;
    float minVal = 0.f, maxVal = 0.f;
    bool flagDensityWeight = false;
    bool flagLogScale = false;
    glm::vec3 center = glm::vec3(0.f);
    glm::vec3 uAxis = glm::vec3(1.f,0.f,0.f), vAxis = glm::vec3(0.f,1.f,0.f), wAxis = glm::vec3(0.f,0.f,1.f);
    TrackingVector<double> values;
    TrackingVector<double> weights;
    TrackingVector<unsigned char> image;
  };

  void createProjectionMap(ProjectionMap &map, const TrackingVector<pos_val>& particles);
  void createVoronoiSliceMap(ProjectionMap& map, const TrackingVector<pos_val>& particles);
  void createStarMap(ProjectionMap &map, const TrackingVector<pos_val>& particles, float sigma_pix, bool normalize);

#ifdef USE_LUA
  lua_State* gLua_ = nullptr;
  bool flag_init_lua_ = false;
  void ensureLuaInitialized();

  bool EvaluateLuaExpressionNumber(const char* expr, double& outValue);
  bool EvaluateLuaExpressionColor(const char* expr, float& r, float& g, float& b, float& a);
  bool EvaluateLuaExpressionBool(const char* expr, bool& outValue);
#endif

  void addColorBarToMap(ImageCanvas& canvas,
			double cell_size,
			float minVal,
			float maxVal,
			int colorBarWidth,
			const float *colormap,
			int countcolormap,
			const char *barLabel,
			const ProjectionMapParams& params,
			const ProjectionMapContext& ctx);

  void overlayStarParticles(ImageCanvas& canvas,
			    const ProjectionMap& map,
			    const ProjectionMapParams& params,
			    const ProjectionMapContext& ctx,
			    const TrackingVector<ParticleData>& particles);
  float kernel(float u);
  
public:
  RgbImage makeDensityMapImage(ParticleArray& particles,
			       const UnitSystem& units,
			       ProjectionMapParams& params,
			       ProjectionMapContext& ctx);

  int getFontCount() const;
  const std::string& getFontPath(int index) const;
  bool selectFontFileByIndex(int index);
  
  ProjectionMapGenerator();

  TrackingVector<glm::vec3> computeCuboidVertices(float *xmin, float *xmax, glm::vec3 center, glm::quat cuboidTransform);

  static void colormapLookup(float t, float& r, float& g, float& b, const float *colorMap, int countColorMap);  
};

static inline double Ledd_Lsun(double M_Msun){ return 3.8e4 * M_Msun; }
static inline double L_Eker2018_7to31_Lsun(double M_Msun){
  const double a = 2.865; const double b = 1.105; const double x = std::log10(M_Msun);
  return std::pow(10.0, a*x + b);
}
static inline double L_Graefener2011_VMS_Lsun(double M_Msun, double XH=0.7){
  const double x = std::log10(M_Msun);
  const double F1=3.862, F2=-2.486, F3=1.527, F4=1.247, F5=-0.076, F6=-0.183;
  const double y = (F1+F2*XH) + (F3+F4*XH)*x + (F5+F6*XH)*x*x;
  return std::pow(10.0, y);
}
static inline double Lbol_single_massive_Lsun(double M_Msun){
  double L;
  if (M_Msun <= 31.0) L = L_Eker2018_7to31_Lsun(M_Msun);
  else if (M_Msun < 60.0){
    const double M1=31.0, M2=60.0; const double logM = std::log10(M_Msun);
    const double logL1 = std::log10(L_Eker2018_7to31_Lsun(M1));
    const double logL2 = std::log10(L_Graefener2011_VMS_Lsun(M2, 0.7));
    const double t = (logM-std::log10(M1)) / (std::log10(M2)-std::log10(M1));
    L = std::pow(10.0, logL1 + t*(logL2-logL1));
  } else if (M_Msun <= 300.0) L = L_Graefener2011_VMS_Lsun(M_Msun, 0.7);
  else L = Ledd_Lsun(M_Msun);
  return std::min(L, Ledd_Lsun(M_Msun));
}
static inline double Teff_massive_K(double M_Msun){
  if (M_Msun < 10.0) M_Msun = 10.0;
  const double T10=25000.0, T60=45000.0, T300=55000.0;
  double T_10_300;
  if (M_Msun <= 60.0) { const double t = (M_Msun - 10.0) / 50.0; T_10_300 = T10 + t * (T60 - T10); }
  else { const double m = std::min(M_Msun, 300.0); const double t = (m - 60.0) / 240.0; T_10_300 = T60 + t * (T300 - T60); }
  const double Mdrop = 1.0e3; const double T_at_drop = T300; const double Tinf = 6000.0; const double dex = 0.30;
  if (M_Msun <= Mdrop) return (M_Msun <= 300.0) ? T_10_300 : T_at_drop;
  const double s = (std::log10(M_Msun) - std::log10(Mdrop)) / dex;
  return Tinf + (T_at_drop - Tinf) * std::exp(-s);
}
static inline double planck_Blambda(double lambda_m, double T_K){
  constexpr double h=6.62607015e-34, c=299792458.0, kB=1.380649e-23;
  const double x = (h*c) / (lambda_m * kB * T_K); if (x > 700.0) return 0.0;
  const double expx = std::exp(x), denom = expx - 1.0; if (denom <= 0.0) return 0.0;
  const double pref = (2.0*h*c*c) / std::pow(lambda_m, 5); return pref / denom;
}
static inline double band_fraction_rect_lambda(double T_K, double lambda0_m, double dlambda_m){
  constexpr double sigmaSB = 5.670374419e-8; const double sigma_over_pi = sigmaSB / M_PI;
  const double l1 = std::max(1e-12, lambda0_m - 0.5*dlambda_m), l2 = lambda0_m + 0.5*dlambda_m; if (l2 <= l1) return 0.0;
  const int N = 40; const double hstep = (l2 - l1) / N; double sum = 0.0;
  for (int i = 0; i <= N; i++) { const double l = l1 + i*hstep; const double f = planck_Blambda(l, T_K); const double coeff = (i==0 || i==N) ? 1.0 : (i%2==0 ? 2.0 : 4.0); sum += coeff * f; }
  const double integral = (hstep/3.0) * sum; const double norm = sigma_over_pi * std::pow(T_K, 4); if (norm <= 0.0) return 0.0;
  double frac = integral / norm; if (frac < 0.0) frac = 0.0; if (frac > 1.0) frac = 1.0; return frac;
}

static inline double compute_band_luminosity_Lsun(double M_Msun, const FluxSettings& fs);
#endif
