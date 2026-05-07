#include "image/image_canvas.h"
#include "image/image_io.h"

#include "data/simulation_dataset.h"
#include "data/simulation_block.h"
#include "data/sample_coordinates.h"
#include "render/scene_objects.h"

#include <chrono>
#include <fstream>
#include <filesystem>
#include <sstream>

#include "image/rgb_image.h"
#include "render/colormap_defs.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_params.h"
#include "projection/projection_geometry.h"
#include "projection/projection_map_context.h"
#include "projection/stellar_luminosity.h"
#include "projection/opengl_projection_backend.h"
#include "projection/vulkan_projection_backend.h"
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
#include "projection/metal_projection_backend.h"
#endif

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>

#ifdef USE_LUA
#include <lua.hpp>
#endif
#include <nanoflann.hpp>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace{
  // Data container used by nanoflann.
  struct VoronoiParticleCloud {
    std::vector<pos_val> particles;
  
    // kd-tree interface.
    inline size_t kdtree_get_point_count() const { return particles.size(); }
  
    // Return coordinate dim for the requested particle index.
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
      return particles[idx].pos[dim];
    }
  
    // Bounding boxes are omitted.
    template <class BBOX>
    bool kdtree_get_bbox(BBOX & /*bb*/) const { return false; }
  };

  // kd-tree type for 3D searches.
  typedef nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, VoronoiParticleCloud>,
    VoronoiParticleCloud,
    3 /* dim */
    > KDTreeVoronoi;
}

namespace fs = std::filesystem;

ProjectionMapGenerator::ProjectionMapGenerator() = default;

