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

#ifdef GEOMETRICAL_ANALYSIS
#include "GeometricAnalysis/ellipse_fitter.h"
#include "GeometricAnalysis/DiskRadius.hpp"
#endif

// tinyfiledialogs（フォルダ選択用）
#ifndef NONATIVEFILEDIALOG
#include "nfd.h"
#else
#include "ImGuiFileDialog.h" // インクルードパスを合わせる
#endif

#define USE_LETTERBOX 1

#ifdef USE_CONVEX_HULL
#include "FindClumps/create_convex_hull.h"
ConvexHullGenerator* gConvexHullGenerator = nullptr;

struct HullGpuEntry {
  GLuint vao = 0, vbo = 0;
  GLsizei vertexCount = 0; // = number of float3 vertices
  bool dirty = true;
};
static std::unordered_map<int, HullGpuEntry> s_hull;
#endif

#ifdef PYTHON_BRIDGE
#include "PythonBridge/BridgeAdapter.h"
#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/ShmLayout.h"

struct PyBridgeState {
  std::unique_ptr<PythonBridge> ptr;
  bool launched = false;
  bool needUploadPos = false;
};

static PyBridgeState g_py; 
#endif

// ------------------------------
// ウィンドウサイズ設定
// ------------------------------
const unsigned int SCR_WIDTH  = 1280;
const unsigned int SCR_HEIGHT = 720;

std::size_t g_totalAllocated = 0;
std::mutex g_mutex;

struct CameraContext camCtx;

FileInfo *gFileInfo;
std::unique_ptr<RadialProfileComputer> gRadialProfileComputer;
std::unique_ptr<Histogram2DComputer> gHistogram2DComputer;
std::unique_ptr<ProjectionMapGenerator> gProjectionMap2D;
FindClump *gClumpFind;

ParticleArray *P;

#ifdef GEOMETRICAL_ANALYSIS
EllipseFitter *gEllipsoid; 
DiskRadiusFinder *gDiskFinder;
#endif

bool g_flagShowCuboid = false;
TrackingVector<glm::vec3> g_cubicPoints;

static int velocitySubtraction = 100;
bool showVelocityVectors = false;
bool useVelocityArrowLogScale = true;
float arrowScale = 1.;

#include "particle_visual_config.h"
ParticleVisualConfig gParticleVisualConfig;
GLuint colormapTextures[50] = {0};

size_t g_filteredParticleCount = 0;

std::vector<size_t> g_labelIndices;  // 毎回 ImGui 描画に使う
glm::vec3 g_lastCameraPos = camCtx.cameraPos;
float g_queryRadius   = 0.5f;     // r
float g_moveThreshold = 0.1f*g_queryRadius;
int g_nqueryparticles = 50;
bool g_flagShowSinkIDs = false;

#ifdef ISO_CONTOUR
#include "IsoSurface/IsoSurfaceGenerator.h"

IsoSurfaceGenerator * gIsoContour = nullptr;

TrackingVector<float> vertsIsocontour;
TrackingVector<unsigned> indsIsocontour;
unsigned meshIsocontourVAO=0, meshIsocontourVBO=0, meshIsocontourEBO=0, meshIsocontourNBO=0;
bool showIsocontour = false;
float isoOpacity=0.5;
#endif

#ifdef GEOMETRICAL_ANALYSIS
bool showEllipsoid = false;
float isoOpacityEllipsoid=0.5;

bool showDisks = false;
float diskOpacity=0.5;
#endif

#ifdef STREAM_LINE
bool showStreamLine = false;
bool flagStreamDirty = true;
float streamlineopacity = 0.9;

#include "StreamLine/stream_line.h"
StreamlineComputer *gStreamLine;
#endif

#ifdef VOLUME_RENDERING
#include "BVH/BVH.hpp"
#include "VolumeRendering/tau_sph.h"
#include "VolumeRendering/TransferFunctionEditor.hpp"
#include "VolumeRendering/OpacityComputer.hpp"

int flagRT = 2;
bool flagHideAllParticles = false;
bool showVolumeRendering = false;
bool flagUpdateRendering = false;

int   kernelMode = 0;  // 0=Gaussian, 1=Poly6
float gaussNSigma = 2.5; // 2.5
float enlargeKernel = 2.0; // 2.0

int lodMode = 1;
float pxThreshold = 2.;
float tauMax = 1.;  
float rtDownscale = 3.;

struct TBO { GLuint buf=0, tex=0; };
lbvh::MortonBuilder *gBVH;

struct GpuOctree {
  TBO nodeMin, nodeMax;     // GL_RGBA32F
  TBO cornerLo, cornerHi;     // GL_RGBA32F
  TBO texChildA, texChildB;   // GL_RGBA32I (isamplerBuffer)
  int root;
  int nNodes;
};

struct OctTreeState {
  std::unique_ptr<ParticleOctree> cpuTree;  
  GpuOctree gpuTree;
  
  std::vector<const ParticleOctree::Node*> order;  // preorder: index→Node*
  std::vector<NodeInfo>                    info;   // index -> attribute
  std::unordered_map<const ParticleOctree::Node*, int> toIdx; // Node* -> index

  uint64_t versionCPU = 0;   // ツリーを作り直したら ++
  uint64_t versionGPU = 0;   // GPUへ反映したら =versionCPU
  bool     dirtyCPU   = true; // order/info/toIdx が古いなら true
  bool     dirtyGPU   = true; // TBO/SSBO が古いなら true
} gOctTree;


TransferFunctionEditor *gTF;
RhoSigmaLUT g_rho2sigma;
#endif

bool flagCubesDirty = true;
bool flagShowCoordinates = true;

// ------------------------------
// マウス操作用
// ------------------------------
float lastX = SCR_WIDTH  / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// ------------------------------
// メインループ用の時間計測変数
// ------------------------------
float lastFrame = 0.0f;

// **ズーム範囲の最小・最大値（デフォルト値を設定）**
static float minZoom = 0.1f;  
static float maxZoom = 500.0f;  

// 仮想球面へのマッピング関数
glm::vec3 mapToSphere(float x, float y) {
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  float centerX = displaySize.x * 0.5f;
  float centerY = displaySize.y * 0.5f;
  float sizemax = std::max(displaySize.x, displaySize.y);
  
  // 画面中心を原点にして -1～1 に正規化
  //float nx = 2.0f * (x - centerX) / screenWidth;
  float nx = 2.0f * (centerX - x) / sizemax;
  float ny = 2.0f * (centerY - y) / sizemax; // Y座標は上方向が正になるように
  //float ny = (2.0f * y - screenWidth) / screenHeight; // Y座標は上方向が正になるように
  float lengthSquared = nx * nx + ny * ny;

  glm::vec3 result;
  if (lengthSquared <= 1.0f) {
    // 球面上の点：z = sqrt(1 - x^2 - y^2)
    result = glm::vec3(nx, ny, sqrt(1.0f - lengthSquared));
  } else {
    // 球の外側の場合は正規化（楕円状に補正）
    result = glm::normalize(glm::vec3(nx, ny, 0.0f));
  }

  return result;
}


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
    firstMouse = true;
    return;
  }
    
  if (firstMouse) {
    lastX = xpos;
    lastY = ypos;
    firstMouse = false;
  }
    
  float xoffset = xpos - lastX;
  float yoffset = lastY - ypos;
  lastX = xpos;
  lastY = ypos;
    
  if (shiftPressed) {
    float panSensitivity = 0.005f * camCtx.distance;
    glm::vec3 forward = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
    glm::vec3 rightVec = glm::normalize(glm::cross(forward, camCtx.cameraUp));
    glm::vec3 upVec = glm::normalize(glm::cross(rightVec, forward));
    //glm::vec3 upVec = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 panOffset = (-xoffset * panSensitivity) * rightVec + (-yoffset * panSensitivity) * upVec;

    camCtx.cameraTarget += panOffset;
    camCtx.cameraPos += panOffset;
  } else {
#ifdef ROTATE_ARCBALL
    // Arcball によるマウス座標の写像
    glm::vec3 startSphere = mapToSphere(lastX - xoffset, lastY - yoffset);
    glm::vec3 endSphere   = mapToSphere(xpos, ypos);
    
    // 始点と終点の外積で回転軸を求める
    glm::vec3 rotAxis = glm::cross(startSphere, endSphere);
    
    // 回転軸がほとんどゼロなら回転は行わない
    if (glm::length(rotAxis) > 1e-5f) {
      rotAxis = glm::normalize(rotAxis);
      // 始点と終点の内積から回転角度を算出（acos による角度 [rad]）
      float dotVal = glm::clamp(glm::dot(startSphere, endSphere), -1.0f, 1.0f);
      float angle = acos(dotVal);
      // 必要に応じて感度調整（例: angle に乗じる）
      float sensitivity = 1.0f; // 調整値（1.0ならそのまま）
      angle *= sensitivity;

      // Arcball 回転四元数の作成
      glm::quat qArcball = glm::angleAxis(angle, rotAxis);
      // カメラの回転を更新（回転の合成順序は用途に応じて調整）
      //camCtx.cameraOrientation = glm::normalize(qArcball * camCtx.cameraOrientation);
      camCtx.cameraOrientation = glm::normalize(camCtx.cameraOrientation * qArcball);
    }

    // 更新後のカメラの向きに基づいて、カメラ位置と上方向を更新する
    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
    camCtx.cameraPos = camCtx.cameraTarget - direction * camCtx.distance;
    camCtx.cameraUp  = camCtx.cameraOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
#elif defined(ROTATE_QUATERNION)
    float sensitivity = 0.1f;
    // ----- 四元数による回転処理 -----
    // xoffset による yaw（左右回転）は、グローバルな上軸 (0,1,0) 周りの回転
    float yawAngle = glm::radians(-xoffset * sensitivity);
    glm::quat qYaw = glm::angleAxis(yawAngle, glm::vec3(0.0f, 1.0f, 0.0f));

    // yoffset による pitch（上下回転）は、カメラの右軸周りの回転
    // 現在のカメラの右軸は、camCtx.cameraOrientation を (1,0,0) に適用して求める
    glm::vec3 right = camCtx.cameraOrientation * glm::vec3(1.0f, 0.0f, 0.0f);
    float pitchAngle = glm::radians(yoffset * sensitivity);
    glm::quat qPitch = glm::angleAxis(pitchAngle, right);

    // 更新順序を分ける：
    // まず yaw を更新してから、pitch を加える
    camCtx.cameraOrientation = glm::normalize(qYaw * camCtx.cameraOrientation);
    camCtx.cameraOrientation = glm::normalize(camCtx.cameraOrientation * qPitch);
	
    // カメラの正面方向は、単位ベクトル (0,0,-1) に camCtx.cameraOrientation を適用して得る
    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
    camCtx.cameraPos = camCtx.cameraTarget - direction * camCtx.distance;

    // カメラの上方向も更新（任意：ビュー行列作成に利用する場合）
    camCtx.cameraUp = camCtx.cameraOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
#else
    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;
        
    yaw   += xoffset;
    pitch += yoffset;

    // ピッチが ±90° を超えたら、視点の不連続を避けるために補正する
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
}


// ------------------------------
// スクロールコールバック（ズーム操作）
// ------------------------------
void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset)
{
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  float zoomSpeed = 0.1f * camCtx.distance;
  
  camCtx.distance += static_cast<float>(yoffset) * zoomSpeed;
  //if (distance < 1.0f)   distance = 1.0f;
  //if (distance > 100.0f) distance = 100.0f;

  if (camCtx.distance < minZoom) camCtx.distance = minZoom;
  if (camCtx.distance > maxZoom) camCtx.distance = maxZoom;
    
  glm::vec3 direction;
#if defined(ROTATE_ARCBALL) || defined(ROTATE_QUATERNION)
  direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
#else
  direction.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
  direction.y = sin(glm::radians(pitch));
  direction.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
  direction = glm::normalize(direction);
#endif
  camCtx.cameraPos = camCtx.cameraTarget - direction * camCtx.distance;
  
}

#ifdef USE_LETTERBOX
int g_viewportX = 0, g_viewportY = 0;
int g_viewportWidth = SCR_WIDTH, g_viewportHeight = SCR_HEIGHT;

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
  // 固定のターゲットアスペクト比（例：1280/720）
  const float targetAspect = 1280.0f / 720.0f;
  float windowAspect = static_cast<float>(width) / static_cast<float>(height);

  if (windowAspect > targetAspect) {
    // ウィンドウが横に広い場合：上下にレターボックス
    g_viewportHeight = height;
    g_viewportWidth = static_cast<int>(height * targetAspect);
    g_viewportX = (width - g_viewportWidth) / 2;
    g_viewportY = 0;
  } else {
    // ウィンドウが縦に長い場合：左右にレターボックス
    g_viewportWidth = width;
    g_viewportHeight = static_cast<int>(width / targetAspect);
    g_viewportX = 0;
    g_viewportY = (height - g_viewportHeight) / 2;
  }
  glViewport(g_viewportX, g_viewportY, g_viewportWidth, g_viewportHeight);

}
#else
// 方法A: ウィンドウ全体を使う（glViewport はウィンドウサイズ全体に設定）
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
  glViewport(0, 0, width, height);
}
#endif

inline int viewportWidth() {
#ifdef USE_LETTERBOX
    return g_viewportWidth;
#else
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    return w;
#endif
}

inline int viewportHeight() {
#ifdef USE_LETTERBOX
    return g_viewportHeight;
#else
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    return h;
#endif
}


void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

// ------------------------------
// シェーダー関連ヘルパー関数
// ------------------------------
static const char* shaderHeader = 
  "#version 330 core\n";

static const char* shaderHeader410 =
  "#version 410 core\n";

// 汎用：任意ヘッダを先頭に付けてコンパイル
unsigned int compileShaderWithHeader(unsigned int type, const char* header, const char* source)
{
    unsigned int shader = glCreateShader(type);
    const char* sources[] = { header, source };
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[4096];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR::SHADER_COMPILATION_FAILED\n" << infoLog << std::endl;
	printf("ERROR::PROGRAM_LINKING_FAILED\n");
    }
    return shader;
}

// 既存の VS/FS 用はヘッダを 330 に固定
unsigned int compileShader(unsigned int type, const char* source)
{
    return compileShaderWithHeader(type, shaderHeader, source);
}

unsigned int createShaderProgramWithHeader(const char* vertexSource, const char* fragmentSource, const char *header)
{
  unsigned int vertexShader = compileShaderWithHeader(GL_VERTEX_SHADER, header, vertexSource);
  unsigned int fragmentShader = compileShaderWithHeader(GL_FRAGMENT_SHADER, header, fragmentSource);
    
  unsigned int program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);
    
  int success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[511];
    glGetProgramInfoLog(program, 511, nullptr, infoLog);
    std::cerr << "ERROR::PROGRAM_LINKING_FAILED\n" << infoLog << std::endl;
    printf("ERROR::PROGRAM_LINKING_FAILED\n");
  }
    
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  
  return program;
}


unsigned int createShaderProgram(const char* vertexSource, const char* fragmentSource)
{
  return createShaderProgramWithHeader(vertexSource, fragmentSource, shaderHeader);
}



// ------------------------------
// シェーダーソースコード
// ------------------------------
const char* particleVertexShaderSource = R"(
layout (location = 0) in vec3 aPos;
layout (location=1) in uint aType;
layout (location=2) in uint aFlag;
layout (location = 3) in float aHsml;
layout (location = 4) in float aValShow;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float pointSizes[6];

out float vHsml;
out float val_show;

flat out int nodeFlag;
flat out int vType;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    gl_PointSize = pointSizes[int(aType)];

    vHsml = aHsml;
    val_show = aValShow;

    vType = int(aType);
    nodeFlag = int(aFlag);
}
)";

const char* particleFragmentShaderSource = R"(
in float vHsml;
in float val_show;

flat in int vType;
flat in int nodeFlag;
out vec4 FragColor;

uniform float valueMin[6];
uniform float valueMax[6];
uniform int useLog[6]; // 0:通常, 1:対数表示

uniform vec3 lowColors[6];
uniform vec3 highColors[6];
uniform sampler1D colormaps[6];
uniform int periodicMapping[6]; // 1:周期的に表示、0:通常表示

void main()
{
  float varValue;

  varValue = val_show;

  float normVal = (varValue - valueMin[vType]) / (valueMax[vType] - valueMin[vType]);
  if (useLog[vType] == 1) 
    normVal = (log(varValue)/log(10.0) - valueMin[vType]) / (valueMax[vType] - valueMin[vType]);

  if (periodicMapping[vType] == 1) {
    normVal = fract(normVal);
  } else {
    normVal = clamp(normVal, 0.0, 1.0);
  }

  //vec3 color = mix(lowColors[vType], highColors[vType], normVal);
  vec3 color;
  if(vType == 0)
    color = texture(colormaps[0], normVal).rgb;
  else if(vType == 1)
    color = texture(colormaps[1], normVal).rgb;
  else if(vType == 2)
    color = texture(colormaps[2], normVal).rgb;
  else if(vType == 3)
    color = texture(colormaps[3], normVal).rgb;
  else if(vType == 4)
    color = texture(colormaps[4], normVal).rgb;
  else if(vType == 5)
    color = texture(colormaps[5], normVal).rgb;

  //vec3 color = texture(colormaps[vType], normVal).rgb; //it doesn't work for Mesa

  // --- point sprite: circle mask ---
  vec2 uv = gl_PointCoord * 2.0 - 1.0;    // [-1,1]
  float r2 = dot(uv, uv);
  if (r2 > 1.0) discard;                 // round points

  // base alpha (soft edge)
  float edge = smoothstep(1.0, 0.90, r2); // outer 10% fade
  float alpha = edge;

  if (nodeFlag == 1) {
    vec3 stressColor = vec3(1.0, 0.9, 0.2);

  // ring mask: only near the edge
  float ring = smoothstep(0.70, 0.82, r2) * (1.0 - smoothstep(0.82, 0.98, r2));

  // optional: very subtle outer glow (still mostly edge)
  float glow = smoothstep(0.55, 0.90, r2) * (1.0 - smoothstep(0.90, 1.00, r2));
  glow *= 0.25; // ←弱め（好みで）

  // Keep center color; tint only where ring/glow is present
  float tint = clamp(ring * 1.0 + glow, 0.0, 1.0);
  color = mix(color, stressColor, tint);

  // Make ring brighter without recoloring center
  color += ring * stressColor * 1.5;

  // alpha: keep soft edge; stressed slightly less transparent near ring
  alpha = max(alpha, ring * 0.9);
  }

  //if (nodeFlag == 1) 
   // color = mix(color, vec3(1.0), 0.5);

  FragColor = vec4(color, alpha);
}
)";

const char* lineVertexShaderSource = R"(
layout (location = 0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
void main()
{
    gl_Position = projection * view * vec4(aPos, 1.0);
}
)";

const char* lineFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec4 color;
void main()
{
    FragColor = color;
}
)";

// カラーバー用のグローバル変数
GLuint colorbarVAO, colorbarVBO, colorbarEBO;
GLuint colorbarProgram;

// 現在使用するカラーマップテクスチャ（たとえば UI で選択したもの）
// これは、InitColorMaps() 等で初期化しているもの（例：jetTex, viridisTex など）から選択します。
GLuint currentColorMapTex = 0;  // ※ 既に定義済みのグローバル変数とする


// カラーバー用頂点シェーダー（スクリーン空間の NDC 座標で描画）
const char* colorbarVertexShaderSource = R"(
layout(location = 0) in vec2 aPos;       // 位置（NDC座標）
layout(location = 1) in vec2 aTexCoord;    // テクスチャ座標
out vec2 TexCoords;
void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoords = aTexCoord;
}
)";

// カラーバー用フラグメントシェーダー（1D テクスチャから色をサンプリング）
const char* colorbarFragmentShaderSource = R"(
in vec2 TexCoords;
uniform sampler1D colormap;
out vec4 FragColor;
void main()
{
    // 横方向のテクスチャ座標（TexCoords.x）でカラーマップから色を取得
    FragColor = texture(colormap, TexCoords.x);
}
)";

// 例: velocity_arrow.vert の内容を生の文字列リテラルとして定義
const char* velocityArrowVertexShaderSource = R"(
// モデル空間で定義した矢印の形状（ここでは(0,0,0)→(0,0,1)の直線）
layout (location = 0) in vec3 aPos; 

// インスタンス属性：粒子の位置と速度
layout (location = 1) in vec3 instancePos; 
layout (location = 2) in vec3 instanceVel; 

uniform mat4 view;
uniform mat4 projection;
// 速度ベクトルの大きさにかけるスケール（任意の調整用）
uniform float scaleFactor;
// 1.0ならログスケールを使う、0.0ならそのまま
uniform float logScale;

out vec3 fragColor;

void main() {
    // 速度の大きさを取得
    float speed = length(instanceVel);
    // ログスケールを使う場合（+1でゼロ除算を防ぐ）
    float arrowLength = (logScale > 0.5) ? log(speed + 1.0) : speed;
    arrowLength *= scaleFactor;

    // 回転行列を初期化
    mat3 rotationMatrix = mat3(1.0);
    if (speed > 1e-6) {
        // デフォルトの方向 (0,0,1) を、粒子の速度方向に合わせる
        vec3 defaultDir = vec3(0.0, 0.0, 1.0);
        vec3 dir = normalize(instanceVel);
        float cosAngle = dot(defaultDir, dir);
        if (cosAngle < 0.9999) {
            vec3 axis = normalize(cross(defaultDir, dir));
            float angle = acos(cosAngle);
            float s = sin(angle);
            float c = cos(angle);
            float oc = 1.0 - c;
            rotationMatrix = mat3(
                oc * axis.x * axis.x + c,         oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s,
                oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,         oc * axis.y * axis.z - axis.x * s,
                oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c
            );
        }
    }

    // aPos を arrowLength でスケールし、回転を適用する
    vec3 arrowPos = aPos * arrowLength;
    vec3 transformedArrowPos = rotationMatrix * arrowPos;
    
    // 最終的な位置は、粒子の位置に回転・スケーリングした矢印を足す
    vec3 pos = instancePos + transformedArrowPos;
    gl_Position = projection * view * vec4(pos, 1.0);
    
    // 色は赤（任意）
    fragColor = vec3(1.0, 0.0, 0.0);
}
)";

// 同様に、フラグメントシェーダーも
const char* velocityArrowFragmentShaderSource = R"(
in vec3 fragColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(fragColor, 1.0);
}
)";



// simple_tex.vert
const char *colormap2DShaderSource = R"(

layout (location = 0) in vec2 inPos;       // 頂点の位置
layout (location = 1) in vec2 inTexCoord;  // テクスチャ座標

out vec2 TexCoord;

void main()
{
    gl_Position = vec4(inPos, 0.0, 1.0); // そのままクリップ空間に
    TexCoord = inTexCoord;
}
)";

// simple_tex.frag
const char* colormap2DFragmentShaderSource = R"(

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uTexture;

void main()
{
    FragColor = texture(uTexture, TexCoord);
}
)";


#ifdef ISO_CONTOUR
const char* isocontourVertexShaderSource = R"(   
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal  = mat3(transpose(inverse(model))) * aNormal; // モデル空間→ワールド空間
    gl_Position = projection * view * worldPos;
}
)";

const char* isocontourFragmentShaderSource = R"(   
in vec3 FragPos;
in vec3 Normal;

uniform vec3 lightPos;
uniform vec3 viewPos;      // カメラ位置（視点）
uniform vec3 diffuseColor; // 例: vec3(1.0, 0.2, 0.2)
uniform vec3 ambientColor; // 例: vec3(0.1, 0.1, 0.1)
uniform float opacity;

out vec4 FragColor;

