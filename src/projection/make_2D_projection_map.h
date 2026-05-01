#ifndef PROJECTION_MAP_GENERATOR_H
#define PROJECTION_MAP_GENERATOR_H

#ifdef USE_LUA
#include <lua.hpp>
#endif

#include "image/bitmap_font_renderer.h"

#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

class ImageCanvas;
class SimulationDataset;
class SimulationElement;
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
    float sourceWorldToRenderScale = 1.0f;
    float minVal = 0.f, maxVal = 0.f;
    bool flagDensityWeight = false;
    bool flagLogScale = false;
    glm::vec3 center = glm::vec3(0.f);
    glm::vec3 uAxis = glm::vec3(1.f,0.f,0.f), vAxis = glm::vec3(0.f,1.f,0.f), wAxis = glm::vec3(0.f,0.f,1.f);
    std::vector<double> values;
    std::vector<double> weights;
    std::vector<unsigned char> image;
  };

  void createProjectionMap(ProjectionMap &map, const std::vector<pos_val>& particles);
  void createVoronoiSliceMap(ProjectionMap& map, const std::vector<pos_val>& particles);
  void createStarMap(ProjectionMap &map, const std::vector<pos_val>& particles, float sigma_pix, bool normalize);

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
			    const std::vector<SimulationElement>& particles);
  float kernel(float u);
  ProjectionMap buildProjectionMap(const ProjectionMapParams& params,
                                   const ProjectionMapContext& ctx) const;
  RgbImage composeProjectionMapImage(ProjectionMap& map,
                                     const ProjectionMapParams& params,
                                     const ProjectionMapContext& ctx,
                                     const std::vector<SimulationElement>& originalParticles);
  RgbImage makeSingleDensityMapImage(SimulationDataset& particles,
                                     const UnitSystem& units,
                                     ProjectionMapParams& params,
                                     ProjectionMapContext& ctx);
  RgbImage makeMultiPanelDensityMapImage(SimulationDataset& particles,
                                         const UnitSystem& units,
                                         ProjectionMapParams& params,
                                         ProjectionMapContext& ctx);
  
public:
  RgbImage makeDensityMapImage(SimulationDataset& particles,
			       const UnitSystem& units,
			       ProjectionMapParams& params,
			       ProjectionMapContext& ctx);

  int getFontCount() const;
  const std::string& getFontPath(int index) const;
  bool selectFontFileByIndex(int index);
  
  ProjectionMapGenerator();

  std::vector<glm::vec3> computeCuboidVertices(float *xmin, float *xmax, glm::vec3 center, glm::quat cuboidTransform);

  static void colormapLookup(float t, float& r, float& g, float& b, const float *colorMap, int countColorMap);  
};

#endif