namespace {
inline void HashCombine(std::size_t& seed, std::size_t value)
{
  seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

inline void HashFloat(std::size_t& seed, float value)
{
  static_assert(sizeof(float) == sizeof(std::uint32_t));
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  HashCombine(seed, static_cast<std::size_t>(bits));
}

double EvaluateProjectionTfComponent(
  const ProjectionTransferFunctionComponent& c,
  double value)
{
  if (!std::isfinite(value)) {
    return 0.0;
  }

  const double width = std::max<double>(std::abs(c.width), 1.0e-12);
  const double amp = std::max<double>(c.amplitude, 0.0);
  if (amp <= 0.0) {
    return 0.0;
  }

  if (c.type == 0 && c.logDomain) {
    if (value <= 0.0 || c.center <= 0.0) {
      return 0.0;
    }
    const double x = std::log10(value);
    const double center = std::log10(std::max<double>(c.center, 1.0e-30));
    const double q = (x - center) / width;
    return amp * std::exp(-0.5 * q * q);
  }

  const double q = std::abs(value - static_cast<double>(c.center));
  if (c.type == 0) {
    const double u = q / width;
    return amp * std::exp(-0.5 * u * u);
  }
  if (c.type == 1) {
    return q <= width ? amp : 0.0;
  }
  if (c.type == 2) {
    return q <= width ? amp * (1.0 - q / width) : 0.0;
  }
  return 0.0;
}

double EvaluateProjectionTf(const ProjectionMapParams& params, double value)
{
  double sigma = 0.0;
  for (const ProjectionTransferFunctionComponent& c :
       params.voronoiTfComponents) {
    sigma += EvaluateProjectionTfComponent(c, value);
  }
  return std::max(sigma, 0.0);
}
} // namespace

RgbImage ProjectionMapGenerator::makeDensityMapImage(SimulationDataset& particles,
						     const UnitSystem& units,
						     ProjectionMapParams& params,
						     ProjectionMapContext& ctx)
{
  ProjectionEnsureLayoutInitialized(params);
  if (params.multiPanelEnabled && params.dataSource == DataSource::Gas) {
    return makeMultiPanelDensityMapImage(particles, units, params, ctx);
  }

  ProjectionMapParams panelParams = params;
  panelParams.multiPanelEnabled = false;
  const int blockIndex = std::clamp(panelParams.panels[0].viewBlockIndex,
                                    0,
                                    panelParams.viewBlockCount - 1);
  const ProjectionViewBlockSpec block =
    ProjectionResolveViewBlock(panelParams, blockIndex);
  ProjectionApplyViewBlockToParams(panelParams, block);
  ProjectionApplyPanelToParams(panelParams, panelParams.panels[0], block);
  ProjectionMapContext panelCtx =
    BuildProjectionMapContext(panelParams,
                              ctx.scaleToPhysical,
                              ctx.time);
  return makeSingleDensityMapImage(particles, units, panelParams, panelCtx);
}

RgbImage ProjectionMapGenerator::makeSingleDensityMapImage(SimulationDataset& particles,
							   const UnitSystem& units,
							   ProjectionMapParams& params,
							   ProjectionMapContext& ctx)
{
#ifdef USE_LUA
  ensureLuaInitialized();
#endif

  std::vector<SimulationElement>& originalParticles = particles.simulationBlock.particles;
  
  if(params.flagSpecifyZoomRegionByMass){
    //construct xmin, xmax, center here
    double xmax_zoom[3] = {-1.e30, -1.e30, -1.e30};
    double xmin_zoom[3] = {1.e30, 1.e30, 1.e30};
    double xsum_zoom[3] = {0., 0., 0.,};
    double weight = 0.;
    
    int count = 0;
    for (const auto& p : originalParticles) {
      if(p.type != ctx.selectedType)
	continue;
      
      if(p.mass > params.criticalGasMassForZoomRegion)
	continue;
      
      const glm::vec3 pos =
        renderPosition(p, particles.simulationBlock.worldToRenderScale);
      for(int k=0;k<3;k++){
	if(pos[k] < xmin_zoom[k])
	  xmin_zoom[k] = pos[k];

	if(pos[k] > xmax_zoom[k])
	  xmax_zoom[k] = pos[k];

	xsum_zoom[k] += p.mass * pos[k];
      }

      weight += p.mass;
      count++;
    }

    if(count > 0 && weight > 0.0){
      for(int k=0;k<3;k++)
	xsum_zoom[k] /= weight;

      printf("xmin_zoom=%g %g %g max=%g %g %g mean=%g %g %g\n"
	     , xmin_zoom[0], xmin_zoom[1], xmin_zoom[2]
	     , xmax_zoom[0], xmax_zoom[1], xmax_zoom[2]
	     , xsum_zoom[0], xsum_zoom[1], xsum_zoom[2]
	     );
      
      ctx.center.x = xsum_zoom[0];
      ctx.center.y = xsum_zoom[1];
      ctx.center.z = xsum_zoom[2];
      if (params.lenZoomRegion > 0.0f && ctx.scaleToPhysical > 0.0) {
        const float lenNormalized =
          params.lenZoomRegion / static_cast<float>(ctx.scaleToPhysical);
        params.xlen[0] = lenNormalized;
        params.xlen[1] = lenNormalized;
        params.xlen[2] = lenNormalized;
      }
    }else
      printf("no particles have been found...\n");
  }

  float xmin[3], xmin_cut[3], xmax_cut[3];
  xmin[0] = ctx.center.x - 0.5 * params.xlen[0];
  xmin[1] = ctx.center.y - 0.5 * params.xlen[1];
  xmin[2] = ctx.center.z - 0.5 * params.xlen[2];

  /*we temporally define here, xmin_cut and xmax_cut should be removed... */
  xmin_cut[0] = ctx.center.x - params.xlen[0];
  xmin_cut[1] = ctx.center.y - params.xlen[1];
  xmin_cut[2] = ctx.center.z - params.xlen[2];

  xmax_cut[0] = ctx.center.x + params.xlen[0];
  xmax_cut[1] = ctx.center.y + params.xlen[1];
  xmax_cut[2] = ctx.center.z + params.xlen[2];  
  
  std::vector<pos_val> insideParticles;

  for (int idx = 0; idx < (int)originalParticles.size(); ++idx) {
    const auto& p = originalParticles[idx];
    if (p.type != ctx.selectedType) continue;

    bool inside = false;
    const glm::vec3 pos =
      renderPosition(p, particles.simulationBlock.worldToRenderScale);
    glm::vec4 localPos =
      glm::inverse(ctx.cuboidTransform)
      * glm::vec4(pos - ctx.center, 1.0f)
      + glm::vec4(ctx.center.x, ctx.center.y, ctx.center.z, 0.);

    if (localPos.x >= xmin_cut[0] && localPos.x <= xmax_cut[0] &&
	localPos.y >= xmin_cut[1] && localPos.y <= xmax_cut[1] &&
	localPos.z >= xmin_cut[2] && localPos.z <= xmax_cut[2]) {
      inside = true;
    }
    if (!inside) continue;

    pos_val pp;
    pp.pos[0] = pos.x; pp.pos[1] = pos.y; pp.pos[2] = pos.z;
    pp.hsml = renderSupportRadius(p, particles.simulationBlock.worldToRenderScale);
    pp.mass = p.mass;
    
    if (params.dataSource == DataSource::Gas) {
      const QuantityId projectionQuantity =
        (params.flagVoronoi &&
         params.voronoiMode == ProjectionVoronoiMode::OpacityRendering)
          ? params.voronoiOpacityVarGas
          : params.selectedVarGas;
      pp.val = getScalarValue(particles.simulationBlock,
                              p,
                              idx,
                              projectionQuantity);
      pp.colorVal = getScalarValue(particles.simulationBlock,
                                   p,
                                   idx,
                                   params.selectedVarGas);
      pp.density = p.density;
    }
    
    if (params.dataSource == DataSource::Stars) {
      if (params.starQuantity == StarQuantity::Metallicity) {
	float zmet = 0.0f;
	particles.simulationBlock.readSoAAs(soa_views::Metallicity, (size_t)idx, zmet);
	pp.val = zmet;
        pp.colorVal = pp.val;
      } else if (params.starQuantity == StarQuantity::Mass) {
	pp.val = p.mass;
        pp.colorVal = pp.val;
      } else if (params.starQuantity == StarQuantity::Density) {
	pp.val = p.density;
        pp.colorVal = pp.val;
      } else if (params.starQuantity == StarQuantity::Flux) {
	pp.val = (float)compute_band_luminosity_Lsun(p.mass*units.mass_msun, params.flux);
        pp.colorVal = pp.val;
      } else {
	pp.val = 1.0f;
        pp.colorVal = pp.val;
      }
    }

    insideParticles.push_back(pp);
  }

  if (params.dataSource == DataSource::Gas) 
    if(insideParticles.size() < 0.1 * params.npixel * params.npixel){
      printf("too few particles inside specified region... npart=%ld while there are %dx%d pixels.\n",
	     insideParticles.size(), params.npixel, params.npixel);
      return {};
    }
    
  ProjectionMap map = buildProjectionMap(params, ctx);
  map.sourceWorldToRenderScale = particles.simulationBlock.worldToRenderScale;
  for (int k = 0; k < 3; ++k) map.xmin[k] = xmin[k];

  using namespace std::chrono;
  auto start = high_resolution_clock::now();

  if (params.dataSource == DataSource::Gas) {
    bool projectedOnGpu = false;
    if (params.useGpuProjection) {
      ProjectionGpuMapInput input;
      input.width = map.npixel_x;
      input.height = map.npixel_y;
      input.depth = map.npixel_z;
      input.dx = map.dx;
      input.dy = map.dy;
      input.dz = map.dz;
      input.xminLocal[0] = map.xmin[0] - map.center.x;
      input.xminLocal[1] = map.xmin[1] - map.center.y;
      input.xminLocal[2] = map.xmin[2] - map.center.z;
      input.center = map.center;
      input.uAxis = map.uAxis;
      input.vAxis = map.vAxis;
      input.densityWeight = map.flagDensityWeight;
      input.renderColor[0] = params.voronoiRenderColor[0];
      input.renderColor[1] = params.voronoiRenderColor[1];
      input.renderColor[2] = params.voronoiRenderColor[2];
      input.colorLogScale = params.flagLogScale;
      input.colorMapSize = ctx.colorMapSize;
      input.colorMap.assign(ctx.colorMap,
                            ctx.colorMap + static_cast<size_t>(ctx.colorMapSize) * 3);
      for (const ProjectionTransferFunctionComponent& src :
           params.voronoiTfComponents) {
        if (input.transferComponents.size() >=
            static_cast<std::size_t>(kProjectionGpuMaxTfComponents)) {
          break;
        }
        ProjectionGpuTransferComponent dst;
        dst.type = src.type;
        dst.center = src.center;
        dst.width = src.width;
        dst.amplitude = src.amplitude;
        dst.logDomain = src.logDomain;
        input.transferComponents.push_back(dst);
      }
      input.particles.reserve(insideParticles.size());
      double pxMin = std::numeric_limits<double>::max();
      double pxMax = -std::numeric_limits<double>::max();
      double pyMin = std::numeric_limits<double>::max();
      double pyMax = -std::numeric_limits<double>::max();
      double radiusPixMin = std::numeric_limits<double>::max();
      double radiusPixMax = 0.0;
      double radiusPixSum = 0.0;
      size_t positiveRadiusCount = 0;
      size_t projectedInsideCount = 0;
      double weightNormMin = std::numeric_limits<double>::max();
      double weightNormMax = 0.0;
      size_t finiteWeightNormCount = 0;
      size_t nonfiniteInputCount = 0;
      double valueMin = std::numeric_limits<double>::max();
      double valueMax = -std::numeric_limits<double>::max();
      double colorValueMin = std::numeric_limits<double>::max();
      double colorValueMax = -std::numeric_limits<double>::max();
      size_t finiteColorValueCount = 0;
      for (const pos_val& p : insideParticles) {
        const glm::vec3 diff =
          glm::vec3(p.pos[0], p.pos[1], p.pos[2]) - map.center;
        const double cx = glm::dot(diff, map.uAxis);
        const double cy = glm::dot(diff, map.vAxis);
        const double cz = glm::dot(diff, map.wAxis);

        ProjectionGpuParticle gp;
        if (params.flagVoronoi) {
          gp.pos[0] = static_cast<float>(cx);
          gp.pos[1] = static_cast<float>(cy);
          gp.pos[2] = static_cast<float>(cz);
        } else {
          gp.pos[0] = p.pos[0];
          gp.pos[1] = p.pos[1];
          gp.pos[2] = p.pos[2];
        }
        gp.val = p.val;
        gp.colorVal = p.colorVal;
        gp.density = p.density;
        gp.mass = p.mass;
        gp.hsml = p.hsml;
        input.particles.push_back(gp);

        const double px = (cx - input.xminLocal[0]) / input.dx;
        const double py = (cy - input.xminLocal[1]) / input.dy;
        pxMin = std::min(pxMin, px);
        pxMax = std::max(pxMax, px);
        pyMin = std::min(pyMin, py);
        pyMax = std::max(pyMax, py);
        const double radiusPix = p.hsml / std::max<double>(input.dx, 1.0e-30);
        if (radiusPix > 0.0) {
          radiusPixMin = std::min(radiusPixMin, radiusPix);
          radiusPixMax = std::max(radiusPixMax, radiusPix);
          radiusPixSum += radiusPix;
          ++positiveRadiusCount;
        }
        if (px + radiusPix >= 0.0 && px - radiusPix <= input.width &&
            py + radiusPix >= 0.0 && py - radiusPix <= input.height) {
          ++projectedInsideCount;
        }

        const double hsml3 =
          static_cast<double>(p.hsml) * p.hsml * p.hsml;
        const double density = std::max<double>(p.density, 1.0e-30);
        const double weightNorm =
          static_cast<double>(p.mass) / std::max(hsml3 * density, 1.0e-30);
        if (std::isfinite(weightNorm) && weightNorm > 0.0 &&
            std::isfinite(p.val) && std::isfinite(p.hsml) &&
            std::isfinite(p.density) && std::isfinite(p.mass)) {
          weightNormMin = std::min(weightNormMin, weightNorm);
          weightNormMax = std::max(weightNormMax, weightNorm);
          valueMin = std::min<double>(valueMin, p.val);
          valueMax = std::max<double>(valueMax, p.val);
          ++finiteWeightNormCount;
        } else {
          ++nonfiniteInputCount;
        }
        if (std::isfinite(p.colorVal)) {
          double colorValue = p.colorVal;
          if (params.flagLogScale) {
            if (colorValue <= 0.0) {
              continue;
            }
            colorValue = std::log10(colorValue);
          }
          colorValueMin = std::min<double>(colorValueMin, colorValue);
          colorValueMax = std::max<double>(colorValueMax, colorValue);
          ++finiteColorValueCount;
        }
      }
      if (finiteWeightNormCount > 0) {
        input.valueMin = static_cast<float>(valueMin);
        input.valueMax = static_cast<float>(valueMax);
      }
      if (finiteColorValueCount > 0) {
        input.colorValueMin =
          params.autoRange ? static_cast<float>(colorValueMin)
                           : params.range_min;
        input.colorValueMax =
          params.autoRange ? static_cast<float>(colorValueMax)
                           : params.range_max;
        map.colorMinVal = input.colorValueMin;
        map.colorMaxVal = input.colorValueMax;
      }

      std::cout << "GPU projection input: particles="
                << input.particles.size()
                << " pixels=" << input.width << "x" << input.height
                << " pxRange=[" << pxMin << ", " << pxMax << "]"
                << " pyRange=[" << pyMin << ", " << pyMax << "]"
                << " projectedInside=" << projectedInsideCount
                << " radiusPixRange=["
                << (positiveRadiusCount > 0 ? radiusPixMin : 0.0)
                << ", " << radiusPixMax << "]"
                << " radiusPixMean="
                << (positiveRadiusCount > 0
                      ? radiusPixSum / static_cast<double>(positiveRadiusCount)
                      : 0.0)
                << " weightNormRange=["
                << (finiteWeightNormCount > 0 ? weightNormMin : 0.0)
                << ", " << weightNormMax << "]"
                << " valueRange=["
                << (finiteWeightNormCount > 0 ? valueMin : 0.0)
                << ", " << (finiteWeightNormCount > 0 ? valueMax : 0.0)
                << "]"
                << " finiteWeightNorm=" << finiteWeightNormCount
                << " nonfiniteInput=" << nonfiniteInputCount
                << std::endl;

      ProjectionGpuMapOutput output;
      bool projectionOk = false;
      std::string projectionBackendUsed;
      const char* requestedBackendEnv =
        std::getenv("PARTICLE_VIS_PROJECTION_BACKEND");
      const char* renderBackendEnv =
        std::getenv("PARTICLE_VIS_RENDER_BACKEND");
      std::string requestedBackend =
        requestedBackendEnv ? requestedBackendEnv : "auto";
      if (requestedBackend == "auto" && renderBackendEnv &&
          renderBackendEnv[0] != '\0') {
        // Keep compute projection aligned with the selected render backend.
        // If that backend cannot compute this projection, fall back to CPU
        // rather than silently mixing GPU APIs.
        requestedBackend = renderBackendEnv;
      }

      const auto tryMetalProjection = [&]() {
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
        if (params.flagVoronoi) {
          return false;
        }
        if (RunMetalProjectionMap(input, output)) {
          projectionBackendUsed = "Metal";
          return true;
        }
        return false;
#else
        return false;
#endif
      };
      const auto tryMetalVoronoiProjection = [&]() {
#ifdef PARTICLE_VIS_ENABLE_METAL_BACKEND
        if (!params.flagVoronoi) {
          return false;
        }
        std::size_t cacheKey = 1469598103934665603ull;
        HashCombine(cacheKey, static_cast<std::size_t>(input.width));
        HashCombine(cacheKey, static_cast<std::size_t>(input.height));
        HashCombine(cacheKey, static_cast<std::size_t>(input.depth));
        HashFloat(cacheKey, input.dx);
        HashFloat(cacheKey, input.dy);
        HashFloat(cacheKey, input.dz);
        HashFloat(cacheKey, input.xminLocal[0]);
        HashFloat(cacheKey, input.xminLocal[1]);
        HashFloat(cacheKey, input.xminLocal[2]);
        for (int k = 0; k < 3; ++k) {
          HashFloat(cacheKey, input.uAxis[k]);
          HashFloat(cacheKey, input.vAxis[k]);
          HashFloat(cacheKey, map.wAxis[k]);
        }
        HashCombine(cacheKey, input.particles.size());
        for (const ProjectionGpuParticle& p : input.particles) {
          HashFloat(cacheKey, p.pos[0]);
          HashFloat(cacheKey, p.pos[1]);
          HashFloat(cacheKey, p.pos[2]);
        }

        if (!metalVoronoiCache_.valid ||
            metalVoronoiCache_.key != cacheKey) {
          metalVoronoiCache_.valid = false;
          metalVoronoiCache_.key = cacheKey;
          projectionOk =
            BuildMetalVoronoiLabelGrid(input, metalVoronoiCache_.grid);
          metalVoronoiCache_.valid = projectionOk;
        } else {
          std::cout << "Metal Voronoi label cache hit: grid="
                    << metalVoronoiCache_.grid.width << "x"
                    << metalVoronoiCache_.grid.height << "x"
                    << metalVoronoiCache_.grid.depth << std::endl;
          projectionOk = true;
        }
        if (projectionOk) {
          projectionOk =
            (params.voronoiMode == ProjectionVoronoiMode::OpacityRendering)
              ? RenderMetalVoronoiLabelGrid(input,
                                            metalVoronoiCache_.grid,
                                            output)
              : IntegrateMetalVoronoiLabelGrid(input,
                                               metalVoronoiCache_.grid,
                                               output);
          if (projectionOk) {
            output.elapsedMs += metalVoronoiCache_.grid.elapsedMs;
          }
        }
        if (projectionOk) {
          projectionBackendUsed = "Metal";
        }
        return projectionOk;
#else
        return false;
#endif
      };
      const auto tryVulkanProjection = [&]() {
#ifdef PARTICLE_VIS_ENABLE_VULKAN_BACKEND
        bool ok = false;
        if (params.flagVoronoi) {
          std::size_t cacheKey = 1469598103934665603ull;
          HashCombine(cacheKey, static_cast<std::size_t>(input.width));
          HashCombine(cacheKey, static_cast<std::size_t>(input.height));
          HashCombine(cacheKey, static_cast<std::size_t>(input.depth));
          HashFloat(cacheKey, input.dx);
          HashFloat(cacheKey, input.dy);
          HashFloat(cacheKey, input.dz);
          HashFloat(cacheKey, input.xminLocal[0]);
          HashFloat(cacheKey, input.xminLocal[1]);
          HashFloat(cacheKey, input.xminLocal[2]);
          for (int k = 0; k < 3; ++k) {
            HashFloat(cacheKey, input.uAxis[k]);
            HashFloat(cacheKey, input.vAxis[k]);
            HashFloat(cacheKey, map.wAxis[k]);
          }
          HashCombine(cacheKey, input.particles.size());
          for (const ProjectionGpuParticle& p : input.particles) {
            HashFloat(cacheKey, p.pos[0]);
            HashFloat(cacheKey, p.pos[1]);
            HashFloat(cacheKey, p.pos[2]);
          }

          if (!vulkanVoronoiCache_.valid ||
              vulkanVoronoiCache_.key != cacheKey) {
            vulkanVoronoiCache_.valid = false;
            vulkanVoronoiCache_.key = cacheKey;
            ok = BuildVulkanVoronoiLabelGrid(input, vulkanVoronoiCache_.grid);
            vulkanVoronoiCache_.valid = ok;
          } else {
            std::cout << "Vulkan Voronoi label cache hit: grid="
                      << vulkanVoronoiCache_.grid.width << "x"
                      << vulkanVoronoiCache_.grid.height << "x"
                      << vulkanVoronoiCache_.grid.depth << std::endl;
            ok = true;
          }
          if (ok) {
            ok = (params.voronoiMode == ProjectionVoronoiMode::OpacityRendering)
                   ? RenderVulkanVoronoiLabelGrid(input,
                                                  vulkanVoronoiCache_.grid,
                                                  output)
                   : IntegrateVulkanVoronoiLabelGrid(input,
                                                     vulkanVoronoiCache_.grid,
                                                     output);
            if (ok) {
              output.elapsedMs += vulkanVoronoiCache_.grid.elapsedMs;
            }
          }
        } else {
          ok = RunVulkanProjectionMap(input, output);
        }
        if (ok) {
          projectionBackendUsed = "Vulkan";
        }
        return ok;
#else
        return false;
#endif
      };
      const auto tryOpenGLProjection = [&]() {
        if (params.flagVoronoi &&
            params.voronoiMode == ProjectionVoronoiMode::OpacityRendering) {
          return false;
        }
        const bool ok = params.flagVoronoi
                          ? RunOpenGLVoronoiProjectionMap(input, output)
                          : RunOpenGLProjectionMap(input, output);
        if (ok) {
          projectionBackendUsed = "OpenGL";
        }
        return ok;
      };

      if (requestedBackend == "metal") {
        projectionOk = params.flagVoronoi ? tryMetalVoronoiProjection()
                                          : tryMetalProjection();
      } else if (requestedBackend == "vulkan") {
        projectionOk = tryVulkanProjection();
      } else if (requestedBackend == "opengl") {
        projectionOk = tryOpenGLProjection();
      } else {
        projectionOk = params.flagVoronoi ? tryMetalVoronoiProjection()
                                          : tryMetalProjection();
        if (!projectionOk) {
          projectionOk = tryVulkanProjection();
        }
        if (!projectionOk) {
          projectionOk = tryOpenGLProjection();
        }
      }
      if (projectionOk) {
        if (params.flagVoronoi &&
            params.voronoiMode == ProjectionVoronoiMode::OpacityRendering &&
            output.rgb.size() >= map.values.size() * 3) {
          map.image.assign(map.values.size() * 3, 0);
          for (size_t i = 0; i < map.values.size(); ++i) {
            const float alpha =
              (i < output.weights.size()) ? output.weights[i] : 0.0f;
            map.weights[i] = alpha;
            map.values[i] = alpha;
            map.image[i * 3 + 0] = static_cast<unsigned char>(
              std::clamp(output.rgb[i * 3 + 0], 0.0f, 1.0f) * 255.0f);
            map.image[i * 3 + 1] = static_cast<unsigned char>(
              std::clamp(output.rgb[i * 3 + 1], 0.0f, 1.0f) * 255.0f);
            map.image[i * 3 + 2] = static_cast<unsigned char>(
              std::clamp(output.rgb[i * 3 + 2], 0.0f, 1.0f) * 255.0f);
          }
        } else {
          for (size_t i = 0; i < map.values.size(); ++i) {
            const double weight = output.weights[i];
            map.weights[i] = weight;
            map.values[i] = weight > 0.0 ? output.values[i] / weight : 0.0;
          }
        }
        std::cout << projectionBackendUsed << " "
                  << (params.flagVoronoi ? "Voronoi " : "")
                  << "projection elapsed time: "
                  << output.elapsedMs * 1.0e-3 << " sec\n";
        projectedOnGpu = true;
      } else {
        std::cerr << "GPU projection failed; falling back to CPU projection."
                  << std::endl;
      }
    }
    if (!projectedOnGpu) {
      if (params.flagVoronoi) createVoronoiSliceMap(map, insideParticles, params, ctx);
      else             createProjectionMap(map, insideParticles);
    }
  }
  else if (params.dataSource == DataSource::Stars) {
    bool normalize = (params.starQuantity != StarQuantity::Flux);
    createStarMap(map, insideParticles, params.psf_sigma_pix, normalize);
  }

  auto end = high_resolution_clock::now();
  std::cout << "Elapsed time: " << duration_cast<duration<double>>(end - start).count() << " sec\n";

  return composeProjectionMapImage(map, params, ctx, particles.simulationBlock);
}

RgbImage ProjectionMapGenerator::makeMultiPanelDensityMapImage(SimulationDataset& particles,
							       const UnitSystem& units,
							       ProjectionMapParams& params,
							       ProjectionMapContext& ctx)
{
  ProjectionEnsureLayoutInitialized(params);
  const int rows = std::clamp(params.multiPanelRows, 1, 3);
  const int cols = std::clamp(params.multiPanelCols, 1, 6);
  const int panelCount = std::min(rows * cols, kProjectionMaxPanels);

  std::vector<RgbImage> panelImages;
  panelImages.resize(static_cast<size_t>(panelCount));

  int panelWidth = 0;
  int panelHeight = 0;

  for (int blockIndex = 0; blockIndex < params.viewBlockCount; ++blockIndex) {
    const ProjectionViewBlockSpec block =
      ProjectionResolveViewBlock(params, blockIndex);
    for (int i = 0; i < panelCount; ++i) {
      const ProjectionPanelSpec& panelSpec = params.panels[i];
      if (panelSpec.viewBlockIndex != blockIndex) {
        continue;
      }

      ProjectionMapParams panelParams = params;
      panelParams.multiPanelEnabled = false;
      panelParams.panels[0] = panelSpec;
      ProjectionApplyViewBlockToParams(panelParams, block);
      ProjectionApplyPanelToParams(panelParams, panelSpec, block);

      ProjectionMapContext panelCtx =
        BuildProjectionMapContext(panelParams,
                                  ctx.scaleToPhysical,
                                  ctx.time);
      RgbImage panel =
        makeSingleDensityMapImage(particles, units, panelParams, panelCtx);
      if (!panel.valid()) {
        return {};
      }

      panelWidth = std::max(panelWidth, panel.width);
      panelHeight = std::max(panelHeight, panel.height);
      panelImages[static_cast<size_t>(i)] = std::move(panel);
    }
  }

  std::vector<unsigned char> tiledRgb;
  tiledRgb.resize(static_cast<size_t>(panelWidth) * panelHeight * rows * cols * 3,
                  0);
  ImageCanvas canvas{tiledRgb, panelWidth * cols, panelHeight * rows};

  for (int i = 0; i < panelCount; ++i) {
    if (!panelImages[static_cast<size_t>(i)].valid()) {
      return {};
    }
    const int col = i % cols;
    const int row = i / cols;
    canvas.copyRgbImage(panelImages[static_cast<size_t>(i)].rgb,
                        panelImages[static_cast<size_t>(i)].width,
                        panelImages[static_cast<size_t>(i)].height,
                        col * panelWidth,
                        row * panelHeight);
  }

  return ToRgbImage(canvas);
}

ProjectionMapGenerator::ProjectionMap
ProjectionMapGenerator::buildProjectionMap(const ProjectionMapParams& params,
                                           const ProjectionMapContext& ctx) const
{
  ProjectionMap map;
  for (int k = 0; k < 3; ++k) {
    map.xlen[k] = params.xlen[k];
    map.xmin[k] = ctx.center[k] - 0.5f * params.xlen[k];
  }

  printf("xlen=%g %g %g xmin=%g %g %g center=%g %g %g\n",
         params.xlen[0], params.xlen[1], params.xlen[2],
         map.xmin[0], map.xmin[1], map.xmin[2],
         ctx.center.x, ctx.center.y, ctx.center.z);

  map.npixel = params.npixel;
  map.cell_size = std::max(map.xlen[0], map.xlen[1]) / static_cast<float>(params.npixel);
  map.dx = map.cell_size;
  map.dy = map.cell_size;

  map.npixel_x = static_cast<int>(params.xlen[0] / map.cell_size);
  map.npixel_y = static_cast<int>(params.xlen[1] / map.cell_size);
  map.values.resize(map.npixel_x * map.npixel_y, 0.0f);
  map.weights.resize(map.npixel_x * map.npixel_y, 0.0f);

  if (params.flagVoronoi) {
    map.npixel_z = params.step_z;
    if (params.step_z % 2 == 0) map.npixel_z = params.step_z - 1;

    map.dz = 0.0f;
    if (params.step_z > 1) {
      map.dz = map.xlen[2] / static_cast<float>(map.npixel_z);
    }
  }

  map.flagDensityWeight = params.flagDensityWeight;
  map.flagLogScale = params.flagLogScale;

  glm::vec3 axisX = glm::normalize(ctx.cuboidTransform * glm::vec3(1, 0, 0));
  glm::vec3 axisY = glm::normalize(ctx.cuboidTransform * glm::vec3(0, 1, 0));
  glm::vec3 axisZ = glm::normalize(ctx.cuboidTransform * glm::vec3(0, 0, 1));
  glm::vec3 axes[3] = {axisX, axisY, axisZ};
  const int viewAxis = std::clamp(params.selectedAxis, 0, 2);
  const int upAxis = std::clamp(params.upAxis, 0, 2);
  const float viewSign = params.projectionSign < 0 ? -1.0f : 1.0f;
  const float upSign = params.upSign < 0 ? -1.0f : 1.0f;

  map.wAxis = glm::normalize(viewSign * axes[viewAxis]);
  glm::vec3 upHint = upSign * axes[upAxis];
  glm::vec3 vAxis = upHint - glm::dot(upHint, map.wAxis) * map.wAxis;
  if (glm::dot(vAxis, vAxis) < 1.0e-8f) {
    for (int candidate = 0; candidate < 3; ++candidate) {
      if (candidate == viewAxis) continue;
      upHint = axes[candidate];
      vAxis = upHint - glm::dot(upHint, map.wAxis) * map.wAxis;
      if (glm::dot(vAxis, vAxis) >= 1.0e-8f) break;
    }
  }
  map.vAxis = glm::normalize(vAxis);
  map.uAxis = glm::normalize(glm::cross(map.vAxis, map.wAxis));

  map.center = ctx.center;
  return map;
}

RgbImage ProjectionMapGenerator::composeProjectionMapImage(
  ProjectionMap& map,
  const ProjectionMapParams& params,
  const ProjectionMapContext& ctx,
  const SimulationBlock& block)
{
  const std::vector<SimulationElement>& originalParticles = block.particles;
  if (params.flagVoronoi &&
      params.voronoiMode == ProjectionVoronoiMode::OpacityRendering &&
      !map.image.empty()) {
    map.minVal = *std::min_element(map.values.begin(), map.values.end());
    map.maxVal = *std::max_element(map.values.begin(), map.values.end());
    if (map.maxVal <= map.minVal) {
      map.maxVal = map.minVal + 1.0e-6f;
    }

    int colorBarWidth = static_cast<int>(0.07f * map.npixel_x);
    ImageCanvas canvas{map.image, map.npixel_x, map.npixel_y};
    overlayStarParticles(canvas, map, params, ctx, originalParticles);
    overlayVectorField(canvas, map, params, block);

    addColorBarToMap(canvas,
                     map.cell_size,
                     map.colorMinVal,
                     map.colorMaxVal,
                     colorBarWidth,
                     ctx.colorMap,
                     ctx.colorMapSize,
                     QuantityLabel(params.selectedVarGas),
                     params,
                     ctx);

    return ToRgbImage(canvas);
  }

  float minVal = FLT_MAX;
  for (auto val : map.values) {
    if (val < minVal && val > 0.0f) minVal = val;
  }

  if (map.flagLogScale) {
    if (minVal > 0.0f) {
      for (size_t i = 0; i < map.values.size(); i++) {
        if (map.values[i] > 0.0f)
          map.values[i] = log10(map.values[i]);
        else
          map.values[i] = log10(minVal) - 1.0f;
      }
    } else {
      printf("minus quantity appears. we will use linear scale.\n");
    }
  }

  map.minVal = *std::min_element(map.values.begin(), map.values.end());
  map.maxVal = *std::max_element(map.values.begin(), map.values.end());

  float rangeMin = params.autoRange ? map.minVal : params.range_min;
  float rangeMax = params.autoRange ? map.maxVal : params.range_max;

  for (int i = 0; i < map.npixel_x * map.npixel_y; i++) {
    float norm = (map.values[i] - rangeMin) / (rangeMax - rangeMin + 1.e-6f);
    float rF, gF, bF;
    colormapLookup(norm, rF, gF, bF, ctx.colorMap, ctx.colorMapSize);
    unsigned char rC = (unsigned char)(rF * 255);
    unsigned char gC = (unsigned char)(gF * 255);
    unsigned char bC = (unsigned char)(bF * 255);

    map.image.push_back(rC);
    map.image.push_back(gC);
    map.image.push_back(bC);
  }

  int colorBarWidth = static_cast<int>(0.07f * map.npixel_x);
  ImageCanvas canvas{map.image, map.npixel_x, map.npixel_y};
  overlayStarParticles(canvas, map, params, ctx, originalParticles);
  overlayVectorField(canvas, map, params, block);

  addColorBarToMap(canvas,
                   map.cell_size,
                   rangeMin,
                   rangeMax,
                   colorBarWidth,
                   ctx.colorMap,
                   ctx.colorMapSize,
                   params.var.c_str(),
                   params,
                   ctx);

  return ToRgbImage(canvas);
}

