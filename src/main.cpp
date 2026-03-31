 // main.cpp
// -------------
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "implot.h"
#include <tuple>
#include <limits>
#include <future>
#include <cstddef>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <unordered_set>

// GLM 関連
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// 標準ライブラリ
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <cstdio>
#include <string>
#include <cstring>
#include <sstream>
#include <cmath>

#include <mutex>

#include "main.h"
#include "colormap_defs.h"
#include "UI.h"
#include "camera.h"
#include "object.h"
#include "FindClumps/find_clumps.h"
#include "compute_radial_profile.h"
#include "compute_2D_histogram.h"
#include "make_2D_projection_map.h"
#include "FileIO/file_io.h"

#ifdef STREAM_LINE
#include "StreamLine/stream_line_new.h"
#endif

#ifdef GEOMETRICAL_ANALYSIS
#include "GeometricAnalysis/ellipse_fitter.h"
#include "GeometricAnalysis/DiskRadius.hpp"
#endif

#ifndef NONATIVEFILEDIALOG
#include <nfd.h>
#endif

#include "shader_utils.h"
#include "shader_sources.h"

#include "app_services.h"
AppServices gAppServices;

#include "ui_state.h"
SettingsRuntimeState gSettingsRuntimeState;
RenderRuntimeState gRenderRuntimeState;

#include "particle_renderer.h"
#include "object_renderer.h"
#include "field_renderer.h"
ColorbarGizmo gColorbar;

#include "interaction_utils.h"

#include "imgui_context.h"
#include "window_context.h"
WindowContext gWindowContext;

#include "render_programs.h"
#include "render_resources.h"

#define USE_LETTERBOX 1

#ifdef USE_CONVEX_HULL
#include "FindClumps/create_convex_hull.h"
ConvexHullGenerator* gConvexHullGenerator = nullptr;
#endif

#ifdef PYTHON_BRIDGE
#include "PythonBridge/BridgeAdapter.h"
#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/ShmLayout.h"
#endif

std::size_t g_totalAllocated = 0;
std::mutex g_mutex;

struct CameraContext camCtx;

FileInfo *gFileInfo;
ParticleArray *P;

#include "particle_visual_config.h"
ParticleVisualConfig gParticleVisualConfig;

std::vector<size_t> g_labelIndices;  // 毎回 ImGui 描画に使う

struct ProjectionPreviewGLState {
  GLuint tex = 0;
  int width = 0;
  int height = 0;
  uint64_t uploadedVersion = 0;
};

ProjectionPreviewGLState gProjectionPreviewGLState;

#ifdef ISO_CONTOUR
#include "IsoSurface/IsoSurfaceGenerator.h"
#endif

#ifdef VOLUME_RENDERING
#include "BVH/BVH.hpp"
#include "VolumeRendering/tau_sph.h"
#include "VolumeRendering/TransferFunctionEditor.hpp"
#include "VolumeRendering/OpacityComputer.hpp"

struct TBO { GLuint buf=0, tex=0; };
struct GpuOctree {
  TBO nodeMin, nodeMax;     // GL_RGBA32F
  TBO cornerLo, cornerHi;     // GL_RGBA32F
  TBO texChildA, texChildB;   // GL_RGBA32I (isamplerBuffer)
  int root;
  int nNodes;
} gGPUOctTree;
#endif

bool flagShowCoordinates = true;
InteractionState gInteractionState;

// ------------------------------
// マウスコールバック
// ------------------------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
  if (ImGui::GetIO().WantCaptureMouse || camCtx.stopCameraMode)
    return;

  bool leftPressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
  bool shiftPressed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

  if (!leftPressed) {
    gInteractionState.resetMouse();
    return;
  }

  if (gInteractionState.firstMouse()) {
    gInteractionState.setMousePosition((float)xpos, (float)ypos);
    return;
  }

  float oldX = gInteractionState.lastX();
  float oldY = gInteractionState.lastY();

  float xoffset = (float)xpos - oldX;
  float yoffset = oldY - (float)ypos;

  gInteractionState.setMousePosition((float)xpos, (float)ypos);

  if (shiftPressed) {
    ApplyCameraPan(camCtx, xoffset, yoffset);
    return;
  }

#if defined(ROTATE_ARCBALL)
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  ApplyCameraArcballRotation(camCtx,
                             oldX, oldY,
                             (float)xpos, (float)ypos,
                             displaySize.x, displaySize.y);
#elif defined(ROTATE_QUATERNION)
  float sensitivity = 0.1f;

  float yawAngle = glm::radians(-xoffset * sensitivity);
  glm::quat qYaw = glm::angleAxis(yawAngle, glm::vec3(0.0f, 1.0f, 0.0f));

  glm::vec3 right = camCtx.cameraOrientation * glm::vec3(1.0f, 0.0f, 0.0f);
  float pitchAngle = glm::radians(yoffset * sensitivity);
  glm::quat qPitch = glm::angleAxis(pitchAngle, right);

  camCtx.cameraOrientation = glm::normalize(qYaw * camCtx.cameraOrientation);
  camCtx.cameraOrientation = glm::normalize(camCtx.cameraOrientation * qPitch);

  glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
  camCtx.cameraPos = camCtx.cameraTarget - direction * camCtx.distance;
  camCtx.cameraUp  = camCtx.cameraOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
#else
  float sensitivity = 0.1f;
  xoffset *= sensitivity;
  yoffset *= sensitivity;

  yaw   += xoffset;
  pitch += yoffset;

  if (pitch > 90.0f) {
    pitch = 180.0f - pitch;
    yaw += 180.0f;
  }
  if (pitch < -90.0f) {
    pitch = -180.0f - pitch;
    yaw += 180.0f;
  }
  if (pitch > 89.0f)  pitch = 89.0f;
  if (pitch < -89.0f) pitch = -89.0f;

  glm::vec3 direction;
  direction.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
  direction.y = sin(glm::radians(pitch));
  direction.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
  direction = glm::normalize(direction);
  camCtx.cameraPos = camCtx.cameraTarget - direction * camCtx.distance;
#endif
}

