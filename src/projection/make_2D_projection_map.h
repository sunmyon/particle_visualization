#ifndef PROJECTION_MAP_GENERATOR_H
#define PROJECTION_MAP_GENERATOR_H

#include "image/bitmap_font_renderer.h"

#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

#include "projection/projection_gpu_backend.h"
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
#include "projection/metal_projection_backend.h"
#endif

class ImageCanvas;
struct SimulationBlock;
class SimulationDataset;
class SimulationElement;
struct ProjectionMapParams;
struct ProjectionMapContext;
struct RgbImage;
struct FluxSettings;
struct UnitSystem;
struct QuantityState;

ProjectionMapContext BuildProjectionMapContext(const ProjectionMapParams& params,
                                               double time);

extern const float jetMap[];
extern const float viridisMap[];
extern const float plasmaMap[];

// Detached projection work item. Values are copied from SimulationBlock via
// accessors before CPU/GPU projection code sees them.
struct ProjectionParticleSample {
  float pos[3];
  float val;
  float colorVal;
  float density;
  float mass;
  float hsml;
};

class ProjectionMapGenerator {
private:
  BitmapFontRenderer fontRenderer_;

#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
  struct MetalVoronoiCache {
    bool valid = false;
    std::size_t key = 0;
    ProjectionGpuLabelGrid grid;
  };
  MetalVoronoiCache metalVoronoiCache_;
#endif

#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
  struct VulkanVoronoiCache {
    bool valid = false;
    std::size_t key = 0;
    ProjectionGpuLabelGrid grid;
  };
  VulkanVoronoiCache vulkanVoronoiCache_;
#endif
  
  struct ProjectionMap {
    int npixel = 0, npixel_x = 0, npixel_y = 0, npixel_z = 0;
    float xlen[3] = {0.f, 0.f, 0.f}, xmin[3] = {0.f, 0.f, 0.f};
    float dx = 0.f, dy = 0.f, dz = 0.f;
    float cell_size = 0.f;
    float minVal = 0.f, maxVal = 0.f;
    float colorMinVal = 0.f, colorMaxVal = 1.f;
    bool flagDensityWeight = false;
    bool flagLogScale = false;
    glm::vec3 center = glm::vec3(0.f);
    glm::vec3 uAxis = glm::vec3(1.f,0.f,0.f), vAxis = glm::vec3(0.f,1.f,0.f), wAxis = glm::vec3(0.f,0.f,1.f);
    std::vector<double> values;
    std::vector<double> weights;
    std::vector<unsigned char> image;
  };

  struct VoronoiLabelGrid {
    int width = 0;
    int height = 0;
    int depth = 0;
    std::vector<int> labels;
  };

  struct CpuVoronoiCache {
    bool valid = false;
    std::size_t key = 0;
    VoronoiLabelGrid grid;
  };
  CpuVoronoiCache cpuVoronoiCache_;

  void createProjectionMap(ProjectionMap &map, const std::vector<ProjectionParticleSample>& particles);
  void createVoronoiSliceMap(ProjectionMap& map,
                             const std::vector<ProjectionParticleSample>& particles,
                             const ProjectionMapParams& params,
                             const ProjectionMapContext& ctx);
  VoronoiLabelGrid buildCpuVoronoiLabelGrid(const ProjectionMap& map,
                                            const std::vector<ProjectionParticleSample>& particles);
  void integrateCpuVoronoiLabelGrid(ProjectionMap& map,
                                    const std::vector<ProjectionParticleSample>& particles,
                                    const VoronoiLabelGrid& grid);
  void renderCpuVoronoiLabelGrid(ProjectionMap& map,
                                 const std::vector<ProjectionParticleSample>& particles,
                                 const VoronoiLabelGrid& grid,
                                 const ProjectionMapParams& params,
                                 const ProjectionMapContext& ctx);
  void createStarMap(ProjectionMap &map, const std::vector<ProjectionParticleSample>& particles, float sigma_pix, bool normalize);

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
				    const SimulationBlock& block,
				    const UnitSystem& units);
	  void overlayVectorField(ImageCanvas& canvas,
	                          const ProjectionMap& map,
	                          const ProjectionMapParams& params,
	                          const SimulationBlock& block);
	  float kernel(float u);
  ProjectionMap buildProjectionMap(const ProjectionMapParams& params,
                                   const ProjectionMapContext& ctx) const;
  RgbImage composeProjectionMapImage(ProjectionMap& map,
	                                     const ProjectionMapParams& params,
	                                     const ProjectionMapContext& ctx,
	                                     const SimulationBlock& block,
	                                     const UnitSystem& units);
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