  // Compute the eight cuboid vertices in local coordinates and transform them to world coordinates.
std::vector<glm::vec3> ProjectionMapGenerator::computeCuboidVertices(float *xmin, float *xmax, glm::vec3 center, glm::quat cuboidTransform)
{
  // Compute the local AABB center and half extents.
  float hx = (xmax[0] - xmin[0]) * 0.5f;
  float hy = (xmax[1] - xmin[1]) * 0.5f;
  float hz = (xmax[2] - xmin[2]) * 0.5f;
    
  std::vector<glm::vec3> local = {
    {  - hx, - hy, - hz },
    {  + hx, - hy, - hz },
    {  + hx, + hy, - hz },
    {  - hx, + hy, - hz },
    {  - hx, - hy, + hz },
    {  + hx, - hy, + hz },
    {  + hx, + hy, + hz },
    {  - hx, + hy, + hz }
    };
  
  glm::mat4 modelMat = glm::translate(glm::mat4(1.f), center)
    * glm::mat4_cast(cuboidTransform);
    
  std::vector<glm::vec3> world;
  for (const auto &v : local) {
    glm::vec4 wpos = modelMat * glm::vec4(v, 1.0f);
    world.push_back(glm::vec3(wpos));
  }
  return world;
}
  
float ProjectionMapGenerator::kernel(float u) {
  if(u  < 0.5)
    return 1.- 6.*u*u + 6.*u*u*u;
  else if(u < 1.)
    return 2. * pow(1.-u, 3.);
  else
    return 0.;
}
  
void ProjectionMapGenerator::createProjectionMap(ProjectionMap &map, const std::vector<pos_val>& particles)
{
  float xmin_local[3];
  xmin_local[0] = map.xmin[0] - map.center.x;
  xmin_local[1] = map.xmin[1] - map.center.y;
  xmin_local[2] = map.xmin[2] - map.center.z;

  for (const auto& p : particles) {    
    float hsml = p.hsml;
    float hsml2 = hsml * hsml;

    glm::vec3 diff = glm::vec3(p.pos[0], p.pos[1], p.pos[2]) - map.center;
    float cx = glm::dot(diff, map.uAxis);
    float cy = glm::dot(diff, map.vAxis);
      
    // Candidate y range as row indices.
    int j_min = std::max(0, static_cast<int>(std::floor((cy - hsml - xmin_local[1]) / map.dy)));
    int j_max = std::min(map.npixel_y - 1, static_cast<int>(std::ceil((cy + hsml - xmin_local[1]) / map.dy)) - 1);
      
    for (int j = j_min; j <= j_max; j++) {
      float cell_y = xmin_local[1] + (j + 0.5f) * map.dy;
      float dy_val = cell_y - cy;
      float dy_val2 = dy_val * dy_val;

      if(hsml2 < dy_val2)
	continue;	
      
      float horiz = std::sqrt(hsml2 - dy_val2);
      
      float x_lower = cx - horiz;
      float x_upper = cx + horiz;
        
      int i_min = std::max(0, static_cast<int>(std::floor((x_lower - xmin_local[0]) / map.dx)));
      int i_max = std::min(map.npixel_x - 1, static_cast<int>(std::ceil((x_upper - xmin_local[0]) / map.dx)) - 1);
        
      for (int i = i_min; i <= i_max; i++) {
	float cell_x = xmin_local[0] + (i + 0.5f) * map.dx;
	float dx_val = cell_x - cx;
	float dx_val2 = dx_val * dx_val;
	
	float dist = std::sqrt(dx_val2 + dy_val2);

	if (dist <= hsml) {
	  float u = dist / hsml;
	  float weight = kernel(u);	  	  
	  float w_j = p.mass / hsml / hsml2 / p.density;
	  weight *= w_j;

	  if(map.flagDensityWeight == true)
	    weight *= p.density;	    
	  
	  float property = p.val;
	  map.values[j * map.npixel_x + i] += property * weight;
	  map.weights[j * map.npixel_x + i] += weight;
	}
      }
    }
  }

  for (size_t i = 0; i < map.values.size(); i++){
    if(map.weights[i])
      map.values[i] /= map.weights[i];       
  }
}


void ProjectionMapGenerator::createVoronoiSliceMap(
  ProjectionMap& map,
  const std::vector<pos_val>& particles,
  const ProjectionMapParams& params,
  const ProjectionMapContext& ctx)
{  
  std::size_t cacheKey = 1099511628211ull;
  HashCombine(cacheKey, static_cast<std::size_t>(map.npixel_x));
  HashCombine(cacheKey, static_cast<std::size_t>(map.npixel_y));
  HashCombine(cacheKey, static_cast<std::size_t>(map.npixel_z));
  HashFloat(cacheKey, map.dx);
  HashFloat(cacheKey, map.dy);
  HashFloat(cacheKey, map.dz);
  HashFloat(cacheKey, map.xmin[0] - map.center.x);
  HashFloat(cacheKey, map.xmin[1] - map.center.y);
  HashFloat(cacheKey, map.xmin[2] - map.center.z);
  for (int k = 0; k < 3; ++k) {
    HashFloat(cacheKey, map.uAxis[k]);
    HashFloat(cacheKey, map.vAxis[k]);
    HashFloat(cacheKey, map.wAxis[k]);
  }
  HashCombine(cacheKey, particles.size());
  for (const pos_val& p : particles) {
    glm::vec3 diff = glm::vec3(p.pos[0], p.pos[1], p.pos[2]) - map.center;
    HashFloat(cacheKey, glm::dot(diff, map.uAxis));
    HashFloat(cacheKey, glm::dot(diff, map.vAxis));
    HashFloat(cacheKey, glm::dot(diff, map.wAxis));
  }

  if (!cpuVoronoiCache_.valid || cpuVoronoiCache_.key != cacheKey) {
    cpuVoronoiCache_.grid = buildCpuVoronoiLabelGrid(map, particles);
    cpuVoronoiCache_.key = cacheKey;
    cpuVoronoiCache_.valid = !cpuVoronoiCache_.grid.labels.empty();
  } else {
    std::cout << "CPU Voronoi label cache hit: grid="
              << cpuVoronoiCache_.grid.width << "x"
              << cpuVoronoiCache_.grid.height << "x"
              << cpuVoronoiCache_.grid.depth << std::endl;
  }

  if (cpuVoronoiCache_.valid &&
      params.voronoiMode == ProjectionVoronoiMode::OpacityRendering) {
    renderCpuVoronoiLabelGrid(map, particles, cpuVoronoiCache_.grid, params, ctx);
  } else if (cpuVoronoiCache_.valid) {
    integrateCpuVoronoiLabelGrid(map, particles, cpuVoronoiCache_.grid);
  }
}

ProjectionMapGenerator::VoronoiLabelGrid
ProjectionMapGenerator::buildCpuVoronoiLabelGrid(
  const ProjectionMap& map,
  const std::vector<pos_val>& particles)
{
  std::vector<pos_val> filtered;
  for (size_t i=0;i<particles.size();i++)
    {
      const pos_val& p = particles[i];
      pos_val sp;
      
      glm::vec3 diff = glm::vec3(p.pos[0], p.pos[1], p.pos[2]) - map.center;
      sp.pos[0] = glm::dot(diff, map.uAxis);
      sp.pos[1] = glm::dot(diff, map.vAxis);
      sp.pos[2] = glm::dot(diff, map.wAxis);      

      sp.val = p.val;
      sp.colorVal = p.colorVal;
      sp.density = p.density;
      sp.hsml = p.hsml;
      sp.mass = p.mass;
      
      filtered.push_back(sp);      
    }

  VoronoiLabelGrid grid;
  grid.width = map.npixel_x;
  grid.height = map.npixel_y;
  grid.depth = map.npixel_z;
  grid.labels.assign(static_cast<size_t>(grid.width) *
                       static_cast<size_t>(grid.height) *
                       static_cast<size_t>(grid.depth),
                     -1);

  float xmin_local[3];
  xmin_local[0] = map.xmin[0] - map.center.x;
  xmin_local[1] = map.xmin[1] - map.center.y;
  xmin_local[2] = map.xmin[2] - map.center.z;

  // Copy into the data container. References or pointers could be used later if needed.
  VoronoiParticleCloud cloud;
  cloud.particles = filtered;
  
  // Build the kd-tree.
  KDTreeVoronoi kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10 /* max leaf */));
  kdTree.buildIndex();

  nanoflann::SearchParameters params;  