void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset)
{
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  ApplyCameraZoom(camCtx,
                  static_cast<float>(yoffset),
                  gSettingsRuntimeState.minZoom,
                  gSettingsRuntimeState.maxZoom);
}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

// ------------------------------
// normalize 用グローバル変数
// ------------------------------
// 例：利用可能なカラーマップのテクスチャID（これらは InitColorMaps() 内で作成）
GLuint jetTex, viridisTex, plasmaTex;

RenderPrograms gRenderPrograms;
RenderResources gRenderResources;

#ifdef VOLUME_RENDERING
lbvh::BuildResult gBVHresult;

static void UploadLUT_1D(GLuint& texLUT, const std::vector<float>& lutData) {
    if (!texLUT) glGenTextures(1, &texLUT);
    glBindTexture(GL_TEXTURE_1D, texLUT);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F,
                 (GLsizei)lutData.size(), 0, GL_RED, GL_FLOAT, lutData.data());
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_1D, 0);
}

static TBO makeTBO(GLenum internalFormat, GLsizeiptr bytes, const void* data){
    TBO t; glGenBuffers(1,&t.buf);
    glBindBuffer(GL_TEXTURE_BUFFER, t.buf);
    glBufferData(GL_TEXTURE_BUFFER, bytes, data, GL_STATIC_DRAW);  // 4.1 OK
    glGenTextures(1,&t.tex);
    glBindTexture(GL_TEXTURE_BUFFER, t.tex);
    glTexBuffer(GL_TEXTURE_BUFFER, internalFormat, t.buf);         // フォーマットを決める
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    return t;
}

static void bindTBO(GLuint prog, const char* uniformName, GLuint texture, GLint unit){
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_BUFFER, texture);
    glUniform1i(glGetUniformLocation(prog, uniformName), unit);
}

struct NodeCh  { int left,right,first,count; };

struct BVHGpuPack {
  TBO nodeMin, nodeMax;     // GL_RGBA32F
  TBO nodeChild;            // GL_RGBA32I (isamplerBuffer)
  TBO particles;            // GL_RGBA32F (pos.xyz, radius)
  TBO partSigma;            // GL_R32F
  // 必要なら per-particle sigma も
  int root;
  int nNodes, nLeaves;
};

BVHGpuPack G_bvh;

static BVHGpuPack uploadBVH_TBO(lbvh::BuildResult bvh){
    const auto& N = bvh.nodes;
    const auto& P = bvh.gpu;
    
    BVHGpuPack G{};
    G.nNodes  = (int)N.size();
    G.nLeaves = (int)P.size();
    G.root    = bvh.root;

    std::vector<glm::vec4> vMin(G.nNodes), vMax(G.nNodes);
    std::vector<NodeCh>    vCh (G.nNodes);

    for(int i=0;i<G.nNodes;i++){
        const auto& n=N[i];
        vMin[i]=glm::vec4(n.bmin[0], n.bmin[1], n.bmin[2], n.sigma_avg);
        vMax[i]=glm::vec4(n.bmax[0], n.bmax[1], n.bmax[2], n.sigma_max);
        vCh[i] ={n.left,n.right,n.first,n.count};
    }

    // 粒子 (pos.xyz, radius)
    std::vector<glm::vec4> vPart(G.nLeaves);
    std::vector<float> vPartSigma(G.nLeaves);
    for(int i=0;i<G.nLeaves;i++){
        const auto& p=P[i];
        vPart[i] = glm::vec4(p.pos[0],p.pos[1],p.pos[2],p.pos[3]);
	vPartSigma[i] = p.sigma0;
    }

    G.nodeMin  = makeTBO(GL_RGBA32F, sizeof(glm::vec4)*vMin.size(), vMin.data());
    G.nodeMax  = makeTBO(GL_RGBA32F, sizeof(glm::vec4)*vMax.size(), vMax.data());
    G.nodeChild= makeTBO(GL_RGBA32I, sizeof(NodeCh   )*vCh .size(), vCh .data());
    G.particles= makeTBO(GL_RGBA32F, sizeof(glm::vec4)*vPart.size(),vPart.data());
    G.partSigma= makeTBO(GL_R32F,    sizeof(float    )*vPartSigma.size(),vPartSigma.data());
    
    return G;
}

void uploadOctTree_TBO(OctTreeCPUState& cpu, GpuOctree& gpu, const RhoSigmaLUT& rho2sigma){
  if (!cpu.cpuTree || cpu.order.empty() || cpu.info.size() != cpu.order.size()) {
    return;
  }

  const int N = (int)cpu.order.size();
  std::vector<glm::vec4> vMin(N), vMax(N);
  std::vector<glm::ivec4> vChA(N), vChB(N);
  std::vector<glm::vec4> vCornerLo(N), vCornerHi(N);

  for (int i=0;i<N;++i){
    const auto* n = cpu.order[i];
    const auto& ni= cpu.info[i];

    vMin[i] = glm::vec4(n->box.min, ni.sigmaAvg);
    vMax[i] = glm::vec4(n->box.max, ni.sigmaMax);
    vChA[i] = glm::ivec4(ni.child[0], ni.child[1], ni.child[2], ni.child[3]);
    vChB[i] = glm::ivec4(ni.child[4], ni.child[5], ni.child[6], ni.child[7]);

    float sig[8];
    for (int c=0;c<8;++c) {
      float rho = n->edgeValues[c];
      sig[c] = rho2sigma(rho);
    }
    vCornerLo[i] = glm::vec4(sig[0], sig[1], sig[2], sig[3]);
    vCornerHi[i] = glm::vec4(sig[4], sig[5], sig[6], sig[7]);
  }

  GpuOctree G{};
  G.nodeMin   = makeTBO(GL_RGBA32F, sizeof(glm::vec4)*vMin.size(), vMin.data());
  G.nodeMax   = makeTBO(GL_RGBA32F, sizeof(glm::vec4)*vMax.size(), vMax.data());
  G.texChildA = makeTBO(GL_RGBA32I, sizeof(glm::ivec4)*vChA.size(), vChA.data());
  G.texChildB = makeTBO(GL_RGBA32I, sizeof(glm::ivec4)*vChB.size(), vChB.data());
  G.cornerLo  = makeTBO(GL_RGBA32F, sizeof(glm::vec4)*vCornerLo.size(), vCornerLo.data());
  G.cornerHi  = makeTBO(GL_RGBA32F, sizeof(glm::vec4)*vCornerHi.size(), vCornerHi.data());
  
  G.root = 0;
  G.nNodes = N;

  gpu = G;
}
#endif

