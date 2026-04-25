#include "image/image_canvas.h"
#include "image/image_io.h"

#include "data/particle_array.h"
#include "object.h"

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

#ifdef USE_LUA
#include <lua.hpp>
#endif
#include <nanoflann.hpp>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace{
  // nanoflann用のデータコンテナ
  struct VoronoiParticleCloud {
    TrackingVector<pos_val> particles;
  
    // kd-tree インターフェース
    inline size_t kdtree_get_point_count() const { return particles.size(); }
  
    // 指定インデックスの次元 dim の値を返す
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
      return particles[idx].pos[dim];
    }
  
    // バウンディングボックスは省略（falseを返す）
    template <class BBOX>
    bool kdtree_get_bbox(BBOX & /*bb*/) const { return false; }
  };

  // kd-treeの型定義（3次元用）
  typedef nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, VoronoiParticleCloud>,
    VoronoiParticleCloud,
    3 /* dim */
    > KDTreeVoronoi;
}

namespace fs = std::filesystem;

ProjectionMapGenerator::ProjectionMapGenerator() = default;


RgbImage ProjectionMapGenerator::makeDensityMapImage(ParticleArray& particles,
						     const UnitSystem& units,
						     ProjectionMapParams& params,
						     ProjectionMapContext& ctx){
#ifdef USE_LUA
  ensureLuaInitialized();
#endif

  TrackingVector<ParticleData>& originalParticles = particles.particleBlock.particles;
  
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
      
      for(int k=0;k<3;k++){
	if(p.pos[k] < xmin_zoom[k])
	  xmin_zoom[k] = p.pos[k];

	if(p.pos[k] > xmax_zoom[k])
	  xmax_zoom[k] = p.pos[k];

	xsum_zoom[k] += p.mass * p.pos[k];
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
  
  TrackingVector<pos_val> insideParticles;

  for (int idx = 0; idx < (int)originalParticles.size(); ++idx) {
    const auto& p = originalParticles[idx];
    if (p.type != ctx.selectedType) continue;

    bool inside = false;
    glm::vec4 localPos =
      glm::inverse(ctx.cuboidTransform)
      * glm::vec4(glm::vec3(p.pos[0] - ctx.center.x, p.pos[1] - ctx.center.y, p.pos[2] - ctx.center.z), 1.0f)
      + glm::vec4(ctx.center.x, ctx.center.y, ctx.center.z, 0.);

    if (localPos.x >= xmin_cut[0] && localPos.x <= xmax_cut[0] &&
	localPos.y >= xmin_cut[1] && localPos.y <= xmax_cut[1] &&
	localPos.z >= xmin_cut[2] && localPos.z <= xmax_cut[2]) {
      inside = true;
    }
    if (!inside) continue;

    pos_val pp;
    pp.pos[0] = p.pos[0]; pp.pos[1] = p.pos[1]; pp.pos[2] = p.pos[2];
    pp.hsml = p.Hsml;
    pp.mass = p.mass;
    
    if (params.dataSource == DataSource::Gas) {    
      pp.val = getScalarValue(particles.particleBlock, p, idx, params.selectedVarGas);
      pp.density = p.density;
    }
    
    if (params.dataSource == DataSource::Stars) {
      if (params.starQuantity == StarQuantity::Metallicity) {
	float zmet = 0.0f;
	particles.particleBlock.readSoAAs(soa_views::Metallicity, (size_t)idx, zmet);	
	pp.val = zmet;
      } else if (params.starQuantity == StarQuantity::Mass) {
	pp.val = p.mass;
      } else if (params.starQuantity == StarQuantity::Density) {
	pp.val = p.density;
      } else if (params.starQuantity == StarQuantity::Flux) {
	pp.val = (float)compute_band_luminosity_Lsun(p.mass*units.mass_msun, params.flux);
      } else {
	pp.val = 1.0f;
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
    
  ProjectionMap map;
  for(int k=0;k<3;k++){
    map.xlen[k] = params.xlen[k];
    map.xmin[k] = xmin[k];
  }

  printf("xlen=%g %g %g xmin=%g %g %g center=%g %g %g\n", params.xlen[0], params.xlen[1], params.xlen[2], xmin[0], xmin[1], xmin[2], ctx.center.x, ctx.center.y, ctx.center.z);
  
  map.npixel = params.npixel;
  map.cell_size = std::max(map.xlen[0], map.xlen[1]) / static_cast<float>(params.npixel);  
  map.dx = map.cell_size;
  map.dy = map.cell_size;   
    
  map.npixel_x = static_cast<int>(params.xlen[0] / map.cell_size);
  map.npixel_y = static_cast<int>(params.xlen[1] / map.cell_size);   
  map.values.resize(map.npixel_x * map.npixel_y, 0.0f);
  map.weights.resize(map.npixel_x * map.npixel_y, 0.0f);
        
  if(params.flagVoronoi){
    map.npixel_z = params.step_z;
    if(params.step_z % 2 == 0)
      map.npixel_z = params.step_z - 1;
      
    map.dz = 0.;
    if(params.step_z > 1)
      map.dz = (map.xlen[2]) / static_cast<float>(map.npixel_z);
  }

  map.flagDensityWeight = params.flagDensityWeight;
  map.flagLogScale = params.flagLogScale;
    
  // 平面の基底を計算
  glm::vec3 axisX = glm::normalize(ctx.cuboidTransform * glm::vec3(1,0,0));
  glm::vec3 axisY = glm::normalize(ctx.cuboidTransform * glm::vec3(0,1,0));
  glm::vec3 axisZ = glm::normalize(ctx.cuboidTransform * glm::vec3(0,0,1));

  // selectedAxis: 0=X, 1=Y, 2=Z から見ている
  if (params.selectedAxis == 0) {
    // X 軸方向から見る: 画面奥(w)が axisX、画面 x が axisY、画面 y が axisZ
    map.wAxis = axisX;
    map.uAxis = axisY;
    map.vAxis = axisZ;
  }
  else if (params.selectedAxis == 1) {
    // Y 軸方向から見る
    map.wAxis = axisY;
    map.uAxis = axisZ;
    map.vAxis = axisX;
  }
  else { // selectedAxis == 2
    // Z 軸方向から見る（face-on）
    map.wAxis = axisZ;
    map.uAxis = axisX;
    map.vAxis = axisY;
  }

  map.center = ctx.center;
  
  using namespace std::chrono;
  auto start = high_resolution_clock::now();
  
  if (params.dataSource == DataSource::Gas) {
    if (params.flagVoronoi) createVoronoiSliceMap(map, insideParticles);
    else             createProjectionMap(map, insideParticles);
  }
  else if (params.dataSource == DataSource::Stars) {
    bool normalize = (params.starQuantity != StarQuantity::Flux); 
    createStarMap(map, insideParticles, params.psf_sigma_pix, normalize);
  }
   
  auto end = high_resolution_clock::now();    
  std::cout << "Elapsed time: " << duration_cast<duration<double>>(end - start).count() << " sec\n";
  
  float minVal = FLT_MAX;    
  for (auto val : map.values){
    if(val < minVal && val > 0.)
      minVal = val;
  }
  
  if(map.flagLogScale){
    if(minVal >0.){    
      for (size_t i=0;i< map.values.size();i++){
	if(map.values[i] > 0.)
	  map.values[i] = log10(map.values[i]);
	else
	  map.values[i] = log10(minVal) - 1.;	
      }	
    }else
      printf("minus quantity appears. we will use linear scale.\n");
  }
  
  map.minVal = *std::min_element(map.values.begin(), map.values.end());
  map.maxVal = *std::max_element(map.values.begin(), map.values.end());

  if(params.autoRange){
    params.range_min = map.minVal;
    params.range_max = map.maxVal;
  }
    
  for (int i = 0; i < map.npixel_x * map.npixel_y; i++) {
    float norm = (map.values[i] - params.range_min) / (params.range_max - params.range_min  + 1.e-6);
    float rF,gF,bF;
    colormapLookup(norm, rF, gF, bF, ctx.colorMap, ctx.colorMapSize);
    unsigned char rC=(unsigned char)(rF*255);
    unsigned char gC=(unsigned char)(gF*255);
    unsigned char bC=(unsigned char)(bF*255);

    map.image.push_back(rC);
    map.image.push_back(gC);
    map.image.push_back(bC);
  }

  int colorBarWidth = static_cast<int>(0.07 * map.npixel_x);
  ImageCanvas canvas{map.image, map.npixel_x, map.npixel_y};  
  if(params.flagShowStarParticles)
    overlayStarParticles(canvas, map, params, ctx, originalParticles);  

  addColorBarToMap(canvas, map.cell_size, params.range_min, params.range_max, colorBarWidth, ctx.colorMap, ctx.colorMapSize, params.var.c_str(), params, ctx);  

  return ToRgbImage(canvas);
}

  // 直方体の8頂点（ローカル座標）を計算し、cuboidTransformを適用してワールド座標に変換する関数
TrackingVector<glm::vec3> ProjectionMapGenerator::computeCuboidVertices(float *xmin, float *xmax, glm::vec3 center, glm::quat cuboidTransform)
{
  // ローカルAABBの中心と半幅を計算
  float hx = (xmax[0] - xmin[0]) * 0.5f;
  float hy = (xmax[1] - xmin[1]) * 0.5f;
  float hz = (xmax[2] - xmin[2]) * 0.5f;
    
  TrackingVector<glm::vec3> local = {
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
    
  TrackingVector<glm::vec3> world;
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
  
void ProjectionMapGenerator::createProjectionMap(ProjectionMap &map, const TrackingVector<pos_val>& particles)
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
      
    // y方向の候補範囲：行インデックス
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


void ProjectionMapGenerator::createVoronoiSliceMap(ProjectionMap& map, const TrackingVector<pos_val>& particles)
{  
  TrackingVector<pos_val> filtered;
  for (size_t i=0;i<particles.size();i++)
    {
      const pos_val& p = particles[i];
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

  float xmin_local[3];
  xmin_local[0] = map.xmin[0] - map.center.x;
  xmin_local[1] = map.xmin[1] - map.center.y;
  xmin_local[2] = map.xmin[2] - map.center.z;

  // データコンテナにコピー（必要に応じて参照やポインタを使ってもよい）
  VoronoiParticleCloud cloud;
  cloud.particles = filtered;
  
  // kd-treeの構築
  KDTreeVoronoi kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10 /* max leaf */));
  kdTree.buildIndex();

  nanoflann::SearchParameters params;  

#ifdef _OPENMP
  int numProcs = omp_get_num_procs();
  // その数をスレッド数として設定
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

	double hsml = cloud.particles[ret_index].hsml;
	double vol = hsml * hsml * hsml;	
	double weight = cloud.particles[ret_index].mass / vol;

	if(map.flagDensityWeight == true)
	  weight *= cloud.particles[ret_index].density;	    

	map.values[j * map.npixel_x + i] += cloud.particles[ret_index].val * weight;
	map.weights[j * map.npixel_x + i] += weight;
      }
    }
  }

  for (size_t i = 0; i < map.values.size(); i++){
    //printf("[%zu] value=%g weights=%g\n", i, map.values[i], map.weights[i]);
    if(map.weights[i])
      map.values[i] /= map.weights[i];       
  }
}


static inline double gaussian2d_weight(double r2, double sigma2, double pixel_area)
{
  const double norm = 1.0 / (2.0 * M_PI * sigma2);
  return norm * std::exp(-0.5 * r2 / sigma2) * pixel_area;
}

void ProjectionMapGenerator::createStarMap(ProjectionMap &map,
                                           const TrackingVector<pos_val>& particles,
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
  // Lua の式を評価して数値を返す関数
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
  // Lua の式を評価してテーブル（色）を取得する関数
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
      lua_pop(gLua_, 1); // テーブルをポップ
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
  // Lua の式を評価して論理値を取得する関数（フィルタ用）
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

void ProjectionMapGenerator::overlayStarParticles(ImageCanvas& canvas,
						  const ProjectionMap& map,
						  const ProjectionMapParams& params,
						  const ProjectionMapContext& ctx,
						  const TrackingVector<ParticleData>& particles)
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

  for (const auto &p : particles) {
    if(p.type < 3 || p.type > 5)
      continue;

    if (params.dataSource == DataSource::Stars)
      if(p.type == ctx.selectedType)
	continue;
    
    double pointSize = 5.0;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    
#ifdef USE_LUA
    if(flag_init_lua_){
      lua_settop(gLua_, 0);
      lua_pushnumber(gLua_, p.mass);
      lua_setglobal(gLua_, "m");
      lua_pushnumber(gLua_, p.Hsml);
      lua_setglobal(gLua_, "Hsml");
      lua_pushnumber(gLua_, p.type);
      lua_setglobal(gLua_, "ptype");

      // 1. フィルタ条件の評価
      bool pass = false;
      if (!EvaluateLuaExpressionBool(params.filterExpr, pass)) {
	std::cerr << "Error evaluating filter expression\n";
	continue;
      }

      if (!pass) {
	continue;  // 条件に合致しなければ描画しない
      }

      // 2. 点サイズの評価
      if (!EvaluateLuaExpressionNumber(params.pointSizeExpr, pointSize)) {
	std::cerr << "Error evaluating point size expression\n";
	pointSize = 1.0;
      }

      // 3. 点の色の評価
      if (!EvaluateLuaExpressionColor(params.pointColorExpr, r, g, b, a)) {
	std::cerr << "Error evaluating point color expression\n";
	r = g = b = 1.0f; a = 1.0f;
      }
    }
#endif

    unsigned char ur = static_cast<unsigned char>(r * 255);
    unsigned char ug = static_cast<unsigned char>(g * 255);
    unsigned char ub = static_cast<unsigned char>(b * 255);
      
    // 3D位置から2D画像上の座標 (px, py) を計算（createProjectionMap() と同じ手法で）
    glm::vec3 rad = glm::vec3(p.pos[0],p.pos[1],p.pos[2]) - map.center;
    float u = glm::dot(rad, map.uAxis);  // 画像上のX軸方向の成分
    float v = glm::dot(rad, map.vAxis);  // 画像上のY軸方向の成分
    int px = static_cast<int>((u / (map.xlen[0] * 0.5f) + 1.0f) * 0.5f * map.npixel_x);
    int py = static_cast<int>((v / (map.xlen[1] * 0.5f) + 1.0f) * 0.5f * map.npixel_y);
      
    float desiredStarSize = pointSize * map.npixel_x * 0.02f;
    int radius = std::max(1, static_cast<int>(desiredStarSize * 0.5f));
    
    canvas.drawAsterisk(px, py, radius, ur, ug, ub, a);                   
  }
}

  // --------------------------------------------------------
  // 関数：colormapLookup
  //   [0,1] の値tに基づき、jetMap配列からRGBを補間して返す
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
  // (3) カラーバーに ticks (5分割) とラベルを描画 (簡易ドット)
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
  
  // 余白（padding）を加える
  int padding = 4;
  int ticksWidth = static_cast<int>(ceil(maxTicksWidth)) + 2 * padding;
  int rotateLabelWidth = charPixelSizeLabel + 2 * padding;

  // 出力画像の総幅は、メイン画像＋色バー＋ラベル領域
  int outW = outW_init + colorBarWidth + ticksWidth + rotateLabelWidth;
  int outH = outH_init;
  canvas.resizeKeepContent(outW, outH, 0);

  // (2) カラーバーを右側 colorBarWidth 幅に描画
  //     y=0 が最上部, y=outH が最下部
  //     0～1の値を [0..outH-1] に対応させる
  for (int py = 0; py < outH; py++) {
    float t = 1.0f - float(py) / float(outH - 1); // 上->下が 1->0
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
    
    // 2) 表示したい左上位置を決める（例: (10,10) に置く）
    int baseX = 10;
    int baseY = 10;

    // 3) パディングを足した領域を半透明黒で塗る
    int padding = 4;
    int x0 = baseX - padding;           // 左上X
    int y0 = baseY - padding;           // 左上Y
    int x1 = baseX + textW + padding;   // 右下X
    int y1 = baseY + textH + padding;   // 右下Y

    canvas.blendRect(x0, y0, x1, y1, 0, 0, 0, 0.5f);

    // 4) 実際に文字を描画する
    //    draw_value_on_image() は「(pos_x, pos_y) を中心」として扱うので
    //    テキスト全体の左上が (baseX, baseY) になるようにするには
    //    中心を (baseX + textW/2, baseY + textH/2) で呼び出す
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
      if(params.flagScaleOriginalCoordinate)
	arrowLenX_scaled *= ctx.scaleToPhysical;

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

static inline double compute_band_luminosity_Lsun(double M_Msun, const FluxSettings& fs)
{
  const double Lbol = Lbol_single_massive_Lsun(M_Msun); if (!(Lbol > 0.0) || !std::isfinite(Lbol)) return 0.0;
  const double Teff = Teff_massive_K(M_Msun); if (!(Teff > 0.0) || !std::isfinite(Teff)) return 0.0;
  const double lambda0_m = std::max(1.0, (double)fs.band_center_nm) * 1e-9;
  const double dlambda_m = std::max(1.0, (double)fs.band_width_nm ) * 1e-9;
  const double frac = band_fraction_rect_lambda(Teff, lambda0_m, dlambda_m);
  return Lbol * frac;
}