#ifdef _OPENMP
  int numProcs = omp_get_num_procs();
  // Use that number as the OpenMP thread count.
  omp_set_num_threads(numProcs);
  std::cout << "Using " << numProcs << " threads." << std::endl;
#endif

#pragma omp parallel for collapse(2)
  for (int j = 0; j < map.npixel_y; j++){
    for (int i = 0; i < map.npixel_x; i++){
      float yj = xmin_local[1] + (j + 0.5) * map.dy;
      float xi = xmin_local[0] + (i + 0.5) * map.dx;


      for (int k = 0; k < map.npixel_z; k++){      
	float zk = xmin_local[2] + k * map.dz;
	  
	float out_dist;
	KDTreeVoronoi::IndexType ret_index;
	
	float query_pt[3] = {xi, yj, zk};
	size_t num_results = kdTree.knnSearch(query_pt, 1, &ret_index, &out_dist);
	
	if(num_results == 0){
	  printf("no neighbours found. why this happen?\n");
	  continue;
	}
        const size_t idx =
          static_cast<size_t>(k) * static_cast<size_t>(map.npixel_x) *
            static_cast<size_t>(map.npixel_y) +
          static_cast<size_t>(j) * static_cast<size_t>(map.npixel_x) +
          static_cast<size_t>(i);
        grid.labels[idx] = static_cast<int>(ret_index);
      }
    }
  }

  std::cout << "CPU Voronoi label grid built: grid="
            << grid.width << "x" << grid.height << "x" << grid.depth
            << " particles=" << filtered.size() << std::endl;
  return grid;
}

