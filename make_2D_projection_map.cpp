#include "main.h"
#include "camera.h"
#include "object.h"

#include <fstream>
#include <filesystem>

// GLM 関連
//#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include"make_2D_projection_map.h"

#ifdef USE_LUA
#include <lua.hpp>
#endif
#include <nanoflann.hpp>

#ifdef _OPENMP
#include <omp.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace{
  struct pos_val {
    float pos[3];
    float val;
    float density;
    float mass;
    float hsml;
  };

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

ProjectionMapGenerator::ProjectionMapGenerator(){
  availableFonts = {};
  loadedFonts = {};
    
  std::vector<std::string> searchPaths;
  
  // 環境変数 "MYAPP_FONT_PATH" が設定されていれば追加（ユーザー定義のパス）
  if (const char* envPath = std::getenv("MYAPP_FONT_PATH")) {
    searchPaths.push_back(envPath);
  }
  
  // プラットフォームごとの標準ディレクトリを追加
#ifdef __APPLE__
  searchPaths.push_back("/Library/Fonts");
  searchPaths.push_back("/System/Library/Fonts");
  searchPaths.push_back("/System/Library/Fonts/Supplemental");
#elif defined(_WIN32)
  searchPaths.push_back("C:\\Windows\\Fonts");
#elif defined(__linux__)
  searchPaths.push_back("/usr/share/fonts");
  searchPaths.push_back("/usr/share/fonts/ttf");
  searchPaths.push_back("/usr/local/share/fonts");
#endif

  // 現在の作業ディレクトリも追加（同梱フォント用など）
  searchPaths.push_back(fs::current_path().string());  
  getAvailableFonts(searchPaths);
}

void ProjectionMapGenerator::showWindow(){
  showWindowProjection = true;

}

void ProjectionMapGenerator::RenderProjectionUI(ParticleArray *P, CameraContext& camCtx, int indexfile)
{
  if (!showWindowProjection) return;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("make projectoin map", &showWindowProjection, ImGuiWindowFlags_None);

  static bool selectMode = false;
  static bool useOriginalCoordinate = true;
  
  originalMax = P->originalMax;
  desiredMax = P->desiredMax;

  // ImGuiウィンドウにパラメータ用のコントロールを表示
  static float xlen_input[3];
  ImGui::Checkbox("use original coordinate for side length", &useOriginalCoordinate);
  ImGui::InputFloat3("side lenght", xlen_input);

  if(useOriginalCoordinate){
    for(int k=0;k<3;k++)
      xlen[k] = xlen_input[k] * desiredMax /originalMax;
  }else{
    for(int k=0;k<3;k++)
      xlen[k] = xlen_input[k];
  }
  
  ImGui::InputFloat3("offset center", xoffset);      
  ImGui::InputInt("npixel", &npixel, 10, 1000);
  ImGui::InputFloat3("Plane Tilt (deg) x", tilt);

  if(ImGui::Button("move center to camera pos")){
    for(int k=0;k<3;k++)
      xoffset[k] = camCtx.cameraTarget[k];
  }

  center.x = xoffset[0];
  center.y = xoffset[1];
  center.z = xoffset[2];

  float xmin[3], xmax[3];
  for(int k=0;k<3;k++){
    xmin[k] = xoffset[k] - 0.5 * xlen[k];
    xmax[k] = xoffset[k] + 0.5 * xlen[k];
  }
  
  cuboidTransform = UpdateTransformFromEuler(tilt);
  ImGui::Checkbox("show cubic region", &flagShowCuboid);
  
  camCtx.stopCameraMode = selectMode;
  
  // 状態に応じてボタン色を変更（オン：緑、オフ：デフォルトまたは赤）
  if (selectMode) {
    // selectMode がオンの場合、ボタン背景を緑色にする
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
  }else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
  }
    
  // ボタンラベルも状態に合わせて変更
  if (ImGui::Button(selectMode ? "Exit Region Select" : "Select Region (Mouse Drag)")) {
    // ボタンがクリックされたら、selectMode の状態を反転
    selectMode = !selectMode;
    printf("center_init=%g %g %g\n", center.x, center.y, center.z);
  }
  
  // PushStyleColor で設定した色は PopStyleColor で戻す
  ImGui::PopStyleColor();
  glm::vec3 pivot(center.x, center.y, center.z);
  
  // マウスによる領域選択処理
  if (selectMode){
    glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
    
    ImGuiIO& io = ImGui::GetIO();

    float xpos = io.MousePos.x;
    float ypos = io.MousePos.y;
    static float lastX = xpos;
    static float lastY = ypos;
    
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;
    
    // マウスがドラッグ中なら直方体の変換を更新
    if (ImGui::IsMouseDown(0))
      UpdateCuboidTransformArcball(center, cuboidTransform, xpos - xoffset, ypos - yoffset, lastX, lastY, view, pivot);
    
    // ラジアンの Euler 角を取得し、度に変換
    glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(cuboidTransform));
    // UIに表示する tilt 配列を更新
    tilt[0] = eulerAngles.x;
    tilt[1] = eulerAngles.y;
    tilt[2] = eulerAngles.z;
  }else{
    cuboidTransform = UpdateTransformFromEuler(tilt);
  }

  if (ImGui::Button("set axis from angular momentum")){
    glm::vec3 normal(0.f, 0.f, 1.f);
    planeNormal = glm::normalize(calc_angular_momentum_axis(P->particles, center, xlen));

    printf("planeNormal = %g %g %g\n", planeNormal.x, planeNormal.y, planeNormal.z);
    
    glm::vec3 v = glm::cross(normal, planeNormal);
    float w = 1.0f + glm::dot(normal, planeNormal);
    cuboidTransform = glm::normalize(glm::quat(w, v.x, v.y, v.z));

    // ラジアンの Euler 角を取得し、度に変換
    glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(cuboidTransform));
    // UIに表示する tilt 配列を更新
    tilt[0] = eulerAngles.x;
    tilt[1] = eulerAngles.y;
    tilt[2] = eulerAngles.z;
  }
  
  // 3D領域（直方体）の8頂点を計算
  TrackingVector<glm::vec3> cubicPoints = computeCuboidVertices(xmin, xmax, center, cuboidTransform);

  g_flagShowCuboid = flagShowCuboid;
  g_cubicPoints = cubicPoints;

  
  ImGui::InputText("File Format", fileFormat, IM_ARRAYSIZE(fileFormat));
  ImGui::InputText("Folder", folderPath, IM_ARRAYSIZE(folderPath));

  // フルパスのファイル名を生成
  char filename[512];
  snprintf(filename, sizeof(filename), "%s/%s", folderPath, fileFormat);
  snprintf(filename, sizeof(filename), filename, indexfile);

  // プルダウンメニューで射映軸を選択
  const char* axisLabels[] = { "X-Axis (YZ Plane)", "Y-Axis (XZ Plane)", "Z-Axis (XY Plane)" };
  ImGui::Combo("Projection Normal Axis", &selectedAxis, axisLabels, IM_ARRAYSIZE(axisLabels));
  
  // 法線ベクトルを決定
  glm::vec3 normal(0, 0, 1); // デフォルト Y 軸
  if (selectedAxis == 0) normal = glm::vec3(1, 0, 0); // X 軸 (YZ 平面)
  if (selectedAxis == 1) normal = glm::vec3(0, 1, 0); // X 軸 (YZ 平面)
  if (selectedAxis == 2) normal = glm::vec3(0, 0, 1); // Z 軸 (XY 平面)

  g_selectedAxis = selectedAxis;
  
  // cuboidTransform（四元数）を適用して回転後の法線を計算
  planeNormal = glm::normalize(cuboidTransform * normal);
 
  const char* quantities[] = {"Density", "Temperature", "val", "val2", "Hsml", "Mass" };
  // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
  static int selectedVar = 0;
  ImGui::Combo("Quantity", &selectedVar, quantities, IM_ARRAYSIZE(quantities));
  var = quantities[selectedVar];
  
  const char* availableColormapNames[] = { "Jet", "Viridis", "Plasma" };

  std::string comboLabel = "Colormap##";
  ImGui::Combo(comboLabel.c_str(), &colormapindex, availableColormapNames, IM_ARRAYSIZE(availableColormapNames));
  // ユーザーの選択に応じて colormapForType を更新
  switch (colormapindex) {
  case 0: colorMap = jetMap, countColorMap = 9; break;
  case 1: colorMap = viridisMap, countColorMap = 11; break;
  case 2: colorMap = plasmaMap, countColorMap = 11; break;
  default: colorMap = jetMap, countColorMap = 9; break;
  }

  ImGui::Checkbox("Density Weighting", &flagDensityWeight);
  ImGui::Checkbox("Use Voronoi tesselation", &flagVoronoi);

  if(flagVoronoi){
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SameLine();
    ImGui::InputInt("nz", &step_z, 10, 1000);
  }
  
  ImGui::Checkbox("Use Log color scale", &flagLogScale);
  ImGui::Checkbox("Auto Range", &autoRange);
  if (!autoRange)
    {
      ImGui::Indent();       
      ImGui::SetNextItemWidth(100.0f);
      ImGui::InputFloat("Min", &range_min, 0.0f, 0.0f, "%g");

      ImGui::SetNextItemWidth(100.0f);
      ImGui::SameLine();
      ImGui::InputFloat("Max", &range_max, 0.0f, 0.0f, "%g");
      ImGui::Unindent(); 
    }


  ImGui::Checkbox("Show time label", &flagTimeLabel);
  if(flagTimeLabel){
    ImGui::Indent(); 
    ImGui::InputText("Time Format", timeFormatBuf, IM_ARRAYSIZE(timeFormatBuf));    
    ImGui::Unindent();
  }
  
  ImGui::Checkbox("Show Spacial scale", &flagPlaceScale);
  if(flagPlaceScale){
    ImGui::Indent(); 
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("arrow size", &arrowLenX);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("arrow label", arrowLabelStr, sizeof(arrowLabelStr));

    ImGui::SameLine();
    ImGui::Checkbox("Use original coordinate", &flagScaleOriginalCoordinate);
    
    ImGui::Unindent();
  }

  ImGui::Checkbox("Rescale center to zoom-in region", &flagSpecifyZoomRegionByMass);
  if(flagSpecifyZoomRegionByMass){
    ImGui::Indent(); 
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("critical gas mass", &criticalGasMassForZoomRegion, 0.0f, 0.0f, "%g");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("size of zoom-in region", &lenZoomRegion, 0.0f, 0.0f, "%g");

    ImGui::SameLine();
    ImGui::Checkbox("Use original coordinate", &flagScaleOriginalCoordinateZoomRegion);
    
    ImGui::Unindent();
  }
  
  
  ImGui::Checkbox("draw star particle", &flagShowStarParticles);