std::pair<std::string, int> convertFilenameToFormatAndExtractNumber(const std::string& filename)
{
  size_t dotPos = filename.find_last_of('.');
  std::string basename = (dotPos == std::string::npos) ? filename : filename.substr(0, dotPos);
  std::string extension = (dotPos == std::string::npos) ? "" : filename.substr(dotPos); 

  // 最後の数字がある位置を探す
    size_t pos = basename.find_last_of("0123456789");
    if (pos == std::string::npos)
        return std::make_pair(filename, 0); // 数字が見つからなければそのまま返す

    // 数字部分の開始位置を後ろから辿って求める
    size_t numEnd = pos;
    size_t numStart = pos;
    while (numStart > 0 && std::isdigit(basename[numStart - 1])) {
        numStart--;
    }
    size_t numLen = numEnd - numStart + 1;

    // ファイル名の前半部分（数字部分以前）を取得
    std::string prefix = basename.substr(0, numStart);
    // この例では数字部分以降の文字列は取り除く（必要に応じて拡張子などを扱えます）
    std::string suffix = "";

    // 桁数に合わせたフォーマット指定子を作成（例: 3桁なら "%03d"）
    std::string formatSpecifier = "%" + std::string("0") + std::to_string(numLen) + "d";

    std::string newFormat = prefix + formatSpecifier + suffix + extension;

    // 数字部分を整数に変換
    int fileNumber = -1;
    try {
        fileNumber = std::stoi(basename.substr(numStart, numLen));
    } catch (const std::exception& e) {
        fileNumber = -1;
    }
    
    return std::make_pair(newFormat, fileNumber);
}

void ParticleArray::recomputeHaloPositionsFromParticles(bool useMassWeight,
                                                        bool useOriginalPos,
                                                        int  minParticles)
{
  if (!haloIDsLoaded) return;
  if (haloIDs.size() != Haloes.size()) return;

  // ID辞書が必要
  if (particleBlock.id2indexDirty) particleBlock.rebuildIdIndex();

  const size_t nHalos = Haloes.size();

  for (size_t ih = 0; ih < nHalos; ++ih) {
    const auto& ids = haloIDs[ih];
    if ((int)ids.size() < minParticles) continue;

    double sx = 0.0, sy = 0.0, sz = 0.0;
    double sw = 0.0;

    for (uint64_t pid : ids) {
      const size_t ip = particleBlock.findIndexByID(pid);
      if (ip == (size_t)-1) continue;

      const ParticleData& p = particleBlock.particles[ip];

      const float* x = useOriginalPos ? p.original_pos : p.pos;

      double w = 1.0;
      if (useMassWeight) 
        w = (p.mass > 0.0f) ? (double)p.mass : 1.0;      

      sx += w * (double)x[0];
      sy += w * (double)x[1];
      sz += w * (double)x[2];
      sw += w;
    }

    if (sw <= 0.0) continue;

    const float cx = (float)(sx / sw);
    const float cy = (float)(sy / sw);
    const float cz = (float)(sz / sw);

    Haloes[ih].GroupPos[0] = cx;
    Haloes[ih].GroupPos[1] = cy;
    Haloes[ih].GroupPos[2] = cz;
  }
}

void UpdateLabelIndicesIfNeeded()
{
  if(P->flagParticleIndexDirty == false){
    if (glm::distance(camCtx.cameraPos, gSettingsRuntimeState.lastCameraPos) < gSettingsRuntimeState.moveThreshold)
      return;                     // 動いていなければ何もしない
  }
  
  gSettingsRuntimeState.lastCameraPos = camCtx.cameraPos;
  g_labelIndices.clear();
  
  float query_pt[3] = { camCtx.cameraPos.x,
			camCtx.cameraPos.y,
			camCtx.cameraPos.z };

  const float radius2 = gSettingsRuntimeState.queryRadius * gSettingsRuntimeState.queryRadius;

  struct Hit { size_t idx; float dist2; };
  std::vector<Hit> hits;

  for (size_t i = 0; i < P->particleBlock.particles.size(); ++i) {
    auto &p = P->particleBlock.particles[i];

    if(p.type <= 2)
      continue;
    
    float dist2 = (p.pos[0] - query_pt[0])*(p.pos[0] - query_pt[0]) +
      (p.pos[1] - query_pt[1])*(p.pos[1] - query_pt[1]) +
      (p.pos[2] - query_pt[2])*(p.pos[2] - query_pt[2]);
    
    if(dist2 > radius2)
      continue;

    hits.push_back({i, dist2});
  }
  
  std::sort(hits.begin(), hits.end(),
	    [](const Hit& a, const Hit& b){ return a.dist2 < b.dist2; });

  g_labelIndices.clear();

  for (size_t k = 0; k < hits.size() && k < 50; ++k)
    g_labelIndices.push_back(hits[k].idx);
  
  P->flagParticleIndexDirty = false;
}