void ProjectionMapGenerator::integrateCpuVoronoiLabelGrid(
  ProjectionMap& map,
  const std::vector<pos_val>& particles,
  const VoronoiLabelGrid& grid)
{
  std::vector<pos_val> filtered;
  filtered.reserve(particles.size());
  for (const pos_val& p : particles) {
    pos_val sp;
    glm::vec3 diff = glm::vec3(p.pos[0], p.pos[1], p.pos[2]) - map.center;
    sp.pos[0] = glm::dot(diff, map.uAxis);
    sp.pos[1] = glm::dot(diff, map.vAxis);
    sp.pos[2] = glm::dot(diff, map.wAxis);
    sp.val = p.val;
    sp.density = p.density;
    sp.hsml = p.hsml;
    sp.mass = p.mass;
    filtered.push_back(sp);
  }

#pragma omp parallel for collapse(2)
  for (int j = 0; j < map.npixel_y; j++) {
    for (int i = 0; i < map.npixel_x; i++) {
      double value = 0.0;
      double weightSum = 0.0;
      for (int k = 0; k < map.npixel_z; k++) {
        const size_t labelIdx =
          static_cast<size_t>(k) * static_cast<size_t>(map.npixel_x) *
            static_cast<size_t>(map.npixel_y) +
          static_cast<size_t>(j) * static_cast<size_t>(map.npixel_x) +
          static_cast<size_t>(i);
        const int label = grid.labels[labelIdx];
        if (label < 0 ||
            static_cast<size_t>(label) >= filtered.size()) {
          continue;
        }
        const pos_val& p = filtered[static_cast<size_t>(label)];
        double weight = 1.0;
        if (map.flagDensityWeight == true)
          weight *= p.density;
        if (!(weight > 0.0) || !std::isfinite(weight) ||
            !std::isfinite(static_cast<double>(p.val))) {
          continue;
        }
        value += p.val * weight;
        weightSum += weight;
      }
      const size_t outIdx =
        static_cast<size_t>(j) * static_cast<size_t>(map.npixel_x) +
        static_cast<size_t>(i);
      map.values[outIdx] += value;
      map.weights[outIdx] += weightSum;
    }
  }

  for (size_t i = 0; i < map.values.size(); i++){
    if(map.weights[i])
      map.values[i] /= map.weights[i];       
  }
}

void ProjectionMapGenerator::renderCpuVoronoiLabelGrid(
  ProjectionMap& map,
  const std::vector<pos_val>& particles,
  const VoronoiLabelGrid& grid,
  const ProjectionMapParams& params,
  const ProjectionMapContext& ctx)
{
  std::vector<pos_val> filtered;
  filtered.reserve(particles.size());
  for (const pos_val& p : particles) {
    pos_val sp;
    glm::vec3 diff = glm::vec3(p.pos[0], p.pos[1], p.pos[2]) - map.center;
    sp.pos[0] = glm::dot(diff, map.uAxis);
    sp.pos[1] = glm::dot(diff, map.vAxis);
    sp.pos[2] = glm::dot(diff, map.wAxis);
    sp.val = p.val;
    sp.colorVal = p.colorVal;
    sp.density = p.density;
    sp.hsml = p.hsml;
    sp.mass = p.mass;
    filtered.push_back(sp);
  }

  map.image.assign(static_cast<size_t>(map.npixel_x) *
                     static_cast<size_t>(map.npixel_y) * 3,
                   0);
  std::fill(map.values.begin(), map.values.end(), 0.0);
  std::fill(map.weights.begin(), map.weights.end(), 0.0);

  const double dz = std::max<double>(std::abs(map.dz), 1.0e-30);
  double colorMin = std::numeric_limits<double>::max();
  double colorMax = -std::numeric_limits<double>::max();
  for (const pos_val& p : filtered) {
    double value = p.colorVal;
    if (!std::isfinite(value)) continue;
    if (params.flagLogScale) {
      if (value <= 0.0) continue;
      value = std::log10(value);
    }
    colorMin = std::min(colorMin, value);
    colorMax = std::max(colorMax, value);
  }
  if (!params.autoRange) {
    colorMin = params.range_min;
    colorMax = params.range_max;
  }
  if (!(colorMax > colorMin)) {
    colorMax = colorMin + 1.0e-6;
  }
  map.colorMinVal = static_cast<float>(colorMin);
  map.colorMaxVal = static_cast<float>(colorMax);

#pragma omp parallel for collapse(2)
  for (int j = 0; j < map.npixel_y; j++) {
    for (int i = 0; i < map.npixel_x; i++) {
      double accumR = 0.0;
      double accumG = 0.0;
      double accumB = 0.0;
      double accumA = 0.0;

      for (int k = 0; k < map.npixel_z && accumA < 0.999; k++) {
        const size_t labelIdx =
          static_cast<size_t>(k) * static_cast<size_t>(map.npixel_x) *
            static_cast<size_t>(map.npixel_y) +
          static_cast<size_t>(j) * static_cast<size_t>(map.npixel_x) +
          static_cast<size_t>(i);
        const int label = grid.labels[labelIdx];
        if (label < 0 ||
            static_cast<size_t>(label) >= filtered.size()) {
          continue;
        }

        const pos_val& p = filtered[static_cast<size_t>(label)];
        const double sigma = EvaluateProjectionTf(params,
                                                  static_cast<double>(p.val));
        if (!(sigma > 0.0) || !std::isfinite(sigma)) {
          continue;
        }

        const double alpha =
          std::clamp(1.0 - std::exp(-sigma * dz), 0.0, 1.0);
        double colorValue = p.colorVal;
        if (params.flagLogScale && colorValue > 0.0) {
          colorValue = std::log10(colorValue);
        }
        const float colorT = static_cast<float>(
          std::clamp((colorValue - colorMin) / (colorMax - colorMin),
                     0.0,
                     1.0));
        float colorR = 0.0f;
        float colorG = 0.0f;
        float colorB = 0.0f;
        colormapLookup(colorT,
                       colorR,
                       colorG,
                       colorB,
                       ctx.colorMap,
                       ctx.colorMapSize);
        const double trans = 1.0 - accumA;
        accumR += trans * alpha * colorR;
        accumG += trans * alpha * colorG;
        accumB += trans * alpha * colorB;
        accumA += trans * alpha;
      }

      const size_t outIdx =
        static_cast<size_t>(j) * static_cast<size_t>(map.npixel_x) +
        static_cast<size_t>(i);
      map.values[outIdx] = accumA;
      map.weights[outIdx] = accumA;
      const size_t rgbIdx = outIdx * 3;
      map.image[rgbIdx + 0] =
        static_cast<unsigned char>(std::clamp(accumR, 0.0, 1.0) * 255.0);
      map.image[rgbIdx + 1] =
        static_cast<unsigned char>(std::clamp(accumG, 0.0, 1.0) * 255.0);
      map.image[rgbIdx + 2] =
        static_cast<unsigned char>(std::clamp(accumB, 0.0, 1.0) * 255.0);
    }
  }
}


static inline double gaussian2d_weight(double r2, double sigma2, double pixel_area)
{
  const double norm = 1.0 / (2.0 * M_PI * sigma2);
  return norm * std::exp(-0.5 * r2 / sigma2) * pixel_area;
}

