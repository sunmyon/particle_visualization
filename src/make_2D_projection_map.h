#ifndef PROJECTION_MAP_GENERATOR_H
#define PROJECTION_MAP_GENERATOR_H

#ifdef USE_LUA
#include <lua.hpp>
#endif

#include "stb_image_write.h"
#include "stb_truetype.h"

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

#include "data/particle_array.h"
#include "core/tracking_vector.h"
#include "core/quantity.h"
#include "render/colormap_defs.h"

#include "object.h"

struct ProjectionImage {
  int width = 0;
  int height = 0;
  uint64_t version = 0;
  TrackingVector<uint8_t> rgb; // size = width*height*3, RGB8
};

enum class DataSource : int { Gas = 0, DM = 1, Stars = 2 };
enum class StarQuantity : int { Density=0, Metallicity=1, Mass=2, Flux=3 };

struct FluxSettings {
  float band_center_nm = 1500.0f;
  float band_width_nm  = 200.0f;
};

struct ProjectionMapParams {
  int npixel = 200;

  float xlen[3] = {2.f, 2.f, 1.f};
  float xoffset[3] = {0.f, 0.f, 0.f};
  float tilt[3] = {0.f, 0.f, 0.f};

  bool flagDensityWeight = true;
  bool flagVoronoi = true;
  int step_z = 200;

  bool flagLogScale = true;
  bool autoRange = true;
  float range_min = 0.0f;
  float range_max = 1.0f;

  bool flagShowStarParticles = true;
  bool flagShowCuboid = false;

  bool flagSpecifyZoomRegionByMass = false;
  bool flagScaleOriginalCoordinateZoomRegion = true;
  float criticalGasMassForZoomRegion = 0.0f;
  float lenZoomRegion = 0.0f;

  bool flagPlaceScale = false;
  bool flagScaleOriginalCoordinate = true;
  float arrowLenX = 100.0f;
  char arrowLabelStr[255] = "100 au";

  bool flagTimeLabel = true;
  bool flagUseRedshift = false;
  char timeFormatBuf[255] = "t=%.3f";

  char fileFormat[255] = "image_%04d.png";
  char folderPath[255] = "./output";

  std::string var;

  int selectedAxis = 2;
  int selectedType = 0;
  int colormapindex = 0;

  float factorShownTimeInUnitTime = 1.0f;

  DataSource dataSource = DataSource::Gas;
  StarQuantity starQuantity = StarQuantity::Density;
  FluxSettings flux;
  float psf_sigma_pix = 1.5f;
  QuantityId selectedVarGas = QuantityId::Density;

  char filterExpr[256] = "return m > 10.0";
  char pointSizeExpr[256] = "return m / 10.0";
  char pointColorExpr[256] = "return { r = m/100.0, g = 0.5, b = 0.2, a = 1.0 }";
  char minValueExpr[32] = "return 0.0";
  char maxValueExpr[32] = "return 1.0";
};

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
  ProjectionImage image_;
  bool flag_image_ = false;
  bool dirty_ = true;
  uint64_t nextVersion_ = 1;
  char type_ = 0;

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

  CuboidObject interactiveCuboid_;
  
public:
  ProjectionMapParams params;

  HeaderInfo Header;
  bool flagFontLoaded = false;
  bool showWindowSelectFont = false;
  double originalMax = 1.0;
  double desiredMax = 1.0;
  glm::quat cuboidTransform = glm::quat(1.f,0.f,0.f,0.f);
  glm::vec3 center = glm::vec3(0.f);
  glm::vec3 planeNormal = glm::vec3(0.f,0.f,1.f);
  int countColorMap = gColormapDefs[0].count;
  const float* colorMap = gColormapDefs[0].data;
  TrackingVector<unsigned char> outImage;
  int outW = 0, outH = 0;
  bool flag2DprojectionComputed = false;

#ifdef USE_LUA
  lua_State* gLua = nullptr;
  bool flag_init_lua = false;
#endif

  stbtt_fontinfo fontCharacter;
  std::vector<unsigned char> ttf_buffer;
  std::vector<std::string> availableFonts = {};
  std::vector<ImFont*> loadedFonts = {};

  int getFontCount() const;
  const std::string& getFontPath(int index) const;
  bool selectFontFileByIndex(int index);
  
  ProjectionMapGenerator();

  void reset_flag(){
    flag_image_ = false;
    dirty_ = true;
  }

  void setTexture2D(const TrackingVector<unsigned char> &rgb, const int width, const int height){
    image_.width = width;
    image_.height = height;
    image_.rgb = rgb;
    flag_image_ = true;
    dirty_ = true;
    image_.version = nextVersion_++;
  }

  const ProjectionImage &getImage() const noexcept{ return image_; }
  bool getImageFlag() const { return flag_image_; }

  void syncStateFromParams() {
    type_ = static_cast<char>(params.selectedType);
    center.x = params.xoffset[0];
    center.y = params.xoffset[1];
    center.z = params.xoffset[2];
    cuboidTransform = UpdateTransformFromEuler(params.tilt);
    planeNormal = glm::normalize(cuboidTransform * glm::vec3(0.f, 0.f, 1.f));

    if (params.colormapindex < 0) params.colormapindex = 0;
    if (params.colormapindex >= gNumColormaps) params.colormapindex = gNumColormaps - 1;

    colorMap = gColormapDefs[params.colormapindex].data;
    countColorMap = gColormapDefs[params.colormapindex].count;
  }

  glm::vec3 calc_angular_momentum_axis(const TrackingVector<ParticleData>& originalParticles, glm::vec3 &center, float *xlen);
  void make_density_map(ParticleArray *P, char *filename);
  TrackingVector<glm::vec3> computeCuboidVertices(float *xmin, float *xmax, glm::vec3 center, glm::quat cuboidTransform);
  glm::quat UpdateTransformFromEuler(float *eulerAngles);
  float kernel(float u);