#ifdef USE_LUA
  if(flag_init_lua == false){
    gLua = luaL_newstate();
    luaL_openlibs(gLua);
    flag_init_lua = true;
  }
#endif
  
  if (ImGui::Button("Select font")){
    showWindowSelectFont = true;
  }
  ShowFontSelectionWindow();
  
  if(flagShowStarParticles){
    ImGui::InputText("Filter Expression", filterExpr, IM_ARRAYSIZE(filterExpr));
    ImGui::InputText("Point Size Expression", pointSizeExpr, IM_ARRAYSIZE(pointSizeExpr));
    ImGui::InputText("Point Color Expression", pointColorExpr, IM_ARRAYSIZE(pointColorExpr));
    ImGui::InputText("Min Value Expression", minValueExpr, IM_ARRAYSIZE(minValueExpr));
    ImGui::InputText("Max Value Expression", maxValueExpr, IM_ARRAYSIZE(maxValueExpr));
  }
  
  if(ImGui::Button("render 2D projection map"))
    make_density_map(P, filename);
    
  ImGui::End();

  if(!flag2DprojectionComputed)
    return;
  
  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("projection map", &flag2DprojectionComputed, ImGuiWindowFlags_None);
  
  ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2((float)outW, (float)outH));
  
  ImGui::End();
}


void ProjectionMapGenerator::set_projection_parameters(const TrackingVector<ParticleData>& originalParticles, const int useAngularMomentumAxis, 
						       const float* pos_center, const float len, const float val_min, const float val_max,
						       const int npixel_input, const int nslices, std::string var_new){
  if(pos_center != nullptr) {
    center.x = pos_center[0];
    center.y = pos_center[1];
    center.z = pos_center[2];
  }

  if(len > 0.){
    xlen[0] = len;
    xlen[1] = len;
    xlen[2] = len;
  }

  if (!std::isnan(val_min)) {
    range_min = val_min;
    autoRange = false;
  }
  if (!std::isnan(val_max)) {
    range_max = val_max;
    autoRange  = false;
  }
  
  if (npixel_input > 0) 
    npixel = npixel_input;

  if (nslices > 0) 
    step_z = nslices;
  
  if (!var_new.empty())
    var = var_new;
  
  flagShowStarParticles = true;
  flagDensityWeight = true;
  flagLogScale = true;
  //flagVoronoi = true;
  
  if(useAngularMomentumAxis){
    glm::vec3 normal(0.f, 0.f, 1.f);
    planeNormal = glm::normalize(calc_angular_momentum_axis(originalParticles, center, xlen));

    glm::vec3 v = glm::cross(normal, planeNormal);
    float w = 1.0f + glm::dot(normal, planeNormal);
    cuboidTransform = glm::normalize(glm::quat(w, v.x, v.y, v.z));
  }

  colorMap = jetMap, countColorMap = 9;
}


glm::vec3 ProjectionMapGenerator::calc_angular_momentum_axis(const TrackingVector<ParticleData>& originalParticles, glm::vec3 &center, float *xlen){
  TrackingVector<ParticleData> insideParticles;

  double xmean[3]={0.,0.,0.};
  double vmean[3]={0.,0.,0.};
  
  double total_mass = 0., total_weight = 0.;
  float xmin[3], xmax[3];

  for(int k=0;k<3;k++){
    xmin[k] = -0.5 * xlen[k];
    xmax[k] = 0.5 * xlen[k];
  }
  
  double len_crit2 = 4.* (xlen[0]*xlen[0] + xlen[1]*xlen[1] + xlen[2]*xlen[2]);
  for (const auto& p : originalParticles) {
    glm::vec3 localPos = glm::vec3(p.pos[0] - center.x, p.pos[1] - center.y, p.pos[2] - center.z);
    if(std::sqrt(localPos.x*localPos.x + localPos.y*localPos.y + localPos.z*localPos.z >= len_crit2))
      continue;

    /*if (localPos.x >= xmin[0] && localPos.x <= xmax[0] &&
	localPos.y >= xmin[1] && localPos.y <= xmax[1] &&
	localPos.z >= xmin[2] && localPos.z <= xmax[2]) */
      
    float mass = p.mass;
    xmean[0] += p.pos[0] * p.density;
    xmean[1] += p.pos[1] * p.density;
    xmean[2] += p.pos[2] * p.density;      
    
    vmean[0] += mass * p.vel[0];
    vmean[1] += mass * p.vel[1];
    vmean[2] += mass * p.vel[2];
    
    total_mass += mass;
    total_weight += p.density;
  }

  vmean[0] /= total_mass;
  vmean[1] /= total_mass;
  vmean[2] /= total_mass;

  xmean[0] /= total_weight;
  xmean[1] /= total_weight;
  xmean[2] /= total_weight;

  center.x = xmean[0];
  center.y = xmean[1];
  center.z = xmean[2];
  
  glm::vec3 angular_momentum;
  angular_momentum.x = angular_momentum.y = angular_momentum.z = 0.;
  
  for (const auto& p : originalParticles) {
    glm::vec3 localPos = glm::vec3(p.pos[0] - center.x, p.pos[1] - center.y, p.pos[2] - center.z);
    if (localPos.x >= xmin[0] && localPos.x <= xmax[0] &&
	localPos.y >= xmin[1] && localPos.y <= xmax[1] &&
	localPos.z >= xmin[2] && localPos.z <= xmax[2]) {

      float mass = p.mass;
      float dv[3];
      for(int k=0;k<3;k++)
	dv[k] = p.vel[k] - vmean[k];

      angular_momentum.x += mass * (localPos.y * dv[2] - localPos.z * dv[1]);
      angular_momentum.y += mass * (localPos.z * dv[0] - localPos.x * dv[2]);
      angular_momentum.z += mass * (localPos.x * dv[1] - localPos.y * dv[0]);
    }    
  }

  angular_momentum.x /= total_mass;
  angular_momentum.y /= total_mass;
  angular_momentum.z /= total_mass;

  return angular_momentum;
}