void main() {
    // ノーマルと光線ベクトル
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightPos - FragPos);
    // ディフューズ項
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * diffuseColor;
    // アンビエント項
    vec3 ambient = ambientColor * diffuseColor;
    vec3 result = ambient + diffuse;
    //FragColor = vec4(result, 0.2);
    FragColor = vec4(1.0,1.0,1.0, opacity);
}
)";
#endif

#ifdef GEOMETRICAL_ANALYSIS
const char* ellipseVertexShaderSource = R"(
layout(location=0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* ellipseFragmentShaderSource = R"(
out vec4 FragColor;

uniform vec3 color;
uniform float opacity;

void main() {
    FragColor = vec4(1.,1.,1., opacity);
}
)";

const char* ellipsoidVertexShaderSource = R"(
layout(location=0) in vec3 aPos;
uniform mat4 model, view, projection;

void main(){
    gl_Position = projection * view * model * vec4(aPos,1.0);
}
)";

const char* ellipsoidFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec3  color;
uniform float opacity;

void main(){
    FragColor  = vec4(color, opacity);
}
)";

const char* diskVertexShaderSource = R"(
layout(location=0) in vec3 aPos;
uniform mat4 model, view, projection;
void main(){
    gl_Position = projection * view * model * vec4(aPos,1.0);
}
)";

const char* diskFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec3 color;
uniform float opacity;
void main(){ FragColor = vec4(color, opacity); }
)";
#endif

#ifdef STREAM_LINE
const char* streamlineVertexShaderSource = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main(){
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";


const char* streamlineFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec3  color;
uniform float opacity;

void main()
{
    FragColor = vec4(color, opacity);
}
)";
#endif

const char* cubicShaderSource = R"(
layout (location = 0) in vec3 aPos;
layout (location = 1) in mat4 instanceModel;
layout(location = 5) in float instanceOpacity;

uniform mat4 view;
uniform mat4 projection;

out float vOpacity;

void main(){
    gl_Position = projection * view * instanceModel * vec4(aPos, 1.0);
    vOpacity = instanceOpacity;
}
)";


const char* cubicFragmentShaderSource = R"(
out vec4 FragColor;
uniform vec3  color;
in  float vOpacity;  

void main()
{
    FragColor = vec4(color, vOpacity);
}
)";


const char* coordShaderSource = R"(
layout(location=0) in vec3 aPos;  
layout(location=1) in vec3 aColor;

uniform mat3 uCamRot;    // カメラの「ワールド→カメラ」の逆回転行列の転置＝カメラ向き
uniform float uScale;    // スクリーン上での軸の長さ（ndc単位）
uniform vec2  uOffset;   // スクリーン右下の基準点（ndc単位）

out vec3 vColor;

void main(){
    // 1) ワールド軸をカメラ向きに回転
    vec3 dir = uCamRot * aPos;       // 例: aPos=(0,0,1) → カメラから見たZ方向

    // 2) スクリーンXYに取り出す
    vec2 sc = dir.xy * uScale + uOffset;

    // 3) gl_Position にスクリーン座標を直接セット
    gl_Position = vec4(sc, 0.0, 1.0);

    vColor = aColor;
}
)";


const char* coordFragmentShaderSource = R"(
in  vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

#ifdef VOLUME_RENDERING
const char* fullscreenShaderSource = R"(
const vec2 V[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main(){ gl_Position = vec4(V[gl_VertexID],0,1); }
)";

const char* rtFragmentShaderSource = R"(
uniform int uDebugMode;

// ---- BVH TBO ----
uniform samplerBuffer nodeMinTB;    // vec4(bmin.xyz,sigma_avg)
uniform samplerBuffer nodeMaxTB;    // vec4(bmax.xyz,sigma_max)
uniform isamplerBuffer nodeChildTB; // ivec4(left,right,first,count)
uniform samplerBuffer particlesTB;  // vec4(pos.xyz, radius)
uniform samplerBuffer partSigmaTB;  // float sigma

// ---- カメラ ----
uniform mat4 invProj;
uniform mat4 invView;    // 使わなければ省略
uniform mat4 view;    // 使わなければ省略
uniform vec3 uCamForward;
uniform float uFocalPx;  // 画面の焦点距離(px) = 0.5*H/tan(fovY/2)
uniform int   uRoot;
uniform vec2  uResolution;

// ---- LOD ----
uniform int   uLodMode;       // 0:LeafOnly, 1:AutoLOD, 2:ForceNode
uniform float uPxThreshold;   // 例: 1.0〜2.0 px

// ---- 光学 ----
uniform float uTauMax;        // 途中終了の閾値 (例: 1.0)
uniform float uStepBias;      // 数値安定用の微小 >0

out vec4 oColor;

// --------- 交差系 ---------
bool hitAABB(vec3 mn, vec3 mx, vec3 ro, vec3 invDir, inout float t0, inout float t1) {
    vec3 t1v = (mn - ro) * invDir;
    vec3 t2v = (mx - ro) * invDir;
    vec3 tminv = min(t1v, t2v);
    vec3 tmaxv = max(t1v, t2v);
    t0 = max(max(tminv.x, tminv.y), tminv.z);
    t1 = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
    return t1 >= max(t0, 0.0);
}


float hitSphere(vec3 c, float r, vec3 ro, vec3 rd, out float tIn, out float tOut){
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float c2 = dot(oc, oc) - r*r;
    float disc = b*b - c2;
    if (disc < 0.0) { tIn = tOut = -1.0; return -1.0; }
    float s = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;

    // 内側にいる場合の特別処理
    if (t0 < 0.0 && t1 > 0.0) {
        tIn = 0.0; // 今の位置からすぐ
        tOut = t1;
        return t1;
    }

    tIn = (t0 > 0.0) ? t0 : ((t1 > 0.0) ? t1 : -1.0);
    tOut = t1;
    return (tIn > 0.0) ? tIn : -1.0;
}

// 画面半径(px)の概算
float screenRadiusPx(float r_eff, float z_view, float focal_px){
    return (z_view > 0.0) ? (focal_px * r_eff / z_view) : 1e9;
}

bool pointInsideAABB(vec3 p, vec3 mn, vec3 mx){
    return all(greaterThanEqual(p, mn)) && all(lessThanEqual(p, mx));
}

vec3 heat(float t){          // t in [0,1]
    t = clamp(t,0.0,1.0);
    float r = smoothstep(0.5,1.0,t);
    float g = t<0.5 ? smoothstep(0.0,0.5,t) : smoothstep(1.0,0.5,t);
    float b = smoothstep(1.0,0.5,t);
    return vec3(r,g,b);
}

void main(){
    // --- レイを view 空間で ---
    // NDC を自前で復元（画面サイズから uv を渡してもOK）
    vec2 ndc = vec2( (gl_FragCoord.x * 2.0) / float(uResolution.x) - 1.0,
                     (gl_FragCoord.y * 2.0) / float(uResolution.y) - 1.0 ); // フレームワークに合わせて修正
    vec4 pN = invProj * vec4(ndc, -1.0, 1.0);
    pN /= pN.w;

    // ビュー空間の原点と方向
    vec3 ro_view = vec3(0.0);
    vec3 rd_view = normalize(vec3(pN));

    // ワールドへ変換
    vec3 ro = (invView * vec4(ro_view, 1.0)).xyz;
    vec3 rd = normalize((invView * vec4(rd_view, 0.0)).xyz);
    vec3 invDir = 1.0 / max(abs(rd), vec3(1e-30)) * sign(rd);

    // --- デバッグ用カウンタ ---
    int leafHits = 0;        // ヒットした葉（粒子）数
    int nodeApprox = 0;      // 近似（ノード球）で積分した回数
    int aabbMiss = 0;        // AABB ミス回数（ヒットしなかったノード）
    int visits = 0;
    const int MAX_VISITS = 1512;

    const float uSkipEps = 1.e-2;

    // --- 反復BVH ---
    struct StackItem { int id; float t0; float t1; vec3 center; float r; float sigma; float sigma_max;};
    const int STACK_MAX = 64;
    StackItem stack[STACK_MAX];
    int sp=0;
    float tau = 0.0;

    vec4 rootMin = texelFetch(nodeMinTB, uRoot);
    vec4 rootMax = texelFetch(nodeMaxTB, uRoot);
    float rt0=0.0, rt1=1e30;
    if (hitAABB(rootMin.xyz, rootMax.xyz, ro, invDir, rt0, rt1)) {
        vec3  cRoot   = 0.5 * (rootMin.xyz + rootMax.xyz);
        vec3  extRoot = rootMax.xyz - rootMin.xyz;
        float rRoot   = 0.5 * length(extRoot);                 // 球近似の半径
        float sRoot  = rootMin.w;
        float sRootMax = rootMax.w;

        stack[sp++] = StackItem(uRoot, rt0, rt1, cRoot, rRoot, sRoot, sRootMax);
    }

    while(sp>0 && tau < uTauMax && visits < MAX_VISITS){
        visits++;

        StackItem it = stack[--sp];
        int id   = it.id;
        float t0 = it.t0;
        float t1 = it.t1;

        ivec4 ch    = texelFetch(nodeChildTB, id);
        bool isLeaf = (ch.x<0 && ch.y<0);

        if(isLeaf){
            // ---- leaf node ----
            int first = ch.z;
            int count = ch.w; // 今は 1 前提でもOK
            for(int k=0;k<count;k++){
                vec4 pr = texelFetch(particlesTB, first+k);
                float sigma = texelFetch(partSigmaTB, first+k).x;
                float tIn,tOut;
                if(hitSphere(pr.xyz, pr.w, ro, rd, tIn, tOut) > 0.0){
                    float seg0 = max(tIn, t0);
                    float seg1 = min(tOut, t1);
                    float L = max(0.0, seg1 - seg0);
                    tau += sigma * max(0.0, L - uStepBias);
                    leafHits++;
                    if(tau >= uTauMax) break;
                }
            }
        }else{
            // ---- LOD ----
            float zView = dot(it.center - ro, uCamForward); 
            bool useApprox = false;
            if(uLodMode==1){
                float r_px = screenRadiusPx(it.r, zView, uFocalPx);
                if(r_px < uPxThreshold) useApprox = true;
            }

            if(useApprox){
                // ノードを「球」で近似して一括加算
                float tIn,tOut;
                if(hitSphere(it.center, it.r, ro, rd, tIn, tOut) > 0.0){
                    float L = max(0.0, tOut - tIn);
                    tau += it.sigma * max(0.0, L - uStepBias);
                    nodeApprox++;

                    if (tau >= uTauMax) break; 
                    continue;
                }
            }

            float possible = it.sigma_max * max(0.0, t1 - t0); // sigma_max は StackItem に追加
            if (possible < uSkipEps) {
                continue;
            }

            int Li = ch.x, Ri = ch.y;
            bool hL = false; float t0L=0.0, t1L=1e30; vec3 cL=vec3(0.0); float rL=0.0, sL=0.0, sL_max=0.0;
            if (Li >= 0) {
                vec4 lmin = texelFetch(nodeMinTB, Li);
                vec4 lmax = texelFetch(nodeMaxTB, Li);
                hL = hitAABB(lmin.xyz, lmax.xyz, ro, invDir, t0L, t1L);
                if (hL) {
                    // 親区間でクリップ
                    t0L = max(t0L, t0);
                    t1L = min(t1L, t1);
                    hL = (t1L >= t0L);
                    if (hL) {
                        cL = 0.5 * (lmin.xyz + lmax.xyz);
                        rL = 0.5 * length(lmax.xyz - lmin.xyz);
                        sL = lmin.w;
                        sL_max = lmax.w;
                    }
                }
            }

            // Right child
            bool hR = false; float t0R=0.0, t1R=1e30; vec3 cR=vec3(0.0); float rR=0.0, sR=0.0, sR_max=0.0;
            if (Ri >= 0) {
                vec4 rmin = texelFetch(nodeMinTB, Ri);
                vec4 rmax = texelFetch(nodeMaxTB, Ri);
                hR = hitAABB(rmin.xyz, rmax.xyz, ro, invDir, t0R, t1R);
                if (hR) {
                    t0R = max(t0R, t0);
                    t1R = min(t1R, t1);
                    hR = (t1R >= t0R);
                    if (hR) {
                        cR = 0.5 * (rmin.xyz + rmax.xyz);
                        rR = 0.5 * length(rmax.xyz - rmin.xyz);
                        sR = rmin.w;
                        sR_max = rmax.w;
                    }
                }
            }

            // 近い方から処理（遠い方を先に push）
            if (hL && hR) {
                bool Lnear = (t0L <= t0R);
                // far
                if (sp < STACK_MAX) {
                    if (Lnear) stack[sp++] = StackItem(Ri, t0R, t1R, cR, rR, sR, sR_max);
                    else       stack[sp++] = StackItem(Li, t0L, t1L, cL, rL, sL, sL_max);
                }
                // near
                if (sp < STACK_MAX) {
                    if (Lnear) stack[sp++] = StackItem(Li, t0L, t1L, cL, rL, sL, sL_max);
                    else       stack[sp++] = StackItem(Ri, t0R, t1R, cR, rR, sR, sR_max);
                }
            } else if (hL) {
                if (sp < STACK_MAX) stack[sp++] = StackItem(Li, t0L, t1L, cL, rL, sL, sL_max);
            } else if (hR) {
                if (sp < STACK_MAX) stack[sp++] = StackItem(Ri, t0R, t1R, cR, rR, sR, sR_max);
            }
        }
    }

    // ===== デバッグ可視化 =====
    if(uDebugMode == 10){
        // 訪問回数 heat
        float t = float(visits) / max(1.0, float(MAX_VISITS));
        //float t = float(visits) / 100.;
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 11){
        // τ heat（uMaxTauVisで正規化）
        float t = clamp(tau / max(uTauMax, 1e-6), 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 12){
        // 葉ヒット回数
        float t = clamp(float(leafHits)/64.0, 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 13){
        // 近似使用回数
        float t = clamp(float(nodeApprox)/64.0, 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }
    if(uDebugMode == 14){
        // AABB miss 回数
        float t = clamp(float(aabbMiss)/float(visits+1), 0.0, 1.0);
        oColor = vec4(heat(t), 1.0);
        return;
    }

    // 可視化：τからトーンマップ（お好みで）
    float a = clamp(tau / uTauMax, 0.0, 1.0);
    oColor = vec4(1., 1., 1., 1 - exp(-tau));  // 例：1-exp(-τ)
}
)";


const char* upscaleVS = R"(
out vec2 vUV;
void main(){
    const vec2 pos[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    const vec2 uv[3]  = vec2[3](vec2(0,0),   vec2(2,0),  vec2(0,2));
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
    vUV = uv[gl_VertexID];
}
)";

const char* upscaleFS = R"(
in  vec2 vUV;
out vec4 FragColor;
uniform sampler2D uLow;
void main(){
    FragColor = texture(uLow, vUV);
}
)";

const char* octrayFragmentShaderSource = R"(
uniform samplerBuffer nodeMinTB;   // vec4(bmin.xyz, sigma_avg)
uniform samplerBuffer nodeMaxTB;   // vec4(bmax.xyz, sigma_max)
uniform isamplerBuffer childATB;   // ivec4(children 0..3)
uniform isamplerBuffer childBTB;   // ivec4(children 4..7)
uniform samplerBuffer cornerLoTB;  // vec4: sigma[0..3]
uniform samplerBuffer cornerHiTB;  // vec4: sigma[4..7]

uniform int uDebugMode;

// ---- カメラ ----
uniform mat4 invProj;
uniform mat4 invView;    // 使わなければ省略
uniform mat4 view;    // 使わなければ省略
uniform vec3 uCamForward;
uniform float uFocalPx;  // 画面の焦点距離(px) = 0.5*H/tan(fovY/2)
uniform int   uRoot;
uniform vec2  uResolution;

// ---- LOD ----
uniform float uPxThreshold;   // 例: 1.0〜2.0 px

// ---- 光学 ----
uniform float uTauMax;        // 途中終了の閾値 (例: 1.0)
uniform float uStepBias;      // 数値安定用の微小 >0

out vec4 FragColor;

vec3 heat(float t){          // t in [0,1]
    t = clamp(t,0.0,1.0);
    float r = smoothstep(0.5,1.0,t);
    float g = t<0.5 ? smoothstep(0.0,0.5,t) : smoothstep(1.0,0.5,t);
    float b = smoothstep(1.0,0.5,t);
    return vec3(r,g,b);
}

float screenRadiusPx(float r_eff, float z_view, float focal_px){
    return (z_view > 0.0) ? (focal_px * r_eff / z_view) : 1e9;
}

bool rayBox(vec3 ro, vec3 invd, vec3 mn, vec3 mx, inout float t0, inout float t1) {
    vec3 t1v = (mn - ro) * invd;
    vec3 t2v = (mx - ro) * invd;
    vec3 tminv = min(t1v, t2v);
    vec3 tmaxv = max(t1v, t2v);
    float lo = max(max(tminv.x, tminv.y), tminv.z);
    float hi = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
    t0 = max(t0, lo);
    t1 = min(t1, hi);
    return (t1 >= max(t0, 0.0));
}

float trilerp8(vec4 lo, vec4 hi, vec3 uvw) {
    //  lo.x: idx0 = (0,0,0) = c000
    //  lo.y: idx1 = (1,0,0) = c100
    //  lo.z: idx2 = (1,1,0) = c110
    //  lo.w: idx3 = (0,1,0) = c010
    //  hi.x: idx4 = (0,0,1) = c001
    //  hi.y: idx5 = (1,0,1) = c101
    //  hi.z: idx6 = (1,1,1) = c111
    //  hi.w: idx7 = (0,1,1) = c011
    float c000 = lo.x;
    float c100 = lo.y;
    float c110 = lo.z;
    float c010 = lo.w;
    float c001 = hi.x;
    float c101 = hi.y;
    float c111 = hi.z;
    float c011 = hi.w;

    float ux = clamp(uvw.x, 0.0, 1.0);
    float uy = clamp(uvw.y, 0.0, 1.0);
    float uz = clamp(uvw.z, 0.0, 1.0);

    float c00 = mix(c000, c100, ux); // z=0, y=0
    float c10 = mix(c010, c110, ux); // z=0, y=1
    float c01 = mix(c001, c101, ux); // z=1, y=0
    float c11 = mix(c011, c111, ux); // z=1, y=1
    float c0  = mix(c00,  c10,  uy);
    float c1  = mix(c01,  c11,  uy);
    return mix(c0, c1, uz);
}

void main(){
    // レイ生成（ビュー空間→ワールド）
    vec2 ndc = vec2( (gl_FragCoord.x*2.0)/uResolution.x - 1.0,
                     (gl_FragCoord.y*2.0)/uResolution.y - 1.0 );


    if (uDebugMode == 1) {
        FragColor = vec4(1,0,1,1);
        return;
    }

    if (uDebugMode == 2) {
        // 見逃しづらいテストパターン：UVグラデ＋チェッカ
        float cx = step(0.5, fract(gl_FragCoord.x/16.0));
        float cy = step(0.5, fract(gl_FragCoord.y/16.0));
        float chk = mod(cx + cy, 2.0);
        vec2 uv01 = gl_FragCoord.xy / uResolution;
        vec3  col = mix(vec3(uv01, 0.5), vec3(1.0, 0.2, 0.8), chk);
        FragColor = vec4(col, 1.0);
        return;
    }

    vec4 pN  = invProj * vec4(ndc, -1.0, 1.0);
    pN      /= pN.w;
    vec3 ro  = (invView * vec4(0,0,0,1)).xyz;
    vec3 rd  = normalize((invView * vec4(vec3(pN),0)).xyz);
    vec3 invd= 1.0 / max(abs(rd), vec3(1e-30)) * sign(rd);

    // ルートと交差
    vec3 rootMin = texelFetch(nodeMinTB, uRoot).xyz;
    vec3 rootMax = texelFetch(nodeMaxTB, uRoot).xyz;
    float t0=0.0, t1=1e30;
    if(!rayBox(ro, invd, rootMin, rootMax, t0, t1)){
        FragColor=vec4(0);
        return;
    }

    // 手動スタック（410でもOK）
    const int STACK_MAX = 64;
    int   stack[STACK_MAX];
    float t0s[STACK_MAX];
    float t1s[STACK_MAX];
    int sp=0;

    stack[sp]=uRoot;
    t0s[sp]=t0;
    t1s[sp]=t1;
    sp++;

    float alpha=0.0; vec3 color=vec3(0.0);

    int visits = 0;
    const int MAX_VISITS = 1000;

    while(sp>0 && visits < MAX_VISITS){
        int   id = stack[--sp];
        t0 = t0s[sp]; t1 = t1s[sp];

        visits++;

        vec4 vmin = texelFetch(nodeMinTB, id);
        vec4 vmax = texelFetch(nodeMaxTB, id);
        vec3 bmin = vmin.xyz;
        vec3 bmax = vmax.xyz;
        float sigma_avg = vmin.w;
        float sigma_max = vmax.w;

        // 画素LOD：セルが画素より十分小さければ近似で一発
        float radius = length(bmax - bmin);   // 簡易直径
        vec3 center = 0.5 * (bmin + bmax);
        float zView = dot(center - ro, uCamForward);
        //float r_px = screenRadiusPx(radius, zView, uFocalPx);
        float r_px = 1000.;

        bool isLeaf = false;
        ivec4 cA = texelFetch(childATB, id);
        ivec4 cB = texelFetch(childBTB, id);
        isLeaf = (cA.x<0 && cA.y<0 && cA.z<0 && cA.w<0 &&
                  cB.x<0 && cB.y<0 && cB.z<0 && cB.w<0);

        if (isLeaf || r_px < 2.0*uPxThreshold) {
            float dt = max(0.0, t1 - t0);

            // 区間の中心点で uvw を取って σ を trilerp
            vec3 pmid = ro + rd * (0.5*(t0+t1));
            vec3 size = max(bmax - bmin, vec3(1e-8));
            vec3 uvw  = clamp((pmid - bmin) / size, 0.0, 1.0);
 
            vec4 sLo = texelFetch(cornerLoTB, id);
            vec4 sHi = texelFetch(cornerHiTB, id);
            float sigma = trilerp8(sLo, sHi, uvw);

            float a  = 1.0 - exp(-sigma * dt);
            //float a  = 1.0 - exp(-sigma_avg * dt);

            vec3 tfc = vec3(0.6,0.7,1.0); // 好きなTFに
            color += (1.0 - alpha) * a * tfc;
            alpha  = 1.0 - (1.0 - alpha)*(1.0 - a);
            continue;
        }

        // 子へ。空間スキップ
        int childIdx[8] = int[8](cA.x,cA.y,cA.z,cA.w,cB.x,cB.y,cB.z,cB.w);

        // 簡単優先：近い順にしたいなら entry t でバブル2段pushでもOK
        for(int k=0;k<8;k++){
            int cid = childIdx[k];
            if (cid<0) continue;
            vec3 cmn = texelFetch(nodeMinTB, cid).xyz;
            vec3 cmx = texelFetch(nodeMaxTB, cid).xyz;
            float c0=t0, c1=t1;
            if(!rayBox(ro,invd, cmn, cmx, c0,c1)) continue;
            float cmax = texelFetch(nodeMaxTB, cid).w;
            if (cmax <= 0.0) continue; // 空ならスキップ
            if (sp < STACK_MAX){ stack[sp]=cid; t0s[sp]=c0; t1s[sp]=c1; sp++; }
        }

    }

    if(uDebugMode == 10){
        // 訪問回数 heat
        //float t = float(visits) / max(1.0, float(MAX_VISITS));
        float t = float(visits) / 100.;
        FragColor = vec4(heat(t), 1.0);
        return;
    }

//    FragColor = vec4(1,0,1,1);
    FragColor = vec4(color, alpha);
}
)";