#ifdef USE_LUA
  bool EvaluateLuaExpressionNumber(const char* expr, double& outValue);
  bool EvaluateLuaExpressionColor(const char* expr, float& r, float& g, float& b, float& a);
  bool EvaluateLuaExpressionBool(const char* expr, bool& outValue);
#endif

  void overlayStarParticles(ProjectionMap& map, const TrackingVector<ParticleData>& particles);
  static void colormapLookup(float t, float& r, float& g, float& b, const float *colorMap, int countColorMap);
  void addColorBarToMap(const ProjectionMap& map,
                        float minVal, float maxVal,
                        int colorBarWidth,
                        const float *colormap, int countcolormap,
                        TrackingVector<unsigned char>& outImage,
                        int& outW, int& outH, const char *barLabel);

  void ShowFontSelectionWindow();
  void initFonts();
  bool containsIgnoreCase(const std::string& str, const std::string& substr);
  void getAvailableFonts(const std::vector<std::string>& fontDirectory);
  bool loadFontFile(const std::string& fontFilename, std::vector<unsigned char>& buffer);
  void renderGlyphExample(const std::vector<unsigned char>& ttf_buffer, char c, int fontSize);

  float stbtt_CalcTextWidth(const stbtt_fontinfo* font, float scale, const char* text);
  void measure_text(const char* text, stbtt_fontinfo* font, float pixelSize, int& outWidth, int& outHeight);
  void measure_text_bbox(const char* text, stbtt_fontinfo* font, float scale, int& outWidth, int& outHeight, float& outMinX, float& outMinY);
  void draw_value_on_image(TrackingVector<unsigned char>& image, int img_width, int img_height,
                           int pos_x, int pos_y, double value,
                           stbtt_fontinfo *font, float scale, const char *format);
  void draw_text_label_centered(TrackingVector<unsigned char>& image,
                                int img_width, int img_height,
                                int pos_x, int pos_y,
                                const char* text,
                                stbtt_fontinfo* font,
                                float charpixelsize);
  void draw_text_rotated_on_image(TrackingVector<unsigned char>& image,
                                  int img_width, int img_height,
                                  int center_x, int center_y,
                                  const char* text,
                                  stbtt_fontinfo *font, float charpixelsize);
  void draw_char(TrackingVector<unsigned char>& image, int img_width, int img_height,
                 int pos_x, int pos_y, int codepoint,
                 stbtt_fontinfo *font, float scale);
  void draw_rotated_char(TrackingVector<unsigned char>& image,
                         int img_width, int img_height,
                         int pos_x, int pos_y, int codepoint,
                         stbtt_fontinfo *font, float scale);
  void drawTextBaselineAndRotate90(TrackingVector<unsigned char>& image,
                                   int img_width, int img_height,
                                   int center_x, int center_y,
                                   const char* text,
                                   stbtt_fontinfo *font, float charpixelsize);
  void set_projection_parameters(const TrackingVector<ParticleData>& originalParticles, const int useAngularMomentumAxis,
                                 const float* pos_center, const float len, const float val_min, const float val_max,
                                 const int npixel_input, const int nslices, std::string var);

  CuboidObject& interactiveCuboid() { return interactiveCuboid_; }
  const CuboidObject& interactiveCuboid() const { return interactiveCuboid_; }
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
static inline double compute_band_luminosity_Lsun(double M_Msun, const FluxSettings& fs){
  const double Lbol = Lbol_single_massive_Lsun(M_Msun); if (!(Lbol > 0.0) || !std::isfinite(Lbol)) return 0.0;
  const double Teff = Teff_massive_K(M_Msun); if (!(Teff > 0.0) || !std::isfinite(Teff)) return 0.0;
  const double lambda0_m = std::max(1.0, (double)fs.band_center_nm) * 1e-9;
  const double dlambda_m = std::max(1.0, (double)fs.band_width_nm ) * 1e-9;
  const double frac = band_fraction_rect_lambda(Teff, lambda0_m, dlambda_m);
  return Lbol * frac;
}

#endif