void ProjectionMapGenerator::make_density_map(ParticleArray *P, char *filename){

  TrackingVector<ParticleData>& originalParticles = P->particles;
  Header = P->Header;
  
  if(flagSpecifyZoomRegionByMass){
    //construct xmin, xmax, center here
    double xmax_zoom[3] = {-1.e30, -1.e30, -1.e30};
    double xmin_zoom[3] = {1.e30, 1.e30, 1.e30};
    double xsum_zoom[3] = {0., 0., 0.,};
    double weight = 0.;
    
    int count = 0;
    for (const auto& p : originalParticles) {
      if(p.type != 0)
	continue;
      
      if(p.mass > criticalGasMassForZoomRegion)
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

    for(int k=0;k<3;k++)
      xsum_zoom[k] /= weight;
    
    printf("xmin_zoom=%g %g %g max=%g %g %g mean=%g %g %g\n"
	   , xmin_zoom[0], xmin_zoom[1], xmin_zoom[2]
	   , xmax_zoom[0], xmax_zoom[1], xmax_zoom[2]
	   , xsum_zoom[0], xsum_zoom[1], xsum_zoom[2]
	   );
    
    if(count > 0){
      float len_zoom = lenZoomRegion;
      if(flagScaleOriginalCoordinateZoomRegion)
	len_zoom *= (desiredMax /originalMax);      
      
      //center.x = 0.5 * (xmax_zoom[0] + xmin_zoom[0]);
      //center.y = 0.5 * (xmax_zoom[1] + xmin_zoom[1]);
      //center.z = 0.5 * (xmax_zoom[2] + xmin_zoom[2]);

      center.x = xsum_zoom[0];
      center.y = xsum_zoom[1];
      center.z = xsum_zoom[2];
      
      xlen[0] = xlen[1] = xlen[2] = len_zoom;
    }else
      printf("no particles have been found...\n");
  }

  float xmin[3], xmin_cut[3], xmax_cut[3];
  xmin[0] = center.x - 0.5 * xlen[0];
  xmin[1] = center.y - 0.5 * xlen[1];
  xmin[2] = center.z - 0.5 * xlen[2];

  /*we temporally define here, xmin_cut and xmax_cut should be removed... */
  xmin_cut[0] = center.x - xlen[0];
  xmin_cut[1] = center.y - xlen[1];
  xmin_cut[2] = center.z - xlen[2];

  xmax_cut[0] = center.x + xlen[0];
  xmax_cut[1] = center.y + xlen[1];
  xmax_cut[2] = center.z + xlen[2];  
  
  TrackingVector<ParticleData> insideParticles;
  
  for (const auto& p : originalParticles) {
    glm::vec4 localPos = glm::inverse(cuboidTransform) * glm::vec4(glm::vec3(p.pos[0] - center.x, p.pos[1] - center.y, p.pos[2] - center.z), 1.0f) + glm::vec4(center.x, center.y, center.z, 0.);
    if (localPos.x >= xmin_cut[0] && localPos.x <= xmax_cut[0] &&
	localPos.y >= xmin_cut[1] && localPos.y <= xmax_cut[1] &&
	localPos.z >= xmin_cut[2] && localPos.z <= xmax_cut[2]) {
      insideParticles.push_back(p);
    }
  }

  if(insideParticles.size() < 0.1 * npixel * npixel){
    printf("too few particles inside specified region... npart=%ld while there are %dx%d pixels.\n",
	   insideParticles.size(), npixel, npixel);
    return;
  }
    
  ProjectionMap map;
  for(int k=0;k<3;k++){
    map.xlen[k] = xlen[k];
    map.xmin[k] = xmin[k];
  }

  printf("xlen=%g %g %g xmin=%g %g %g center=%g %g %g\n", xlen[0], xlen[1], xlen[2], xmin[0], xmin[1], xmin[2], center.x, center.y, center.z);
  
  map.npixel = npixel;
  map.cell_size = std::max(map.xlen[0], map.xlen[1]) / static_cast<float>(npixel);  
  map.dx = map.cell_size;
  map.dy = map.cell_size;   
    
  map.npixel_x = static_cast<int>(xlen[0] / map.cell_size);
  map.npixel_y = static_cast<int>(xlen[1] / map.cell_size);   
  map.values.resize(map.npixel_x * map.npixel_y, 0.0f);
  map.weights.resize(map.npixel_x * map.npixel_y, 0.0f);
        
  if(flagVoronoi){
    map.npixel_z = step_z;
    if(step_z % 2 == 0)
      map.npixel_z = step_z - 1;
      
    map.dz = 0.;
    if(step_z > 1)
      map.dz = (map.xlen[2]) / static_cast<float>(map.npixel_z);
  }

  map.flagDensityWeight = flagDensityWeight;
  map.flagLogScale = flagLogScale;
    
  // 平面の基底を計算
  glm::vec3 up(0.f, 1.f, 0.f);
  if (fabs(glm::dot(up, planeNormal)) > 0.99f) 
    up = glm::vec3(1.f, 0.f, 0.f);
        
  map.uAxis = glm::normalize(glm::cross(planeNormal, up));
  map.vAxis = glm::normalize(glm::cross(planeNormal, map.uAxis));
  map.wAxis = glm::normalize(planeNormal);
  map.center = center;
    
  if(flagVoronoi == true)
    createVoronoiSliceMap(map, insideParticles);
  else    
    createProjectionMap(map, insideParticles);

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

  if(autoRange){
    range_min = map.minVal;
    range_max = map.maxVal;
  }
    
  for (int i = 0; i < map.npixel_x * map.npixel_y; i++) {
    float norm = (map.values[i] - range_min) / (range_max - range_min  + 1.e-6);
    float rF,gF,bF;
    colormapLookup(norm, rF, gF, bF, colorMap, countColorMap);
    unsigned char rC=(unsigned char)(rF*255);
    unsigned char gC=(unsigned char)(gF*255);
    unsigned char bC=(unsigned char)(bF*255);

    map.image.push_back(rC);
    map.image.push_back(gC);
    map.image.push_back(bC);
  }

  if(flagShowStarParticles)
    overlayStarParticles(map, originalParticles);

  outW = map.npixel_x;
  outH = map.npixel_y;

  int colorBarWidth = static_cast<int>(0.07 * outW);
  outImage = map.image;
  addColorBarToMap(map, range_min, range_max, colorBarWidth, colorMap, countColorMap, outImage, outW, outH, var.c_str());

  //output PNG file
  stbi_write_png(filename, outW, outH, 3, outImage.data(), outW*3);
    
  texID = CreateTexture2D(outImage.data(), outW, outH);
  flag2DprojectionComputed = true;
}

  // 直方体の8頂点（ローカル座標）を計算し、g_cuboidTransformを適用してワールド座標に変換する関数
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
  
glm::quat ProjectionMapGenerator::UpdateTransformFromEuler(float *eulerAngles)
{
  glm::quat qx = glm::angleAxis(glm::radians(eulerAngles[0]), glm::vec3(1.0f, 0.0f, 0.0f));
  glm::quat qy = glm::angleAxis(glm::radians(eulerAngles[1]), glm::vec3(0.0f, 1.0f, 0.0f));
  glm::quat qz = glm::angleAxis(glm::radians(eulerAngles[2]), glm::vec3(0.0f, 0.0f, 1.0f));
    
  return qz * qy * qx;  
}
  
float ProjectionMapGenerator::kernel(float u) {
  if(u  < 0.5)
    return 1.- 6.*u*u + 6.*u*u*u;
  else if(u < 1.)
    return 2. * pow(1.-u, 3.);
  else
    return 0.;
}
  
void ProjectionMapGenerator::createProjectionMap(ProjectionMap &map, const TrackingVector<ParticleData>& particles)
{
  float xmin_local[3];
  xmin_local[0] = map.xmin[0] - map.center.x;
  xmin_local[1] = map.xmin[1] - map.center.y;
  xmin_local[2] = map.xmin[2] - map.center.z;

  for (const auto& p : particles) {    
    float hsml = p.Hsml;
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
	  
	  float property = p.getValue(var);
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


void ProjectionMapGenerator::createVoronoiSliceMap(ProjectionMap& map, const TrackingVector<ParticleData>& particles)
{  
  TrackingVector<pos_val> filtered;
  for (size_t i=0;i<particles.size();i++)
    {
      const ParticleData& p = particles[i];
      pos_val sp;
      
      glm::vec3 diff = glm::vec3(p.pos[0], p.pos[1], p.pos[2]) - map.center;
      sp.pos[0] = glm::dot(diff, map.uAxis);
      sp.pos[1] = glm::dot(diff, map.vAxis);
      sp.pos[2] = glm::dot(diff, map.wAxis);      

      sp.val = p.getValue(var);
      sp.density = p.density;
      sp.mass = p.mass;
      sp.hsml = p.Hsml;

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
    if(map.weights[i])
      map.values[i] /= map.weights[i];       
  }
}


#ifdef USE_LUA
  // ---------------------------
  // Lua の式を評価して数値を返す関数
bool ProjectionMapGenerator::EvaluateLuaExpressionNumber(const char* expr, double& outValue) {
  lua_settop(gLua, 0);
  if (luaL_dostring(gLua, expr) == LUA_OK) {
    if(lua_isnumber(gLua, -1)) {
      outValue = lua_tonumber(gLua, -1);
      lua_pop(gLua, 1);
      return true;
    }
  } else {
    const char* err = lua_tostring(gLua, -1);
    std::cerr << "Lua error: " << err << std::endl;
    lua_pop(gLua, 1);
  }
  return false;
}


  // ---------------------------
  // Lua の式を評価してテーブル（色）を取得する関数
bool ProjectionMapGenerator::EvaluateLuaExpressionColor(const char* expr, float& r, float& g, float& b, float& a) {
  lua_settop(gLua, 0);
  if (luaL_dostring(gLua, expr) == LUA_OK) {
    if(lua_istable(gLua, -1)) {
      lua_getfield(gLua, -1, "r");
      r = static_cast<float>(lua_tonumber(gLua, -1));
      lua_pop(gLua, 1);
      lua_getfield(gLua, -1, "g");
      g = static_cast<float>(lua_tonumber(gLua, -1));
      lua_pop(gLua, 1);
      lua_getfield(gLua, -1, "b");
      b = static_cast<float>(lua_tonumber(gLua, -1));
      lua_pop(gLua, 1);
      lua_getfield(gLua, -1, "a");
      a = static_cast<float>(lua_tonumber(gLua, -1));
      lua_pop(gLua, 1);
      lua_pop(gLua, 1); // テーブルをポップ
      return true;
    }
    lua_pop(gLua, 1);
  } else {
    const char* err = lua_tostring(gLua, -1);
    std::cerr << "Lua error: " << err << std::endl;
    lua_pop(gLua, 1);
  }
  return false;
}


  // ---------------------------
  // Lua の式を評価して論理値を取得する関数（フィルタ用）
bool ProjectionMapGenerator::EvaluateLuaExpressionBool(const char* expr, bool& outValue) {
  lua_settop(gLua, 0);
  if (luaL_dostring(gLua, expr) == LUA_OK) {
    if(lua_isboolean(gLua, -1)) {
      outValue = lua_toboolean(gLua, -1);
      lua_pop(gLua, 1);
      return true;
    }
    lua_pop(gLua, 1);
  } else {
    const char* err = lua_tostring(gLua, -1);
    std::cerr << "Lua error: " << err << std::endl;
    lua_pop(gLua, 1);
  }
  return false;
}
#endif

void ProjectionMapGenerator::overlayStarParticles(ProjectionMap& map, const TrackingVector<ParticleData>& particles)
{

#ifdef USE_LUA
  if(flag_init_lua){
    double minVal = 0.0, maxVal = 1.0;
    if(!EvaluateLuaExpressionNumber(minValueExpr, minVal)) {
      std::cerr << "Error evaluating min value expression\n";
    }
    if(!EvaluateLuaExpressionNumber(maxValueExpr, maxVal)) {
      std::cerr << "Error evaluating max value expression\n";
    }
    lua_pushnumber(gLua, minVal);
    lua_setglobal(gLua, "min");
    lua_pushnumber(gLua, maxVal);
    lua_setglobal(gLua, "max");
  }
#endif
  
  for (const auto &p : particles) {
    double pointSize = 5.0;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

#ifdef USE_LUA
    if(flag_init_lua){
      lua_settop(gLua, 0);
      lua_pushnumber(gLua, p.mass);
      lua_setglobal(gLua, "m");
      lua_pushnumber(gLua, p.Hsml);
      lua_setglobal(gLua, "Hsml");
      lua_pushnumber(gLua, p.val);
      lua_setglobal(gLua, "val");
      lua_pushnumber(gLua, p.type);
      lua_setglobal(gLua, "ptype");
      
      // 1. フィルタ条件の評価
      bool pass = false;
      if (!EvaluateLuaExpressionBool(filterExpr, pass)) {
	std::cerr << "Error evaluating filter expression\n";
	continue;
      }
      
      if (!pass) {
	continue;  // 条件に合致しなければ描画しない
      }

      // 2. 点サイズの評価
      if (!EvaluateLuaExpressionNumber(pointSizeExpr, pointSize)) {
	std::cerr << "Error evaluating point size expression\n";
	pointSize = 1.0;
      }

      // 3. 点の色の評価
      if (!EvaluateLuaExpressionColor(pointColorExpr, r, g, b, a)) {
	std::cerr << "Error evaluating point color expression\n";
	r = g = b = 1.0f; a = 1.0f;
      }
    }
#endif
    
    unsigned char ur = static_cast<unsigned char>(r * 255);
    unsigned char ug = static_cast<unsigned char>(g * 255);
    unsigned char ub = static_cast<unsigned char>(b * 255);
      
    if (p.type >= 3 && p.type <= 5) {
      // 3D位置から2D画像上の座標 (px, py) を計算（createProjectionMap() と同じ手法で）
      glm::vec3 rad = glm::vec3(p.pos[0],p.pos[1],p.pos[2]) - map.center;
      float u = glm::dot(rad, map.uAxis);  // 画像上のX軸方向の成分
      float v = glm::dot(rad, map.vAxis);  // 画像上のY軸方向の成分
      int px = static_cast<int>((u / (map.xlen[0] * 0.5f) + 1.0f) * 0.5f * map.npixel_x);
      int py = static_cast<int>((v / (map.xlen[1] * 0.5f) + 1.0f) * 0.5f * map.npixel_y);
      
      // 画像の幅に応じたスケール因子を計算
      float desiredStarSize = pointSize * map.npixel_x * 0.02f; // 例: 画像幅の2%が星全体のサイズ

      // 指定文字のグリフビットマップを取得
      float scale = stbtt_ScaleForPixelHeight(&fontCharacter, desiredStarSize);
      int width, height, xoffset, yoffset;

      int codepoint = static_cast<int>('*');
      unsigned char* bitmap = stbtt_GetCodepointBitmap(&fontCharacter, 0, scale, codepoint, &width, &height, &xoffset, &yoffset);
      if (!bitmap) {
        std::cerr << "Failed to generate bitmap for character: " << codepoint << std::endl;
        return;
      }

      //printf("width=%d height=%d xoffset=%d yoffset=%d", width, height, xoffset, yoffset);
      
      int penX = px - (width / 2);// + xoffset;
      int penY = py - (height / 2);// + yoffset;

      // bitmap を image バッファにコピーする（image は RGB 形式）
      for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
	  unsigned char pixelAlpha = bitmap[i + j * width];

	  int x = penX + i;
	  int y = penY + j;
	  
	  if (x >= 0 && x < map.npixel_x && y >= 0 && y < map.npixel_y) {
	    int idx = (y * map.npixel_x + x) * 3;
	    
	    unsigned char ur_new = (ur * pixelAlpha / 255) + (map.image[idx + 0] * (255 - pixelAlpha) / 255);
	    unsigned char ug_new = (ug * pixelAlpha / 255) + (map.image[idx + 1] * (255 - pixelAlpha) / 255);
	    unsigned char ub_new = (ub * pixelAlpha / 255) + (map.image[idx + 2] * (255 - pixelAlpha) / 255);
	  
	    map.image[idx + 0] = ur_new;
	    map.image[idx + 1] = ug_new;
	    map.image[idx + 2] = ub_new;
	  }
	  
	}
      }
      
    }
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


bool ProjectionMapGenerator::containsIgnoreCase(const std::string& str, const std::string& substr)
{
    auto it = std::search(
        str.begin(), str.end(),
        substr.begin(), substr.end(),
        [](char ch1, char ch2) {
            return std::toupper(ch1) == std::toupper(ch2);
        });
    return (it != str.end());
}

void ProjectionMapGenerator::getAvailableFonts(const std::vector<std::string>& fontDirectories)
{
  for (const auto& dir : fontDirectories)
  {
    if (!fs::exists(dir) || !fs::is_directory(dir))
      continue;
    
    for (const auto& entry : fs::directory_iterator(dir))
      {
	if (entry.is_regular_file())
	  {
	    // 拡張子を取得
	    std::string ext = entry.path().extension().string();
	    
	    // 小文字に変換
	    std::transform(ext.begin(), ext.end(), ext.begin(),
			   [](unsigned char c){ return std::tolower(c); });

	    if (ext == ".ttf" || ext == ".otf")
	      {
		// .ttf または .otf の場合は追加
		
		std::string pathStr = entry.path().string();
		std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(),
			       [](unsigned char c){ return std::tolower(c); });
		
		//if(containsIgnoreCase(pathStr, "zapfdingbats") || containsIgnoreCase(pathStr, "marlett")|| containsIgnoreCase(pathStr, "symbol")|| containsIgnoreCase(pathStr, "webdings")|| containsIgnoreCase(pathStr, "wingding"))
		//continue;
		
		bool flag = false;
		if(pathStr.find("arial") != std::string::npos ||
		   pathStr.find("helvetica") != std::string::npos ||
		   pathStr.find("verdana") != std::string::npos ||
		   pathStr.find("calibri") != std::string::npos ||
		   pathStr.find("times") != std::string::npos ||
		   pathStr.find("roman") != std::string::npos )
		  flag = true;
		
		printf("%s flag=%d\n", pathStr.c_str(), flag);
		
		if(flag == false)
		  continue;
		
		this->availableFonts.push_back(entry.path().string());
	      }
	  }
      }
  }

  if(availableFonts.size() > 0){
    initFonts();
    
    if (!loadFontFile(availableFonts[0].c_str(), ttf_buffer)) {
      printf("Can't read font file %s\n", availableFonts[0].c_str());
      return;
    }
    
    if (!stbtt_InitFont(&fontCharacter, ttf_buffer.data(), 0)) {
      std::cerr << "Failed to initialize font." << std::endl;
      return;    
    }
  }
}

void ProjectionMapGenerator::initFonts() {
  // availableFonts にすでに検索済みのフォントパスが入っている前提
  loadedFonts.clear();
  ImGui::GetIO().Fonts->AddFontDefault();

  for (const auto& fontPath : availableFonts) {
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.c_str(), 24.0f);
    if (font) {
      loadedFonts.push_back(font);
    }
  }
  // フォントアトラスをビルドする
  ImGui::GetIO().Fonts->Build();
}


void ProjectionMapGenerator::ShowFontSelectionWindow()
{
  if (!showWindowSelectFont) return;

  static int currentFontIndex = 0;
  // previewFont は loadedFonts[currentFontIndex] を指すだけ
  ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Appearing);
  ImGui::Begin("Font Selection Preview", &showWindowSelectFont, ImGuiWindowFlags_None);

  // コンボボックスでフォント一覧を表示
  if (ImGui::BeginCombo("Select Font", availableFonts[currentFontIndex].c_str()))
    {
      for (size_t i = 0; i < availableFonts.size(); i++)
        {
	  bool isSelected = (currentFontIndex == static_cast<int>(i));
	  if (ImGui::Selectable(availableFonts[i].c_str(), isSelected))
            {
	      currentFontIndex = i;
            }
	  if (isSelected)
	    ImGui::SetItemDefaultFocus();
        }
      ImGui::EndCombo();
    }

  ImGui::Text("Selected Font: %s", availableFonts[currentFontIndex].c_str());
  // プレビュー用テキストを表示
  if (!loadedFonts.empty() && loadedFonts[currentFontIndex])
    {
      ImGui::PushFont(loadedFonts[currentFontIndex]);
      ImGui::Text("The quick brown fox jumps over the lazy dog.");
      ImGui::PopFont();
    }

  if (!loadFontFile(availableFonts[currentFontIndex].c_str(), ttf_buffer)) {
    printf("Can't read font file %s\n", availableFonts[currentFontIndex].c_str());
    return;
  }

  if (!stbtt_InitFont(&fontCharacter, ttf_buffer.data(), 0)) {
    std::cerr << "Failed to initialize font." << std::endl;
    return;    
  }
    
  ImGui::End();
}
 