void ProjectionMapGenerator::createStarMap(ProjectionMap &map,
                                           const std::vector<pos_val>& particles,
                                           float sigma_pix,
                                           bool normalize)
{
  // map.xmin is in world coords; map.center is world center
  float xmin_local[3];
  xmin_local[0] = map.xmin[0] - map.center.x;
  xmin_local[1] = map.xmin[1] - map.center.y;
  xmin_local[2] = map.xmin[2] - map.center.z;

  // Convert sigma from pixels to length units
  const float sigma_len_min = 0.5f * map.dx; // avoid too sharp kernels
  float sigma_len = std::max(sigma_pix * map.dx, sigma_len_min);
  const float sigma2 = sigma_len * sigma_len;

  // cutoff radius
  const float rcut  = 4.0f * sigma_len;
  const float rcut2 = rcut * rcut;

  const double pixel_area = (double)map.dx * (double)map.dy;

  // If user set sigma_pix very small, fall back to nearest-pixel deposit
  const bool point_deposit = (sigma_pix <= 0.0f);

  for (const auto& p : particles) {

    // 2D coordinates in the projection plane
    glm::vec3 diff = glm::vec3(p.pos[0], p.pos[1], p.pos[2]) - map.center;
    float cx = glm::dot(diff, map.uAxis);
    float cy = glm::dot(diff, map.vAxis);

    const float val = p.val;
    printf("val=%g cx=%g cy=%g\n", val, cx, cy);
	
    
    if (!std::isfinite(val)) continue;

    if (point_deposit) {
      // nearest pixel deposit
      int i = (int)std::floor((cx - xmin_local[0]) / map.dx);
      int j = (int)std::floor((cy - xmin_local[1]) / map.dy);
      if (i < 0 || i >= map.npixel_x || j < 0 || j >= map.npixel_y) continue;

      const int idx = j * map.npixel_x + i;
      map.values[idx] += val;
      if (normalize) map.weights[idx] += 1.0f;
      continue;
    }

    // y pixel range within rcut
    int j_min = std::max(0, (int)std::floor((cy - rcut - xmin_local[1]) / map.dy));
    int j_max = std::min(map.npixel_y - 1, (int)std::ceil ((cy + rcut - xmin_local[1]) / map.dy) - 1);

    printf("j_min=%d %d\n", j_min, j_max);
    
    for (int j = j_min; j <= j_max; j++) {
      float cell_y = xmin_local[1] + (j + 0.5f) * map.dy;
      float dy = cell_y - cy;
      float dy2 = dy * dy;
      if (dy2 > rcut2) continue;

      // x range from circle cutoff
      float horiz = std::sqrt(std::max(0.0f, rcut2 - dy2));
      float x_lower = cx - horiz;
      float x_upper = cx + horiz;

      int i_min = std::max(0, (int)std::floor((x_lower - xmin_local[0]) / map.dx));
      int i_max = std::min(map.npixel_x - 1, (int)std::ceil ((x_upper - xmin_local[0]) / map.dx) - 1);

      for (int i = i_min; i <= i_max; i++) {
        float cell_x = xmin_local[0] + (i + 0.5f) * map.dx;
        float dx = cell_x - cx;
        float r2 = dx*dx + dy2;
        if (r2 > rcut2) continue;

        const double w = gaussian2d_weight((double)r2, (double)sigma2, pixel_area);
        const int idx = j * map.npixel_x + i;

        map.values[idx] += (float)(val * w);
        if (normalize) map.weights[idx] += (float)w;
      }
    }
  }

  if (normalize) {
    for (size_t i = 0; i < map.values.size(); i++){
      if (map.weights[i] > 0.0f) map.values[i] /= map.weights[i];
    }
  }
}

#ifdef USE_LUA
void ProjectionMapGenerator::ensureLuaInitialized(){
  if (flag_init_lua_ == false) {
    gLua_ = luaL_newstate();
    luaL_openlibs(gLua_);
    flag_init_lua_ = true;
  }
}

  // ---------------------------
  // Evaluate a Lua expression and return a numeric value.
bool ProjectionMapGenerator::EvaluateLuaExpressionNumber(const char* expr, double& outValue) {
  lua_settop(gLua_, 0);
  if (luaL_dostring(gLua_, expr) == LUA_OK) {
    if(lua_isnumber(gLua_, -1)) {
      outValue = lua_tonumber(gLua_, -1);
      lua_pop(gLua_, 1);
      return true;
    }
  } else {
    const char* err = lua_tostring(gLua_, -1);
    std::cerr << "Lua error: " << err << std::endl;
    lua_pop(gLua_, 1);
  }
  return false;
}


  // ---------------------------
  // Evaluate a Lua expression and read a color table.
bool ProjectionMapGenerator::EvaluateLuaExpressionColor(const char* expr, float& r, float& g, float& b, float& a) {
  lua_settop(gLua_, 0);
  if (luaL_dostring(gLua_, expr) == LUA_OK) {
    if(lua_istable(gLua_, -1)) {
      lua_getfield(gLua_, -1, "r");
      r = static_cast<float>(lua_tonumber(gLua_, -1));
      lua_pop(gLua_, 1);
      lua_getfield(gLua_, -1, "g");
      g = static_cast<float>(lua_tonumber(gLua_, -1));
      lua_pop(gLua_, 1);
      lua_getfield(gLua_, -1, "b");
      b = static_cast<float>(lua_tonumber(gLua_, -1));
      lua_pop(gLua_, 1);
      lua_getfield(gLua_, -1, "a");
      a = static_cast<float>(lua_tonumber(gLua_, -1));
      lua_pop(gLua_, 1);
      lua_pop(gLua_, 1); // Pop the table.
      return true;
    }
    lua_pop(gLua_, 1);
  } else {
    const char* err = lua_tostring(gLua_, -1);
    std::cerr << "Lua error: " << err << std::endl;
    lua_pop(gLua_, 1);
  }
  return false;
}


  // ---------------------------
  // Evaluate a Lua expression and return a boolean value for filtering.
bool ProjectionMapGenerator::EvaluateLuaExpressionBool(const char* expr, bool& outValue) {
  lua_settop(gLua_, 0);
  if (luaL_dostring(gLua_, expr) == LUA_OK) {
    if(lua_isboolean(gLua_, -1)) {
      outValue = lua_toboolean(gLua_, -1);
      lua_pop(gLua_, 1);
      return true;
    }
    lua_pop(gLua_, 1);
  } else {
    const char* err = lua_tostring(gLua_, -1);
    std::cerr << "Lua error: " << err << std::endl;
    lua_pop(gLua_, 1);
  }
  return false;
}
#endif