const char *wboitParticleShaderSource = R"(
layout (location=0) in vec3  aPos;      // 粒子位置（world）
layout (location=3) in float aHsml;     // smoothing length [world]
layout (location=4) in float aDensity;  // 密度

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// 画面の “焦点距離(px)” = 0.5*H / tan(fovY/2) を CPU 側で渡す
uniform float uFocalPx;
uniform int   uKernelMode;  // 0=Gaussian, 1=Poly6
uniform float uGaussNSigma; // 2.5
uniform float uEnlargeHsml; // 2.0

out VS_OUT {
    float hsmlWorld;
    float density;
    float zView;
    float spriteRadius;
} vs;

void main() {
    vec4 Pw = model * vec4(aPos, 1.0);
    vec4 Pv = view  * Pw;
    gl_Position = projection * Pv;

    vs.zView     = Pv.z;        // 参考：必要なら FS で使える
    vs.hsmlWorld = aHsml;
    vs.density   = aDensity;

    // OpenGL の右手系だとカメラは -Z を向く前提が多いので depth は -Pv.z を使うのが無難
    float R = (uKernelMode == 0) ? (uGaussNSigma * aHsml)   // Gaussian: R = nσ h
                                 : (uEnlargeHsml * aHsml);  // Poly6:   R = h
    vs.spriteRadius = R;

    float depth   = max(1e-6, abs(Pv.z));
    float sizePx  = max(1.0, uFocalPx * (2.0 * R) / depth); // 直径 px
    gl_PointSize  = sizePx;
})";


const char *wboitParticleFragmentShaderSource = R"(
in VS_OUT {
    float hsmlWorld;
    float density;
    float zView;
    float spriteRadius;
} fs;

// --- density → sigma 変換（どちらかを使う） ---
// 1) 1D LUT の場合（密度は 0..1 に正規化されている想定）
uniform sampler1D uRho2Sigma;       // .r に sigma

// 着色（必要なら TF を使って差し替え）
uniform vec3  uBaseColor = vec3(1.0);
uniform int   uKernelMode;     // 0=Gaussian, 1=Poly6
uniform float uGaussNSigma;    // Gaussian のみ
uniform float uEnlargeHsml; 

// WBOIT 出力（蓄積ターゲット 2 枚）
layout(location=0) out vec4  oAccum;   // rgb: color*alpha*weight, a: alpha*weight
layout(location=1) out float oReveal;  // 透過の積（*で蓄積するため log で加算にする実装もあるがここは単純に）

void main(){
    // point sprite 内の円形カットアウト
    vec2  uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;

    float R = fs.spriteRadius;
    float r = sqrt(r2);
    float b = r * R;

    // 密度→σ
    float rho   = fs.density;
    //float sigma = texture(uRho2Sigma, clamp(rho, 0.0, 1.0)).r;
    float sigma = 0.1;
    if(rho < 0.01)
        sigma = 0.;

    float tau;
    if (uKernelMode == 0) {
        // --- Gaussian： τ = σ * √π h * exp(-(b/h)^2) ---
        float q2 = (b*b) / max(1e-12, fs.hsmlWorld*fs.hsmlWorld);
        tau = sigma * sqrt(3.14159265) * fs.hsmlWorld * exp(-q2);
        
        // ※ R は nσ*h にしているので、スプライト端(ρ=1)で exp(-(nσ)^2) までしか描かない
        //   例: nσ=2.5 → exp(-6.25)=0.0019 で十分減衰
    }
    else {
        // --- Poly6 近似（見た目寄せ） ---
        if (b < R) {
            float x = 1.0 - (b*b) / (R*R); // 0..1
            float kernel = x*x*x;                // (1 - (b/h)^2)^3
            float thickness = 2.0 * sqrt(max(0.0, R*R - b*b));
            tau = sigma * thickness * kernel;
        } else {
            tau = 0.0;
        }
    }

    float alpha = clamp(1.0 - exp(-tau), 0.0, 1.0);

    // WBOIT の weight（シンプル版。必要に応じて McGuire の式に）
    // 例: weight = clamp(alpha + 1e-2, 1e-3, 1.0);
    float weight = clamp(alpha + 1e-2, 1e-3, 1.0);

    vec3 color = uBaseColor; // ここを TF にすると視覚的にわかりやすい

    oAccum  = vec4(color * alpha * weight, alpha * weight);
    oReveal = alpha;   // 後段で積算（最終 α = 1 - Π(1-α_i) の近似）
}
)";

const char *wboitResolveShaderSource = R"(
const vec2 V[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main(){
gl_Position = vec4(V[gl_VertexID],0,1);
}
)";

const char *wboitResolveFragmentShaderSource = R"(
uniform sampler2D uAccumTex;   // RGBA（蓄積）
uniform sampler2D uRevealTex;  // R   （1-α の積の近似）
out vec4 FragColor;

void main(){
    vec2  uv   = gl_FragCoord.xy / vec2(textureSize(uAccumTex, 0));
    vec4  acc  = texture(uAccumTex,  uv);
    float rev  = texture(uRevealTex, uv).r;

    // 逆重み取り（acc.a が weight*alpha の総和）
    vec3  col  = (acc.a > 1e-6) ? (acc.rgb / acc.a) : vec3(0.0);
    float aFin = 1.0 - clamp(rev, 0.0, 1.0);

    FragColor = vec4(col, aFin);
}
)";
#endif

static float quadVertices[] = {
    //   inPos (x,y)   inTexCoord (u,v)
    -1.f, -1.f,    0.f, 0.f,
     1.f, -1.f,    1.f, 0.f,
     1.f,  1.f,    1.f, 1.f,

    -1.f, -1.f,    0.f, 0.f,
     1.f,  1.f,    1.f, 1.f,
    -1.f,  1.f,    0.f, 1.f
};

GLuint quadVAO = 0;
GLuint quadVBO = 0;

void SetupQuad()
{
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);

    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // inPos -> location=0
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // inTexCoord -> location=1
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}


// ------------------------------
// normalize 用グローバル変数
// ------------------------------
// 例：利用可能なカラーマップのテクスチャID（これらは InitColorMaps() 内で作成）
GLuint jetTex, viridisTex, plasmaTex;
// … 他にも必要なら追加

float crossSize = 0.05f;


void InitApplication() {
  // 設定のロード（前回の状態を復元）
}

GLFWwindow* window = nullptr;

void InitGLFW() {
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    exit(EXIT_FAILURE);
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "3D Particle Visualization", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    exit(EXIT_FAILURE);
  }
  glfwMakeContextCurrent(window);
  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetScrollCallback(window, scroll_callback);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    exit(EXIT_FAILURE);
  }
  glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_PROGRAM_POINT_SIZE); 

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

  int fbW, fbH;
  glfwGetFramebufferSize(window, &fbW, &fbH);
  framebuffer_size_callback(window, fbW, fbH);
}

void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImPlot::CreateContext();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; 
    
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

GLuint particleProgram, lineProgram;
GLuint velocityArrowShader = 0; // 上記のシェーダープログラムID
GLuint quadShader = 0;

#ifdef GEOMETRICAL_ANALYSIS
GLuint ellipseProgram = 0;
GLuint ellipsoidProgram = 0;
GLuint diskProgram = 0;
#endif

#ifdef ISO_CONTOUR
GLuint isocontourProgram;
#endif

#ifdef STREAM_LINE
GLuint streamlineProgram;
#endif

#ifdef VOLUME_RENDERING
GLuint rtProgram;
GLuint octrayProgram;
GLuint upscaleProgram;

GLuint wboitParticleProgram;
GLuint wboitCompositeProgram;
#endif

GLuint cubicProgram, coordProgram;

#define N_LINES_FOR_CROSS 3

void InitShaders() {
  particleProgram = createShaderProgram(particleVertexShaderSource, particleFragmentShaderSource);
  lineProgram = createShaderProgram(lineVertexShaderSource, lineFragmentShaderSource);
  velocityArrowShader = createShaderProgram(velocityArrowVertexShaderSource, velocityArrowFragmentShaderSource);

#ifdef ISO_CONTOUR
  isocontourProgram = createShaderProgram(isocontourVertexShaderSource, isocontourFragmentShaderSource);
#endif

#ifdef GEOMETRICAL_ANALYSIS
  ellipseProgram = createShaderProgram(ellipseVertexShaderSource, ellipseFragmentShaderSource);
  ellipsoidProgram = createShaderProgram(ellipsoidVertexShaderSource, ellipsoidFragmentShaderSource);
  diskProgram = createShaderProgram(diskVertexShaderSource, diskFragmentShaderSource);  
#endif

#ifdef STREAM_LINE
  streamlineProgram = createShaderProgram(streamlineVertexShaderSource, streamlineFragmentShaderSource);
#endif

  cubicProgram = createShaderProgram(cubicShaderSource, cubicFragmentShaderSource);
  coordProgram = createShaderProgram(coordShaderSource, coordFragmentShaderSource);
  quadShader = createShaderProgram(colormap2DShaderSource, colormap2DFragmentShaderSource);
  // Quad 準備
  SetupQuad();

#ifdef VOLUME_RENDERING
  rtProgram = createShaderProgramWithHeader(fullscreenShaderSource, rtFragmentShaderSource, shaderHeader410);
  octrayProgram = createShaderProgramWithHeader(fullscreenShaderSource, octrayFragmentShaderSource, shaderHeader410);
  upscaleProgram = createShaderProgramWithHeader(upscaleVS, upscaleFS, shaderHeader410);

  wboitParticleProgram = createShaderProgramWithHeader(wboitParticleShaderSource, wboitParticleFragmentShaderSource, shaderHeader410);
  wboitCompositeProgram = createShaderProgramWithHeader(wboitResolveShaderSource, wboitResolveFragmentShaderSource, shaderHeader410);
#endif
}


// グローバル変数（初期化時に生成）
GLuint velocityArrowVAO = 0, velocityArrowVBO = 0, instanceVBO = 0;
int arrowVertexCount = 2;       // 直線なので2頂点
int instanceCount = 0;          // 更新時に粒子数をセット

// 矢印モデルの頂点データ（直線）
float arrowVertices[] = {
    0.0f, 0.0f, 0.0f, // 始点（原点）
    0.0f, 0.0f, 1.0f  // 終点（Z軸方向、1単位）
};

void InitVelocityArrowGeometry() {
    glGenVertexArrays(1, &velocityArrowVAO);
    glGenBuffers(1, &velocityArrowVBO);
    glGenBuffers(1, &instanceVBO);

    glBindVertexArray(velocityArrowVAO);

    // モデル頂点データ
    glBindBuffer(GL_ARRAY_BUFFER, velocityArrowVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(arrowVertices), arrowVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // インスタンス属性用のVBO（各粒子につき、位置(vec3)と速度(vec3)の合計6要素）
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    // 初期は空データ。後でUpdateVelocityInstanceBuffer()で更新する。
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    // instancePos：location 1
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribDivisor(1, 1); // インスタンス属性
    // instanceVel：location 2
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

GLuint particleVAO, particleVBO, crossVAO, crossVBO;
GLuint cubicVAO, cubicVBO, cubicEBO, cubicInstanceVBO, cubicOpacityVBO;
GLuint coordVAO, coordVBO;
#ifdef STREAM_LINE
GLuint streamlineVAO, streamlineVBO;
#endif
#ifdef VOLUME_RENDERING
GLuint fullscreenVAO;
#endif
#ifdef GEOMETRICAL_ANALYSIS
GLuint ellipsoidVAO = 0, ellipsoidVBO = 0;
GLuint diskVAO, diskVBO, diskEBO;
GLsizei g_indexCountDisk;

#ifdef USE_ELLIPSES
const int maxSegmentsEllipse=128;

static std::vector<glm::vec3> buildWireSphere(int slices = 64)
{
    const float TWO_PI = 6.28318530718f;
    std::vector<glm::vec3> verts; verts.reserve(3*slices);

    /* (1) XY 平面 */
    for (int i=0;i<slices;++i){
        float t = i*TWO_PI/slices;
        verts.emplace_back(std::cos(t), std::sin(t), 0.f);
    }
    /* (2) YZ 平面 */
    for (int i=0;i<slices;++i){
        float t = i*TWO_PI/slices;
        verts.emplace_back(0.f, std::cos(t), std::sin(t));
    }
    /* (3) ZX 平面 */
    for (int i=0;i<slices;++i){
        float t = i*TWO_PI/slices;
        verts.emplace_back(std::cos(t), 0.f, std::sin(t));
    }
    return verts;
}
#else
GLuint ellipsoidIBO = 0;
struct Vtx { glm::vec3 pos; glm::vec3 nrm; };
std::vector<Vtx>          sphereV;
std::vector<uint32_t>     sphereI;

// ---------------- build UV-sphere  (stacks×slices = 64×128 くらいで十分)
void buildSphereMesh(int stacks, int slices,
                     std::vector<Vtx>& V, std::vector<uint32_t>& I)
{
  const float PI = 3.1415926535f;
  for (int i=0;i<=stacks;++i){
    float v = float(i)/stacks;
    float phi = PI*(v-0.5f);              // -π/2 .. +π/2
    float z = std::sin(phi);
    float r = std::cos(phi);
    for (int j=0;j<=slices;++j){
      float u = float(j)/slices;
      float theta = 2*PI*u;
      float x = r*std::cos(theta);
      float y = r*std::sin(theta);
      glm::vec3 p(x,y,z);
      V.push_back({p,p});               // 半径1なので法線=位置
    }
  }
  for (int i=0;i<stacks;++i)
    for (int j=0;j<slices;++j){
      int a=i*(slices+1)+j;
      int b=(i+1)*(slices+1)+j;

      uint32_t a32 = static_cast<uint32_t>(a);
      uint32_t b32 = static_cast<uint32_t>(b);
	    
      I.insert(I.end(),{a32,b32,b32+1, a32,b32+1,a32+1});      // 2 三角形/クアッド
    }
}
#endif

struct MeshData {
  std::vector<glm::vec3> verts;
  std::vector<uint32_t>  inds;
};

inline MeshData buildFlatDiskMesh(int slices = 64)
{
  MeshData m;
  m.verts.reserve(2 + (slices+1)*2);
  m.inds .reserve(slices*12);          // 粗い見積り

  /* 上下中心 */
  m.verts.push_back({0, 0.5f, 0});
  m.verts.push_back({0,-0.5f, 0});

  for (int i=0;i<=slices;++i){
    float th = 2.f*glm::pi<float>()*i/slices;
    float x = std::cos(th), z = std::sin(th);
    m.verts.push_back({x, 0.5f, z});
    m.verts.push_back({x,-0.5f, z});
  }
  /* 上面・下面ファン */
  for (int i=0;i<slices;++i){
    m.inds.insert(m.inds.end(), { 0u, 2u+i*2u, 2u+(i+1u)*2u });
    m.inds.insert(m.inds.end(), { 1u, 3u+(i+1u)*2u, 3u+i*2u });
  }
  /* 側面 */
  for (int i=0;i<slices;++i){
    uint32_t a=2u+i*2u, b=a+1u, c=2u+(i+1u)*2u, d=c+1u;
    m.inds.insert(m.inds.end(), { a,b,c,  c,b,d });
  }
  return m;
}
#endif

float cubicVerts[] = {
    -0.5f, -0.5f, -0.5f,  
     0.5f, -0.5f, -0.5f,  
     0.5f,  0.5f, -0.5f,  
    -0.5f,  0.5f, -0.5f,  
    -0.5f, -0.5f,  0.5f,  
     0.5f, -0.5f,  0.5f,  
     0.5f,  0.5f,  0.5f,  
    -0.5f,  0.5f,  0.5f   
};
unsigned int cubicIdx[] = {
    // back face
    0,1,2,  2,3,0,
    // front face
    4,5,6,  6,7,4,
    // left face
    4,0,3,  3,7,4,
    // right face
    1,5,6,  6,2,1,
    // bottom face
    4,5,1,  1,0,4,
    // top face
    3,2,6,  6,7,3
};

void InitBuffers() {
  // ---- パーティクルデータの VAO/VBO を作成 ----
  glGenVertexArrays(1, &particleVAO);
  glGenBuffers(1, &particleVBO);

  glBindVertexArray(particleVAO);
  glBindBuffer(GL_ARRAY_BUFFER, particleVBO);    
    
  // 初期データを転送（サイズは `originalParticles` に応じて変更可能）
  glBufferData(GL_ARRAY_BUFFER, P->particleBlock.particles.size() * sizeof(ParticleData), P->particleBlock.particles.data(), GL_DYNAMIC_DRAW);
    
  // attribute 0: pos → offset 0
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleData),  (void*)offsetof(ParticleData, pos));
  glEnableVertexAttribArray(0);
  // attribute 1: type
  glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE, sizeof(ParticleData), (void*)offsetof(ParticleData, type));
  glEnableVertexAttribArray(1);
  // attribute 2: flag
  glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(ParticleData), (void*)offsetof(ParticleData, flag_stress));
  glEnableVertexAttribArray(2);
  // attribute 3: Hsml
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleData), (void*)offsetof(ParticleData, Hsml));
  glEnableVertexAttribArray(3);
  
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleData), (void*)offsetof(ParticleData, val_show));
  glEnableVertexAttribArray(4);
    
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  // ---- 交差点マーカーの VAO/VBO を作成 ----
  glGenVertexArrays(1, &crossVAO);
  glGenBuffers(1, &crossVBO);

  glBindVertexArray(crossVAO);
  glBindBuffer(GL_ARRAY_BUFFER, crossVBO);

  // 交差点マーカー用の 6点データ（初期化は `nullptr` で後から `glBufferSubData()` で更新）
  glBufferData(GL_ARRAY_BUFFER, 2 * N_LINES_FOR_CROSS * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

#ifdef GEOMETRICAL_ANALYSIS
#ifdef USE_ELLIPSES
  const int slices = maxSegmentsEllipse;     // 例: 64
  std::vector<glm::vec3> wireVerts = buildWireSphere(slices);
  const int totalVerts = static_cast<int>(wireVerts.size());
    
  glGenVertexArrays(1, &ellipsoidVAO);
  glGenBuffers(1,      &ellipsoidVBO);
    
  glBindVertexArray(ellipsoidVAO);
  glBindBuffer(GL_ARRAY_BUFFER, ellipsoidVBO);
  glBufferData(GL_ARRAY_BUFFER,
	       sizeof(glm::vec3) * totalVerts,
	       wireVerts.data(),          // ← 実データ
	       GL_STATIC_DRAW);
    
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
  glBindVertexArray(0);
#else
  buildSphereMesh(64,128,sphereV,sphereI);

  glGenVertexArrays(1,&ellipsoidVAO);
  glGenBuffers(1,&ellipsoidVBO);
  glGenBuffers(1,&ellipsoidIBO);

  glBindVertexArray(ellipsoidVAO);
  glBindBuffer(GL_ARRAY_BUFFER,ellipsoidVBO);
  glBufferData(GL_ARRAY_BUFFER,sphereV.size()*sizeof(Vtx),
	       sphereV.data(),GL_STATIC_DRAW);
  
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ellipsoidIBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,sphereI.size()*sizeof(uint32_t),
	       sphereI.data(),GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);                 // pos
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vtx),(void*)0);
  glBindVertexArray(0);
#endif

  MeshData m = buildFlatDiskMesh();
  glGenVertexArrays(1,&diskVAO);
  glGenBuffers(1,&diskVBO);
  glGenBuffers(1,&diskEBO);

  glBindVertexArray(diskVAO);

  /* VBO */
  glBindBuffer(GL_ARRAY_BUFFER, diskVBO);
  glBufferData(GL_ARRAY_BUFFER, m.verts.size()*sizeof(glm::vec3), m.verts.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),(void*)0);

  /* EBO */
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, diskEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.inds.size()*sizeof(uint32_t), m.inds.data(), GL_STATIC_DRAW);

  glBindVertexArray(0);
  g_indexCountDisk = static_cast<GLsizei>(m.inds.size());
#endif

#ifdef STREAM_LINE
  glGenVertexArrays(1, &streamlineVAO);
  glGenBuffers     (1, &streamlineVBO);

  glBindVertexArray(streamlineVAO);
  glBindBuffer(GL_ARRAY_BUFFER, streamlineVBO);
  // 頂点属性 layout(location=0) に vec3 を割り当て
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
  glBindVertexArray(0);
#endif

#ifdef VOLUME_RENDERING
  glGenVertexArrays(1, &fullscreenVAO);
#endif
  
  glGenVertexArrays(1, &cubicVAO);
  glGenBuffers(1, &cubicVBO);
  glGenBuffers(1, &cubicEBO);
  glGenBuffers(1, &cubicInstanceVBO);
  glGenBuffers(1, &cubicOpacityVBO);
  
  // 2) VAO に頂点属性を登録
  glBindVertexArray(cubicVAO);

  // ── (a) 既存の頂点座標バッファ
  glBindBuffer(GL_ARRAY_BUFFER, cubicVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cubicVerts), cubicVerts, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

  // ── (b) インデンス行列用バッファ（まだ中身は入れない。後で glBufferData します）
  glBindBuffer(GL_ARRAY_BUFFER, cubicInstanceVBO);
  // 4×4 行列は vec4 が 4 つに分かれるので attribute location 1,2,3,4 を使う
  GLsizei vec4Size = sizeof(glm::vec4);
  for (GLuint i = 0; i < 4; ++i) {
    glEnableVertexAttribArray(1 + i);
    glVertexAttribPointer(
			  1 + i,             // location 1,2,3,4
			  4,                  // vec4
			  GL_FLOAT, GL_FALSE,
			  sizeof(glm::mat4),  // ストライド
			  (void*)(i * vec4Size)
			  );
    glVertexAttribDivisor(1 + i, 1);  // ★ インスタンスごとに１個進める
  }

  // ── (c) インスタンス不透明度用バッファ (location = 5)
  glBindBuffer(GL_ARRAY_BUFFER, cubicOpacityVBO);
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(
			5,               // location 5
			1,               // float
			GL_FLOAT,
			GL_FALSE,
			sizeof(float),   // stride (1要素)
			(void*)0         // offset
			);
  glVertexAttribDivisor(5, 1);
  
  // ── (c) インデックスバッファ
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubicEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubicIdx), cubicIdx, GL_STATIC_DRAW);
  
  glBindVertexArray(0);

  float axesVertsColored[] = {
    //  pos         ,    color
    0,0,0,          1,0,0,    // X 軸 始点（赤）
    1,0,0,          1,0,0,    // X 軸 終点
    0,0,0,          0,1,0,    // Y 軸 始点（緑）
    0,1,0,          0,1,0,    // Y 軸 終点
    0,0,0,          1,1,1,    // Z 軸 始点（青）
    0,0,1,          1,1,1     // Z 軸 終点
  };
  
  glGenVertexArrays(1, &coordVAO);
  glGenBuffers(1, &coordVBO);

  glBindVertexArray(coordVAO);
  glBindBuffer(GL_ARRAY_BUFFER, coordVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(axesVertsColored),
	       axesVertsColored, GL_STATIC_DRAW);

  // aPos (location = 0)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
			6 * sizeof(float), (void*)0);

  // aColor (location = 1)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
			6 * sizeof(float),
			(void*)(3 * sizeof(float)));

  glBindVertexArray(0);
  
  InitVelocityArrowGeometry();
}

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