bool ProjectionMapGenerator::loadFontFile(const std::string& fontFilename, std::vector<unsigned char>& buffer)
{
    std::ifstream file(fontFilename, std::ios::binary);
    if (!file) {
        std::cerr << "Could not open font file: " << fontFilename << std::endl;
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string str = ss.str();
    buffer.assign(str.begin(), str.end());
    return true;
}


void ProjectionMapGenerator::renderGlyphExample(const std::vector<unsigned char>& ttf_buffer, char c, int fontSize)
{
  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, ttf_buffer.data(), 0)) {
    std::cerr << "Failed to initialize font." << std::endl;
    return;
  }

  // フォントサイズに合わせたスケールを計算
  float scale = stbtt_ScaleForPixelHeight(&font, fontSize);
  
  int width, height, xoffset, yoffset;
  // 指定した文字 c のグリフビットマップを生成
  unsigned char* bitmap = stbtt_GetCodepointBitmap(&font, 0, scale, c, &width, &height, &xoffset, &yoffset);
  if (!bitmap) {
    std::cerr << "Failed to get bitmap for character: " << c << std::endl;
    return;
  }
  
  // bitmap はグレースケールのピクセル値 (0-255)
  // ここではその内容を標準出力に簡易的に表示（実際は画像バッファにコピーするなどの処理が必要）
    std::cout << "Bitmap for '" << c << "': (" << width << "x" << height << ")\n";
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
	unsigned char pixel = bitmap[x + y * width];
	std::cout << (pixel > 128 ? "#" : " ");
      }
      std::cout << "\n";
    }
    
    stbtt_FreeBitmap(bitmap, nullptr);
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