void ProjectionMapGenerator::overlayVectorField(ImageCanvas& canvas,
                                                const ProjectionMap& map,
                                                const ProjectionMapParams& params,
                                                const SimulationBlock& block)
{
  const ProjectionPanelSpec& panel = params.panels[0];
  if (panel.vectorOverlayIndex <= 0 ||
      panel.vectorOverlayIndex > params.vectorOverlayCount ||
      panel.vectorOverlayIndex > kProjectionMaxVectorOverlays) {
    return;
  }
  const ProjectionVectorOverlaySpec& overlay =
    params.vectorOverlays[static_cast<size_t>(panel.vectorOverlayIndex - 1)];

  if (overlay.vectorField == ProjectionVectorField::MagneticField &&
      !block.hasSoAAs(soa_views::Bfield)) {
    return;
  }

  const int gridSize = std::clamp(overlay.vectorGridSize, 4, 256);
  const int gridX = gridSize;
  const int gridY = gridSize;
  const int ncell = gridX * gridY;

  std::vector<double> sumU(static_cast<size_t>(ncell), 0.0);
  std::vector<double> sumV(static_cast<size_t>(ncell), 0.0);
  std::vector<double> weightSum(static_cast<size_t>(ncell), 0.0);

  for (size_t ipart = 0; ipart < block.particles.size(); ++ipart) {
    const SimulationElement& p = block.particles[ipart];
    if (p.type != static_cast<uint8_t>(params.selectedType)) continue;

    const glm::vec3 pos = renderPosition(p, map.sourceWorldToRenderScale);
    const glm::vec3 rad = pos - map.center;
    const float u = glm::dot(rad, map.uAxis);
    const float v = glm::dot(rad, map.vAxis);
    const float w = glm::dot(rad, map.wAxis);
    if (std::abs(u) > 0.5f * map.xlen[0] ||
        std::abs(v) > 0.5f * map.xlen[1] ||
        std::abs(w) > 0.5f * map.xlen[2]) {
      continue;
    }

    float vec3[3] = {0.0f, 0.0f, 0.0f};
    if (overlay.vectorField == ProjectionVectorField::Velocity) {
      vec3[0] = p.vel[0];
      vec3[1] = p.vel[1];
      vec3[2] = p.vel[2];
    } else if (!block.readSoAAs(soa_views::Bfield, ipart, vec3)) {
      continue;
    }

    const glm::vec3 vec(vec3[0], vec3[1], vec3[2]);
    const float vu = glm::dot(vec, map.uAxis);
    const float vv = glm::dot(vec, map.vAxis);
    if (!std::isfinite(vu) || !std::isfinite(vv)) continue;

    const float halfX = std::max(0.5f * map.xlen[0], 1.0e-30f);
    const float halfY = std::max(0.5f * map.xlen[1], 1.0e-30f);
    const int ix = static_cast<int>(((u / halfX) + 1.0f) * 0.5f * gridX);
    const int iy = static_cast<int>(((v / halfY) + 1.0f) * 0.5f * gridY);
    if (ix < 0 || ix >= gridX || iy < 0 || iy >= gridY) continue;

    const int idx = iy * gridX + ix;
    const double weight =
      (overlay.vectorField == ProjectionVectorField::Velocity &&
       std::isfinite(p.mass) && p.mass > 0.0f)
        ? static_cast<double>(p.mass)
        : 1.0;
    sumU[static_cast<size_t>(idx)] += weight * static_cast<double>(vu);
    sumV[static_cast<size_t>(idx)] += weight * static_cast<double>(vv);
    weightSum[static_cast<size_t>(idx)] += weight;
  }

  std::vector<float> avgU(static_cast<size_t>(ncell), 0.0f);
  std::vector<float> avgV(static_cast<size_t>(ncell), 0.0f);
  float autoMinMagnitude = std::numeric_limits<float>::max();
  float autoMaxMagnitude = 0.0f;
  bool hasMagnitude = false;
  for (int i = 0; i < ncell; ++i) {
    const double weight = weightSum[static_cast<size_t>(i)];
    if (weight <= 0.0) continue;
    const float u = static_cast<float>(sumU[static_cast<size_t>(i)] / weight);
    const float v = static_cast<float>(sumV[static_cast<size_t>(i)] / weight);
    const float mag = std::sqrt(u * u + v * v);
    if (mag <= 0.0f || !std::isfinite(mag)) {
      continue;
    }
    avgU[static_cast<size_t>(i)] = u;
    avgV[static_cast<size_t>(i)] = v;
    autoMinMagnitude = std::min(autoMinMagnitude, mag);
    autoMaxMagnitude = std::max(autoMaxMagnitude, mag);
    hasMagnitude = true;
  }
  if (!hasMagnitude) return;
  const float rangeMin =
    overlay.autoMagnitudeRange
      ? autoMinMagnitude
      : overlay.vectorMinMagnitude;
  float rangeMax =
    overlay.autoMagnitudeRange
      ? autoMaxMagnitude
      : overlay.vectorMaxMagnitude;
  if (rangeMax <= 0.0f) return;
  if (rangeMax <= rangeMin) {
    rangeMax = rangeMin + std::max(std::abs(rangeMin) * 1.0e-6f, 1.0e-30f);
  }

  const float alpha = std::clamp(overlay.vectorOpacity, 0.0f, 1.0f);
  const float minLength = std::max(overlay.vectorMinLengthPx, 0.0f);
  const float maxLength = std::max(overlay.vectorMaxLengthPx, minLength);
  const ColormapDef* vectorColormaps = AvailableColormaps();
  const int vectorColormapCount = AvailableColormapCount();
  const int vectorColormapIndex =
    std::clamp(overlay.vectorColormapIndex, 0, vectorColormapCount - 1);

  auto colorForMagnitude =
    [&](float mag, unsigned char& r, unsigned char& g, unsigned char& b) {
      if (overlay.vectorColorByMagnitude) {
        float t = 0.0f;
        if (overlay.vectorColorScaleMode ==
            ProjectionVectorColorScaleMode::Log) {
          const float safeMin = std::max(rangeMin, 0.0f);
          const float safeMag = std::max(mag, safeMin);
          t = std::log1p(9.0f * (safeMag - safeMin) /
                         std::max(rangeMax - safeMin, 1.0e-30f)) /
              std::log(10.0f);
        } else {
          t = (mag - rangeMin) / (rangeMax - rangeMin);
        }
        t = std::clamp(t, 0.0f, 1.0f);
        float rf = 1.0f, gf = 1.0f, bf = 1.0f;
        colormapLookup(t,
                       rf,
                       gf,
                       bf,
                       vectorColormaps[vectorColormapIndex].data,
                       vectorColormaps[vectorColormapIndex].count);
        r = static_cast<unsigned char>(std::clamp(rf, 0.0f, 1.0f) * 255.0f);
        g = static_cast<unsigned char>(std::clamp(gf, 0.0f, 1.0f) * 255.0f);
        b = static_cast<unsigned char>(std::clamp(bf, 0.0f, 1.0f) * 255.0f);
      } else {
        r = static_cast<unsigned char>(
          std::clamp(overlay.vectorColor[0], 0.0f, 1.0f) * 255.0f);
        g = static_cast<unsigned char>(
          std::clamp(overlay.vectorColor[1], 0.0f, 1.0f) * 255.0f);
        b = static_cast<unsigned char>(
          std::clamp(overlay.vectorColor[2], 0.0f, 1.0f) * 255.0f);
      }
    };

  auto normalizedMagnitudeFactor = [&](float mag) {
    if (overlay.vectorScaleMode == ProjectionVectorScaleMode::Linear) {
      return (mag - rangeMin) / (rangeMax - rangeMin);
    }
    if (overlay.vectorScaleMode == ProjectionVectorScaleMode::Log) {
      const float safeMin = std::max(rangeMin, 0.0f);
      const float safeMag = std::max(mag, safeMin);
      return std::log1p(9.0f * (safeMag - safeMin) /
                        std::max(rangeMax - safeMin, 1.0e-30f)) /
             std::log(10.0f);
    }
    return 1.0f;
  };

  if (overlay.vectorMode == ProjectionVectorOverlayMode::Arrows) {
    for (int iy = 0; iy < gridY; ++iy) {
      for (int ix = 0; ix < gridX; ++ix) {
        const int idx = iy * gridX + ix;
        const float u = avgU[static_cast<size_t>(idx)];
        const float v = avgV[static_cast<size_t>(idx)];
        const float mag = std::sqrt(u * u + v * v);
        if (mag <= 0.0f) continue;

        const float t = std::clamp(normalizedMagnitudeFactor(mag), 0.0f, 1.0f);
        const float length =
          (overlay.vectorScaleMode == ProjectionVectorScaleMode::Normalized)
            ? maxLength
            : minLength + (maxLength - minLength) * t;
        if (length <= 0.5f) continue;

        unsigned char r = 255, g = 255, b = 255;
        colorForMagnitude(mag, r, g, b);
        const float dirX = u / mag;
        const float dirY = v / mag;
        const float cx = (static_cast<float>(ix) + 0.5f) *
                         static_cast<float>(map.npixel_x) / gridX;
        const float cy = (static_cast<float>(iy) + 0.5f) *
                         static_cast<float>(map.npixel_y) / gridY;
        const float x0 = cx - 0.5f * length * dirX;
        const float y0 = cy - 0.5f * length * dirY;
        const float x1 = cx + 0.5f * length * dirX;
        const float y1 = cy + 0.5f * length * dirY;

        canvas.drawLine(static_cast<int>(std::round(x0)),
                        static_cast<int>(std::round(y0)),
                        static_cast<int>(std::round(x1)),
                        static_cast<int>(std::round(y1)),
                        r,
                        g,
                        b,
                        alpha);

        const float headLen = std::max(3.0f, 0.28f * length);
        const float headWidth = 0.55f * headLen;
        const float px = -dirY;
        const float py = dirX;
        const float bx = x1 - headLen * dirX;
        const float by = y1 - headLen * dirY;
        canvas.drawLine(static_cast<int>(std::round(x1)),
                        static_cast<int>(std::round(y1)),
                        static_cast<int>(std::round(bx + headWidth * px)),
                        static_cast<int>(std::round(by + headWidth * py)),
                        r,
                        g,
                        b,
                        alpha);
        canvas.drawLine(static_cast<int>(std::round(x1)),
                        static_cast<int>(std::round(y1)),
                        static_cast<int>(std::round(bx - headWidth * px)),
                        static_cast<int>(std::round(by - headWidth * py)),
                        r,
                        g,
                        b,
                        alpha);
      }
    }
    return;
  }

  auto sampleField = [&](float x, float y, float& outU, float& outV, float& outMag) {
    if (x < 0.0f || x >= static_cast<float>(map.npixel_x) ||
        y < 0.0f || y >= static_cast<float>(map.npixel_y)) {
      return false;
    }
    const float gx = x / std::max(1.0f, static_cast<float>(map.npixel_x)) *
                     gridX - 0.5f;
    const float gy = y / std::max(1.0f, static_cast<float>(map.npixel_y)) *
                     gridY - 0.5f;
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const float tx = gx - x0;
    const float ty = gy - y0;
    if (x0 < 0 || y0 < 0 || x0 + 1 >= gridX || y0 + 1 >= gridY) {
      return false;
    }
    auto sample = [&](const std::vector<float>& data, int ix, int iy) {
      return data[static_cast<size_t>(iy * gridX + ix)];
    };
    const float u00 = sample(avgU, x0, y0);
    const float u10 = sample(avgU, x0 + 1, y0);
    const float u01 = sample(avgU, x0, y0 + 1);
    const float u11 = sample(avgU, x0 + 1, y0 + 1);
    const float v00 = sample(avgV, x0, y0);
    const float v10 = sample(avgV, x0 + 1, y0);
    const float v01 = sample(avgV, x0, y0 + 1);
    const float v11 = sample(avgV, x0 + 1, y0 + 1);
    outU = (1.0f - ty) * ((1.0f - tx) * u00 + tx * u10) +
           ty * ((1.0f - tx) * u01 + tx * u11);
    outV = (1.0f - ty) * ((1.0f - tx) * v00 + tx * v10) +
           ty * ((1.0f - tx) * v01 + tx * v11);
    outMag = std::sqrt(outU * outU + outV * outV);
    return std::isfinite(outMag) && outMag > 0.0f;
  };

  const int maskSize = std::clamp(overlay.streamlineMaskSize, 8, 512);
  const int maskX = maskSize;
  const int maskY = maskSize;
  std::vector<unsigned char> occupied(static_cast<size_t>(maskX * maskY), 0);
  auto maskIndex = [&](float x, float y, int& outIdx) {
    const int mx = static_cast<int>(x / std::max(1.0f, static_cast<float>(map.npixel_x)) * maskX);
    const int my = static_cast<int>(y / std::max(1.0f, static_cast<float>(map.npixel_y)) * maskY);
    if (mx < 0 || mx >= maskX || my < 0 || my >= maskY) return false;
    outIdx = my * maskX + mx;
    return true;
  };

  const float stepPx = std::max(overlay.streamlineStepPx, 0.1f);
  const int maxSteps = std::max(overlay.streamlineMaxSteps, 1);
  for (int sy = 0; sy < maskY; ++sy) {
    for (int sx = 0; sx < maskX; ++sx) {
      const int startMask = sy * maskX + sx;
      if (occupied[static_cast<size_t>(startMask)] != 0) continue;
      const float seedX = (static_cast<float>(sx) + 0.5f) *
                          static_cast<float>(map.npixel_x) / maskX;
      const float seedY = (static_cast<float>(sy) + 0.5f) *
                          static_cast<float>(map.npixel_y) / maskY;
      float seedU = 0.0f, seedV = 0.0f, seedMag = 0.0f;
      if (!sampleField(seedX, seedY, seedU, seedV, seedMag)) continue;

      occupied[static_cast<size_t>(startMask)] = 1;
      for (int directionSign : {-1, 1}) {
        float x = seedX;
        float y = seedY;
        for (int step = 0; step < maxSteps; ++step) {
          float fu = 0.0f, fv = 0.0f, mag = 0.0f;
          if (!sampleField(x, y, fu, fv, mag)) break;
          const float invMag = 1.0f / std::max(mag, 1.0e-30f);
          const float nx = directionSign * fu * invMag;
          const float ny = directionSign * fv * invMag;
          const float nextX = x + stepPx * nx;
          const float nextY = y + stepPx * ny;
          int nextMask = 0;
          if (!maskIndex(nextX, nextY, nextMask)) break;
          if (occupied[static_cast<size_t>(nextMask)] != 0 &&
              nextMask != startMask) {
            break;
          }
          unsigned char r = 255, g = 255, b = 255;
          colorForMagnitude(mag, r, g, b);
          canvas.drawLine(static_cast<int>(std::round(x)),
                          static_cast<int>(std::round(y)),
                          static_cast<int>(std::round(nextX)),
                          static_cast<int>(std::round(nextY)),
                          r,
                          g,
                          b,
                          alpha);
          occupied[static_cast<size_t>(nextMask)] = 1;
          x = nextX;
          y = nextY;
        }
      }
    }
  }
}