void uploadOctTree_TBO(OctTreeState& state){
  if (!state.cpuTree || state.order.empty() || state.info.size() != state.order.size()) {
    return;
  }

  const int N = (int)state.order.size();
  std::vector<glm::vec4> vMin(N), vMax(N);
  std::vector<glm::ivec4> vChA(N), vChB(N);
  std::vector<glm::vec4> vCornerLo(N), vCornerHi(N);

  for (int i=0;i<N;++i){
    const auto* n = state.order[i];
    const auto& ni= state.info[i];

    vMin[i] = glm::vec4(n->box.min, ni.sigmaAvg);
    vMax[i] = glm::vec4(n->box.max, ni.sigmaMax);
    vChA[i] = glm::ivec4(ni.child[0], ni.child[1], ni.child[2], ni.child[3]);
    vChB[i] = glm::ivec4(ni.child[4], ni.child[5], ni.child[6], ni.child[7]);

    float sig[8];
    for (int c=0;c<8;++c) {
      float rho = n->edgeValues[c];
      sig[c] = g_rho2sigma(rho);
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

  state.gpuTree = G;
}  
#endif

static GLuint CreateColormapTexture1D(const float* rgb, int nColors)
{
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_1D, tex);

  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB32F, nColors, 0, GL_RGB, GL_FLOAT, rgb);
  glBindTexture(GL_TEXTURE_1D, 0);
  return tex;
}

void InitColorMaps()
{
  for (int i = 0; i < gNumColormaps; ++i) {
    colormapTextures[i] =
      CreateColormapTexture1D(gColormapDefs[i].data, gColormapDefs[i].count);
  }
}

void InitColorBar() {
    // 画面右下に表示するカラーバーの頂点データ（NDC 座標）
    // ここでは、左下 (0.70, -0.95)、右下 (0.85, -0.95)、右上 (0.85, -0.65)、左上 (0.70, -0.65) の矩形を定義
    float dummyVertices[] = {
        // 位置 (x, y)      // テクスチャ座標 (u, v)
         0.70f, -0.95f,     0.0f, 0.0f,   // bottom-left
         0.85f, -0.95f,     1.0f, 0.0f,   // bottom-right
         0.85f, -0.9f,     1.0f, 1.0f,   // top-right
         0.70f, -0.9f,     0.0f, 1.0f    // top-left
	 };
    
    // インデックス（2つの三角形で矩形を構成）
    unsigned int indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    // VAO, VBO, EBO の生成
    glGenVertexArrays(1, &colorbarVAO);
    glGenBuffers(1, &colorbarVBO);
    glGenBuffers(1, &colorbarEBO);

    glBindVertexArray(colorbarVAO);

    // 頂点データのバッファ転送
    glBindBuffer(GL_ARRAY_BUFFER, colorbarVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(dummyVertices), dummyVertices, GL_STATIC_DRAW);

    // インデックスデータのバッファ転送
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, colorbarEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // 頂点属性の設定
    // 位置属性：location = 0, vec2, stride = 4 * sizeof(float)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // テクスチャ座標属性：location = 1, vec2, オフセット = 2 * sizeof(float)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // シェーダープログラムの生成（createShaderProgram() はご利用のヘルパー関数）
    colorbarProgram = createShaderProgram(colorbarVertexShaderSource, colorbarFragmentShaderSource);
}

#ifdef ISO_CONTOUR
inline void computeNormals(const TrackingVector<float>& verts, const TrackingVector<unsigned>& inds, TrackingVector<float>& out_normals)
{
    size_t vcount = verts.size() / 3;
    std::vector<glm::vec3> normals(vcount, glm::vec3(0.0f));
    // 各三角形フェイス法線を頂点に加算
    for (size_t i = 0; i < inds.size(); i += 3) {
        unsigned i0 = inds[i+0], i1 = inds[i+1], i2 = inds[i+2];
        glm::vec3 v0{verts[3*i0+0], verts[3*i0+1], verts[3*i0+2]};
        glm::vec3 v1{verts[3*i1+0], verts[3*i1+1], verts[3*i1+2]};
        glm::vec3 v2{verts[3*i2+0], verts[3*i2+1], verts[3*i2+2]};
	
	glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
	if (glm::length(n) > 1e-6f) n = glm::normalize(n);
	
        normals[i0] += n;
        normals[i1] += n;
        normals[i2] += n;
    }
    // 頂点法線を正規化してフラット配列に
    out_normals.resize(verts.size());
    for (size_t v = 0; v < vcount; ++v) {
        glm::vec3 n = glm::normalize(normals[v]);
        out_normals[3*v+0] = n.x;
        out_normals[3*v+1] = n.y;
        out_normals[3*v+2] = n.z;
    }
}

static void uploadMeshWithNormals(const TrackingVector<float>& verts, const TrackingVector<unsigned>& inds,
				  GLuint& vao, GLuint& vbo, GLuint& nbo, GLuint& ebo)
{
    // 法線を計算
    TrackingVector<float> normals;
    computeNormals(verts, inds, normals);

    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &nbo);
        glGenBuffers(1, &ebo);
    }
    glBindVertexArray(vao);

    // 頂点座標
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    // 頂点法線
    glBindBuffer(GL_ARRAY_BUFFER, nbo);
    glBufferData(GL_ARRAY_BUFFER, normals.size()*sizeof(float), normals.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    // インデックス
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, inds.size()*sizeof(unsigned), inds.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}
#endif

void ShowTime(){
    // 画面の左上（ピクセル座標 (10,10)）にウィンドウを固定する
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    // 背景を透明にしたい場合
    ImGui::SetNextWindowBgAlpha(0.3f);
    // ウィンドウフラグでタイトルバーや枠、スクロールバーを非表示にする
    ImGui::Begin("Time Overlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings);
    // glfwGetTime() を用いて経過時間を表示（必要に応じてフォーマットを変更してください）
    ImGui::Text("Time: %.4f", P->particleBlock.header.time);
    ImGui::End();
}

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


float radiusCullingSphere = 1.;

void ShowSettingsUI() {
  ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysVerticalScrollbar);
  ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", camCtx.cameraPos.x, camCtx.cameraPos.y, camCtx.cameraPos.z);
  ImGui::Text("Camera Target:   (%.2f, %.2f, %.2f)", camCtx.cameraTarget.x, camCtx.cameraTarget.y, camCtx.cameraTarget.z);

  // グローバル変数として、各タイプのカラーマップ選択インデックスを保持
  static int colormapIndex[6] = { 0, 1, 2, 0, 1, 2 }; // 0:Jet, 1:Viridis, 2:Plasma
  
  // 利用可能なカラーマップ名
  const char* availableColormapNames[] = { "Jet", "Viridis", "Plasma" };
  
  if (ImGui::CollapsingHeader("Particle Type Settings"))
    {
      for (int i = 0; i < 6; i++) {
	std::string header = "Type " + std::to_string(i);
	if (ImGui::TreeNode(header.c_str())) {
	  auto& cfg = gParticleVisualConfig.types[i];

	  std::string comboLabel = "Colormap##" + std::to_string(i);
	  const char* preview = gColormapDefs[cfg.colormapIndex].name;
	  if (ImGui::BeginCombo(comboLabel.c_str(), preview)) {
	    for (int k = 0; k < gNumColormaps; ++k) {
	      bool selected = (cfg.colormapIndex == k);
	      if (ImGui::Selectable(gColormapDefs[k].name, selected)) {
		cfg.colormapIndex = k;
	      }
	      if (selected) ImGui::SetItemDefaultFocus();
	    }
	    ImGui::EndCombo();
	  }
	  
	  ImGui::Checkbox("Periodic Color Bar", &cfg.periodicColorBar);
	  
	  std::string sliderLabel = "Point Size##" + std::to_string(i);
	  ImGui::SliderFloat(sliderLabel.c_str(), &cfg.pointSize, 1.0f, 100.0f);
	  std::string minLabel = "Value Min##" + std::to_string(i);
	  ImGui::InputFloat(minLabel.c_str(), &cfg.colorMin, 0.01f, 0.1f, "%.3f");
	  std::string maxLabel = "Value Max##" + std::to_string(i);
	  ImGui::InputFloat(maxLabel.c_str(), &cfg.colorMax, 0.01f, 0.1f, "%.3f");
	  std::string logLabel = "Use Log Scale##" + std::to_string(i);
	  ImGui::Checkbox(logLabel.c_str(), &cfg.useLogScale);

	  bool flagHideParticles_prev =  static_cast<bool>(cfg.hideParticles);
	  std::string hideLabel = "Hide particle##" + std::to_string(i);
	  ImGui::Checkbox(hideLabel.c_str(), &cfg.hideParticles);
	  if(flagHideParticles_prev != cfg.hideParticles)
	    P->particlesDirty = true;
	  
	  QuantityId icolor_prev = cfg.selectedQuantity;
	  QuantityId& sel = cfg.selectedQuantity;

	  if (ImGui::BeginCombo("Quantity", QuantityLabel(sel))) {
	    for (int q = 0; q < P->particleBlock.nUIQ; ++q) {
	      QuantityId cand = P->particleBlock.uiQ[q];
	      bool is_selected = (cand == sel);
	      if (ImGui::Selectable(QuantityLabel(cand), is_selected)) sel = cand;
	      if (is_selected) ImGui::SetItemDefaultFocus();
	    }
	    ImGui::EndCombo();
	  }

	  auto findIndex = [&](QuantityId q)->int{
	    for (int k = 0; k < P->particleBlock.nUIQ; ++k) if (P->particleBlock.uiQ[k] == q) return k;
	    return 0; // fallback
	  };

	  int qidx = findIndex(sel);
	  ImGui::Text("Current particle %s range: [%g, %g]",
		      QuantityLabel(sel),
		      P->particleValueMin[qidx][i],
		      P->particleValueMax[qidx][i]);

	  if(icolor_prev != cfg.selectedQuantity){
	    P->particlesDirty = true;  // グローバルなフラグをtrueに設定
	  }
	  
	  ImGui::TreePop();
	}
      }
    }

  if(ImGui::CollapsingHeader("File Navigation")){
    ImGui::InputText("Folder", gFileInfo->folderPath, IM_ARRAYSIZE(gFileInfo->folderPath));
    ImGui::InputText("File Format", gFileInfo->fileFormat, IM_ARRAYSIZE(gFileInfo->fileFormat));
    ImGui::InputInt("initialIndex", &gFileInfo->initialIndex);

    char fileNameOnly[255];
    std::snprintf(fileNameOnly, sizeof(fileNameOnly), gFileInfo->fileFormat, gFileInfo->initialIndex);
    std::snprintf(gFileInfo->filePath, sizeof(gFileInfo->filePath), "%s/%s", gFileInfo->folderPath, fileNameOnly);
    
    if (ImGui::Button("Browse Folder")) {
#ifndef NONATIVEFILEDIALOG
      char* outPath = nullptr;
      // NFD_PickFolder は outPath に選択されたパスを malloc() で割り当てるので、後で free() が必要です。
      nfdresult_t result = NFD_OpenDialog("", gFileInfo->folderPath, &outPath);
      if (result == NFD_OKAY) {
        // 選択されたパスを folderPath にコピー
        std::strncpy(gFileInfo->filePath, outPath, IM_ARRAYSIZE(gFileInfo->folderPath));
        free(outPath);

        // 1. フォルダ部分を抽出
        char* lastSlash = std::strrchr(gFileInfo->filePath, '/');
        if (lastSlash) {
	  size_t folderLen = lastSlash - gFileInfo->filePath + 1; // '/'を含む
	  std::strncpy(gFileInfo->folderPath, gFileInfo->filePath, folderLen);
	  gFileInfo->folderPath[folderLen] = '\0';	    
	}

	char *filename = gFileInfo->filePath;
	if(lastSlash)
	  filename = lastSlash + 1;

#ifdef HAVE_HDF5
        // 2. ファイル形式（拡張子）の抽出
	gFileInfo->useHDF5 = false;
        char* dot = std::strrchr(filename, '.');
        if (dot) {
	  std::string ext(dot);
	  if (ext == ".hdf5") 
	    gFileInfo->useHDF5 = true;	  
        }
#endif
	
	auto res = convertFilenameToFormatAndExtractNumber(filename);       
	std::strncpy(gFileInfo->fileFormat, res.first.c_str(), IM_ARRAYSIZE(gFileInfo->fileFormat));
	gFileInfo->fileFormat[IM_ARRAYSIZE(gFileInfo->fileFormat) - 1] = '\0'; 
	gFileInfo->initialIndex = res.second;	
      }
      else if (result == NFD_CANCEL) {
        // ユーザーがキャンセルした場合
        // （特に処理を行わなくてもよい）
      }
      else {
        // エラー発生時
        std::cerr << "Error: " << NFD_GetError() << std::endl;
      }
#else
      IGFD::FileDialogConfig config;
      // 初期ディレクトリの設定（"path" メンバー）
      //config.path = gFileInfo->filePath;
      config.path = gFileInfo->folderPath;
      // 必要なら初期のファイル名を設定（空の場合はユーザー入力待ち）
      config.fileName = "output"; 
      // その他、選択可能なファイル数などの設定はデフォルトのままでOK
      ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", "**", config);
#endif
    }

#ifdef NONATIVEFILEDIALOG
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
      {
	if (ImGuiFileDialog::Instance()->IsOk())
	  {
	    std::string selectedFile = ImGuiFileDialog::Instance()->GetFilePathName();
	    std::string currentFolder = ImGuiFileDialog::Instance()->GetCurrentPath();
	    std::strncpy(gFileInfo->filePath, selectedFile.c_str(), IM_ARRAYSIZE(gFileInfo->filePath));
	    gFileInfo->filePath[IM_ARRAYSIZE(gFileInfo->filePath)-1] = '\0';
	    	    
	    // 1. フォルダ部分を抽出
	    char* lastSlash = std::strrchr(gFileInfo->filePath, '/');
	    if (lastSlash) {
	      size_t folderLen = lastSlash - gFileInfo->filePath + 1; // '/'を含む
	      std::strncpy(gFileInfo->folderPath, gFileInfo->filePath, folderLen);
	      gFileInfo->folderPath[folderLen] = '\0';	    
	    }
	    
	    char *filename = gFileInfo->filePath;
	    if(lastSlash)
	      filename = lastSlash + 1;

#ifdef HAVE_HDF5
	    // 2. ファイル形式（拡張子）の抽出
	    gFileInfo->useHDF5 = false;
	    char* dot = std::strrchr(filename, '.');
	    if (dot) {
	      std::string ext(dot);
	      if (ext == ".hdf5") 
		gFileInfo->useHDF5 = true;	  
	    }
#endif
	    
	    auto res = convertFilenameToFormatAndExtractNumber(filename);       
	    std::strncpy(gFileInfo->fileFormat, res.first.c_str(), IM_ARRAYSIZE(gFileInfo->fileFormat));
	    gFileInfo->fileFormat[IM_ARRAYSIZE(gFileInfo->fileFormat) - 1] = '\0'; 
	    gFileInfo->initialIndex = res.second;	
	  }
	else
	  {
	    ImGui::Text("File Dialog Cancelled");
	  }
	ImGuiFileDialog::Instance()->Close();
      }
#endif
    
    gFileInfo->currentFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
    ImGui::Text("File: %s", gFileInfo->filePath);
    ImGui::Text("Current File: %d", gFileInfo->currentFileIndex);

    ImGui::BeginDisabled(gFileInfo->isLoading);
    
    // **skipStep の調整**
    static int tempSkipStep = gFileInfo->skipStep;
    if (ImGui::InputInt("Skip Step", &tempSkipStep, 1, 100)) {
      int newStep = std::round((gFileInfo->currentFileIndex - gFileInfo->initialIndex) / static_cast<float>(tempSkipStep));
      gFileInfo->currentStep = std::max(0, newStep);
      gFileInfo->skipStep = tempSkipStep;

      int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
      if (!gFileInfo->isLoading)
	gFileInfo->loadBatch(newFileIndex, gFileInfo->batchSize, gFileInfo->skipStep, P);      

      gFileInfo->currentFileIndex = newFileIndex;
    }
    
    // **スライダーで `gFileInfo->currentFileIndex` を選択**
    if (ImGui::InputInt("Select File Index", &gFileInfo->currentStep, 1, 10)) {
      int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
      gFileInfo->loadNewSnapshot(newFileIndex, P);      
    }
    
    // **前のファイル**
    if (ImGui::Button("Previous File") && gFileInfo->currentStep > 0) {
      gFileInfo->currentStep--;
      
      int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
      gFileInfo->loadNewSnapshot(newFileIndex, P);            
    }

    ImGui::SameLine();

    // **次のファイル**
    if (ImGui::Button("Next File")) {
      gFileInfo->currentStep++;

      int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
      gFileInfo->loadNewSnapshot(newFileIndex, P);            
    }

    if(ImGui::InputInt("Batch Size", &gFileInfo->batchSize)){
      if (!gFileInfo->isLoading)
	gFileInfo->loadBatch(gFileInfo->currentStep, gFileInfo->batchSize, gFileInfo->skipStep, P);      
    }

    ImGui::EndDisabled();
    
    // **ロード状況を表示**
    if (gFileInfo->isLoading) {
      ImGui::Text("Loading...");
    }
    
    if (ImGui::Button("Reload")) {
      if (!gFileInfo->isLoading) {
	int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
	
	gFileInfo->loadBatch(gFileInfo->currentStep, gFileInfo->batchSize, gFileInfo->skipStep, P);      
        std::cout << "Reloaded files starting at file " << newFileIndex << std::endl;
      }
    }
    
    if (ImGui::Button("Edit Data Format")) {
#ifdef HAVE_HDF5
      if (gFileInfo->useHDF5 || gFileInfo->getFormatMode() == static_cast<int>(FileFormat::HDF5))
	gFileInfo->showHDF5Dialog();
      else
#endif
	gFileInfo->showDialog();      
    }

    static const char* FileFormatNames[] = {
      "Auto", "HDF5", "Binary", "Gadget", "Framed"
    };
    // FileFormat::_Count と同じ長さにしておく
    static_assert(static_cast<int>(FileFormat::_Count) == IM_ARRAYSIZE(FileFormatNames), 
		  "FileFormatNames needs to match FileFormat::_Count");
    
    int fmtIdx = gFileInfo->getFormatMode();
    // シンプルに Combo で切り替え
    if (ImGui::Combo("Read data format", &fmtIdx, FileFormatNames, IM_ARRAYSIZE(FileFormatNames))) {
      // ユーザーが選び直したら enum に戻す
      gFileInfo->setFormatMode(static_cast<FileFormat>(fmtIdx));
    }

    if (ImGui::Button("Mask Settings...")) {
      OpenMaskUI();
    }
    
    if (ImGui::Button("Generate test data")) {
      gFileInfo->generateTestData(P);      
    }    
  }
  
  if (ImGui::CollapsingHeader("Normalization")) {
    ImGui::InputFloat("Desired Maximum", &P->desiredMax, 0.f, 0.f, "%g");
    if (ImGui::Button("Normalize Positions")) 
      P->rescalePositions();
    
    ImGui::Text("Original max coordinate: %.3g", P->originalMax);
    ImGui::Text("Max coordinate is normalized to: %.3f", P->desiredMax);
  }
  
  if (ImGui::CollapsingHeader("set sink ID visualization")){
    ImGui::InputFloat("radius", &g_queryRadius, 0.f, 0.f, "%g");
    ImGui::InputInt("number of particles", &g_nqueryparticles);

    g_moveThreshold = 0.1f*g_queryRadius;

    if(ImGui::Button("show sink IDs")){
      g_flagShowSinkIDs = true;
      g_lastCameraPos = camCtx.cameraPos;
    }

    if(ImGui::Button("disable sink IDs")){
      g_flagShowSinkIDs = false;
    }
  }

  
  
  if (ImGui::CollapsingHeader("Set camera pos")) {
    static float centerInput[3] = {0.0f, 0.0f, 0.0f};
    static bool inputIsOriginal = true;
    ImGui::InputFloat3("Center Coordinates", centerInput, "%.3f");
    ImGui::Checkbox("Input in Original Coordinates", &inputIsOriginal);
    if (ImGui::Button("Set Center")) {
      float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
      glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
      
      if (inputIsOriginal) {
	// 入力された座標は original 座標なので、正規化に使用している倍率をかけて normalized 座標に変換
	camCtx.cameraTarget = glm::vec3(centerInput[0], centerInput[1], centerInput[2]) * P->normalizationFactor;
      } else {
	camCtx.cameraTarget = glm::vec3(centerInput[0], centerInput[1], centerInput[2]);
      }

      camCtx.cameraPos = camCtx.cameraTarget - direction * distance;
    }

    static int currentView = 0;
    const char* viewDirections[] = {
      "View from +X", "View from -X",
      "View from +Y", "View from -Y",
      "View from +Z", "View from -Z"
    };
    ImGui::Combo("Projection Direction", &currentView, viewDirections, IM_ARRAYSIZE(viewDirections));

    static float rollAngle = 0.0f; // ロール角度（度単位）
    ImGui::SliderFloat("Roll Angle (deg)", &rollAngle, -180.0f, 180.0f, "%.1f");
    
    if (ImGui::Button("Set Projection")) {
      float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
    
      switch (currentView) {
      case 0: // +X
	camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(distance, 0.0f, 0.0f);
	camCtx.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	break;
      case 1: // -X
	camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(-distance, 0.0f, 0.0f);
	camCtx.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	break;
      case 2: // +Y
	camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, distance, 0.0f);
	camCtx.cameraUp = glm::vec3(0.0f, 0.0f, -1.0f); // Z軸を上方向に
	break;
      case 3: // -Y
	camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, -distance, 0.0f);
	camCtx.cameraUp = glm::vec3(0.0f, 0.0f, 1.0f); // 反対Z軸を上方向に
	break;
      case 4: // +Z
	camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, 0.0f, distance);
	camCtx.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	break;
      case 5: // -Z
	camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, 0.0f, -distance);
	camCtx.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	break;
      }

      // 視線方向ベクトルを取得
      glm::vec3 viewDir = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
      // 回転クォータニオン生成（右手系、正の角度で反時計回り）
      glm::quat rollQuat = glm::angleAxis(glm::radians(rollAngle), viewDir);
      // Upベクトルに適用
      camCtx.cameraUp = rollQuat * camCtx.cameraUp;

      // ビュー行列を更新
      glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
    
      // クォータニオンも更新（必要な場合）
      camCtx.cameraOrientation = glm::quat_cast(glm::inverse(view));
    }

    ImGui::InputFloat("Culling radius", &radiusCullingSphere);    
    if(ImGui::Button("Culling sphere region")){
      for(size_t i=0;i<P->particleBlock.particles.size();i++){
	auto &p = P->particleBlock.particles[i];
	uint8_t flag_mask = 0;
	if(glm::distance(glm::vec3(p.pos[0], p.pos[1], p.pos[2]), camCtx.cameraTarget) > radiusCullingSphere)
	  flag_mask = 1;
	
	P->flag_mask[i] = flag_mask;
      }
      
      P->particlesDirty = true; 
    }

    if(ImGui::Button("disable Culling")){
      for(size_t i=0;i<P->particleBlock.particles.size();i++)
	P->flag_mask[i] = 0;            
      P->particlesDirty = true; 
    }
  }