void DrawParticleLabels(const glm::mat4& view, const glm::mat4& proj)
{
  if(P->particleBlock.particles.size() == 0){
    //to avoid an error when the particle is not loaded.
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  // → Retina や高 DPI の場合の「pixels ÷ points」の比
  float scaleX = io.DisplayFramebufferScale.x;
  float scaleY = io.DisplayFramebufferScale.y;  
  float FBH = io.DisplaySize.y * scaleY;
  
  //ImDrawList* draw = ImGui::GetForegroundDrawList();
  ImDrawList* draw = ImGui::GetBackgroundDrawList(); 
  
  for (size_t idx : g_labelIndices) {
    const auto& p = P->particleBlock.particles[idx];

    glm::vec4 clip = proj * view *
      glm::vec4(p.pos[0], p.pos[1], p.pos[2], 1.0f);

    if (clip.w <= 0.f) continue;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (abs(ndc.x) > 1.f || abs(ndc.y) > 1.f) continue;

#ifdef USE_LETTERBOX
    float px = gWindowContext.viewportX() + (ndc.x * 0.5f + 0.5f) * float(gWindowContext.viewportWidth());
    float py = gWindowContext.viewportY() + (ndc.y * 0.5f + 0.5f) * float(gWindowContext.viewportHeight());

    // px,py は pixels 単位なので
    float sx = px / scaleX;
    float sy = (FBH - py) / scaleY; // ImGuiは左上原点・Y下増加なので反転
#else
    ImGuiIO& io = ImGui::GetIO();
    float sx =    (ndc.x * 0.5f + 0.5f) * io.DisplaySize.x;
    float sy =    (1.0f - (ndc.y * 0.5f + 0.5f)) * io.DisplaySize.y;
#endif
    
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%d", p.ID);
    draw->AddRectFilled(ImVec2(sx-2, sy-2), ImVec2(sx+ImGui::CalcTextSize(buf).x+2, sy+ImGui::GetFontSize()+2),
			IM_COL32(0, 0, 0, 128), 2.0f);
    draw->AddText(ImVec2(sx, sy), IM_COL32_WHITE, buf);
  }
}

// 希望する色バーのサイズと余白（ピクセル単位）
const float colorBarWidth = 400.0f;   // 色バーの横幅
const float colorBarHeight = 40.0f;   // 色バーの高さ
const float margin = 40.0f;           // 画面右下からの余白

// ImGui のディスプレイサイズを利用して色バーの四隅のピクセル座標を計算する関数
void ComputeColorBarPixelCoords(float &left, float &right, float &top, float &bottom) {
#ifdef USE_LETTERBOX
  // 実際のレンダリング領域（レターボックス）のサイズを使う
  float effectiveWidth = static_cast<float>(gWindowContext.viewportWidth());
  float effectiveHeight = static_cast<float>(gWindowContext.viewportHeight());
#else
  ImGuiIO& io = ImGui::GetIO();
  float effectiveWidth = io.DisplaySize.x/io.DisplayFramebufferScale.x;
  float effectiveHeight = io.DisplaySize.y/io.DisplayFramebufferScale.y;
#endif

  left   = effectiveWidth - colorBarWidth - margin;
  right  = effectiveWidth - margin;
  bottom = effectiveHeight - margin;
  top    = effectiveHeight - colorBarHeight - margin;
}

// インスタンスVBOを更新する（すべての粒子の位置と速度をアップロードする例）
static std::vector<float> BuildVelocityInstanceData(const TrackingVector<ParticleData>& particles,
                                                    const SettingsRuntimeState& settings)
{
  std::vector<float> instanceData;
  instanceData.reserve(particles.size() * 6);

  const int stride = (settings.velocitySubtraction > 0)
                   ? settings.velocitySubtraction : 1;

  for (size_t i = 0; i < particles.size(); ++i) {
    if (i % stride != 0) continue;

    const auto& p = particles[i];

    instanceData.push_back(p.pos[0]);
    instanceData.push_back(p.pos[1]);
    instanceData.push_back(p.pos[2]);

    instanceData.push_back(p.vel[0]);
    instanceData.push_back(p.vel[1]);
    instanceData.push_back(p.vel[2]);
  }

  return instanceData;
}


void UpdateUI() {
  ShowTime(P->particleBlock.header.time);

  SettingsUIContext settingsCtx;
  settingsCtx.P = P;
  settingsCtx.fileInfo = gFileInfo;
  settingsCtx.camCtx = &camCtx;
  settingsCtx.particleVisual = &gParticleVisualConfig;
  settingsCtx.services = &gAppServices;
  settingsCtx.render = &gRenderRuntimeState;
  settingsCtx.cubeManager = &gCubeManager;
  
  ShowSettingsUI(settingsCtx, gSettingsRuntimeState);

  ShowCameraSettingsUI();

  gAppServices.clumpFind->ShowFindClumpsUI(P->particleBlock.particles, P->particleBlock.header);

#ifdef CLUMP_DATA_READ
  gAppServices.clumpFind->ReadAndShowClumpsUI(P, gFileInfo->currentFileIndex);
  gAppServices.clumpFind->showClumpChainList(P, gAppServices.projectionMap2D.get());
#endif
  
  gFileInfo->DrawFormatDialog();
#ifdef HAVE_HDF5
  gFileInfo->ShowHDF5FieldMappingDialog();
#endif

  const bool applied = DrawMaskWindow();
  if (applied) {
    MaskConfig cfg = MakeMaskConfigFromUI();
    gFileInfo->setMaskConfig(cfg);
  }
  
#ifdef VOLUME_RENDERING
  gAppServices.tf->showUI();
  gAppServices.volume.rho2sigma = gAppServices.tf->bakeLUT(1024);
#endif
  
  // 新たなウィンドウとしてトップ粒子リストを表示
  DrawTopParticlesUI(P, camCtx);
}


#ifdef VOLUME_RENDERING
GLuint gRtFbo = 0, gRtTex = 0;
int    gRtW   = 0, gRtH = 0;

static void CreateOrResizeRTFBO(int w, int h){
    if (w<=0 || h<=0) return;
    gRtW = w; gRtH = h;

    if (gRtTex == 0) glGenTextures(1, &gRtTex);
    glBindTexture(GL_TEXTURE_2D, gRtTex);
    // 16bit float推奨（αも使うのでRGBA）
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // ← アップスケール時のバイリニア
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (gRtFbo == 0) glGenFramebuffers(1, &gRtFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gRtFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gRtTex, 0);

    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[FBO] incomplete: 0x%x\n", st);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint wboitFbo=0, texAccum=0, texReveal=0;
int wboitW=0, wboitH=0;

void CreateOrResizeWBOITFBO(int W, int H){
    if (W<=0 || H<=0) return;
    wboitW = W; wboitH = H;
    
    if (!wboitFbo) glGenFramebuffers(1, &wboitFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, wboitFbo);

    if (!texAccum) glGenTextures(1, &texAccum);
    glBindTexture(GL_TEXTURE_2D, texAccum);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texAccum, 0);

    if (!texReveal) glGenTextures(1, &texReveal);
    glBindTexture(GL_TEXTURE_2D, texReveal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, W, H, 0, GL_RED, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texReveal, 0);

    GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);

    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr,"[WBOIT FBO incomplete] 0x%x\n", st);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