void ProjectionMapGenerator::addColorBarToMap(const ProjectionMap& map,
					      float minVal, float maxVal,
					      int colorBarWidth,
					      const float *colormap, int countcolormap,
					      TrackingVector<unsigned char>& outImage, int& outW, int& outH, const char *barLabel)
{

  // (3) カラーバーに ticks (5分割) とラベルを描画 (簡易ドット)
  int nTicks = 5;
  std::vector<double> ticks = generate_ticks(minVal, maxVal, nTicks);
  nTicks = ticks.size();

  int charPixelSize = static_cast<int>(0.08 * outH);
  int charPixelSizeLabel = static_cast<int>(0.1 * outH);
  // フォント情報とスケール（stbtt_fontinfo *fontCharacter と scale が設定されている前提）
  float scale = stbtt_ScaleForPixelHeight(&fontCharacter, charPixelSize); // charPixelSizeは希望の高さ

  //float scaleLabel = stbtt_ScaleForPixelHeight(&fontCharacter, charPixelSizeLabel); // charPixelSizeは希望の高さ
  
  // 最大のtickラベルの幅を計算する
  float maxTicksWidth = 0.0f;
  char labelStr[64];
  for (int i = 0; i < nTicks; i++) {
    // 例: "%.1f" の形式で文字列を生成
    snprintf(labelStr, sizeof(labelStr), "%.1f", ticks[i]);
    float w = stbtt_CalcTextWidth(&fontCharacter, scale, labelStr);
    if (w > maxTicksWidth)
      maxTicksWidth = w;
  }
  
  // 余白（padding）を加える
  int padding = 4;
  int ticksWidth = static_cast<int>(ceil(maxTicksWidth)) + 2 * padding;
  int rotateLabelWidth = charPixelSizeLabel + 2 * padding;
  
  // 出力画像の総幅は、メイン画像＋色バー＋ラベル領域
  outW = map.npixel_x + colorBarWidth + ticksWidth + rotateLabelWidth;
  outH = map.npixel_y;
  
  // 出力画像バッファ (RGB)
  outImage.resize(outW * outH * 3, 0);
    
  // (1) メインデータを左側に貼り付け
  for (int y = 0; y < map.npixel_y; y++) {
    for (int x = 0; x < map.npixel_x; x++) {
      int inIdx = (y * map.npixel_x + x) * 3;
      int outIdx = (y * outW + x) * 3;
      outImage[outIdx + 0] = map.image[inIdx + 0];
      outImage[outIdx + 1] = map.image[inIdx + 1];
      outImage[outIdx + 2] = map.image[inIdx + 2];
    }
  }

  // (2) カラーバーを右側 colorBarWidth 幅に描画
  //     y=0 が最上部, y=outH が最下部
  //     0～1の値を [0..outH-1] に対応させる
  for (int py = 0; py < outH; py++) {
    float t = 1.0f - float(py) / float(outH - 1); // 上->下が 1->0
    float rF, gF, bF;
    colormapLookup(t, rF, gF, bF, colormap, countcolormap);
    unsigned char rC = (unsigned char)(rF * 255);
    unsigned char gC = (unsigned char)(gF * 255);
    unsigned char bC = (unsigned char)(bF * 255);
      
    for (int px = 0; px < colorBarWidth; px++) {
      int outX = map.npixel_x + px;
      int idx = (py * outW + outX) * 3;
      outImage[idx + 0] = rC;
      outImage[idx + 1] = gC;
      outImage[idx + 2] = bC;
    }

    for (int px = 0; px < ticksWidth + rotateLabelWidth; px++) {
      int outX = map.npixel_x + colorBarWidth + px;
      int idx = (py * outW + outX) * 3;
      outImage[idx + 0] = 0;
      outImage[idx + 1] = 0;
      outImage[idx + 2] = 0;
    }
  }

  
  auto drawLine = [&](int x1,int y1,int x2,int y2, unsigned char r,unsigned char g,unsigned char b){
    // 単純なBresenham省略し、縦線/横線のみと仮定
    if (y1==y2) {
      // 横線
      for (int x=x1; x<=x2; x++){
	int idx=(y1*outW + x)*3;
	outImage[idx+0]=r; outImage[idx+1]=g; outImage[idx+2]=b;
      }
    } else if (x1==x2) {
      // 縦線
      for (int y=y1; y<=y2; y++){
	int idx=(y*outW + x1)*3;
	outImage[idx+0]=r; outImage[idx+1]=g; outImage[idx+2]=b;
      }
    }
  };

  int labelAreaX = map.npixel_x + colorBarWidth;
  int labelCenterX = labelAreaX + ticksWidth / 2;
  
  for (int i = 0; i < nTicks; i++) {
    if(ticks[i] < minVal || ticks[i] > maxVal)
      continue;

    float frac = (ticks[i] - minVal) / (maxVal - minVal);
    int tickY = int((1.0f - frac) * (outH - 1));
    // tickライン
    int xLineStart = map.npixel_x; 
    int xLineEnd   = map.npixel_x + 10;
    drawLine(xLineStart, tickY, xLineEnd, tickY, 255,255,255);

    // ラベル文字
    draw_value_on_image(outImage, outW, outH, labelCenterX, tickY, ticks[i], &fontCharacter, static_cast<float>(charPixelSize), "%.1f");          
  }

  
  {
    //const char* barLabel = "Temperature";
    int labelAreaX = map.npixel_x + colorBarWidth + ticksWidth;
    int center_x = labelAreaX + rotateLabelWidth / 2;
    int center_y = outH / 2; // 必要に応じて調整
    drawTextBaselineAndRotate90(outImage, outW, outH, center_x, center_y, barLabel, &fontCharacter, static_cast<float>(charPixelSizeLabel));
  }

  if(flagTimeLabel){
    char timeStr[64];
    double t = Header.time; // 適切な時間値
    snprintf(timeStr, sizeof(timeStr), timeFormatBuf, t);

    // 1) テキストの実際の表示サイズを先に計測する
    int textW = 0, textH = 0;
    float min_x, min_y;
    measure_text_bbox(timeStr, &fontCharacter, static_cast<float>(charPixelSizeLabel), textW, textH, min_x, min_y);

    // 2) 表示したい左上位置を決める（例: (10,10) に置く）
    int baseX = 10;
    int baseY = 10;

    // 3) パディングを足した領域を半透明黒で塗る
    int padding = 4;
    int x0 = baseX - padding;           // 左上X
    int y0 = baseY - padding;           // 左上Y
    int x1 = baseX + textW + padding;   // 右下X
    int y1 = baseY + textH + padding;   // 右下Y

    for (int yy = y0; yy < y1; yy++) {
      if (yy < 0 || yy >= outH) continue;
      for (int xx = x0; xx < x1; xx++) {
	if (xx < 0 || xx >= outW) continue;
	int idx = (yy * outW + xx) * 3;
	float alpha = 0.5f; // 半透明度(0.5)
	unsigned char r = outImage[idx + 0];
	unsigned char g = outImage[idx + 1];
	unsigned char b = outImage[idx + 2];
	outImage[idx + 0] = (unsigned char)(0 * alpha + r * (1 - alpha));
	outImage[idx + 1] = (unsigned char)(0 * alpha + g * (1 - alpha));
	outImage[idx + 2] = (unsigned char)(0 * alpha + b * (1 - alpha));
      }
    }

    // 4) 実際に文字を描画する
    //    draw_value_on_image() は「(pos_x, pos_y) を中心」として扱うので
    //    テキスト全体の左上が (baseX, baseY) になるようにするには
    //    中心を (baseX + textW/2, baseY + textH/2) で呼び出す
   
    draw_value_on_image(outImage, outW, outH, baseX + textW/2, baseY - min_y,
                        t, &fontCharacter, static_cast<float>(charPixelSizeLabel), timeFormatBuf);
  }


  // (9) 空間スケールの矢印の描画（例：左下に 100px のスケール表示）
  if(flagPlaceScale)
    {
      int arrowLenX_in_pixel = static_cast<int>(arrowLenX / map.cell_size);

      if(flagScaleOriginalCoordinate)
	arrowLenX_in_pixel *= (desiredMax /originalMax);
	
      int arrowCenterX = map.npixel_x / 2;
      int arrowStartX = arrowCenterX - arrowLenX_in_pixel / 2;
      int arrowEndX = arrowStartX + arrowLenX_in_pixel;
      
      int arrowStartY = outH - 30;
      int arrowEndY = arrowStartY;
      
      drawLine(arrowStartX, arrowStartY, arrowEndX, arrowEndY, 255, 255, 255);
      // 矢印先端
      drawLine(arrowEndX, arrowEndY, arrowEndX - 10, arrowEndY - 5, 255, 255, 255);
      drawLine(arrowEndX, arrowEndY, arrowEndX - 10, arrowEndY + 5, 255, 255, 255);
      // ラベル（例："100 px"）の描画
      draw_text_label_centered(outImage, outW, outH, arrowCenterX, arrowStartY - 10, arrowLabelStr
			       , &fontCharacter, static_cast<float>(charPixelSizeLabel));
    }

}