#ifdef PYTHON_BRIDGE
  if (ImGui::CollapsingHeader("Python:Jupyter notebook")) {
    const bool isOpen = (g_py.ptr != nullptr);

    if (ImGui::Button(isOpen ? "Close notebook" : "Open notebook")) {
      if (!isOpen) {
	// --- Open ---
	g_py.ptr.reset(CreatePythonBridge());
	if (!g_py.ptr) {
	  ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),"Bridge creation failed");
	} else {
	  // 共有メモリ準備
	  const uint64_t N = static_cast<uint64_t>(P->particleBlock.particles.size());
	  if (!g_py.ptr->init(N, /*withB=*/true, "cppvis_pos")) {
	    ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),"Bridge init failed");
	    g_py.ptr.reset();
	  } else {
	    bridge::loadInitialFromAoS(*g_py.ptr, *P, sizeof(ParticleData));	    
	    // Notebook 起動（非同期でもOK／boolで可否だけ握る）
	    g_py.launched = g_py.ptr->launchNotebook("./jupyter_work");
	  }
	}
      } else {
	// --- Close ---
	g_py.ptr->shutdown();
	g_py.ptr.reset();
	g_py.launched = false;
	g_py.needUploadPos = false;
      }
    }

    // ステータス表示
    if (g_py.ptr) {
      ImGui::SameLine();
      ImGui::TextColored(g_py.launched ? ImVec4(0.6f,1,0.6f,1) : ImVec4(1,0.8f,0.4f,1),
			 g_py.launched ? "launched" : "launching...");
    }

    if (g_py.ptr && g_py.launched) {
      const auto& info = g_py.ptr->notebookInfo();

      ImGui::SeparatorText("Jupyter Notebook");
      ImGui::Text("Port : %d", info.port);
      ImGui::TextWrapped("URL  : %s", info.url.c_str());

      // クリップボードにコピー
      ImGui::SameLine();
      if (ImGui::SmallButton("Copy URL")) {
	ImGui::SetClipboardText(info.url.c_str());
      }

      // 既に JupyterLauncher で open/xdg-open 済みでも、手動で開けるボタンを用意
      if (ImGui::SmallButton("Open in Browser")) {
#if defined(__APPLE__)
	std::string cmd = "open \"" + info.url + "\"";
	std::system(cmd.c_str());
#elif defined(__linux__)
	std::string cmd = "xdg-open \"" + info.url + "\"";
	std::system(cmd.c_str());
#elif defined(_WIN32)
	// Windows: start はシェル内蔵。cmd /c 経由で。
	std::string cmd = "cmd /c start \"\" \"" + info.url + "\"";
	std::system(cmd.c_str());
#endif
      }

      // 状態表示（任意）
      ImGui::SameLine();
      ImGui::TextDisabled("(token: %s)", info.token.c_str());
    }
  }