#endif

void UpdateColorbarGizmoFromCurrentState(ColorbarGizmo& gizmo)
{
  gizmo.visible       = true;
  gizmo.colormapIndex = gParticleVisualConfig.types[0].colormapIndex;
  gizmo.valueMin      = gParticleVisualConfig.types[0].colorMin;
  gizmo.valueMax      = gParticleVisualConfig.types[0].colorMax;
  gizmo.numTicks      = 5;

  ComputeColorBarPixelCoords(gizmo.layout.left_pixel,
                             gizmo.layout.right_pixel,
                             gizmo.layout.top_pixel,
                             gizmo.layout.bottom_pixel);

#ifdef USE_LETTERBOX
  gizmo.layout.offsetX = static_cast<float>(gWindowContext.viewportX());
  gizmo.layout.offsetY = static_cast<float>(gWindowContext.viewportY());

  gizmo.effectiveWidth  = static_cast<float>(gWindowContext.viewportWidth());
  gizmo.effectiveHeight = static_cast<float>(gWindowContext.viewportHeight());
#else
  gizmo.layout.offsetX = 0.0f;
  gizmo.layout.offsetY = 0.0f;

  ImGuiIO& io = ImGui::GetIO();
  gizmo.effectiveWidth  = io.DisplaySize.x / io.DisplayFramebufferScale.x;
  gizmo.effectiveHeight = io.DisplaySize.y / io.DisplayFramebufferScale.y;
#endif
}