// ここで TrackingVector は std::vector などと同様と仮定
// ※ stb_truetype.h がインクルードされている前提

float ProjectionMapGenerator::stbtt_CalcTextWidth(const stbtt_fontinfo* font, float scale, const char* text)
{
    float width = 0.0f;
    while (*text) {
        // 文字コードを取得 (ASCII前提)
        int c = (unsigned char)(*text);

        // 各文字の横方向メトリクス (advanceWidth, leftSideBearing) を取得
        int advanceWidth = 0, leftSideBearing = 0;
        stbtt_GetCodepointHMetrics(font, c, &advanceWidth, &leftSideBearing);

        // 幅 (advanceWidth * scale) を加算
        width += (advanceWidth * scale);

        // 文字間のカーニング(字詰め)を考慮
        if (*(text + 1)) {
            int c2 = (unsigned char)(*(text + 1));
            width += scale * stbtt_GetCodepointKernAdvance(font, c, c2);
        }

        text++;
    }
    return width;
}


void ProjectionMapGenerator::measure_text(const char* text,
					  stbtt_fontinfo* font,
					  float pixelSize,
					  int& outWidth,
					  int& outHeight)
{
    outWidth = 0;
    outHeight = 0;
    if (!text || !*text) return;

    float scale = stbtt_ScaleForPixelHeight(font, pixelSize);

    int max_h = 0;
    int len = (int)strlen(text);
    for (int i = 0; i < len; i++) {
        int cp = (unsigned char)text[i];
        int w, h, xoff, yoff;
        // グリフビットマップを一度作り、w,h を取得
        unsigned char* bmp = stbtt_GetCodepointBitmap(font, 0, scale, cp,
                                                      &w, &h, &xoff, &yoff);
        if (bmp) {
            stbtt_FreeBitmap(bmp, NULL);
        }
        outWidth += w;
        if (h > max_h) {
            max_h = h;
        }
    }
    outHeight = max_h;
}

void ProjectionMapGenerator::measure_text_bbox(const char* text,
					       stbtt_fontinfo* font,
					       float pixelSize,
					       int& outWidth,
					       int& outHeight,
					       float& outMinX,
					       float& outMinY)
{
    float pen_x = 0.0f;
    float pen_y = 0.0f;
    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;

    float scale = stbtt_ScaleForPixelHeight(font, pixelSize);
    
    for (int i = 0; text[i]; i++) {
        int code = (unsigned char)text[i];
        if (code == '\n') {
            // 単純に改行処理するならここで pen_x=0, pen_y += 行送り など
            continue;
        }

        // フォントメトリクスから水平アドバンス
        int advWidth, leftBearing;
        stbtt_GetCodepointHMetrics(font, code, &advWidth, &leftBearing);

        // グリフ bbox (ベースライン相対)
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(font, code, scale, scale,
                                    &x0, &y0, &x1, &y1);

        // 実際に描画される左上, 右下 (pen_x が基準)
        float gx0 = pen_x + x0;
        float gy0 = pen_y + y0;
        float gx1 = pen_x + x1;
        float gy1 = pen_y + y1;

        // min/max 更新
        if (gx0 < min_x) min_x = gx0;
        if (gx1 > max_x) max_x = gx1;
        if (gy0 < min_y) min_y = gy0;
        if (gy1 > max_y) max_y = gy1;

        // 次の文字へ
        pen_x += (advWidth * scale);

        // カーニング
        // if (text[i+1]) {
        //     pen_x += scale * stbtt_GetCodepointKernAdvance(
        //                  font, code, (unsigned char)text[i+1]);
        // }
    }

    // 左上(x0,y0), 右下(x1,y1) から幅/高さを計算 (負数の場合に注意)
    if (max_x < min_x || max_y < min_y) {
        outWidth = 0;
        outHeight = 0;
	outMinX   = 0;
	outMinY   = 0;
        return;
    }
    outWidth  = (int)std::ceil(max_x - min_x);
    outHeight = (int)std::ceil(max_y - min_y);
    outMinX   = min_x;
    outMinY   = min_y;
}