void ProjectionMapGenerator::overlayStarParticles(ImageCanvas& canvas,
							  const ProjectionMap& map,
						  const ProjectionMapParams& params,
						  const ProjectionMapContext& ctx,
						  const std::vector<SimulationElement>& particles)
{

#ifdef USE_LUA
  if(flag_init_lua_){
    double minVal = 0.0, maxVal = 1.0;
    if(!EvaluateLuaExpressionNumber(params.minValueExpr, minVal)) {
      std::cerr << "Error evaluating min value expression\n";
    }
    if(!EvaluateLuaExpressionNumber(params.maxValueExpr, maxVal)) {
      std::cerr << "Error evaluating max value expression\n";
    }
    lua_pushnumber(gLua_, minVal);
    lua_setglobal(gLua_, "min");
    lua_pushnumber(gLua_, maxVal);
    lua_setglobal(gLua_, "max");
  }
#endif

  const ProjectionPanelSpec& panel = params.panels[0];
  if (panel.starOverlayIndex <= 0 ||
      panel.starOverlayIndex > params.starOverlayCount ||
      panel.starOverlayIndex > kProjectionMaxStarOverlays) {
    return;
  }
  const ProjectionStarOverlaySpec& overlay =
    params.starOverlays[static_cast<size_t>(panel.starOverlayIndex - 1)];

  float autoMaxMass = overlay.maxMass;
  if (overlay.autoMassRange) {
    autoMaxMass = overlay.minMass;
    for (const auto& p : particles) {
      if (p.type < 3 || p.type > 5) continue;
      if (params.dataSource == DataSource::Stars && p.type == ctx.selectedType) {
        continue;
      }
      if (p.mass >= overlay.minMass && std::isfinite(p.mass)) {
        autoMaxMass = std::max(autoMaxMass, p.mass);
      }
    }
  }

  for (const auto &p : particles) {
    if(p.type < 3 || p.type > 5)
      continue;

    if (params.dataSource == DataSource::Stars)
      if(p.type == ctx.selectedType)
	continue;

    if (p.mass < overlay.minMass) {
      continue;
    }

    const float massScaleMax =
      overlay.autoMassRange
        ? autoMaxMass
        : std::max(overlay.maxMass, overlay.minMass);
    const float massNorm =
      (massScaleMax > overlay.minMass)
        ? std::clamp((p.mass - overlay.minMass) /
                       (massScaleMax - overlay.minMass),
                     0.0f,
                     1.0f)
        : 1.0f;

    float pointSize = std::max(overlay.maxSizePx, 0.0f);
    if (overlay.sizeByMass) {
      pointSize = overlay.minSizePx +
                  (overlay.maxSizePx - overlay.minSizePx) * std::sqrt(massNorm);
    }

    float r = std::clamp(overlay.color[0], 0.0f, 1.0f);
    float g = std::clamp(overlay.color[1], 0.0f, 1.0f);
    float b = std::clamp(overlay.color[2], 0.0f, 1.0f);
    if (overlay.colorByMass) {
      r = r * (1.0f - massNorm) + massNorm;
      g = g * (1.0f - massNorm) + 0.85f * massNorm;
      b = b * (1.0f - massNorm);
    }
    const float a = 1.0f;

    unsigned char ur = static_cast<unsigned char>(r * 255);
    unsigned char ug = static_cast<unsigned char>(g * 255);
    unsigned char ub = static_cast<unsigned char>(b * 255);
      
    // Project the 3D position to image coordinates using the same method as createProjectionMap().
    glm::vec3 rad =
      renderPosition(p, map.sourceWorldToRenderScale) - map.center;
    float u = glm::dot(rad, map.uAxis);  // Component along the image X axis.
    float v = glm::dot(rad, map.vAxis);  // Component along the image Y axis.
    int px = static_cast<int>((u / (map.xlen[0] * 0.5f) + 1.0f) * 0.5f * map.npixel_x);
    int py = static_cast<int>((v / (map.xlen[1] * 0.5f) + 1.0f) * 0.5f * map.npixel_y);
      
    float desiredStarSize = pointSize;
    int radius = std::max(1, static_cast<int>(desiredStarSize * 0.5f));
    
    canvas.drawAsterisk(px, py, radius, ur, ug, ub, a);                   
  }
}

  // --------------------------------------------------------
  // colormapLookup:
  //   Interpolate RGB from the colormap for t in [0, 1].
  // --------------------------------------------------------
void ProjectionMapGenerator::colormapLookup(float t, float& r, float& g, float& b, const float *colorMap, int countColorMap)
{
  t = std::max(0.f, std::min(t, 1.f));
  float pos = t * (countColorMap - 1);
  int idx = (int)pos;
  float frac = pos - idx;
  if (idx >= countColorMap - 1) {
    idx = countColorMap - 2;
    frac = 1.0f;
  }
  int base = idx * 3;
  int base2 = (idx + 1) * 3;
  float r1 = colorMap[base + 0];
  float g1 = colorMap[base + 1];
  float b1 = colorMap[base + 2];
  float r2 = colorMap[base2 + 0];
  float g2 = colorMap[base2 + 1];
  float b2 = colorMap[base2 + 2];
  r = r1 + (r2 - r1) * frac;
  g = g1 + (g2 - g1) * frac;
  b = b1 + (b2 - b1) * frac;
}

std::vector<double> generate_ticks(double min, double max, int n_desired) {
  double range = max - min;
  double roughStep = range / n_desired;
  double exponent = floor(log10(roughStep));
  double fraction = roughStep / pow(10, exponent);
  double niceStep;
    
  if (fraction < 1.5)
    niceStep = 1;
  else if (fraction < 3)
    niceStep = 2;
  else if (fraction < 7)
    niceStep = 5;
  else
    niceStep = 10;
    
  double step = niceStep * pow(10, exponent);
    
  double tick_min = floor(min / step) * step;
  double tick_max = ceil(max / step) * step;

  std::vector<double> ticks;
    
  printf("Ticks: ");
  for (double tick = tick_min; tick <= tick_max; tick += step) {
    ticks.push_back(tick);
  }

  return ticks;
}

void ProjectionMapGenerator::addColorBarToMap(ImageCanvas& canvas,
					      double cell_size,
					      float minVal,
					      float maxVal,
					      int colorBarWidth,
					      const float *colormap,
					      int countcolormap,
					      const char *barLabel,
					      const ProjectionMapParams& params,
					      const ProjectionMapContext& ctx)
{
  (void)colormap;
  (void)countcolormap;
  // Draw ticks and labels on the colorbar.
  int nTicks = 5;
  std::vector<double> ticks = generate_ticks(minVal, maxVal, nTicks);
  nTicks = ticks.size();

  int outW_init = canvas.width();
  int outH_init = canvas.height();
    
  const int charPixelSize = std::max(1, static_cast<int>(0.08f * outH_init));
  const int charPixelSizeLabel = std::max(1, static_cast<int>(0.10f * outH_init));

  float maxTicksWidth = 0.0f;
  char labelStr[64];
  for (int i = 0; i < nTicks; ++i) {
    std::snprintf(labelStr, sizeof(labelStr), "%.1f", ticks[i]);
    
    const float w =
      fontRenderer_.measureTextAdvance(labelStr,
				       static_cast<float>(charPixelSize));
    
    maxTicksWidth = std::max(maxTicksWidth, w);
  }
  
  // Add padding.
  int padding = 4;
  int ticksWidth = static_cast<int>(ceil(maxTicksWidth)) + 2 * padding;
  int rotateLabelWidth = charPixelSizeLabel + 2 * padding;

  // The output width includes the image, colorbar, tick labels, and rotated label.
  int outW = outW_init + colorBarWidth + ticksWidth + rotateLabelWidth;
  int outH = outH_init;
  canvas.resizeKeepContent(outW, outH, 0);

  // Draw the colorbar on the right side.
  // y=0 is the top and y=outH is the bottom.
  // Map values in [0, 1] to [0, outH - 1].
  for (int py = 0; py < outH; py++) {
    float t = 1.0f - float(py) / float(outH - 1); // Top to bottom maps 1 to 0.
    float rF, gF, bF;
    colormapLookup(t, rF, gF, bF, ctx.colorMap, ctx.colorMapSize);
    unsigned char rC = (unsigned char)(rF * 255);
    unsigned char gC = (unsigned char)(gF * 255);
    unsigned char bC = (unsigned char)(bF * 255);

    for (int px = 0; px < colorBarWidth; px++) {
      int outX = outW_init + px;
      canvas.setPixel(outX, py, rC, gC, bC);
    }
  }

  canvas.fillRect(outW_init + colorBarWidth,
		  0,
		  outW,
		  outH,
		  0, 0, 0);
  
  int labelAreaX = outW_init + colorBarWidth;
  int labelCenterX = labelAreaX + ticksWidth / 2;
  
  for (int i = 0; i < nTicks; i++) {
    if(ticks[i] < minVal || ticks[i] > maxVal)
      continue;

    float frac = (ticks[i] - minVal) / (maxVal - minVal);
    int tickY = int((1.0f - frac) * (outH - 1));

    int xLineStart = outW_init; 
    int xLineEnd   = outW_init + 10;
    canvas.drawHorizontalLine(xLineStart, xLineEnd, tickY, 255, 255, 255);
 
    fontRenderer_.drawValueCenteredBaseline(canvas,
					    labelCenterX,
					    tickY,
					    ticks[i],
					    static_cast<float>(charPixelSize),
					    "%.1f");
  }

  
  {
    int labelAreaX = outW_init + colorBarWidth + ticksWidth;
    int center_x = labelAreaX + rotateLabelWidth / 2;
    int center_y = outH / 2;

    fontRenderer_.drawTextRotated90Centered(canvas,
					    center_x,
					    center_y,
					    barLabel,
					    static_cast<float>(charPixelSizeLabel));
  }

  if(params.flagTimeLabel){
    char timeStr[64];
    double t = ctx.time * params.factorShownTimeInUnitTime;
    if(params.flagUseRedshift)
      t = 1./ctx.time - 1.;
    
    snprintf(timeStr, sizeof(timeStr), params.timeFormatBuf, t);

    const TextBBox bbox =
      fontRenderer_.measureTextBBox(timeStr,
				    static_cast<float>(charPixelSizeLabel));
    
    int textW = bbox.width;
    int textH = bbox.height;
    float min_y = bbox.minY;
    
    // Choose the desired top-left text position.
    int baseX = 10;
    int baseY = 10;

    // Fill the padded text background with translucent black.
    int padding = 4;
    int x0 = baseX - padding;           // Top-left X.
    int y0 = baseY - padding;           // Top-left Y.
    int x1 = baseX + textW + padding;   // Bottom-right X.
    int y1 = baseY + textH + padding;   // Bottom-right Y.

    canvas.blendRect(x0, y0, x1, y1, 0, 0, 0, 0.5f);

    // Draw the text. The renderer treats (pos_x, pos_y) as a centered position,
    // so place the center so the text's top-left corner lands at (baseX, baseY).
    fontRenderer_.drawValueCenteredBaseline(canvas,
					    baseX + textW / 2,
					    static_cast<int>(baseY - min_y),
					    t,
					    static_cast<float>(charPixelSizeLabel),
					    params.timeFormatBuf);
  }


  // draw spatial scale
  if(params.flagPlaceScale)
    {
      double arrowLenX_scaled = params.arrowLenX;
      if(ctx.scaleToPhysical > 0.0)
	arrowLenX_scaled /= ctx.scaleToPhysical;

      int arrowLenX_in_pixel = static_cast<int>(arrowLenX_scaled / cell_size);
	
      int arrowCenterX = outW_init / 2;
      int arrowStartX = arrowCenterX - arrowLenX_in_pixel / 2;
      int arrowEndX = arrowStartX + arrowLenX_in_pixel;
      
      int arrowStartY = outH - 30;

      canvas.drawHorizontalLine(arrowStartX, arrowEndX, arrowStartY, 255, 255, 255);
      fontRenderer_.drawTextCenteredBaseline(canvas,
					     arrowCenterX,
					     arrowStartY - 10,
					     params.arrowLabelStr,
					     static_cast<float>(charPixelSizeLabel));
    }
}

int ProjectionMapGenerator::getFontCount() const
{
  return fontRenderer_.fontCount();
}

const std::string& ProjectionMapGenerator::getFontPath(int index) const
{
  return fontRenderer_.fontPath(index);
}

bool ProjectionMapGenerator::selectFontFileByIndex(int index)
{
  return fontRenderer_.loadFontByIndex(index);
}