void RenderScene() {
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  
  gParticleRenderer.sync(*P, gParticleVisualConfig);
  
  glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
  float fovY = 45.0f;
#ifdef USE_LETTERBOX
  //glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)SCR_WIDTH / SCR_HEIGHT, 0.1f, 100.0f);
  const float targetAspect = 1280.0f / 720.0f;  // 固定アスペクト比
  glm::mat4 projection = glm::perspective(glm::radians(fovY), targetAspect, 0.1f, 1000.0f);
  int viewportW = gWindowContext.viewportWidth();
  int viewportH = gWindowContext.viewportHeight();
#else
  // 方法A: 現在のウィンドウサイズからアスペクト比を計算（ウィンドウ全体を使用）
  int width, height;
  glfwGetFramebufferSize(window, &width, &height);
  float aspect = static_cast<float>(width) / static_cast<float>(height);
  glm::mat4 projection = glm::perspective(glm::radians(fovY), aspect, 0.1f, 1000.0f);
  int viewportW = width;
  int viewportH = height;
#endif
  glm::mat4 invProj = glm::inverse(projection);
  glm::mat4 invView = glm::inverse(view);
  glm::vec3 camForward = glm::normalize(glm::vec3(view[0][2], view[1][2], view[2][2])) * -1.0f;

  auto focalPxFromFovY = [](int H, float fovYrad){
    return (H > 0) ? (0.5f * (float)H) / tanf(0.5f * fovYrad) : 1.0f;
  };    
  float focalPx = focalPxFromFovY(viewportH, fovY);
  
  glm::mat4 model = glm::mat4(1.0f);

  gParticleRenderer.draw(gRenderPrograms.particle,
                         model,
                         view,
                         projection,
                         gParticleVisualConfig,
                         gColorbarRenderer,
                         gSettingsRuntimeState.flagHideAllParticles);
  
  if (gSettingsRuntimeState.showVelocityVectors) {
    if (P->velocityDirty) {
      auto instanceData = BuildVelocityInstanceData(P->particleBlock.particles,
						    gSettingsRuntimeState);
      gVelocityFieldRenderer.sync(instanceData, P->velocityDirty);
    }
    
    gVelocityFieldRenderer.draw(gRenderPrograms.velocityArrow,
				view,
				projection,
				gSettingsRuntimeState.arrowScale,
				gSettingsRuntimeState.useVelocityArrowLogScale);
  }
  

  if(gSettingsRuntimeState.flagShowSinkIDs){
    UpdateLabelIndicesIfNeeded(); //test
    DrawParticleLabels(view, projection); //test
  }

#ifdef ISO_CONTOUR
  gIsoContourRenderer.sync(gAppServices.isoContour.verts,
                           gAppServices.isoContour.inds,
                           gRenderRuntimeState.isocontour);

  gIsoContourRenderer.draw(gRenderPrograms.isocontour,
                           model,
                           view,
                           projection,
                           gRenderRuntimeState.isocontour);
#endif

  if (gRenderRuntimeState.cuboidAnnotations.cpuUpdated) {
    gCuboidAnnotationManager.clear();
    
    if (gRenderRuntimeState.cuboidAnnotations.show) {
      CuboidAnnotationObject obj;
      obj.cuboid = gAppServices.projectionMap2D->interactiveCuboid();

      obj.cuboid.edgeColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
      obj.highlightColor   = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
      obj.arrowColor       = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

      const int axis = gAppServices.projectionMap2D->params.selectedAxis;      
      if (axis == 0) obj.selectedAxis = CuboidAxis::X;
      else if (axis == 1) obj.selectedAxis = CuboidAxis::Y;
      else obj.selectedAxis = CuboidAxis::Z;

      obj.showAxisHighlight = true;
      obj.showArrow = true;
      obj.arrowLength = 0.2f;
      obj.arrowHeadLength = 0.05f;
      obj.arrowHeadWidth = 0.03f;
      obj.tag = "interactive_cuboid";

      gCuboidAnnotationManager.add(obj);
    }

    gRenderRuntimeState.cuboidAnnotations.cpuUpdated = false;
  }

  if (gRenderRuntimeState.cuboidAnnotations.show && gCuboidAnnotationManager.show()) {
    for (const auto& obj : gCuboidAnnotationManager.objects()) {
      gCuboidRenderer.draw(obj.cuboid,
			   gRenderPrograms.line,
			   view,
			   projection,
			   gRenderRuntimeState.cuboidAnnotations);

      gCuboidRenderer.drawHighlight(obj,
				    gRenderPrograms.line,
				    view,
				    projection,
				    gRenderRuntimeState.cuboidAnnotations);

      if (obj.showArrow) {
	ArrowObject arrow = buildArrowFromCuboidAnnotation(obj);
	arrow.color = obj.arrowColor;
	gArrowRenderer.draw(arrow,
			    gRenderPrograms.line,
			    view,
			    projection,
			    gRenderRuntimeState.cuboidAnnotations);
      }
    }
  }
  
  gEllipsoidRenderer.draw(gEllipsoidManager, gRenderPrograms.ellipsoid, view,  projection, gRenderRuntimeState.ellipsoids);
  gDiskRenderer.draw(gDiskManager, gRenderPrograms.disk, view, projection, gRenderRuntimeState.disks);  
  gLineRenderer.draw(gLineManager, gRenderPrograms.line, model, view, projection, gRenderRuntimeState.lines);

  gCubeRenderer.sync(gCubeManager, gRenderRuntimeState.cubes);
  gCubeRenderer.draw(gCubeManager, gRenderPrograms.cubic, view, projection, gRenderRuntimeState.cubes);

  gCrossGizmoRenderer.draw(gRenderPrograms.line, view, projection, camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp, gSettingsRuntimeState.crossSize);
  if (flagShowCoordinates) 
    gCoordAxesRenderer.draw(gRenderPrograms.coord, view);

  UpdateColorbarGizmoFromCurrentState(gColorbar);
  gColorbarRenderer.draw(gRenderPrograms.colorbar, gColorbar);
  gColorbarLabelRenderer.draw(gColorbar);
  
#ifdef VOLUME_RENDERING 
  if(gRenderRuntimeState.volume.show && gRenderRuntimeState.volume.flagRT == 1){    
    if(gRenderRuntimeState.volume.cpuUpdated){
      G_bvh = uploadBVH_TBO(gBVHresult);
      gRenderRuntimeState.volume.cpuUpdated = false;
    }
    
    int lowW = std::max(1.0f, gWindowContext.viewportWidth()  / gRenderRuntimeState.volume.rtDownscale);
    int lowH = std::max(1.0f, gWindowContext.viewportHeight() / gRenderRuntimeState.volume.rtDownscale);
    if (lowW != gRtW || lowH != gRtH) {
      CreateOrResizeRTFBO(lowW, lowH);
    }
   
    /*********************** draw rt results on FBO ************************/
    glBindFramebuffer(GL_FRAMEBUFFER, gRtFbo);
    glViewport(0, 0, gRtW, gRtH);
    
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(gRenderPrograms.rt);
    glBindVertexArray(gRenderResources.fullscreenVAO);

    int debug_mode = 0;
    glUniform1i (glGetUniformLocation(gRenderPrograms.rt,"uDebugMode"), debug_mode);
    
    // TBO をユニットへ
    bindTBO(gRenderPrograms.rt, "nodeMinTB",    G_bvh.nodeMin.tex,    0);
    bindTBO(gRenderPrograms.rt, "nodeMaxTB",    G_bvh.nodeMax.tex,    1);
    bindTBO(gRenderPrograms.rt, "nodeChildTB",  G_bvh.nodeChild.tex,  2);
    bindTBO(gRenderPrograms.rt, "particlesTB",  G_bvh.particles.tex,  3);
    bindTBO(gRenderPrograms.rt, "partSigmaTB",  G_bvh.partSigma.tex,  4);

    // カメラ・LOD ユニフォーム
    glUniform1i (glGetUniformLocation(gRenderPrograms.rt,"uRoot"), G_bvh.root);
    glUniform1i (glGetUniformLocation(gRenderPrograms.rt,"uLodMode"), gRenderRuntimeState.volume.lodMode); // 0/1/2
    glUniform1f (glGetUniformLocation(gRenderPrograms.rt,"uPxThreshold"), gRenderRuntimeState.volume.pxThreshold);
    glUniform1f (glGetUniformLocation(gRenderPrograms.rt,"uTauMax"), gRenderRuntimeState.volume.tauMax);
    glUniform1f (glGetUniformLocation(gRenderPrograms.rt,"uStepBias"), 1e-4f);

    glUniform1f (glGetUniformLocation(gRenderPrograms.rt,"uFocalPx"), focalPx);
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.rt,"invProj"),1,GL_FALSE, glm::value_ptr(invProj));
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.rt,"invView"),1,GL_FALSE, glm::value_ptr(invView));
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.rt,"view"),1,GL_FALSE, glm::value_ptr(view));
    glUniform3fv(glGetUniformLocation(gRenderPrograms.rt,"uCamForward"), 1, glm::value_ptr(camForward));

    //glUniform2f(glGetUniformLocation(gRenderPrograms.rt,"uResolution"), (float)viewportW, (float)viewportH);    
    // TBOバインドや各種uniformはそのまま。ただし uResolution は **低解像度** を渡す
    glUniform2f(glGetUniformLocation(gRenderPrograms.rt,"uResolution"), (float)gRtW, (float)gRtH);
    // そのほか uRoot, invProj, invView, view, uTauMax, uPxThreshold... 既存通り
    // （ここは今までのままでOK）

    glDrawArrays(GL_TRIANGLES, 0, 3);    
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // 戻す
    /**************************************************************/

    // ---- Upscale to screen ----
#ifdef USE_LETTERBOX
    glViewport(gWindowContext.viewportX(),
	       gWindowContext.viewportY(),
	       gWindowContext.viewportWidth(),
	       gWindowContext.viewportHeight());