void ProjectionMapGenerator::draw_rotated_char(TrackingVector<unsigned char>& image,
                                                 int img_width, int img_height,
                                                 int pos_x, int pos_y, int codepoint,
                                                 stbtt_fontinfo *font, float scale)
{
    int w, h, xoffset, yoffset;
    // 文字ビットマップを取得（通常の向き）
    unsigned char* bmp = stbtt_GetCodepointBitmap(font, 0, scale, codepoint,
                                                  &w, &h, &xoffset, &yoffset);
    if (!bmp)
        return;

    // 90度回転（時計回り）すると、元の幅w, 高さh は、回転後は new_w = h, new_h = w となる。
    int new_w = h, new_h = w;
    std::vector<unsigned char> rotated(new_w * new_h, 0);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int src_idx = y * w + x;
            int rx = y;                  // 回転後の x 座標
            int ry = new_h - 1 - x;      // 回転後の y 座標
            rotated[ry * new_w + rx] = bmp[src_idx];
        }
    }
    stbtt_FreeBitmap(bmp, NULL);

    int draw_x = pos_x - new_w / 2;
    int draw_y = pos_y - new_h / 2;

    // 各ピクセルをアルファブレンディングで描画する
    for (int y = 0; y < new_h; y++) {
        for (int x = 0; x < new_w; x++) {
            int img_x = draw_x + x;
            int img_y = draw_y + y;
            if (img_x < 0 || img_x >= img_width || img_y < 0 || img_y >= img_height)
                continue;
            int out_idx = (img_y * img_width + img_x) * 3;
            int rot_idx = y * new_w + x;
            unsigned char glyph_alpha = rotated[rot_idx]; // 0～255
            float alpha = glyph_alpha / 255.0f;
            // 前景は白 (255,255,255) と仮定
            for (int c = 0; c < 3; c++) {
                // シンプルなアルファブレンディング: out = fg * alpha + bg * (1-alpha)
                image[out_idx + c] = (unsigned char)(255 * alpha + image[out_idx + c] * (1.0f - alpha));
            }
        }
    }
}

void ProjectionMapGenerator::draw_char(TrackingVector<unsigned char>&image, int img_width, int img_height,
				       int pos_x, int pos_y, int codepoint,
				       stbtt_fontinfo *font, float scale)
{
      // グリフビットマップ取得
    int width, height, xoffset, yoffset;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(
        font, 
        0,         // x_shift=0
        scale,
        codepoint,
        &width, 
        &height, 
        &xoffset, 
        &yoffset);

    if (!bitmap) return;

    // ここでは、(pos_x, pos_y) を「ベースライン左端」と解釈し、
    // stb_truetype が返す xoffset, yoffset を加えた位置にビットマップを置く。
    //
    //  - xoffset: グリフビットマップの左端がベースラインからどれだけずれるか
    //  - yoffset: グリフビットマップの上端がベースラインからどれだけずれるか（多くの場合マイナス）

    int draw_x = pos_x + xoffset; 
    int draw_y = pos_y + yoffset; 

    // 描画(アルファブレンド)
    for (int by = 0; by < height; by++) {
        for (int bx = 0; bx < width; bx++) {
            int img_x = draw_x + bx;
            int img_y = draw_y + by;
            if (img_x < 0 || img_x >= img_width ||
                img_y < 0 || img_y >= img_height)
            {
                continue;
            }

            // bitmap[by * width + bx] は 0～255 のアルファ値
            unsigned char glyph_alpha = bitmap[by * width + bx];
            if (glyph_alpha == 0) continue; // 透過ならスキップ

            int index = img_y * img_width + img_x;
            unsigned char dest_r = image[3*index + 0];
            unsigned char dest_g = image[3*index + 1];
            unsigned char dest_b = image[3*index + 2];

            // 前景は白(255)でブレンド
            float a = glyph_alpha / 255.0f;
            unsigned char out_r = (unsigned char)(255 * a + dest_r * (1.0f - a));
            unsigned char out_g = (unsigned char)(255 * a + dest_g * (1.0f - a));
            unsigned char out_b = (unsigned char)(255 * a + dest_b * (1.0f - a));

            image[3*index + 0] = out_r;
            image[3*index + 1] = out_g;
            image[3*index + 2] = out_b;
        }
    }

    stbtt_FreeBitmap(bitmap, NULL);
}

void ProjectionMapGenerator::draw_text_rotated_on_image(TrackingVector<unsigned char>& image,
                                                        int img_width, int img_height,
                                                        int center_x, int center_y,
                                                        const char* text,
                                                        stbtt_fontinfo *font, float charpixelsize)
{
    int len = (int)strlen(text);
    if(len == 0)
        return;
    
    // スケールの計算
    float scale = stbtt_ScaleForPixelHeight(font, charpixelsize);

    // 90度回転した場合、各文字の描画サイズは (new_w, new_h) = (h, w)
    std::vector<int> char_widths(len), char_heights(len);
    for (int i = 0; i < len; i++) {
        int cp = text[i];
        int w, h, xoffset, yoffset;
        // 取得するのは通常の文字ビットマップ
        unsigned char* bmp = stbtt_GetCodepointBitmap(font, 0, scale, cp, &w, &h, &xoffset, &yoffset);
        if(bmp) {
            // 回転後の幅は h、回転後の高さは w
            char_widths[i] = h;
            char_heights[i] = w;
            stbtt_FreeBitmap(bmp, NULL);
        }
    }
    
    // 文字列全体のサイズ（回転後）：横幅 = total_width, 高さ = max(char_heights)
    int total_height = 0;
    int max_width = 0;
    for (int i = 0; i < len; i++) {
        total_height += char_heights[i];
        if (char_widths[i] > max_width) {
            max_width = char_widths[i];
        }
    }
    
    // 中央配置のための開始位置
    int start_x = center_x - max_width / 2;
    int start_y = center_y + total_height / 2; // ここは中央配置の目安。場合に応じて調整
    
    int curr_y = start_y;
    for (int i = 0; i < len; i++) {
        int cp = text[i];
        // 各文字を、回転した状態で描画
        // draw_rotated_char では、pos_x, pos_y は文字の中心位置として解釈されるので、
        // 文字の中心を計算して渡す。
        int char_center_x = start_x + char_heights[i] / 2;
        int char_center_y = curr_y + char_widths[i] / 2;
        draw_rotated_char(image, img_width, img_height, char_center_x, char_center_y, cp, font, scale);
        curr_y -= char_heights[i]; // 次の文字の開始位置
    }
}