#endif
  
  struct PullDownItem {
    const char* label;
    int mode;
  };

  if (ImGui::CollapsingHeader("Analysis")) {
    enum AnalysisMode {
      ANALYSIS_RADIAL_PROFILE,
      ANALYSIS_2D_HISTOGRAM,
      ANALYSIS_CLUMP_FIND,
      ANALYSIS_STELLAR_DENSITY, 
      ANALYSIS_HALO_CATALOGUE,
      ANALYSIS_POWER_SPEC,
      ANALYSIS_DISK,
      ANALYSIS_ISO_DENSITY
    };
    
    static PullDownItem analysisItems[] = {
      { "radial profile", ANALYSIS_RADIAL_PROFILE },
      { "2D histogram", ANALYSIS_2D_HISTOGRAM },
      { "clump finder", ANALYSIS_CLUMP_FIND },
      { "stellar density", ANALYSIS_STELLAR_DENSITY },
      { "halo catalogue", ANALYSIS_HALO_CATALOGUE },
#ifdef POWER_SPECTRUM
      { "power spectrum", ANALYSIS_POWER_SPEC },
#endif
#ifdef GEOMETRICAL_ANALYSIS
      { "extract disks", ANALYSIS_DISK },
      { "extract iso density", ANALYSIS_ISO_DENSITY },
#endif
    };
    
    static int analysisMode = ANALYSIS_RADIAL_PROFILE;

    // 現在選択中のラベルを探す
    const char* currentLabel = "unknown";
    for (const auto& item : analysisItems) {
      if (item.mode == analysisMode) {
	currentLabel = item.label;
	break;
      }
    }

    if (ImGui::BeginCombo("Analysis mode", currentLabel)) {
      for (const auto& item : analysisItems) {
	bool isSelected = (analysisMode == item.mode);
	if (ImGui::Selectable(item.label, isSelected)) {
	  analysisMode = item.mode;
	}
	if (isSelected)
	  ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    switch (analysisMode) {  
    case ANALYSIS_RADIAL_PROFILE: {
      if (ImGui::Button("Compute radial profile"))
	OpenRadialProfileUI();
      DrawRadialProfileUI(*gRadialProfileComputer, P->particleBlock, P->UnitMass_in_g, P->UnitLength_in_cm, P->UnitTime_in_s);       
      break;
    }
    case ANALYSIS_2D_HISTOGRAM: {
      if (ImGui::Button("Compute 2D histogram"))
	OpenHistogram2DUI();
      DrawHistogram2DUI(*gHistogram2DComputer, P->particleBlock);
      break;
    }
    case ANALYSIS_CLUMP_FIND: {
      if (ImGui::Button("Run Clumps finder")) 
	gClumpFind->showWindow();

#ifdef CLUMP_DATA_READ
      ImGui::Text("create clump data for continuous snapshots");

      static int method = 0;  
    
      // ラジオボタン
      ImGui::RadioButton("FOF",       &method, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Dendrogram",&method, 1);
    
      static int nsnapshots = 10;
      static char outputFileName[255]="clump_data.hdf5";
      static char outputFolderPath[255]="./output/";
      ImGui::InputInt("number of snapshots##FOF", &nsnapshots);
      ImGui::InputText("Output File Name##FOF", outputFileName, IM_ARRAYSIZE(outputFileName));
      ImGui::InputText("Output Folder##FOF", outputFolderPath, IM_ARRAYSIZE(outputFolderPath));

      char filename[512];
      snprintf(filename, sizeof(filename), "%s/%s", outputFolderPath, outputFileName);
    
      ImGui::SameLine();
      if (ImGui::Button("default path")) {
	strcpy(outputFolderPath, gFileInfo->folderPath);
      }

      if(ImGui::Button("generate clump data")){
	int savedStep = gFileInfo->currentStep;

	gClumpFind->initialize_prev_nodes();      
	for(int i=0;i<nsnapshots;i++){
	  gFileInfo->currentStep = savedStep;
	  if(i > 0) gFileInfo->currentStep += i;

	  int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
	  gFileInfo->loadNewSnapshot(newFileIndex, P);            

	  if(P->particleBlock.particles.size() == 0)
	    continue;
	
	  gClumpFind->do_FOF_and_output_clump_data(method, P->particleBlock.particles, P->particleBlock.header, filename, newFileIndex);
	}
            
	gFileInfo->currentStep = savedStep;
	gFileInfo->currentFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;

	int initstep = gFileInfo->currentFileIndex;
	int dstep = gFileInfo->skipStep;
	std::string fname(filename);
	gClumpFind->give_stellar_id_to_clumps(initstep, nsnapshots, dstep, fname);
      }

      if(ImGui::Button("show clump list"))
	gClumpFind->showClumpListWindow();

      if(ImGui::Button("show clump chain list")){
	std::string fname(filename);
	gClumpFind->showWindowClumpChainList(gFileInfo->initialIndex, nsnapshots, gFileInfo->skipStep, fname);
      }
#endif
      break;
    }
      
    case ANALYSIS_STELLAR_DENSITY: {
      static bool selType[6] = { false, false, false, true, true, true };
      
      ImGui::Text("Particle types to include:");
      ImGui::Checkbox("Type 0##stellar_density", &selType[0]); ImGui::SameLine();
      ImGui::Checkbox("Type 1##stellar_density", &selType[1]); ImGui::SameLine();
      ImGui::Checkbox("Type 2##stellar_density", &selType[2]);
      ImGui::Checkbox("Type 3##stellar_density", &selType[3]); ImGui::SameLine();
      ImGui::Checkbox("Type 4##stellar_density", &selType[4]); ImGui::SameLine();
      ImGui::Checkbox("Type 5##stellar_density", &selType[5]);

      static bool flag_overwrite_hsml = false;
      ImGui::Checkbox("overwrite hsml##stellar_density", &flag_overwrite_hsml);

      if (ImGui::Button("Select 3,4,5##stellar_density")) {
	for (int t = 0; t < 6; ++t) selType[t] = false;
	selType[3] = selType[4] = selType[5] = true;
      }
    
      if (ImGui::Button("Compute stellar density##stellar_density")) {
	std::array<bool,6> sel{};
	for (int t=0;t<6;++t) sel[t] = selType[t];
      
	P->computeStellarDensity(sel, flag_overwrite_hsml);
	P->particlesDirty = true;  // グローバルなフラグをtrueに設定
      }
      break;
    }

      
#ifdef HAVE_HDF5
    case ANALYSIS_HALO_CATALOGUE: {
      if(ImGui::Button("Load Halo"))
	OpenHaloesUI();
      DrawHaloesUI(P, camCtx, gFileInfo);
      break;
    }
#endif
      
#ifdef POWER_SPECTRUM
    case ANALYSIS_POWER_SPEC: {
      break;
    }
#endif

#ifdef GEOMETRICAL_ANALYSIS
    case ANALYSIS_DISK: {
      static int queryID_disk=0;
      ImGui::InputInt("Particle ID1##disk", &queryID_disk);
      ImGui::SliderFloat("Opacity##disk", &diskOpacity, 0.0f, 1.0f); 

      DiskRadiusFinder::Params param_disk;
    
      if (ImGui::Button("Find a disk around the paritlce")) {
	bool flag_found = false;
	for(auto &p : P->particleBlock.particles){
	  if(p.ID == queryID_disk){
	    param_disk.mass = p.mass;
	    for(int k=0;k<3;k++){
	      param_disk.center[k] = p.pos[k];
	      param_disk.v_center[k] = p.vel[k];
	    }
	    flag_found = true;
	  }

	  if(flag_found)
	    break;
	}
      
	if(flag_found){
	  showDisks = true;
	  param_disk.G = P->GravConst_internal;	
	  param_disk.max_shell = 100;
	  param_disk.scale_fac = P->originalMax / P->desiredMax;
	
	  gDiskFinder->compute(P->particleBlock.particles, param_disk);
	}
      }
    
      if (ImGui::Button("disable disks")) {
	showDisks = false;
      }

      static char fname_input[255]="binary_fragmentation_ellipticity_all_w_mode.txt";
      static char fname_output[255]="binary_fragmentation_disks.txt";
      ImGui::InputText("Read target from text file##disk", fname_input, IM_ARRAYSIZE(fname_input));
      ImGui::InputText("Output target from text file##disk", fname_output, IM_ARRAYSIZE(fname_output));

      if(ImGui::Button("calc disk radius from text file")){
	struct Row { int idx, idA, idB, snap; };      
	std::vector<Row> rows;
	{
	  std::ifstream fin(fname_input);
	  if (!fin) { std::cerr << "cannot open " << fname_input << '\n'; return; }

	  std::string line;
	  Row r;
	  while (std::getline(fin, line))
	    {
	      if (line.empty() || line[0] == '#')      // ← # 行はスキップ
		continue;
	    
	      std::istringstream iss(line);
	      if (iss >> r.idx >> r.idA >> r.idB >> r.snap)
		rows.push_back(r);                   // 正しく読めた行だけ追加
	      else
		std::cerr << "parse error: " << line << '\n';
	    }
	}

	bool flag_first0 = true;
	for (auto& r : rows){
	  if(r.snap < 0)
	    continue;

	  FILE *fp_out;
	  if(flag_first0){
	    fp_out = std::fopen(fname_output, "w");
	    fprintf(fp_out, "#index idA idB t_disk\n");	  
	    flag_first0 = false; 
	  }else{
	    fp_out = std::fopen(fname_output, "a");
	  }
	
	  double time_disk = -1., time_not_disk = -1.;
	  char fname_evolution[255];
	  snprintf(fname_evolution, sizeof(fname_evolution), "binary_evolution_%d.txt" ,r.idx);
	
	  bool flag_first = true;
	  double dist_disk=0., r_disk1=0., r_disk2=0.;

	  int snap_init = r.snap;
	  snap_init = static_cast<int>(r.snap / gFileInfo->skipStep) * gFileInfo->skipStep;

	  int snap_disk = -1, snap_not_disk = snap_init;
	
	  for (int i=0;i<100;i++) {
	    int snap = snap_init + gFileInfo->skipStep * i;	  
	    gFileInfo->loadNewSnapshot(snap, P);
	    if(P->particleBlock.particles.size() == 0)
	      continue;
	  
	    double r1, r2;
	    float pos1[3], pos2[3];
	    bool flag_found_binary = true;
	    for(int i=0;i<2;i++){
	      int id;
	      float *pos;
	      double *r_disk;
	      if(i==0){
		id = r.idA;
		pos = pos1;
		r_disk = &r1;
	      }else{
		id = r.idB;
		pos = pos2;
		r_disk = &r2;
	      }

	      DiskRadiusFinder::Params param_disk0;
	      bool flag_found = false;
	      for(auto &p : P->particleBlock.particles){
		if(p.ID == id){
		  if(p.type != 0){
		    param_disk0.mass = p.mass;
		    for(int k=0;k<3;k++){
		      param_disk0.center[k] = pos[k] = p.pos[k];
		      param_disk0.v_center[k] = p.vel[k];
		    }
		    flag_found = true;
		  }else
		    break;
		}
	    
		if(flag_found)
		  break;
	      }

	      if(flag_found){
		param_disk0.G = P->GravConst_internal;	
		param_disk0.max_shell = 100;
		param_disk0.scale_fac = P->originalMax / P->desiredMax;
	      
		gDiskFinder->compute(P->particleBlock.particles, param_disk0);
		*r_disk = gDiskFinder->getDiskRadius();
	      }else
		flag_found_binary = false;
	    }

	    if(flag_found_binary == false)
	      continue;

	    FILE *fp_evo;
	    if(flag_first){
	      fp_evo = std::fopen(fname_evolution, "w");
	      flag_first = false;
	      time_not_disk = P->particleBlock.header.time;
	      snap_not_disk = snap;
	    }else
	      fp_evo = std::fopen(fname_evolution, "a");

	    if (!fp_evo) { std::cerr << "cannot open " << fname_output << '\n'; return; }
	
	    if (flag_first) {                       /* ← ① ヘッダは最初だけ */
	      std::fprintf(fp_out, "index ID1 ID2 snap n a b c\n");
	      flag_first = false;
	    }
	  
	    double dist2 = (pos1[0] - pos2[0])*(pos1[0] - pos2[0]) + (pos1[1] - pos2[1])*(pos1[1] - pos2[1]) + (pos1[2] - pos2[2])*(pos1[2] - pos2[2]);
	    bool flag_disk = (sqrt(dist2) < r1 + r2?1:0);
	    double scale_fac = P->originalMax / P->desiredMax;
	  
	    std::fprintf(fp_evo, "%d %g %g %g %g %d\n"
			 , snap, P->particleBlock.header.time, sqrt(dist2)*scale_fac, r1*scale_fac, r2*scale_fac, static_cast<int>(flag_disk));
	    std::fclose(fp_evo);

	    if(flag_disk){
	      time_disk = P->particleBlock.header.time;
	      dist_disk = sqrt(dist2) * scale_fac;
	      snap_disk = snap;
	      r_disk1 = r1 * scale_fac;
	      r_disk2 = r2 * scale_fac;	    
	      break;
	    }else{
	      time_not_disk = P->particleBlock.header.time;
	      snap_not_disk = snap;
	    }
	  }
	
	  std::fprintf(fp_out, "%d %d %d %g %d %g %g %g %g %d\n", r.idx, r.idA, r.idB, time_disk, snap_disk, dist_disk, r_disk1, r_disk2, time_not_disk, snap_not_disk);
	  std::fclose(fp_out);
	}
      }
      break;
    }

    case ANALYSIS_ISO_DENSITY: {
      static int queryID1=0, queryID2=0;
      ImGui::InputInt("Particle ID1", &queryID1);
      ImGui::InputInt("Particle ID2", &queryID2); 
      ImGui::SliderFloat("Opacity##contour_ellipse", &isoOpacityEllipsoid, 0.0f, 1.0f); 
    
      if (ImGui::Button("Fit Iso-density ellipsoid")) {
	showEllipsoid = true;
	gEllipsoid->computeEllipse(P->particleBlock.particles, queryID1, queryID2);
      }
    
      if (ImGui::Button("disable Ellipsoid")) {
	showEllipsoid = false;
      }

      static char fname_input[255]="binary_fragmentation.txt";
      static char fname_output[255]="binary_fragmentation_output.txt";
      ImGui::InputText("Read target from text file", fname_input, IM_ARRAYSIZE(fname_input));
      ImGui::InputText("Output target from text file", fname_output, IM_ARRAYSIZE(fname_output));

      if(ImGui::Button("ellipsoidal fit from text file")){
	struct Row { int idx, idA, idB, snap; };      
	std::vector<Row> rows;
	{
	  std::ifstream fin(fname_input);
	  if (!fin) { std::cerr << "cannot open " << fname_input << '\n'; return; }

	  std::string line;
	  Row r;
	  while (std::getline(fin, line))
	    {
	      if (line.empty() || line[0] == '#')      // ← # 行はスキップ
		continue;
	    
	      std::istringstream iss(line);
	      if (iss >> r.idx >> r.idA >> r.idB >> r.snap)
		rows.push_back(r);                   // 正しく読めた行だけ追加
	      else
		std::cerr << "parse error: " << line << '\n';
	    }
	}

	bool flag_first = true;
	for (auto& r : rows){
	  if(r.snap < 0)
	    continue;
	
	  gFileInfo->loadNewSnapshot(r.snap, P);
	  if(P->particleBlock.particles.size() == 0)
	    continue;        
	
	  gEllipsoid->computeEllipse(P->particleBlock.particles, r.idA, r.idB);

	  FILE *fp_out;
	  if(flag_first)
	    fp_out = std::fopen(fname_output, "a");
	  else
	    fp_out = std::fopen(fname_output, "a");

	  if (!fp_out) { std::cerr << "cannot open " << fname_output << '\n'; return; }
	
	  if (flag_first) {                       /* ← ① ヘッダは最初だけ */
	    std::fprintf(fp_out, "index ID1 ID2 snap n a b c\n");
	    flag_first = false;
	  }
	
	  double a, b, c, n;
	  gEllipsoid->getEllipsoids(&a, &b, &c, &n);
	  std::fprintf(fp_out, "%d %d %d %d %g %g %g %g\n", r.idx, r.idA, r.idB, r.snap, n, a, b, c);
	  std::fclose(fp_out);
	}
     
      }
      break;
    }
#endif      
    }
  }

  if (ImGui::CollapsingHeader("Rendering")) {
    enum RenderingMode {
      RENDER_PROJECTION_MAP,
      RENDER_STREAM_LINE,
      RENDER_ISO_CONTOUR,
      RENDER_VOLUME_RENDERING,
      RENDER_VELOCITY_FIELD
    };
    
    static PullDownItem renderingItems[] = {
      { "projection map", RENDER_PROJECTION_MAP },
#ifdef STREAM_LINE
      { "stream line", RENDER_STREAM_LINE },
#endif
#ifdef ISO_CONTOUR
      { "iso-contour", RENDER_ISO_CONTOUR },
#endif
#ifdef VOLUME_RENDERING
      { "volume rendering", RENDER_VOLUME_RENDERING },
#endif
      { "velocity field", RENDER_VELOCITY_FIELD},
    };
    
    static int renderingMode = RENDER_PROJECTION_MAP;
    
    // 現在選択中のラベルを探す
    const char* currentLabel = "unknown";
    for (const auto& item : renderingItems) {
      if (item.mode == renderingMode) {
	currentLabel = item.label;
	break;
      }
    }
    
    if (ImGui::BeginCombo("Rendering mode", currentLabel)) {
      for (const auto& item : renderingItems) {
	bool isSelected = (renderingMode == item.mode);
	if (ImGui::Selectable(item.label, isSelected)) {
	  renderingMode = item.mode;
	}
	if (isSelected)
	  ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    
    switch (renderingMode) {
    case RENDER_PROJECTION_MAP: {
      if (ImGui::Button("make projection map"))
	OpenProjectionMapUI();    

      DrawProjectionMapUI(*gProjectionMap2D, P, camCtx, gFileInfo->currentFileIndex);
      
      ImGui::Text("create projection maps for continuous snapshots");

      static int nsnapshots = 10;
      static char outputFileFormat[255]="image_%04d.png";
      static char outputFolderPath[255]="./output";
      static char outputFileName[255]="output.mp4";
      ImGui::InputInt("number of snapshots##render", &nsnapshots);
      ImGui::InputText("Output File Format##render", outputFileFormat, IM_ARRAYSIZE(outputFileFormat));
      ImGui::InputText("Output Folder##render", outputFolderPath, IM_ARRAYSIZE(outputFolderPath));
      ImGui::InputText("Output Name of Movie##render", outputFileName, IM_ARRAYSIZE(outputFolderPath));

      static bool flagFaceOn = false;
      ImGui::Checkbox("show face-on view", &flagFaceOn);

      static bool flagSinkCenter = false, flagSinkCenterMassive = false, flagMassCenter = false;
      static int particleID_center = 0;
      static float rcrit_for_MassCenter = 0., ncrit_for_MassCenter = 0.;
      ImGui::Checkbox("follow the center around the particle", &flagSinkCenter);
      if(flagSinkCenter){
	ImGui::Checkbox("the most massive sink particle", &flagSinkCenterMassive);
	if(flagSinkCenterMassive == false)
	  ImGui::InputInt("particle ID", &particleID_center);	

	ImGui::Checkbox("mass center around the particle", &flagMassCenter);
	if(flagMassCenter){
	  ImGui::InputFloat("distance from the particle", &rcrit_for_MassCenter);
	  ImGui::InputFloat("the minimum density", &ncrit_for_MassCenter);
	}
      }
    
      if(ImGui::Button("generate maps")){
	int savedStep = gFileInfo->currentStep;
      
	namespace fs = std::filesystem;
	const fs::path dir = "ffmpeg_frames";
      
	try {
	  auto ensure_dir = [](const fs::path& p) {
	    if (fs::exists(p)) {
	      if (!fs::is_directory(p)) {
		throw fs::filesystem_error("Path exists but is not a directory", p,
					   std::make_error_code(std::errc::not_a_directory));
	      }
	    } else {
	      fs::create_directories(p);
	    }
	  };

	  ensure_dir(dir);
	  ensure_dir(outputFolderPath);
	
	  if (!fs::exists(dir)) {
	    fs::create_directory(dir);
	    std::cout << "Directory created: " << dir << std::endl;
	  }
	
	  if (!fs::exists(outputFolderPath)) {
	    fs::create_directory(outputFolderPath);
	    std::cout << "Directory created: " << outputFolderPath << std::endl;
	  }

	  int count_i = 0;
	  for(int i=0;i<nsnapshots;i++){
	    gFileInfo->currentStep = savedStep;
	    if(i > 0) gFileInfo->currentStep += i;

	    int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
	    gFileInfo->loadNewSnapshot(newFileIndex, P);            
	
	    if(P->particleBlock.particles.size() == 0)
	      continue;
	
	    char filename_format[512];
	    snprintf(filename_format, sizeof(filename_format), "%s/%s", outputFolderPath, outputFileFormat);

	    char filename[512];
	    snprintf(filename, sizeof(filename), filename_format, newFileIndex);

	    int flag_use_amvector = 0;
	    if(i==0 && flagFaceOn)
	      flag_use_amvector = 1;

	    int flag_center = 0;
#ifdef CLUMP_DATA_READ
	    if(P->flag_follow_clump_center)
	      flag_center = 1;
#endif
	    if(P->flag_follow_particle_ID)
	      flag_center = 1;

	    // まず、カメラターゲットを pos_center 配列に格納しておく
	    float pos_center[3] = {
	      camCtx.cameraTarget[0],
	      camCtx.cameraTarget[1],
	      camCtx.cameraTarget[2]
	    };

	    if(flagSinkCenter){
	      double pos_init[3];
	      bool flag_found = false;
	      if(flagSinkCenterMassive == false){
		for(auto &p : P->particleBlock.particles){
		  if(p.ID == particleID_center){
		    pos_init[0] = p.pos[0];
		    pos_init[1] = p.pos[1];
		    pos_init[2] = p.pos[2];
		    flag_found = true;
		  }
		  if(flag_found)
		    break;
		}
	      }

	      if(flagSinkCenterMassive || (flag_found == false)){
		double mass_max = 0.;
		for(auto &p : P->particleBlock.particles){
		  if(p.type < 3)
		    continue;
		
		  if(mass_max < p.mass){
		    pos_init[0] = p.pos[0];
		    pos_init[1] = p.pos[1];
		    pos_init[2] = p.pos[2];
		    flag_found = true;
		    mass_max = p.mass;
		  }
		}
	      }

	      if(flag_found){
		pos_center[0] = pos_init[0];
		pos_center[1] = pos_init[1];
		pos_center[2] = pos_init[2];
		flag_center = 1;
	      }
	      
	      if(flag_found && flagMassCenter){
		double pos_temp[3] = {0.,0.,0.}, weight = 0.;
		for(auto &p : P->particleBlock.particles){
		  if(p.type == 1 || p.type == 2)
		    continue;

		  if(p.type == 0 && p.density < ncrit_for_MassCenter)
		    continue;

		  double dist2 =
		    (pos_init[0] - p.pos[0])*(pos_init[0] - p.pos[0])
		    + (pos_init[1] - p.pos[1])*(pos_init[1] - p.pos[1])
		    + (pos_init[2] - p.pos[2])*(pos_init[2] - p.pos[2]);
		
		  if(dist2 > rcrit_for_MassCenter * rcrit_for_MassCenter)
		    continue;

		  double mass = p.mass;
		  pos_temp[0] += mass * p.pos[0];
		  pos_temp[1] += mass * p.pos[1];
		  pos_temp[2] += mass * p.pos[2];
		  weight += mass;
		}

		pos_center[0] = pos_temp[0] / weight;
		pos_center[1] = pos_temp[1] / weight;
		pos_center[2] = pos_temp[2] / weight;
		flag_center = 1;
	      }
	    }
	  

	    gProjectionMap2D->set_projection_parameters(P->particleBlock.particles, flag_use_amvector, flag_center ? pos_center : nullptr, -1.0f,
							std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(), -1, -1, "");
       	
	    gProjectionMap2D->make_density_map(P, filename);

	    char linkname[512];
	    snprintf(linkname, sizeof(linkname), "ffmpeg_frames/frame_%04d.png", count_i);
	    count_i++;
	  
	    std::filesystem::remove(linkname);
	    std::filesystem::create_symlink(std::filesystem::absolute(filename), linkname);
	  }

	  // ffmpeg を呼び出す（mp4 形式、30fps）
	  std::string ffmpegCommand =
	    "ffmpeg -y -framerate 30 -i ffmpeg_frames/frame_%04d.png -vf \"scale=ceil(iw/2)*2:ceil(ih/2)*2\" -c:v libx264 -pix_fmt yuv420p " + std::string(outputFolderPath) + "/" + std::string(outputFileName);
	  std::system(ffmpegCommand.c_str());
	  fs::remove_all("ffmpeg_frames");
      
	  gFileInfo->currentStep = savedStep;
	  gFileInfo->currentFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;    	
	} catch (const fs::filesystem_error& e) {
	  std::cerr << "Error creating directory: " << e.what() << std::endl;
	}
      }
      break;
    }
      
#ifdef STREAM_LINE
    case RENDER_STREAM_LINE: {
      static int n_seeds=1;
      ImGui::Text("Seed setup");
      ImGui::InputInt("number of seed points", &n_seeds);

      static float seed_center[3] = {0.,0.,0.}, seed_len[3] = {100.,100.,100.}, seed_opacity = 0.1;
      bool seedRegionDirty = false;
    
      if (ImGui::InputFloat3("Center of the region to place seed points", seed_center, "%.3f")){
	seedRegionDirty = true;
      }

      // 2) Side‐length: rebuild when changed
      if (ImGui::InputFloat3("side len", seed_len, "%.3f")) {
	seedRegionDirty = true;
      }

      // 3) Opacity: rebuild when changed
      if (ImGui::SliderFloat("opacity##cubic", &seed_opacity, 0.f, 1.f, "%.2f")) {
	seedRegionDirty = true;
      }

      // 4) If either length or opacity changed, re‐create exactly one cube
      if (seedRegionDirty) {
	// remove only the old seed region cubes
	gCubeManager.clearGroup("seedRegion");

	if (seed_len[0] > 0.f && seed_len[1] > 0.f && seed_len[2] > 0.f) {
	  // add new cube with updated len & opacity
	  gCubeManager.addCube(
			       glm::vec3(seed_center[0], seed_center[1], seed_center[2]),
			       glm::vec3(0.5f * seed_len[0], 0.5f * seed_len[1], 0.5f * seed_len[2]),
			       glm::quat{1,0,0,0},       // no rotation
			       seed_opacity,            // per‐instance opacity
			       "seedRegion"             // tag
			       );
	  gStreamLine->setRegionByHand(seed_center, seed_len);
	}
	else {
	  // zero‐size ⇒ disable region if you like
	  gStreamLine->disableRegion();
	}

	flagCubesDirty = true;
	seedRegionDirty = false;
      }

      static bool flag_limit_stream_region = false;
      static float sl_center[3]={0.,0.,0.}, sl_len[3]={0.,0.,0.};
    
      ImGui::Text("Stream line setting");    
      ImGui::Checkbox("limit stream lines in box", &flag_limit_stream_region);
      if(flag_limit_stream_region){
	bool flag_reset_region = false;
	if(ImGui::InputFloat3("center of stream line region", sl_center, "%.3f")){
	  flag_reset_region = true;
	}

	if(ImGui::InputFloat3("side len##stream line", sl_len, "%.3f")){
	  flag_reset_region = true;
	}

	if(flag_reset_region){
	  if(sl_len[0] > 0. && sl_len[1] > 0. && sl_len[2] > 0.){
	    gStreamLine->setStreamRegionByHand(sl_center, sl_len);
	  }else{
	    gStreamLine->disableStreamRegion();
	  }
	}
      }else
	gStreamLine->disableStreamRegion();
      
      if (ImGui::Button("Build stream lines")) {
	gStreamLine->setRegionFromParticleData(P->particleBlock.particles);
	gStreamLine->setStreamRegionFromParticleData(P->particleBlock.particles);

	gStreamLine->setSeeds(P->particleBlock.particles, n_seeds);
	float degree = 10.;
	gStreamLine->build(P->particleBlock, degree);
      
	showStreamLine = true;
	flagStreamDirty = true;
      }
    
      if (ImGui::Button("disable Grid & Mesh")) {
	showStreamLine = false;
      }    
      break;
    }
#endif

#ifdef ISO_CONTOUR
    case RENDER_ISO_CONTOUR: {
      static float isoLevel = 0.;
    
      ImGui::InputFloat("Threshold value for iso-contour", &isoLevel);
      ImGui::SliderFloat("Opacity", &isoOpacity, 0.0f, 1.0f);

      static int max_treelevel = 15;
      ImGui::SliderInt("Maximum level of OctTree", &max_treelevel, 5, 20);

      static QuantityId selectedVar_iso = QuantityId::Density;
      if (ImGui::BeginCombo("Quantity for Iso-Contour", QuantityLabel(selectedVar_iso))) {
	for (int q = 0; q < P->particleBlock.nUIQ; ++q) {
	  QuantityId cand = P->particleBlock.uiQ[q];
	  bool is_selected = (cand == selectedVar_iso);
	  if (ImGui::Selectable(QuantityLabel(cand), is_selected)) selectedVar_iso = cand;
	  if (is_selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }
        
      if (ImGui::Button("Build OctTree & Mesh")) {
	showIsocontour = true;

	TrackingVector<ParticleDataForTree> particles;
	particles.reserve(P->particleBlock.particles.size());

	for (size_t ipart=0;ipart < P->particleBlock.particles.size();ipart++) {
	  const auto &pd = P->particleBlock.particles[ipart];
	  float val = getScalarValue(P->particleBlock, pd, ipart, selectedVar_iso);
	  particles.push_back({glm::vec3(pd.pos[0], pd.pos[1], pd.pos[2]), val});
	}

	BoundingBox worldBox;
	worldBox.min = glm::vec3( FLT_MAX );
	worldBox.max = glm::vec3( -FLT_MAX );
	for (const auto& p : particles) {
	  worldBox.min = glm::min(worldBox.min, p.pos);
	  worldBox.max = glm::max(worldBox.max, p.pos);
	}

	IsoSurfaceParams params;
	params.particles   = std::move(particles);
	params.worldBox    = worldBox;
	params.isoLevel    = isoLevel;
	params.minParticles = 8;
	params.maxDepth     = max_treelevel;

	auto mesh = IsoSurfaceGenerator::generateVTK(std::move(params));
      
      
	vertsIsocontour = mesh.vertices;
	indsIsocontour  = mesh.indices;

	std::cout << "Verts: " << vertsIsocontour.size()
		  << ", Indices: " << indsIsocontour.size() << std::endl;
      
	uploadMeshWithNormals(vertsIsocontour, indsIsocontour, meshIsocontourVAO, meshIsocontourVBO, meshIsocontourNBO, meshIsocontourEBO);
      }

    
      if (ImGui::Button("disable Grid & Mesh")) {
	showIsocontour = false;
      }    
      break;
    }
#endif
  
#ifdef VOLUME_RENDERING  
    case RENDER_VOLUME_RENDERING: {
      ImGui::Text("RayTracing?");
      ImGui::RadioButton("Ray Tracing with BVH", &flagRT, 1);
      ImGui::SameLine();
      ImGui::RadioButton("Ray Marching with octtree", &flagRT, 2);
      ImGui::SameLine();
      ImGui::RadioButton("No, blending", &flagRT, 0);

      if(flagRT == 0){
	ImGui::Text("kernel");
	ImGui::RadioButton("Gaussian", &kernelMode, 0);
	ImGui::SameLine();
	ImGui::RadioButton("SPH interpolation", &kernelMode, 1);

	if(kernelMode == 0)
	  ImGui::SliderFloat("Maximum size of the kernel", &gaussNSigma, 1.0f, 50.f);    	

	if(kernelMode == 1)
	  ImGui::SliderFloat("Enlarge factor for Kernel", &enlargeKernel, 0.5f, 50.f);    	      
      }
    
      if(flagRT){
	ImGui::Text("LOD");
	ImGui::RadioButton("LEAF ONLY", &lodMode, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Auto LOD", &lodMode, 1);

	if(lodMode == 1)
	  ImGui::SliderFloat("Pixel Threshold (px)", &pxThreshold, 0.5f, 6.0f, "%.2f");      

	ImGui::SliderFloat("Maximum Tau for RT", &tauMax, 0.1f, 100.f);    
	ImGui::SliderFloat("upscaling factor", &rtDownscale, 1.0f, 10.0f);
      }

      static QuantityId selectedVar_volume = QuantityId::Density;
      if (ImGui::BeginCombo("Quantity for Volume rendering", QuantityLabel(selectedVar_volume))) {
	for (int q = 0; q < P->particleBlock.nUIQ; ++q) {
	  QuantityId cand = P->particleBlock.uiQ[q];
	  bool is_selected = (cand == selectedVar_volume);
	  if (ImGui::Selectable(QuantityLabel(cand), is_selected)) selectedVar_volume = cand;
	  if (is_selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }
    
      if(ImGui::Button("set Render Opacitiy")){
	float val_max = -1.e30, val_min = 1.e30;
	for (size_t ipart=0;ipart < P->particleBlock.particles.size();ipart++) {
	  const auto &pd = P->particleBlock.particles[ipart];
	  if(pd.type != 0)
	    continue;

	  float val = getScalarValue(P->particleBlock, pd, ipart, selectedVar_volume);
	  if(val > val_max) val_max = val;
	  if(val < val_min) val_min = val;
	}
      
	gTF->set_minmax(selectedVar_volume, val_min, val_max);      
	gTF->set_window();
      }
    
      if (ImGui::Button("do volume rendering")) {
	if(flagRT == 1){
	  gBVHresult = gBVH->build(P->particleBlock.particles);
	  lbvh::computeSigma(gBVHresult, g_rho2sigma);
	}

	if(flagRT == 2){
	  TrackingVector<ParticleDataForTree> particles;
	  particles.reserve(P->particleBlock.particles.size());

	  for (size_t ipart=0;ipart < P->particleBlock.particles.size();ipart++) {
	    const auto &pd = P->particleBlock.particles[ipart];
	    if(pd.type != 0)
	      continue;

	    float val = getScalarValue(P->particleBlock, pd, ipart, selectedVar_volume);
	    particles.push_back({glm::vec3(pd.pos[0], pd.pos[1], pd.pos[2]), val});
	  }
	
	  BoundingBox worldBox;
	  worldBox.min = glm::vec3( FLT_MAX );
	  worldBox.max = glm::vec3( -FLT_MAX );
	  for (const auto& p : particles) {
	    worldBox.min = glm::min(worldBox.min, p.pos);
	    worldBox.max = glm::max(worldBox.max, p.pos);
	  }
      
	  gOctTree.cpuTree = std::make_unique<ParticleOctree>(std::move(particles),
							      worldBox,
							      /*minParticles*/4,
							      /*maxDepth*/20);

	  printf("Tree construction has finished!\n");	
	  buildIndexAndSigma(*gOctTree.cpuTree, g_rho2sigma, gOctTree.order, gOctTree.info, gOctTree.toIdx);	
	}
      
	showVolumeRendering = true;
	flagUpdateRendering = true;
      }

      if(ImGui::Button("reload opacity")){
	if(flagRT == 1){
	  lbvh::computeSigma(gBVHresult, g_rho2sigma);
	}

	if(flagRT == 2){
	  buildIndexAndSigma(*gOctTree.cpuTree, g_rho2sigma, gOctTree.order, gOctTree.info, gOctTree.toIdx);
	}
      
	showVolumeRendering = true;      
	flagUpdateRendering = true;
      }
    
      if (ImGui::Button("disable Rendering image")) {
	showVolumeRendering = false;
      }

      if(ImGui::Button("Hide particles")){
	flagHideAllParticles = true;
      }

      if(ImGui::Button("Show particles")){
	flagHideAllParticles = false;
      }
      break;
    }
#endif

    case RENDER_VELOCITY_FIELD: {
      ImGui::InputInt("show velocity field out of n particles", &velocitySubtraction);
      ImGui::InputFloat("Arrow Scale", &arrowScale, 0.1f, 1.0f, "%.2f");
      ImGui::Checkbox("Use Log Scale", &useVelocityArrowLogScale);
      
      if(ImGui::Checkbox("render velocity field", &showVelocityVectors)){
	if(showVelocityVectors)
	  P->velocityDirty = true;
      }
      break;
    }
    }
  }
      
   
  if(ImGui::CollapsingHeader("Other settings")){
    bool unitChanged = false;
    if(ImGui::CollapsingHeader("Units")){
      unitChanged |= ImGui::InputDouble("UnitLength_in_cm"   , &P->UnitLength_in_cm   , 0.,0., "%g");
      unitChanged |= ImGui::InputDouble("UnitMass_in_msun"   , &P->UnitMass_in_g      , 0.,0., "%g");
      unitChanged |= ImGui::InputDouble("UnitVelocity_in_cm_per_s", &P->UnitVelocity_in_cm_per_s, 0.,0., "%g");
      unitChanged |= ImGui::InputDouble("Hubble"             , &P->Hubble, 0.,0., "%g");
      unitChanged |= ImGui::Checkbox("ComovingCorrdinate", &P->useComovingCorrdinate);

      ImGui::SeparatorText("Presets");      
      if (ImGui::Button("AU"))   { P->UnitLength_in_cm = P->au_in_cm;      unitChanged = true; }
      ImGui::SameLine();
      if (ImGui::Button("pc"))   { P->UnitLength_in_cm = P->pc_in_cm;      unitChanged = true; }
      ImGui::SameLine();
      if (ImGui::Button("kpc"))  { P->UnitLength_in_cm = P->kpc_in_cm;     unitChanged = true; }
      ImGui::SameLine();
      if (ImGui::Button("Mpc"))  { P->UnitLength_in_cm = P->Mpc_in_cm;     unitChanged = true; }

      if (ImGui::Button("Msun"))   { P->UnitMass_in_g   = P->msolar_in_g;    unitChanged = true; }
      ImGui::SameLine();
      if (ImGui::Button("1e10 Msun")){ P->UnitMass_in_g   = 1.e10*P->msolar_in_g; unitChanged = true; }
    }

    if(unitChanged){
      P->setUnits();
      gFileInfo->setUnit(P);
    }
    
    if(ImGui::CollapsingHeader("Zoom Range")){
      ImGui::InputFloat("Min Zoom", &minZoom, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Max Zoom", &maxZoom, 0.0f, 0.0f, "%g");
    }

    if(ImGui::CollapsingHeader("Cross Marker"))
      ImGui::SliderFloat("Cross Marker Size", &crossSize, 0.01f, 1.0f);
  }
  
  ImGui::End();
}

void ShowCameraSettingsUI() {
}

void UpdateLabelIndicesIfNeeded()
{
  if(P->flagParticleIndexDirty == false){
    if (glm::distance(camCtx.cameraPos, g_lastCameraPos) < g_moveThreshold)
      return;                     // 動いていなければ何もしない
  }
  
  g_lastCameraPos = camCtx.cameraPos;
  g_labelIndices.clear();
  
  float query_pt[3] = { camCtx.cameraPos.x,
			camCtx.cameraPos.y,
			camCtx.cameraPos.z };

  const float radius2 = g_queryRadius * g_queryRadius;

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
    float px = g_viewportX + (ndc.x * 0.5f + 0.5f) * float(g_viewportWidth);
    float py = g_viewportY + (ndc.y * 0.5f + 0.5f) * float(g_viewportHeight);

    // px,py は pixels 単位なので
    float sx = px / scaleX;
    float sy = (FBH - py) / scaleY; // ImGuiは左上原点・Y下増加なので反転
    
    //float sx = g_viewportX + (ndc.x * 0.5f + 0.5f) * g_viewportWidth;
    //float sy = g_viewportY + (1.f   - (ndc.y * 0.5f + 0.5f)) * g_viewportHeight;
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

int g_selectedAxis = 2;
GLuint g_CuboidVAO = 0;
GLuint g_CuboidVBO = 0;
GLuint g_ArrowVAO = 0;
GLuint g_ArrowVBO = 0;

static bool g_CuboidInitialized = false;
static bool g_ArrowInitialized = false;

// 各軸ごとの代表的な頂点インデックスのペア
// ローカル空間で v0=(xmin,ymin,zmin) としておき、
// X軸方向の辺： (v0→v1)、Y軸方向の辺： (v0→v3)、Z軸方向の辺： (v0→v4)
static const std::pair<int,int> repEdgeX = {0, 1};
static const std::pair<int,int> repEdgeY = {0, 3};
static const std::pair<int,int> repEdgeZ = {0, 4};

// 「すべてのエッジ」のインデックス (ワイヤーフレーム用)
static const std::vector<std::pair<int,int>> allEdges = {
	{0,1}, {1,2}, {2,3}, {3,0},  // 底面
	{4,5}, {5,6}, {6,7}, {7,4},  // 上面
	{0,4}, {1,5}, {2,6}, {3,7}   // 垂直辺
};
// 軸ごとのエッジ一覧 (ハイライト用)
static const std::vector<std::pair<int,int>> edgesX = {
	{0,1}, {3,2}, {4,5}, {7,6}
};
static const std::vector<std::pair<int,int>> edgesY = {
	{1,2}, {0,3}, {5,6}, {4,7}
};
static const std::vector<std::pair<int,int>> edgesZ = {
	{0,4}, {1,5}, {2,6}, {3,7}
};

void RenderCuboid(const glm::mat4 &view, const glm::mat4 &projection)
{
  //――――― (1) VAO/VBO を一度だけ生成 ―――――
  if (!g_CuboidInitialized) {
    glGenVertexArrays(1, &g_CuboidVAO);
    glGenBuffers(1, &g_CuboidVBO);
    g_CuboidInitialized = true;
  }
  if (!g_ArrowInitialized) {
    glGenVertexArrays(1, &g_ArrowVAO);
    glGenBuffers(1, &g_ArrowVBO);
    g_ArrowInitialized = true;
  }
	
  //――――― (2) 選択された軸を見て、頂点インデックスから axis_world を計算 ―――――
  glm::vec3 axis_world(0.0f);
  std::vector<glm::vec3> otherEdgeVerts;
  std::vector<glm::vec3> highlightEdgeVerts;
  const std::vector<std::pair<int,int>>* edgePairsForAxis = &edgesZ;

  if (g_selectedAxis == 0) {
    // X 軸を選択 → ローカルの (v0→v1) が回転後の X 軸向き
    glm::vec3 A = g_cubicPoints[repEdgeX.first];  // g_cubicPoints[0]
    glm::vec3 B = g_cubicPoints[repEdgeX.second]; // g_cubicPoints[1]
    axis_world = glm::normalize(B - A);
    edgePairsForAxis = &edgesX;
  }
  else if (g_selectedAxis == 1) {
    // Y 軸を選択 → ローカルの (v0→v3) が回転後の Y 軸向き
    glm::vec3 A = g_cubicPoints[repEdgeY.first];  // g_cubicPoints[0]
    glm::vec3 B = g_cubicPoints[repEdgeY.second]; // g_cubicPoints[3]
    axis_world = glm::normalize(B - A);
    edgePairsForAxis = &edgesY;
  }
  else {
    // Z 軸を選択 → ローカルの (v0→v4) が回転後の Z 軸向き
    glm::vec3 A = g_cubicPoints[repEdgeZ.first];  // g_cubicPoints[0]
    glm::vec3 B = g_cubicPoints[repEdgeZ.second]; // g_cubicPoints[4]
    axis_world = glm::normalize(B - A);
    edgePairsForAxis = &edgesZ;
  }
		
  // 全エッジを走査し、ペアが選択軸群に含まれればハイライト、それ以外はその他へ
  for (auto &e : allEdges) {
    int i0 = e.first;
    int i1 = e.second;
    glm::vec3 A = g_cubicPoints[i0];
    glm::vec3 B = g_cubicPoints[i1];
		
    bool isHighlight = false;
    for (auto &h : *edgePairsForAxis) {
      if ((h.first == i0 && h.second == i1) ||
	  (h.first == i1 && h.second == i0)) {
	isHighlight = true;
	break;
      }
    }
    if (isHighlight) {
      highlightEdgeVerts.push_back(A);
      highlightEdgeVerts.push_back(B);
    }
    else {
      otherEdgeVerts.push_back(A);
      otherEdgeVerts.push_back(B);
    }
  }
	
  //――――― (4) シェーダに行列を送る ―――――
  glUseProgram(lineProgram);
  glUniformMatrix4fv(glGetUniformLocation(lineProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(lineProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
  //――――― (5) 「その他のエッジ」を白で描画 ―――――
  if (!otherEdgeVerts.empty()) {
    glBindVertexArray(g_CuboidVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_CuboidVBO);
    glBufferData(GL_ARRAY_BUFFER,
		 otherEdgeVerts.size() * sizeof(glm::vec3),
		 otherEdgeVerts.data(),
		 GL_STATIC_DRAW);
    GLint locColor = glGetUniformLocation(lineProgram, "color");
    glUniform4f(locColor, 1.0f, 1.0f, 1.0f, 1.0f); // 白
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glDrawArrays(GL_LINES, 0, (GLsizei)otherEdgeVerts.size());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
  }
	
  //――――― (6) 「選択軸方向のエッジ」を赤で描画 ―――――
  if (!highlightEdgeVerts.empty()) {
    glUseProgram(lineProgram);
    glBindVertexArray(g_CuboidVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_CuboidVBO);
    glBufferData(GL_ARRAY_BUFFER,
		 highlightEdgeVerts.size() * sizeof(glm::vec3),
		 highlightEdgeVerts.data(),
		 GL_STATIC_DRAW);
    GLint locColor = glGetUniformLocation(lineProgram, "color");
    glUniform4f(locColor, 1.0f, 0.0f, 0.0f, 1.0f); // 赤
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glDrawArrays(GL_LINES, 0, (GLsizei)highlightEdgeVerts.size());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
  }
	
  //――――― (7) 矢印を追加描画 ―――――
	
  // (7-1) 軸の正方向の面上にある頂点群を探す（dot 値が最大のもの）
  float maxProj = -FLT_MAX;
  std::array<float,8> projs;
  for (int i = 0; i < 8; ++i) {
    projs[i] = glm::dot(g_cubicPoints[i], axis_world);
    if (projs[i] > maxProj) maxProj = projs[i];
  }
  // 面上 (≈4点) を集める
  std::vector<glm::vec3> faceVerts;
  const float faceEps = 1e-4f;
  for (int i = 0; i < 8; ++i) {
    if (glm::abs(projs[i] - maxProj) < faceEps) {
      faceVerts.push_back(g_cubicPoints[i]);
    }
  }
  if (faceVerts.empty()) {
    // 面が取れなければ矢印は描かない
    return;
  }
  // (7-2) 面の中心を計算
  glm::vec3 faceCenter(0.0f);
  for (auto &v : faceVerts) faceCenter += v;
  faceCenter /= (float)faceVerts.size();
	
  // (7-3) 矢印の先端・矢じりを決定
  const float arrowLength    = 0.2f;  // 面中心から先端までの距離
  const float arrowHeadLen   = 0.05f; // 矢じりの長さ
  const float arrowHeadWidth = 0.03f; // 矢じりの幅
	
  glm::vec3 arrowTip = faceCenter + axis_world * arrowLength;
  glm::vec3 base     = arrowTip - axis_world * arrowHeadLen;
	
  // (7-4) axis_world に垂直なベクトル u を作成
  glm::vec3 arbitrary(0.0f, 1.0f, 0.0f);
  if (glm::abs(glm::dot(axis_world, arbitrary)) > 0.9f) {
    arbitrary = glm::vec3(1.0f, 0.0f, 0.0f);
  }
  glm::vec3 u = glm::normalize(glm::cross(axis_world, arbitrary));
  glm::vec3 side1 = base + u * arrowHeadWidth;
  glm::vec3 side2 = base - u * arrowHeadWidth;
	
  // (7-5) 矢印頂点リストを作る
  std::vector<glm::vec3> arrowVerts;
  // シャフト
  arrowVerts.push_back(faceCenter);
  arrowVerts.push_back(arrowTip);
  // 矢じりの両サイド
  arrowVerts.push_back(arrowTip);
  arrowVerts.push_back(side1);
  arrowVerts.push_back(arrowTip);
  arrowVerts.push_back(side2);

  glUseProgram(lineProgram);
 
  // (7-6) Arrow用VAO/VBO にアップロードして描画
  glBindVertexArray(g_ArrowVAO);
  glBindBuffer(GL_ARRAY_BUFFER, g_ArrowVBO);
  glBufferData(GL_ARRAY_BUFFER,
	       arrowVerts.size() * sizeof(glm::vec3),
	       arrowVerts.data(),
	       GL_STATIC_DRAW);
  GLint locColor2 = glGetUniformLocation(lineProgram, "color");
  glUniform4f(locColor2, 1.0f, 0.0f, 0.0f, 1.0f); // 矢印も赤
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
  glDrawArrays(GL_LINES, 0, (GLsizei)arrowVerts.size());
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

glm::vec3 FlipLeftRight(const glm::vec3& v) {
    return glm::vec3(-v.x, -v.y, v.z);
}

void UpdateCuboidTransformArcball(glm::vec3 &center, glm::quat &cuboidTransform
				  , float lastX, float lastY, float xpos, float ypos
				  , const glm::mat4 &view, const glm::vec3 &pivotWorld)
{
  // マウス座標を単位球上の3D点に写像
  glm::vec3 startSphere = mapToSphere(lastX, lastY);
  glm::vec3 endSphere   = mapToSphere(xpos, ypos);

  // 始点と終点の外積で回転軸を求める
  glm::vec3 rotAxis = glm::cross(startSphere, endSphere);
  rotAxis = FlipLeftRight(rotAxis);
  if (glm::length(rotAxis) > 1e-5f) {
    rotAxis = glm::normalize(rotAxis);
    // 内積から回転角度を算出（acosの結果 [rad]）
    float dotVal = glm::clamp(glm::dot(startSphere, endSphere), -1.0f, 1.0f);
    float angle = std::acos(dotVal);
    // 必要に応じて感度調整
    float sensitivity = 1.0f;
    angle *= sensitivity;

    // Arcball 回転四元数を作成
    //glm::quat qArcball = glm::angleAxis(angle, rotAxis);
    // 現在の直方体変換行列に回転を乗算（合成順序は用途に合わせて調整）
    //    cuboidTransform = glm::normalize(cuboidTransform * qArcball);
    // rotAxis はこの時点ではカメラ（画面）空間での軸となっているので、
    // それをビュー行列の逆行列でワールド空間に変換する

    glm::mat4 invView = glm::inverse(view);
    glm::vec3 worldRotAxis = glm::normalize(glm::vec3(invView * glm::vec4(rotAxis, 0.0f)));
    
    // カメラ視点に合わせた回転四元数を生成
    glm::quat qArcball = glm::angleAxis(angle, worldRotAxis);

    // 新しい回転を左側に乗算（前から適用）して直方体の変換行列を更新
    //cuboidTransform = glm::normalize(qArcball * cuboidTransform);   
    
    glm::mat4 T      = glm::translate(glm::mat4(1.0f),  pivotWorld);
    glm::mat4 Tinv   = glm::translate(glm::mat4(1.0f), -pivotWorld);
    glm::mat4 R      = glm::mat4_cast(qArcball);

    glm::mat4 oldMat = glm::translate(glm::mat4(1.f), center)
      * glm::mat4_cast(cuboidTransform);
    
    glm::mat4 newMat = T * R * Tinv * oldMat;

    // 5) newMat を「位置 + 回転」に分解
    //    (スケールがないと仮定)
    //    → translate = 4列目, 回転 = 上左3x3
    glm::vec3 newTrans = glm::vec3(newMat[3].x, newMat[3].y, newMat[3].z);
    glm::mat3 rot3x3   = glm::mat3(newMat);
    glm::quat newRot   = glm::quat_cast(rot3x3);

    printf("center=%g %g %g newTrans=%g %g %g\n", center.x, center.y, center.z, newTrans.x, newTrans.y, newTrans.z);
    
    // 6) 書き戻す
    center          = newTrans;    
    cuboidTransform = glm::normalize(newRot);
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
  float effectiveWidth = static_cast<float>(g_viewportWidth);
  float effectiveHeight = static_cast<float>(g_viewportHeight);
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

// ピクセル座標を NDC に変換する関数
ImVec2 PixelToNDC(float x, float y) {
#ifdef USE_LETTERBOX
  // 実際のレンダリング領域（レターボックス）のサイズを使う
  float effectiveWidth = static_cast<float>(g_viewportWidth);
  float effectiveHeight = static_cast<float>(g_viewportHeight);
#else
  ImGuiIO& io = ImGui::GetIO();
  float effectiveWidth = io.DisplaySize.x/io.DisplayFramebufferScale.x;
  float effectiveHeight = io.DisplaySize.y/io.DisplayFramebufferScale.y;
#endif
  
  float x_ndc = (x / effectiveWidth) * 2.0f - 1.0f;
  float y_ndc = 1.0f - (y / effectiveHeight) * 2.0f;
  
  return ImVec2(x_ndc, y_ndc);
}


void UpdateColorBarVertices() {
    float left_pixel, right_pixel, top_pixel, bottom_pixel;
    ComputeColorBarPixelCoords(left_pixel, right_pixel, top_pixel, bottom_pixel);
    
    // 各頂点のピクセル座標から NDC 座標に変換
    ImVec2 ndc_left_bottom  = PixelToNDC(left_pixel, bottom_pixel);
    ImVec2 ndc_right_bottom = PixelToNDC(right_pixel, bottom_pixel);
    ImVec2 ndc_right_top    = PixelToNDC(right_pixel, top_pixel);
    ImVec2 ndc_left_top     = PixelToNDC(left_pixel, top_pixel);

    float barVertices[] = {
        ndc_left_bottom.x,  ndc_left_bottom.y,  0.0f, 0.0f,   // 左下
        ndc_right_bottom.x, ndc_right_bottom.y, 1.0f, 0.0f,   // 右下
        ndc_right_top.x,    ndc_right_top.y,    1.0f, 1.0f,   // 右上
        ndc_left_top.x,     ndc_left_top.y,     0.0f, 1.0f    // 左上
    };

    glBindBuffer(GL_ARRAY_BUFFER, colorbarVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(barVertices), barVertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


// インスタンスVBOを更新する（すべての粒子の位置と速度をアップロードする例）
void UpdateVelocityInstanceBuffer(const TrackingVector<ParticleData>& particles) {
  TrackingVector<float> instanceData;
  instanceData.reserve(particles.size() * 6);
  for (size_t i=0;i<particles.size();i++) {
    if(i % velocitySubtraction != 0)
      continue;

    const ParticleData &p = particles[i];
      
    // Particle構造体に vel[3] があると仮定
    instanceData.push_back(p.pos[0]);
    instanceData.push_back(p.pos[1]);
    instanceData.push_back(p.pos[2]);
    instanceData.push_back(p.vel[0]);  // 速度のx成分
    instanceData.push_back(p.vel[1]);  // 速度のy成分
    instanceData.push_back(p.vel[2]);  // 速度のz成分
  }
  
  instanceCount = instanceData.size() / 6;

  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
  glBufferData(GL_ARRAY_BUFFER, instanceData.size() * sizeof(float), instanceData.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// 速度ベクトル（矢印）の描画
void RenderVelocityVectors(const glm::mat4 &view, const glm::mat4 &projection, float scaleFactor, bool useLogScale) {
    glUseProgram(velocityArrowShader);
    glUniformMatrix4fv(glGetUniformLocation(velocityArrowShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(velocityArrowShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(velocityArrowShader, "scaleFactor"), scaleFactor);
    glUniform1f(glGetUniformLocation(velocityArrowShader, "logScale"), useLogScale ? 1.0f : 0.0f);

    glBindVertexArray(velocityArrowVAO);
    glDrawArraysInstanced(GL_LINES, 0, arrowVertexCount, instanceCount);
    glBindVertexArray(0);
}


void UpdateUI() {
  UpdateColorBarVertices();

  ShowTime();
   
  ShowSettingsUI();          // 設定 UI
  ShowCameraSettingsUI();

  gClumpFind->ShowFindClumpsUI(P->particleBlock.particles, P->particleBlock.header);

#ifdef CLUMP_DATA_READ
  gClumpFind->ReadAndShowClumpsUI(P, gFileInfo->currentFileIndex);
  gClumpFind->showClumpChainList(P, gProjectionMap2D.get());
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
  gTF->showUI();
  g_rho2sigma = gTF->bakeLUT(1024);
#endif
  
  // 新たなウィンドウとしてトップ粒子リストを表示
  DrawTopParticlesUI(P, camCtx);
}

void RenderCross(const glm::mat4 &view, const glm::mat4 &projection){
  // --- 交差点マーカー描画 ---
  glDisable(GL_DEPTH_TEST); // Zバッファ無効化（手前に表示する）
  glLineWidth(3.0f);        // 線を太くする
    
  glm::vec3 forward = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
  glm::vec3 rightVec = glm::normalize(glm::cross(forward, camCtx.cameraUp));
  glm::vec3 upVec = glm::normalize(glm::cross(rightVec, camCtx.cameraUp));
  glm::vec3 upVec_new = glm::normalize(glm::cross(rightVec, forward));
  glm::vec3 v1 = camCtx.cameraTarget - (rightVec + upVec) * crossSize;
  glm::vec3 v2 = camCtx.cameraTarget + (rightVec + upVec) * crossSize;
  glm::vec3 v3 = camCtx.cameraTarget - (rightVec - upVec) * crossSize;
  glm::vec3 v4 = camCtx.cameraTarget + (rightVec - upVec) * crossSize;
  glm::vec3 v5 = camCtx.cameraTarget - upVec_new * crossSize;
  glm::vec3 v6 = camCtx.cameraTarget + upVec_new * crossSize;

  float crossVertices[6 * N_LINES_FOR_CROSS] = {
    v1.x, v1.y, v1.z,
    v2.x, v2.y, v2.z,
    v3.x, v3.y, v3.z,
    v4.x, v4.y, v4.z,
    v5.x, v5.y, v5.z,
    v6.x, v6.y, v6.z
  };
  glBindBuffer(GL_ARRAY_BUFFER, crossVBO);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(crossVertices), crossVertices);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  
  glUseProgram(lineProgram);
  glUniformMatrix4fv(glGetUniformLocation(lineProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(lineProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
  glUniform4f(glGetUniformLocation(lineProgram, "color"), 1.0f, 1.0f, 1.0f, 1.0f); // 白色

  glBindVertexArray(crossVAO);
  glDrawArrays(GL_LINES, 0, 2 * N_LINES_FOR_CROSS);
  glBindVertexArray(0);

  glEnable(GL_DEPTH_TEST); // Zバッファ再有効化
}


void RenderColorBar(int colormapIndex) {
  // シーンとは別にオーバーレイ表示するため、深度テストを一時無効化
  glDisable(GL_DEPTH_TEST);

  glUseProgram(colorbarProgram);

  // テクスチャユニット 0 に現在のカラーマップテクスチャをバインド
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_1D, colormapTextures[colormapIndex]);
  // シェーダー内の uniform "colormap" をテクスチャユニット 0 に設定
  glUniform1i(glGetUniformLocation(colorbarProgram, "colormap"), 0);

  // VAO をバインドして描画（6個のインデックス）
  glBindVertexArray(colorbarVAO);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  // 描画後、必要に応じて深度テストを再有効化
  glEnable(GL_DEPTH_TEST);
}


void RenderColorBarLabels() {
  ImGuiIO& io = ImGui::GetIO();
  float scaleX = io.DisplayFramebufferScale.x;
  float scaleY = io.DisplayFramebufferScale.y;
  
  // まず、色バーのピクセル座標を計算
  float left_pixel, right_pixel, top_pixel, bottom_pixel;
  ComputeColorBarPixelCoords(left_pixel, right_pixel, top_pixel, bottom_pixel);
    
#ifdef USE_LETTERBOX
  float offsetX = static_cast<float>(g_viewportX);
  float offsetY = static_cast<float>(g_viewportY);
#else
  float offsetX = 0.0f;
  float offsetY = 0.0f;
#endif

  // ここでは色バーの下側に目盛りラベルを表示する例
  int numTicks = 5;
  ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    
  for (int i = 0; i < numTicks; i++) {
    // 色バーのテクスチャ座標 t に対応する値（例：colorBarMin〜colorBarMax）
    float t = float(i) / (numTicks - 1);
    float value = 0.0f + t * (gParticleVisualConfig.types[0].colorMax - gParticleVisualConfig.types[0].colorMin);
    
    // ① 物理ピクセルでのラベル座標
    float px_phys = left_pixel + t*(right_pixel - left_pixel);
    float py_phys = bottom_pixel + 5.0f * scaleY;
    
    // ② 物理ピクセル→ImGuiポイント
    float sx = (px_phys + offsetX) / scaleX;
    float sy = (py_phys + offsetY) / scaleY;
    
    // 6) 丸めてチラつき防止
    float draw_x = std::floor(sx + 0.5f);
    float draw_y = std::floor(sy + 0.5f);
       
    // ラベルを描画（ImGui::GetForegroundDrawList() はスクリーン座標での描画を行うので、ピクセル座標をそのまま使用）
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    draw_list->AddText(ImVec2(draw_x, draw_y), IM_COL32(255,255,255,255), buf);
  }
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

void RenderScene() {
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (P->particlesDirty) {
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);

    for(int i=0;i<6;i++){
      for (size_t ipart=0;ipart < P->particleBlock.particles.size();ipart++) {
	auto &p = P->particleBlock.particles[ipart];	
	int itype = p.type;
	if(itype != i)
	  continue;
	
	p.val_show = getScalarValue(P->particleBlock, p, ipart, gParticleVisualConfig.types[i].selectedQuantity);
      }
    }

    TrackingVector<ParticleData> filtered;
    filtered.reserve(P->particleBlock.particles.size());
    for (size_t i=0;i<P->particleBlock.particles.size();i++){
      auto &p = P->particleBlock.particles[i];
      if (P->flag_mask[i] == 0 && gParticleVisualConfig.types[p.type].hideParticles == false)
	filtered.push_back(p);
    }
      
    g_filteredParticleCount = filtered.size();

    size_t nStress = 0;
    for (auto &pp : filtered) if (pp.flag_stress == 1) nStress++;
    static size_t last = (size_t)-1;
    if (nStress != last) {
      std::printf("[stress] filtered=%zu stressed=%zu\n", filtered.size(), nStress);
      last = nStress;
    }
    
    glBufferData(GL_ARRAY_BUFFER, filtered.size() * sizeof(ParticleData), filtered.data(), GL_DYNAMIC_DRAW);      
        
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    P->particlesDirty = false; // 転送後、フラグをクリア
    P->velocityDirty = true;
  }

  glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
  float fovY = 45.0f;
#ifdef USE_LETTERBOX
  //glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)SCR_WIDTH / SCR_HEIGHT, 0.1f, 100.0f);
  const float targetAspect = 1280.0f / 720.0f;  // 固定アスペクト比
  glm::mat4 projection = glm::perspective(glm::radians(fovY), targetAspect, 0.1f, 1000.0f);
  int viewportW = g_viewportWidth;
  int viewportH = g_viewportHeight;
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
  
  if (g_flagShowCuboid)
    RenderCuboid(view, projection);
  
  glm::mat4 model = glm::mat4(1.0f);

  // --- パーティクル描画 ---
  glUseProgram(particleProgram);
  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

  float pointSizes[6];
  float valueMin[6];
  float valueMax[6];
  int useLog[6];
  int samplers[6];
  int periodicMapping[6];
  
  for (int i = 0; i < 6; ++i) {
    const auto& cfg = gParticleVisualConfig.types[i];

    pointSizes[i] = cfg.pointSize;
    valueMin[i]   = cfg.colorMin;
    valueMax[i]   = cfg.colorMax;
    useLog[i]     = cfg.useLogScale ? 1 : 0;
    periodicMapping[i] = cfg.periodicColorBar ? 1 : 0;
    samplers[i]   = i;

    int cmap = cfg.colormapIndex;
    if (cmap < 0) cmap = 0;
    if (cmap >= gNumColormaps) cmap = gNumColormaps - 1;

    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_1D, colormapTextures[cmap]);
  }

  glUniform1fv(glGetUniformLocation(particleProgram, "pointSizes"), 6, pointSizes);
  glUniform1fv(glGetUniformLocation(particleProgram, "valueMin"), 6, valueMin);
  glUniform1fv(glGetUniformLocation(particleProgram, "valueMax"), 6, valueMax); 
  glUniform1iv(glGetUniformLocation(particleProgram, "useLog"), 6, useLog);
  glUniform1iv(glGetUniformLocation(particleProgram, "periodicMapping"), 6, periodicMapping);  
  glUniform1iv(glGetUniformLocation(particleProgram, "colormaps"), 6, samplers);
  
  glBindVertexArray(particleVAO);
  if(flagHideAllParticles == false)
    glDrawArrays(GL_POINTS, 0, g_filteredParticleCount);
  glBindVertexArray(0);

  // ※速度ベクトルの描画（インスタンシング方式）
  if (showVelocityVectors) {
    // 速度データが更新されている場合のみインスタンスバッファを更新
    if (P->velocityDirty) {
      UpdateVelocityInstanceBuffer(P->particleBlock.particles);
      P->velocityDirty = false;
    }
    RenderVelocityVectors(view, projection, arrowScale, useVelocityArrowLogScale);
  }

  if(g_flagShowSinkIDs){
    UpdateLabelIndicesIfNeeded(); //test
    DrawParticleLabels(view, projection); //test
  }

  RenderCross(view, projection);  // 粒子描画後に呼び出す

#ifdef ISO_CONTOUR
  if (showIsocontour && meshIsocontourVAO) {
    glUseProgram(isocontourProgram);       // if you have a separate shader
    glUniformMatrix4fv(glGetUniformLocation(isocontourProgram, "model"),      1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(isocontourProgram, "view"),       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(isocontourProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(isocontourProgram, "opacity"), isoOpacity);
    glBindVertexArray(meshIsocontourVAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)indsIsocontour.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
  }
#endif

#ifdef GEOMETRICAL_ANALYSIS
  if (showEllipsoid && ellipsoidVAO) {
#ifdef USE_ELLIPSES
    glUseProgram(ellipseProgram);
    
    glm::mat4 M = gEllipsoid->getModelMatrix();
    glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram,"model"),1,GL_FALSE,glm::value_ptr(M));
    glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram,"view"), 1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram,"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(ellipsoidProgram,"opacity"), isoOpacityEllipsoid);

    glBindVertexArray(ellipsoidVAO); 
    glDrawArrays(GL_LINE_LOOP,            0,           maxSegmentsEllipse);
    glDrawArrays(GL_LINE_LOOP,            maxSegmentsEllipse,      maxSegmentsEllipse);
    glDrawArrays(GL_LINE_LOOP,            2*maxSegmentsEllipse,    maxSegmentsEllipse);
    glBindVertexArray(0);
#else
    glUseProgram(ellipsoidProgram);
    glm::mat4 M = gEllipsoid->getModelMatrix();

    glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram,"model"),1,GL_FALSE,glm::value_ptr(M));
    glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram,"view"), 1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram,"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(ellipsoidProgram,"color"),1,glm::value_ptr(glm::vec3(1.,1.,1.)));
    glUniform1f(glGetUniformLocation(ellipsoidProgram,"opacity"), isoOpacityEllipsoid);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);       // 深度は書かず透明を重ねるなら

    glBindVertexArray(ellipsoidVAO);
    glDrawElements(GL_TRIANGLES, sphereI.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif
  }

  if (showDisks && diskVAO) {
    glUseProgram(diskProgram);
    glm::mat4 M = gDiskFinder->getModelMatrix();
    
    glUniformMatrix4fv(glGetUniformLocation(diskProgram,"model"),1,GL_FALSE,glm::value_ptr(M));
    glUniformMatrix4fv(glGetUniformLocation(diskProgram,"view"), 1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(diskProgram,"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(diskProgram,"color"), 1.0f,1.0f,1.0f);
    glUniform1f(glGetUniformLocation(diskProgram,"opacity"), diskOpacity);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);          // 透過重ねる場合

    glBindVertexArray(diskVAO);
    glDrawElements(GL_TRIANGLES, g_indexCountDisk, GL_UNSIGNED_INT, 0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }
#endif

#ifdef STREAM_LINE
  if (showStreamLine && streamlineVAO) {
    static std::vector<size_t> m_firsts, m_counts;    
    static std::vector<GLint> firstsGL;
    static std::vector<GLsizei> countsGL;
    
    if(flagStreamDirty){
      const StreamlineMeshData stream_mesh = gStreamLine->meshData();
      
      m_firsts = stream_mesh.firsts;
      m_counts = stream_mesh.counts;

      firstsGL.resize(m_firsts.size());
      countsGL.resize(m_counts.size());
      for(size_t i=0;i<m_firsts.size();++i){
	firstsGL [i] = static_cast<GLint>(m_firsts[i]);
	countsGL [i] = static_cast<GLsizei>(m_counts[i]);
      }
      
      glBindBuffer(GL_ARRAY_BUFFER, streamlineVBO);
      glBufferData(GL_ARRAY_BUFFER,
		   stream_mesh.vertices.size()*sizeof(float),
		   stream_mesh.vertices.data(),
		   GL_DYNAMIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);

      flagStreamDirty = false;
    }
    
    glUseProgram(streamlineProgram);

    glUniformMatrix4fv(glGetUniformLocation(streamlineProgram,"model"),1,GL_FALSE,glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(streamlineProgram,"view"), 1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(streamlineProgram,"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(streamlineProgram,"color"), 1.0f,1.0f,1.0f);
    glUniform1f(glGetUniformLocation(streamlineProgram,"opacity"), streamlineopacity);

    int streamlineCount = firstsGL.size();
    glBindVertexArray(streamlineVAO);
    glMultiDrawArrays(
		      GL_LINE_STRIP,
		      firstsGL.data(),
		      countsGL.data(),
		      (GLsizei)streamlineCount
		      );
    glBindVertexArray(0);
  }
#endif

  if (gCubeManager.showCubes() && cubicVAO) {
    static std::vector<glm::mat4> instanceModels;
    std::vector<float> opacities;
    
    if (flagCubesDirty) {
      // 1) インスタンス変換行列を再構築
      instanceModels.clear();
      const auto& cubes = gCubeManager.getCubes(); // 立方体の位置・スケール情報を持つ自前管理クラス
      instanceModels.reserve(cubes.size());
      for (auto& c : cubes) {
	glm::mat4 M = glm::mat4(1.0f);
	M = glm::translate(M, c.position);
	M = glm::scale   (M, glm::vec3(c.size));
	instanceModels.push_back(M);
	opacities.push_back(c.opacity);
      }

      // 2) GPU にアップロード
      glBindBuffer(GL_ARRAY_BUFFER, cubicInstanceVBO);    // ← ここを instanceVBO に
      glBufferData(GL_ARRAY_BUFFER,
		   instanceModels.size() * sizeof(glm::mat4),
		   instanceModels.data(),
		   GL_DYNAMIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      
      glBindBuffer(GL_ARRAY_BUFFER, cubicOpacityVBO);
      glBufferData(GL_ARRAY_BUFFER,
		   opacities.size() * sizeof(float),
		   opacities.data(),
		   GL_DYNAMIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      
      flagCubesDirty = false;
    }

    // 3) シェーダーと行列セットアップ
    glUseProgram(cubicProgram);
    glUniformMatrix4fv(glGetUniformLocation(cubicProgram, "view"),       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(cubicProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(cubicProgram,"color"), 1.0f,1.0f,1.0f);
    // 必要なら色やライティングパラメータもここで set

    // 4) 描画
    glBindVertexArray(cubicVAO);
    // インデックス数 36 (= 12 面×3 頂点)、インスタンス数だけ一度に描く
    glDrawElementsInstanced(GL_TRIANGLES,
                            36,
                            GL_UNSIGNED_INT,
                            nullptr,
                            (GLsizei)instanceModels.size());
    glBindVertexArray(0);
  }

  if (flagShowCoordinates && coordVAO) {
    GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // 2) ライン幅を太めに
    glLineWidth(50.0f);

    glm::mat4 P_ortho = glm::ortho(-1.0f, +1.0f,
				   -1.0f, +1.0f,
				   -1.0f, +1.0f);
    
    glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(+0.85f, -0.75f, 0.0f));
    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 0.1f));

    // (A) ビュー回転を打ち消す行列はそのまま
    glm::mat4 R_c = glm::transpose(glm::mat4(glm::mat3(view)));
    
    // 合成して model_axes を作成
    glm::mat4 model_axes = T * R_c * S;

    glUseProgram(coordProgram);

    GLint locModel      = glGetUniformLocation(coordProgram, "model");
    GLint locView       = glGetUniformLocation(coordProgram, "view");
    GLint locProjection = glGetUniformLocation(coordProgram, "projection");

    glUniformMatrix4fv(locModel,      1, GL_FALSE, glm::value_ptr(model_axes));
    glUniformMatrix4fv(locView,       1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));  
    glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(P_ortho));

    glBindVertexArray(coordVAO);
    glDrawArrays(GL_LINES, 0, 6);
    glBindVertexArray(0);
     
    // 8) 深度テスト／深度マスクを元に戻す
    glDepthMask(GL_TRUE);
    if (wasDepthTest) glEnable(GL_DEPTH_TEST);
  }

#ifdef VOLUME_RENDERING 
  if(showVolumeRendering && flagRT == 1){    
    if(flagUpdateRendering){
      G_bvh = uploadBVH_TBO(gBVHresult);
      flagUpdateRendering = false;
    }
    
    int lowW = std::max(1.0f, g_viewportWidth  / rtDownscale);
    int lowH = std::max(1.0f, g_viewportHeight / rtDownscale);
    if (lowW != gRtW || lowH != gRtH) {
      CreateOrResizeRTFBO(lowW, lowH);
    }
   
    /*********************** draw rt results on FBO ************************/
    glBindFramebuffer(GL_FRAMEBUFFER, gRtFbo);
    glViewport(0, 0, gRtW, gRtH);
    
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(rtProgram);
    glBindVertexArray(fullscreenVAO);

    int debug_mode = 0;
    glUniform1i (glGetUniformLocation(rtProgram,"uDebugMode"), debug_mode);
    
    // TBO をユニットへ
    bindTBO(rtProgram, "nodeMinTB",    G_bvh.nodeMin.tex,    0);
    bindTBO(rtProgram, "nodeMaxTB",    G_bvh.nodeMax.tex,    1);
    bindTBO(rtProgram, "nodeChildTB",  G_bvh.nodeChild.tex,  2);
    bindTBO(rtProgram, "particlesTB",  G_bvh.particles.tex,  3);
    bindTBO(rtProgram, "partSigmaTB",  G_bvh.partSigma.tex,  4);

    // カメラ・LOD ユニフォーム
    glUniform1i (glGetUniformLocation(rtProgram,"uRoot"), G_bvh.root);
    glUniform1i (glGetUniformLocation(rtProgram,"uLodMode"), lodMode); // 0/1/2
    glUniform1f (glGetUniformLocation(rtProgram,"uPxThreshold"), pxThreshold);
    glUniform1f (glGetUniformLocation(rtProgram,"uTauMax"), tauMax);
    glUniform1f (glGetUniformLocation(rtProgram,"uStepBias"), 1e-4f);

    glUniform1f (glGetUniformLocation(rtProgram,"uFocalPx"), focalPx);
    glUniformMatrix4fv(glGetUniformLocation(rtProgram,"invProj"),1,GL_FALSE, glm::value_ptr(invProj));
    glUniformMatrix4fv(glGetUniformLocation(rtProgram,"invView"),1,GL_FALSE, glm::value_ptr(invView));
    glUniformMatrix4fv(glGetUniformLocation(rtProgram,"view"),1,GL_FALSE, glm::value_ptr(view));
    glUniform3fv(glGetUniformLocation(rtProgram,"uCamForward"), 1, glm::value_ptr(camForward));

    //glUniform2f(glGetUniformLocation(rtProgram,"uResolution"), (float)viewportW, (float)viewportH);    
    // TBOバインドや各種uniformはそのまま。ただし uResolution は **低解像度** を渡す
    glUniform2f(glGetUniformLocation(rtProgram,"uResolution"), (float)gRtW, (float)gRtH);
    // そのほか uRoot, invProj, invView, view, uTauMax, uPxThreshold... 既存通り
    // （ここは今までのままでOK）

    glDrawArrays(GL_TRIANGLES, 0, 3);    
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // 戻す
    /**************************************************************/

    // ---- Upscale to screen ----
#ifdef USE_LETTERBOX
    glViewport(g_viewportX, g_viewportY, g_viewportWidth, g_viewportHeight);
#else
    glViewport(0, 0, viewportW, viewportH);
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(upscaleProgram);
    glBindVertexArray(fullscreenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRtTex);
    glUniform1i(glGetUniformLocation(upscaleProgram, "uLow"), 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glUseProgram(0);    
  }

  if(showVolumeRendering && flagRT == 2){    
    if(flagUpdateRendering){
      uploadOctTree_TBO(gOctTree);
      flagUpdateRendering = false;
    }
    
    int lowW = std::max(1.0f, g_viewportWidth  / rtDownscale);
    int lowH = std::max(1.0f, g_viewportHeight / rtDownscale);
    if (lowW != gRtW || lowH != gRtH) {
      CreateOrResizeRTFBO(lowW, lowH);
    }
   
    /*********************** draw rt results on FBO ************************/
    glBindFramebuffer(GL_FRAMEBUFFER, gRtFbo);
    glViewport(0, 0, gRtW, gRtH);
    
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(octrayProgram);
    glBindVertexArray(fullscreenVAO);

    int debug_mode = 0;
    glUniform1i (glGetUniformLocation(octrayProgram,"uDebugMode"), debug_mode);
    
    // TBO をユニットへ
    bindTBO(octrayProgram, "nodeMinTB", gOctTree.gpuTree.nodeMin.tex,    0);
    bindTBO(octrayProgram, "nodeMaxTB", gOctTree.gpuTree.nodeMax.tex,    1);
    bindTBO(octrayProgram, "childATB",  gOctTree.gpuTree.texChildA.tex,  2);
    bindTBO(octrayProgram, "childBTB",  gOctTree.gpuTree.texChildB.tex,  3);
    bindTBO(octrayProgram, "cornerLoTB", gOctTree.gpuTree.cornerLo.tex,  4);
    bindTBO(octrayProgram, "cornerHiTB", gOctTree.gpuTree.cornerHi.tex,  5);

    // カメラ・LOD ユニフォーム
    glUniform1i (glGetUniformLocation(octrayProgram,"uRoot"), gOctTree.gpuTree.root);
    glUniform1f (glGetUniformLocation(octrayProgram,"uPxThreshold"), pxThreshold);
    glUniform1f (glGetUniformLocation(octrayProgram,"uTauMax"), tauMax);
    glUniform1f (glGetUniformLocation(octrayProgram,"uStepBias"), 1e-4f);

    glUniform1f (glGetUniformLocation(octrayProgram,"uFocalPx"), focalPx);
    glUniformMatrix4fv(glGetUniformLocation(octrayProgram,"invProj"),1,GL_FALSE, glm::value_ptr(invProj));
    glUniformMatrix4fv(glGetUniformLocation(octrayProgram,"invView"),1,GL_FALSE, glm::value_ptr(invView));
    glUniformMatrix4fv(glGetUniformLocation(octrayProgram,"view"),1,GL_FALSE, glm::value_ptr(view));
    glUniform3fv(glGetUniformLocation(octrayProgram,"uCamForward"), 1, glm::value_ptr(camForward));
    glUniform2f(glGetUniformLocation(octrayProgram,"uResolution"), (float)gRtW, (float)gRtH);
    
    glDrawArrays(GL_TRIANGLES, 0, 3);    
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // 戻す
    /**************************************************************/

    // ---- Upscale to screen ----
#ifdef USE_LETTERBOX
    glViewport(g_viewportX, g_viewportY, g_viewportWidth, g_viewportHeight);
#else
    glViewport(0, 0, viewportW, viewportH);
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(upscaleProgram);
    glBindVertexArray(fullscreenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRtTex);
    glUniform1i(glGetUniformLocation(upscaleProgram, "uLow"), 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glUseProgram(0);    
  }


  if(showVolumeRendering && (flagRT == 0)){    
    int lowW = g_viewportWidth;
    int lowH = g_viewportHeight;
    if (lowW != wboitW || lowH != wboitH) {
      CreateOrResizeWBOITFBO(lowW, lowH);
    }
    
    // 2) WBOIT FBO へバインド&クリア
    glBindFramebuffer(GL_FRAMEBUFFER, wboitFbo);
    GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, bufs);

    // 重要：初期値
    GLfloat color0[4] = {0.f, 0.f, 0.f, 0.f};
    GLfloat color1[4] = {1.f, 0.f, 0.f, 0.f};
    glClearBufferfv(GL_COLOR, 0, color0);  // accum = 0
    glClearBufferfv(GL_COLOR, 1, color1);  // reveal= 1（積の単位元）

    // 3) ブレンド設定（添付ごとに別設定）
    glEnablei(GL_BLEND, 0); // color0 (accum)
    glBlendFunci(0, GL_ONE, GL_ONE);               // accum を加算
    glBlendEquationi(0, GL_FUNC_ADD);

    glEnablei(GL_BLEND, 1); // color1 (reveal)
    // dst = dst * (1 - srcAlpha) になるように：
    //glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR); 
    glBlendEquationi(1, GL_FUNC_ADD);

    glDisable(GL_DEPTH_TEST); // 粒子の順序独立を狙うので基本OFF（必要なら深度読みでもOK）

    // 4) 粒子パス：同じ VAO をそのまま
    glUseProgram(wboitParticleProgram);
    glBindVertexArray(particleVAO);

    glUniformMatrix4fv(glGetUniformLocation(wboitParticleProgram, "model"),1,GL_FALSE,glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(wboitParticleProgram, "view"), 1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(wboitParticleProgram, "projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform1f (glGetUniformLocation(wboitParticleProgram, "uFocalPx"), focalPx);
    glUniform1i (glGetUniformLocation(wboitParticleProgram, "uKernelMode"), kernelMode);
    glUniform1f (glGetUniformLocation(wboitParticleProgram, "uGaussNSigma"), gaussNSigma);
    glUniform1f (glGetUniformLocation(wboitParticleProgram, "uEnlargeHsml"), enlargeKernel);
    // pointSizes[], typeColors[], baseAlpha, softness を適宜
    // 例
    glUniform1f(glGetUniformLocation(wboitParticleProgram,"baseAlpha"), 0.1f);
    glUniform1f(glGetUniformLocation(wboitParticleProgram,"softness"), 0.9f);

    glDrawArrays(GL_POINTS, 0, g_filteredParticleCount);

    glBindVertexArray(0);
    glUseProgram(0);

    // 5) デフォルトFBOへ戻し、合成   
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisablei(GL_BLEND, 0);
    glDisablei(GL_BLEND, 1);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //glEnable(GL_DEPTH_TEST); // 他ジオメトリを描くなら戻してOK
    
    glUseProgram(wboitCompositeProgram);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texAccum);
    glUniform1i(glGetUniformLocation(wboitCompositeProgram,"uAccumTex"), 0);

    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texReveal);
    glUniform1i(glGetUniformLocation(wboitCompositeProgram,"uRevealTex"), 1);

    // フルスクリーン三角形で出力（あなたの fullscreenVAO/三角形流用）
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST); // 他ジオメトリを描くなら戻してOK
    
    glUseProgram(0);
  }
#endif

#ifdef USE_CONVEX_HULL
  if (gClumpFind->checkClumpComputation()){
    glUseProgram(lineProgram);
    glUniformMatrix4fv(glGetUniformLocation(lineProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(lineProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    const int nclumps = gClumpFind->get_nclumps();

    // 2) clump側が「全クリア」と言ったらGPUキャッシュも削除
    if (gClumpFind->checkClearCache()) {
      for (auto& [id, e] : s_hull) {
	if (e.vbo) glDeleteBuffers(1, &e.vbo);
	if (e.vao) glDeleteVertexArrays(1, &e.vao);
      }
      s_hull.clear();
      gClumpFind->finishClearCache();
      // CPU側のPolyhedronや頂点配列は**持っていない**ので何もする必要なし
    }

    // 3) 各クランプの描画
    for (int i = 0; i < nclumps; ++i) {
      if (!gClumpFind->flagShowHull(i)) continue;

      // エントリ確保
      auto& e = s_hull[i];
      if (e.vao == 0) {
	// 初回だけVAO/VBO作成＆レイアウト設定
	glGenVertexArrays(1, &e.vao);
	glGenBuffers(1, &e.vbo);
	glBindVertexArray(e.vao);
	glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
	glBindVertexArray(0);
	e.dirty = true; // 新規なので更新が必要
      }

      // dirtyなら：点群→凸包→線分→VBOへアップロード
      if (e.dirty) {
	TrackingVector<ParticleData> pts = gClumpFind->get_particle_indices(i, P->particleBlock.particles);       
	TrackingVector<float> vertices = gConvexHullGenerator->buildLineVertices(pts);

	e.vertexCount = (GLsizei)(vertices.size() / 3);

	glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size()*sizeof(float), vertices.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	e.dirty = false;
      }

      // ドロー：**VAOは描画時にバインドが必要**
      glBindVertexArray(e.vao);
      glDrawArrays(GL_LINES, 0, e.vertexCount);
      glBindVertexArray(0);
    }

    glUseProgram(0);
  }
#endif
    
  RenderColorBar(gParticleVisualConfig.types[0].colormapIndex);
  RenderColorBarLabels();
}

static GLuint   gProjTex = 0;
static int      gProjW = 0, gProjH = 0;
static uint64_t gUploadedVersion = 0;

void RenderProjectionUI(ProjectionMapGenerator& gen)
{
  if (!gen.getImageFlag())
    return;
  
  const auto& img = gen.getImage();

  // ImGui ウィンドウはここで初めて開く
  ImGui::Begin("2D Projection Map");

  if (img.width > 0 && img.height > 0 &&
      img.rgb.size() == static_cast<size_t>(img.width * img.height * 3))
    {
      // 新しいバージョンならGPUへアップロード
      if (img.version != gUploadedVersion)
        {
	  if (gProjTex == 0) {
	    glGenTextures(1, &gProjTex);
	    glBindTexture(GL_TEXTURE_2D, gProjTex);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	    gProjW = gProjH = 0;
	  }

	  glBindTexture(GL_TEXTURE_2D, gProjTex);
	  GLint prevUnpack;
	  glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpack);
	  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	  if (img.width != gProjW || img.height != gProjH) {
	    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8,
			 img.width, img.height, 0,
			 GL_RGB, GL_UNSIGNED_BYTE, img.rgb.data());
	    gProjW = img.width;
	    gProjH = img.height;
	  } else {
	    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
			    img.width, img.height,
			    GL_RGB, GL_UNSIGNED_BYTE, img.rgb.data());
	  }

	  glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpack);
	  glBindTexture(GL_TEXTURE_2D, 0);
	  gUploadedVersion = img.version;
        }

      // ImGui上にテクスチャを表示
      ImVec2 size((float)gProjW, (float)gProjH);
      ImGui::Image((ImTextureID)(intptr_t)gProjTex, size);
    }
  else {
    ImGui::TextUnformatted("Invalid image buffer.");
  }

  ImGui::End();
}

void Cleanup() {
#ifdef PYTHON_BRIDGE
  if (g_py.ptr) {
    g_py.ptr->shutdown(); 
    g_py.ptr.reset();
  }
#endif
  
  // OpenGL バッファ解放
  glDeleteVertexArrays(1, &particleVAO);
  glDeleteBuffers(1, &particleVBO);
  glDeleteVertexArrays(1, &crossVAO);
  glDeleteBuffers(1, &crossVBO);

  // OpenGL シェーダープログラム解放
  glDeleteProgram(particleProgram);
  glDeleteProgram(lineProgram);

  ConfigMaskState maskState;
  ExportMaskConfigState(maskState);
  saveConfig("config.txt", P, gFileInfo, &gParticleVisualConfig, &maskState);
  
  // ImGui の終了処理
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  ImPlot::DestroyContext();

  // GLFW の終了処理
  glfwDestroyWindow(window);
  glfwTerminate();
}

int main() {  
  InitGLFW();        // GLFW & OpenGL の初期化
  InitImGui();       // ImGui の初期化
  InitShaders();     // シェーダーの初期化
  InitColorMaps();
  InitColorBar();

  gFileInfo = new FileInfo(camCtx);
  gRadialProfileComputer = std::make_unique<RadialProfileComputer>(camCtx.cameraTarget);
  gHistogram2DComputer = std::make_unique<Histogram2DComputer>(camCtx.cameraTarget);
  gProjectionMap2D = std::make_unique<ProjectionMapGenerator>();
  gClumpFind = new FindClump(camCtx);
  P = new ParticleArray(camCtx);
  
#ifdef ISO_CONTOUR
  gIsoContour = new IsoSurfaceGenerator();
#endif
#ifdef GEOMETRICAL_ANALYSIS
  gEllipsoid = new EllipseFitter();
  gDiskFinder = new DiskRadiusFinder();
#endif
  
  InitBuffers();     // OpenGL の VAO/VBO の初期化
  ConfigMaskState maskState;
  if (loadConfig("config.txt", P, gFileInfo, &gParticleVisualConfig, &maskState)) {
    ApplyMaskConfigState(maskState);
  }

  gFileInfo->setUnit(P);
  
  int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
  gFileInfo->loadBatch(newFileIndex, gFileInfo->batchSize, gFileInfo->skipStep, P);      
  
#ifdef USE_CONVEX_HULL
  gConvexHullGenerator = new ConvexHullGenerator();
#endif

#ifdef STREAM_LINE
  gStreamLine = new StreamlineComputer();
#endif

#ifdef VOLUME_RENDERING
  gBVH = new lbvh::MortonBuilder();
  gTF = new TransferFunctionEditor();
#endif
  
  // メインループ
  while (!glfwWindowShouldClose(window)) {
    // 1. フレーム時間の計測   
    float currentFrame = static_cast<float>(glfwGetTime());
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;
    (void)deltaTime;

    // 2. ユーザー入力の処理
    processInput(window);

    // 3. イベント処理（ウィンドウの更新など）
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // 4. UI の描画
    UpdateUI();            // ImGui UI の更新    
    
#ifdef PYTHON_BRIDGE
    if (g_py.ptr) {
      std::vector<FieldId> dirty;
      g_py.ptr->drainEditFields(dirty);
      if (!dirty.empty()) {
	bridge::applyFromSharedToAoS(g_py.ptr->shared(), *P, dirty);
	P->particlesDirty = true;      // 位置などを使うなら
      }
    }
#endif

    RenderProjectionUI(*gProjectionMap2D);
    // 5. 3D シーンの描画
    RenderScene();         // OpenGL 描画
        
    // 6. ImGui のレンダリング
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    glfwSwapBuffers(window);
  }
  
  Cleanup(); // メモリ解放 & ImGui 終了処理

#ifdef VOLUME_RENDERING
  delete gBVH;
  delete gTF;
#endif
#ifdef STREAM_LINE
  delete gStreamLine;
#endif
#ifdef USE_CONVEX_HULL
  delete gConvexHullGenerator;
#endif
#ifdef GEOMETRICAL_ANALYSIS
  delete gDiskFinder;
  delete gEllipsoid;
#endif
#ifdef ISO_CONTOUR
  delete gIsoContour;
#endif
  delete P;
  delete gClumpFind;
  delete gFileInfo;

  return 0;
}
 