#else
    glViewport(0, 0, viewportW, viewportH);
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(gRenderPrograms.upscale);
    glBindVertexArray(gRenderResources.fullscreenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRtTex);
    glUniform1i(glGetUniformLocation(gRenderPrograms.upscale, "uLow"), 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glUseProgram(0);    
  }

  if(gRenderRuntimeState.volume.show && gRenderRuntimeState.volume.flagRT == 2){    
    if(gRenderRuntimeState.volume.cpuUpdated){
      uploadOctTree_TBO(gAppServices.volume.octTree, gGPUOctTree, gAppServices.volume.rho2sigma);
      gRenderRuntimeState.volume.cpuUpdated = false;
    }
    
    int lowW = std::max(1.0f, gWindowContext.viewportWidth()  / gRenderRuntimeState.volume.rtDownscale);
    int lowH = std::max(1.0f, gWindowContext.viewportHeight() / gRenderRuntimeState.volume.rtDownscale);
    if (lowW != gRtW || lowH != gRtH) {
      CreateOrResizeRTFBO(lowW, lowH);
    }
   
    /*********************** draw rt results on FBO ************************/
    glBindFramebuffer(GL_FRAMEBUFFER, gRtFbo);
    glViewport(0, 0, gRtW, gRtH);
    
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(gRenderPrograms.octray);
    glBindVertexArray(gRenderResources.fullscreenVAO);

    int debug_mode = 0;
    glUniform1i (glGetUniformLocation(gRenderPrograms.octray,"uDebugMode"), debug_mode);
    
    // TBO をユニットへ
    bindTBO(gRenderPrograms.octray, "nodeMinTB", gGPUOctTree.nodeMin.tex,    0);
    bindTBO(gRenderPrograms.octray, "nodeMaxTB", gGPUOctTree.nodeMax.tex,    1);
    bindTBO(gRenderPrograms.octray, "childATB",  gGPUOctTree.texChildA.tex,  2);
    bindTBO(gRenderPrograms.octray, "childBTB",  gGPUOctTree.texChildB.tex,  3);
    bindTBO(gRenderPrograms.octray, "cornerLoTB", gGPUOctTree.cornerLo.tex,  4);
    bindTBO(gRenderPrograms.octray, "cornerHiTB", gGPUOctTree.cornerHi.tex,  5);

    // カメラ・LOD ユニフォーム
    glUniform1i (glGetUniformLocation(gRenderPrograms.octray,"uRoot"), gGPUOctTree.root);
    glUniform1f (glGetUniformLocation(gRenderPrograms.octray,"uPxThreshold"), gRenderRuntimeState.volume.pxThreshold);
    glUniform1f (glGetUniformLocation(gRenderPrograms.octray,"uTauMax"), gRenderRuntimeState.volume.tauMax);
    glUniform1f (glGetUniformLocation(gRenderPrograms.octray,"uStepBias"), 1e-4f);

    glUniform1f (glGetUniformLocation(gRenderPrograms.octray,"uFocalPx"), focalPx);
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.octray,"invProj"),1,GL_FALSE, glm::value_ptr(invProj));
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.octray,"invView"),1,GL_FALSE, glm::value_ptr(invView));
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.octray,"view"),1,GL_FALSE, glm::value_ptr(view));
    glUniform3fv(glGetUniformLocation(gRenderPrograms.octray,"uCamForward"), 1, glm::value_ptr(camForward));
    glUniform2f(glGetUniformLocation(gRenderPrograms.octray,"uResolution"), (float)gRtW, (float)gRtH);
    
    glDrawArrays(GL_TRIANGLES, 0, 3);    
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // 戻す
    /**************************************************************/

    // ---- Upscale to screen ----
#ifdef USE_LETTERBOX
    glViewport(gWindowContext.viewportX(),
	       gWindowContext.viewportY(),
	       gWindowContext.viewportWidth(),
	       gWindowContext.viewportHeight());
#else
    glViewport(0, 0, viewportW, viewportH);
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(gRenderPrograms.upscale);
    glBindVertexArray(gRenderResources.fullscreenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRtTex);
    glUniform1i(glGetUniformLocation(gRenderPrograms.upscale, "uLow"), 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glUseProgram(0);    
  }
#endif

#ifdef USE_CONVEX_HULL
  // ===========================
  // CPU side: update scene objects
  // ===========================
  if (gAppServices.clumpFind->checkClumpComputation()) {
    if (gAppServices.clumpFind->checkClearCache()) {
      gPolyhedronManager.clearGroup("convex_hull");
      gPolyhedronRenderer.requestResetGroup("convex_hull");
      gAppServices.clumpFind->finishClearCache();
    }

    gRenderRuntimeState.polyhedra.cpuUpdated = gAppServices.clumpFind->get_flagUpdated();
    
    if (gRenderRuntimeState.polyhedra.cpuUpdated) {
      gPolyhedronManager.clearGroup("convex_hull");
      
      const int nclumps = gAppServices.clumpFind->get_nclumps();
      printf("[convex] nclumps=%d\n", nclumps);

      for (int i = 0; i < nclumps; ++i) {
	bool showHull = gAppServices.clumpFind->flagShowHull(i);
	printf("[convex] clump=%d showHull=%d\n", i, (int)showHull);	
	if (!showHull)
	  continue;

	TrackingVector<ParticleData> pts =
	  gAppServices.clumpFind->get_particle_indices(i, P->particleBlock.particles);

	TrackingVector<float> vertices =
	  gConvexHullGenerator->buildLineVertices(pts);

	PolyhedronObject obj;
	obj.vertices.reserve(vertices.size() / 3);
	for (size_t k = 0; k + 2 < vertices.size(); k += 3) {
	  obj.vertices.emplace_back(vertices[k], vertices[k + 1], vertices[k + 2]);
	}

	printf("[convex] clump=%d obj.vertices=%zu\n", i, obj.vertices.size());      
	
	obj.color = glm::vec3(1.0f);
	obj.opacity = gRenderRuntimeState.polyhedra.opacity;
	obj.tag = "convex_hull";

	gPolyhedronManager.add(i, obj);
      }
      
      gRenderRuntimeState.polyhedra.cpuUpdated = false;
      gAppServices.clumpFind->disable_flagUpdated();
      
      gPolyhedronRenderer.requestResetGroup("convex_hull");
    }
  }
  
  if (gPolyhedronManager.show()) 
    gPolyhedronRenderer.drawWireframe(gPolyhedronManager, view, projection, gRenderPrograms.line);
#endif
}