void ProjectionMapGenerator::drawTextBaselineAndRotate90(TrackingVector<unsigned char>& image, 
							 int img_width, int img_height,
							 int center_x, int center_y,
							 const char* text,
							 stbtt_fontinfo* font, float pixelSize)
{
  if (!text || !*text) return; // 空文字は描画しない

  // 1) フォントのスケール & VMetrics (アセント, ディセント, ラインギャップ)
  float scale = stbtt_ScaleForPixelHeight(font, pixelSize);

  int ascent, descent, lineGap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);
  //float fAscent  = ascent  * scale;
  //float fDescent = descent * scale;

  // 文字列全体の境界を求めるために、まず「計測パス」を回す
  float x = 0.0f; // ペンの x 位置(ベースライン上)
  float y = 0.0f; // 今回、ベースラインを y=0 として扱う

  float min_x =  1e9f;
  float max_x = -1e9f;
  float min_y =  1e9f;
  float max_y = -1e9f;

  for (int i = 0; text[i]; i++) {
    int code = (unsigned char)text[i];

    // 水平メトリクス（advanceWidth, leftSideBearing）
    int advanceWidth, leftBearing;
    stbtt_GetCodepointHMetrics(font, code, &advanceWidth, &leftBearing);

    // 実際にビットマップ化したときの bbox (x0,y0 ~ x1,y1)
    int c_x0, c_y0, c_x1, c_y1;
    stbtt_GetCodepointBitmapBox(font, code, scale, scale, 
				&c_x0, &c_y0, &c_x1, &c_y1);

    // グリフの左上は ( x + leftSideBearing*scale, y + c_y0 )
    float gx0 = x + (leftBearing * scale);
    float gy0 = y + c_y0;
    float gx1 = gx0 + (c_x1 - c_x0);
    float gy1 = gy0 + (c_y1 - c_y0);

    // min/max 更新
    if (gx0 < min_x) min_x = gx0;
    if (gx1 > max_x) max_x = gx1;
    if (gy0 < min_y) min_y = gy0;
    if (gy1 > max_y) max_y = gy1;

    // 次の文字へ進める
    x += (advanceWidth * scale);

    // ※カーニングを考慮するならここで:
    //   if (text[i+1]) {
    //       x += scale * stbtt_GetCodepointKernAdvance(font, code, (unsigned char)text[i+1]);
    //   }
  }

  // テキスト全体のバウンディングボックス（横書き）
  int textWidth  = (int)std::ceil(max_x - min_x);
  int textHeight = (int)std::ceil(max_y - min_y);
  if (textWidth <= 0 || textHeight <= 0) return;

  // 2) 横書き用の一時バッファ(グレースケール: アルファ値のみ格納)
  std::vector<unsigned char> temp(textWidth * textHeight, 0);

  // 3) 二度目のパスで実際に各文字ビットマップを合成
  //    BBox の min_x,min_y がマイナスかもしれないので offset で補正
  float offsetX = -min_x;
  float offsetY = -min_y;

  x = 0.0f;
  y = 0.0f;

  for (int i = 0; text[i]; i++) {
    int code = (unsigned char)text[i];
    int advanceWidth, leftBearing;
    stbtt_GetCodepointHMetrics(font, code, &advanceWidth, &leftBearing);

    int c_x0, c_y0, c_x1, c_y1;
    stbtt_GetCodepointBitmapBox(font, code, scale, scale, 
				&c_x0, &c_y0, &c_x1, &c_y1);

    // グリフ開始座標(左上) in float
    float gx = x + leftBearing * scale;
    float gy = y + c_y0;

    // integer に丸める
    int dstX = (int)std::floor(gx + offsetX);
    int dstY = (int)std::floor(gy + offsetY);

    // stbtt_GetCodepointBitmap でアルファのみのビットマップを生成
    int bw, bh; // 実際の幅高さ
    unsigned char* glyphBMP = stbtt_GetCodepointBitmap(
						       font,
						       scale, scale, 
						       code,
						       &bw, &bh, 
						       /*xoff=*/nullptr, /*yoff=*/nullptr);

    // バッファにブレンド（ここでは単に上書き）
    // glyphBMP は 0..255 のアルファ値が連続
    for (int row = 0; row < bh; row++) {
      for (int col = 0; col < bw; col++) {
	int px = dstX + col;
	int py = dstY + row;
	if (px < 0 || px >= textWidth || py < 0 || py >= textHeight) {
	  continue;
	}
	unsigned char alpha = glyphBMP[row * bw + col];
	temp[py * textWidth + px] = alpha;
      }
    }

    // ビットマップ開放
    stbtt_FreeBitmap(glyphBMP, nullptr);

    // 次文字へ
    x += (advanceWidth * scale);

    // (カーニングを考慮するなら同様に)
  }

  int rotatedW = textHeight; 
  int rotatedH = textWidth;
  std::vector<unsigned char> rotated(rotatedW * rotatedH, 0);
  
  for (int oldY = 0; oldY < textHeight; oldY++) {
    for (int oldX = 0; oldX < textWidth; oldX++) {
      unsigned char val = temp[oldY * textWidth + oldX];
      if (val == 0) continue;
      
      int newX = oldY;                        
      int newY = (textWidth - 1) - oldX;      
      rotated[newY * rotatedW + newX] = val;
    }
  }

  // 5) 回転結果を outImage へ白文字 + アルファブレンドで合成
  //    (centerX,centerY) を画像の中心とし、回転後画像の真ん中がそこに来るように貼り付ける
  int halfW = rotatedW / 2;
  int halfH = rotatedH / 2;

  for (int ry = 0; ry < rotatedH; ry++) {
    for (int rx = 0; rx < rotatedW; rx++) {
      int tgtX = center_x + (rx - halfW);
      int tgtY = center_y + (ry - halfH);
      if (tgtX < 0 || tgtX >= img_width ||
	  tgtY < 0 || tgtY >= img_height) 
	{
	  continue;
	}

      unsigned char alpha = rotated[ry * rotatedW + rx];
      if (alpha == 0) continue;

      // アルファブレンド: 前景(白), 背景(outImage[tgtIdx..+2])
      float a = alpha / 255.0f;
      int tgtIdx = (tgtY * img_width + tgtX) * 3;
      for (int c = 0; c < 3; c++) {
	float bg = image[tgtIdx + c];   // 背景
	float fg = 255.0f;                // 白
	float outC = fg * a + bg * (1.0f - a);
	image[tgtIdx + c] = (unsigned char)(outC);
      }
    }
  }
}


// 数値（整数や浮動小数点数）を文字列に変換し、1文字ずつ描画する関数
void ProjectionMapGenerator::draw_value_on_image(TrackingVector<unsigned char>& image, int img_width, int img_height,
                           int pos_x, int pos_y, double value,
                           stbtt_fontinfo *font, float charpixelsize, const char *format)
{
    // format に "%.2f" などのフォーマットを指定すると、浮動小数点数が対応可能
    char labelStr[64];
    snprintf(labelStr, sizeof(labelStr), format, value); 
    int label_len = strlen(labelStr);
    if (label_len == 0) return;

    // フォントスケール
    float scale = stbtt_ScaleForPixelHeight(font, charpixelsize);

    // 文字列のピクセル幅（advanceWidth から計算）を合計
    int total_width = 0;
    int *char_widths = (int *)malloc(label_len * sizeof(int));
    if (!char_widths) return;

    for (int i = 0; i < label_len; i++) {
        int cp = (unsigned char)labelStr[i];
        // 水平メトリクス取得 (advanceWidth, leftBearing)
        int advanceWidth, leftBearing;
        stbtt_GetCodepointHMetrics(font, cp, &advanceWidth, &leftBearing);

        // 実際の描画幅 (ピクセル) はおおむね advanceWidth * scale
        int glyph_width = (int)(advanceWidth * scale);

        char_widths[i] = glyph_width;
        total_width += glyph_width;

        // ※もしカーニングを考慮したい場合は
        //   if (i < label_len - 1) {
        //       int next_cp = (unsigned char)labelStr[i+1];
        //       total_width += (int)(stbtt_GetCodepointKernAdvance(font, cp, next_cp) * scale);
        //   }
    }

    // 横幅をもとにテキストを中央寄せするための開始X座標
    int start_x = pos_x - total_width / 2;
    // 今回、pos_y はベースラインの位置とする
    int base_y = pos_y;

    // 実際に文字を並べる
    int curr_x = start_x;
    for (int i = 0; i < label_len; i++) {
        int cp = (unsigned char)labelStr[i];

        // draw_char() では、(curr_x, base_y) を
        // 「グリフのベースライン左端」として扱うようにする
        draw_char(image, img_width, img_height,
                  curr_x, base_y, 
                  cp, font, scale);

        // 次の文字へ進む
        curr_x += char_widths[i];

        // ※カーニングを入れたい場合
        // if (i < label_len - 1) {
        //   int next_cp = (unsigned char)labelStr[i+1];
        //   curr_x += (int)(stbtt_GetCodepointKernAdvance(font, cp, next_cp) * scale);
        // }
    }

    free(char_widths);
}

void ProjectionMapGenerator::draw_text_label_centered(TrackingVector<unsigned char>& image,
                                                      int img_width, int img_height,
                                                      int pos_x, int pos_y,
                                                      const char* text,
                                                      stbtt_fontinfo* font,
                                                      float charpixelsize)
{
    if (!text || strlen(text) == 0) return;

    int label_len = strlen(text);
    float scale = stbtt_ScaleForPixelHeight(font, charpixelsize);

    // 文字ごとの幅を事前に取得
    int* char_widths = (int*)malloc(label_len * sizeof(int));
    if (!char_widths) return;

    int total_width = 0;
    for (int i = 0; i < label_len; i++) {
        int cp = (unsigned char)text[i];
        int advanceWidth, leftBearing;
        stbtt_GetCodepointHMetrics(font, cp, &advanceWidth, &leftBearing);

        int glyph_width = (int)(advanceWidth * scale);
        char_widths[i] = glyph_width;
        total_width += glyph_width;

        // カーニング考慮するならここで加える
        // if (i < label_len - 1) {
        //     int next_cp = (unsigned char)text[i + 1];
        //     total_width += (int)(stbtt_GetCodepointKernAdvance(font, cp, next_cp) * scale);
        // }
    }

    // 中心から描画開始位置を決める
    int start_x = pos_x - total_width / 2;
    int base_y = pos_y;

    // 描画
    int curr_x = start_x;
    for (int i = 0; i < label_len; i++) {
        int cp = (unsigned char)text[i];
        draw_char(image, img_width, img_height, curr_x, base_y, cp, font, scale);
        curr_x += char_widths[i];
    }

    free(char_widths);
}

GLuint ProjectionMapGenerator::CreateTexture2D(const unsigned char* data, int width, int height)
{
  // -------------- ここが修正ポイント (1) --------------
  // テクスチャへピクセル転送する前に、ピクセル行のアライメントを1に設定
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  // -----------------------------------------------------
  
  GLuint texID;
  glGenTextures(1, &texID);
  glBindTexture(GL_TEXTURE_2D, texID);

  // ここでは RGB8 画像を想定
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
	       GL_RGB, GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glBindTexture(GL_TEXTURE_2D, 0);

  // -------------- ここが修正ポイント (2) --------------
  // 必要に応じて、後の描画処理に影響しないようデフォルト(4)に戻す
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  // -----------------------------------------------------
    
  return texID;
}