ProjectionPreviewUIState MakeProjectionPreviewUIState(const ProjectionPreviewGLState& glst)
{
  ProjectionPreviewUIState ui;
  ui.textureId = (void*)(intptr_t)glst.tex;
  ui.width = glst.width;
  ui.height = glst.height;
  ui.valid = (glst.tex != 0 && glst.width > 0 && glst.height > 0);
  return ui;
}

void UpdateProjectionPreviewTexture(const ProjectionImage& img,
                                    ProjectionPreviewGLState& st)
{
  if (img.width <= 0 || img.height <= 0) return;
  if (img.rgb.size() != static_cast<size_t>(img.width * img.height * 3)) return;
  if (img.version == st.uploadedVersion) return;

  if (st.tex == 0) {
    glGenTextures(1, &st.tex);
    glBindTexture(GL_TEXTURE_2D, st.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    st.width = 0;
    st.height = 0;
  }

  glBindTexture(GL_TEXTURE_2D, st.tex);

  GLint prevUnpack;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpack);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  if (img.width != st.width || img.height != st.height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8,
                 img.width, img.height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, img.rgb.data());
    st.width = img.width;
    st.height = img.height;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    img.width, img.height,
                    GL_RGB, GL_UNSIGNED_BYTE, img.rgb.data());
  }

  glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpack);
  glBindTexture(GL_TEXTURE_2D, 0);

  st.uploadedVersion = img.version;
}


void Cleanup() {
#ifdef PYTHON_BRIDGE
  if (gAppServices.py.ptr) {
    gAppServices.py.ptr->shutdown(); 
    gAppServices.py.ptr.reset();
  }
#endif
  
  DestroyRenderResources(gRenderResources);
  DestroyRenderPrograms(gRenderPrograms);

  ConfigMaskState maskState;
  ExportMaskConfigState(maskState);
  saveConfig("config.txt", P, gFileInfo, &gParticleVisualConfig, &maskState);

#ifndef NONATIVEFILEDIALOG
  NFD_Quit();
#endif
  
  // GLFW の終了処理
  ShutdownImGuiContext();
  gWindowContext.destroy();
}

static void InitAppServices(AppServices& services, CameraContext& camCtx)
{
  services.radialProfile   = std::make_unique<RadialProfileComputer>(camCtx.cameraTarget);
  services.histogram2D     = std::make_unique<Histogram2DComputer>(camCtx.cameraTarget);
  services.projectionMap2D = std::make_unique<ProjectionMapGenerator>();
  services.clumpFind       = std::make_unique<FindClump>(camCtx);
#ifdef ISO_CONTOUR
  //services.isoContour      = std::make_unique<IsoSurfaceGenerator>();
#endif
#ifdef GEOMETRICAL_ANALYSIS
  services.diskFinder      = std::make_unique<DiskRadiusFinder>();
  services.ellipsoid       = std::make_unique<EllipseFitter>();
#endif
#ifdef STREAM_LINE
  services.streamLine      = std::make_unique<StreamlineComputer>();
#endif
#ifdef VOLUME_RENDERING
  services.bvh             = std::make_unique<lbvh::MortonBuilder>();
  services.tf              = std::make_unique<TransferFunctionEditor>();
#endif
}

int main() {
  if (!gWindowContext.init(1280, 720, "3D Particle Visualization")) {
    return EXIT_FAILURE;
  }
  gWindowContext.attachCallbacks(mouse_callback, scroll_callback);

  InitImGuiContext(gWindowContext.handle());

#ifndef NONATIVEFILEDIALOG
  if (NFD_Init() != NFD_OKAY) {
    std::cerr << "NFD_Init failed: " << NFD_GetError() << std::endl;
  }
#endif
  
  InitRenderPrograms(gRenderPrograms);
  InitAppServices(gAppServices, camCtx);
  
  gFileInfo = new FileInfo(camCtx);
  P = new ParticleArray(camCtx);

  InitRenderResources(gRenderResources, *P);
  ConfigMaskState maskState;
  if (loadConfig("config.txt", P, gFileInfo, &gParticleVisualConfig, &maskState)) {
    ApplyMaskConfigState(maskState);

    MaskConfig cfg = MakeMaskConfigFromUI();
    gFileInfo->setMaskConfig(cfg);
  }

  gFileInfo->setUnit(P);
  
  int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
  gFileInfo->loadBatch(newFileIndex, gFileInfo->batchSize, gFileInfo->skipStep, P);      
  
#ifdef USE_CONVEX_HULL
  gConvexHullGenerator = new ConvexHullGenerator();
#endif

  while (!glfwWindowShouldClose(gWindowContext.handle())) {
    // 1. フレーム時間の計測
    float currentFrame = static_cast<float>(glfwGetTime());
    float deltaTime = gInteractionState.beginFrame(currentFrame);
    (void)deltaTime;
    
    // 2. ユーザー入力の処理
    processInput(gWindowContext.handle());
    
    // 3. イベント処理（ウィンドウの更新など）
    glfwPollEvents();

    BeginImGuiFrame();
    
    // 4. UI の描画
    UpdateUI();            // ImGui UI の更新    
    
#ifdef PYTHON_BRIDGE
    if (gAppServices.py.ptr) {
      std::vector<FieldId> dirty;
      gAppServices.py.ptr->drainEditFields(dirty);
      if (!dirty.empty()) {
	bridge::applyFromSharedToAoS(gAppServices.py.ptr->shared(), *P, dirty);
	P->particlesDirty = true;      // 位置などを使うなら
      }
    }
#endif
    
    UpdateProjectionPreviewTexture(gAppServices.projectionMap2D->getImage(), gProjectionPreviewGLState);
    ProjectionPreviewUIState previewUI = MakeProjectionPreviewUIState(gProjectionPreviewGLState);
    DrawProjectionPreviewUI(*gAppServices.projectionMap2D, previewUI);

    // 5. 3D シーンの描画
    RenderScene();         // OpenGL 描画
        
    EndImGuiFrame();
    
    glfwSwapBuffers(gWindowContext.handle());
  }
  
  Cleanup(); // メモリ解放 & ImGui 終了処理

#ifdef USE_CONVEX_HULL
  delete gConvexHullGenerator;
#endif
  delete P;
  delete gFileInfo;

  return 0;
} 
