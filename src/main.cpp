// main.cpp
// -------------
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#ifdef MACOS
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

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
ConvexHullRenderer* gConvexHullRenderer = nullptr;
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
radialProfile *gRadialProfile;
histogram2D *gHistogram2D;
FindClump *gClumpFind;
ProjectionMapGenerator *gProjectionMap2D;
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

float typePointSizes[6];
float typeColorMax[6];
float typeColorMin[6];
bool useLogScale[6];
bool flagHideParticles[6] = {false, false, false, false, false, false};
size_t g_filteredParticleCount = 0;

const char* quantities_to_show[] = { "Density", "Temperature", "val", "val2", "Mass", "Hsml"};
int selectedColorMode[6];

// 粒子タイプごとに使うカラーマップ（6タイプ分）
GLuint colormapForType[6];

// 周期モードのフラグ（デフォルトはオフ）
bool periodicColorBar[6];

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

bool flagCubesDirty = true;

// ------------------------------
// config ファイル読み込み／保存
// ------------------------------
void loadConfig() {
  std::ifstream infile("config.txt");
  if (!infile.is_open()) return;
  std::string line;
  while (std::getline(infile, line)) {
    if (line.find("FileFormat=") == 0) {
      std::string val = line.substr(strlen("FileFormat="));
      std::strncpy(gFileInfo->fileFormat, val.c_str(), sizeof(gFileInfo->fileFormat)-1);
    } else if (line.find("FolderPath=") == 0) {  // フォルダパスを読み込む
      std::string val = line.substr(strlen("FolderPath="));
      std::strncpy(gFileInfo->folderPath, val.c_str(), sizeof(gFileInfo->folderPath)-1);
    } else if (line.find("TokenCount=") == 0) {
      int tokenCount = std::stoi(line.substr(strlen("TokenCount=")));
      gFileInfo->formatTokens.clear();
      for (int i = 0; i < tokenCount; i++) {
	if (!std::getline(infile, line)) break;
	std::istringstream iss(line);
	std::string tokenLabel, tokenType, tokenCountStr;
	if (!std::getline(iss, tokenLabel, ',')) continue;
	if (!std::getline(iss, tokenType, ',')) continue;
	if (!std::getline(iss, tokenCountStr)) continue;
	FormatToken ft;
	std::strncpy(ft.label, tokenLabel.c_str(), sizeof(ft.label)-1);

	ft.type = (tokenType == "float") ? DataType::Float :
	  (tokenType == "int32")  ? DataType::Int32 :(tokenType == "int") ? DataType::Int32 :
	  (tokenType == "int64") ? DataType::Int64 : (tokenType == "long long") ? DataType::Int64 :
	  DataType::Double;
	
	ft.count = std::stoi(tokenCountStr);
	FormatToken::SetDefaultDisplayName(ft);
	gFileInfo->formatTokens.push_back(ft);
      }
    }  else if (line.find("ParticleType") == 0) {
      // 例: "ParticleType0_Size=" の形式
      size_t pos = line.find("_");
      if (pos != std::string::npos) {
	std::string typeStr = line.substr(12, pos - 12);  // "0" の部分
	int type = std::stoi(typeStr);
	std::string key = line.substr(pos+1); // 例: "Size=2.5"

	printf("key=%s\n", key.c_str());
	
	if (key.find("Size=") == 0) {
	  typePointSizes[type] = std::stof(key.substr(5));
	} else if (key.find("Min=") == 0) {
	  typeColorMin[type] = std::stof(key.substr(4));
	} else if (key.find("Max=") == 0) {
	  typeColorMax[type] = std::stof(key.substr(4));
	} else if (key.find("Periodic=") == 0) {
	  periodicColorBar[type] = (std::stoi(key.substr(9)) == 1);
	} else if (key.find("UseLog=") == 0) {
	  useLogScale[type] = (std::stoi(key.substr(7)) == 1);
	}
      }
    } else if (line.find("NormalizationFactor=") == 0) {	    
      P->desiredMax = std::stof(line.substr(strlen("NormalizationFactor=")));
    } else if (line.find("skipStep=") == 0) {
      gFileInfo->skipStep = std::stoi(line.substr(strlen("skipStep=")));
    } else if (line.find("currentStep=") == 0) {
      gFileInfo->currentStep = std::stoi(line.substr(strlen("currentStep=")));    
    } else if (line.find("UnitMass_in_msolar") == 0){
      P->UnitMass_in_msolar = std::stof(line.substr(strlen("UnitMass_in_msolar=")));
    } else if (line.find("UnitLength_in_pc") == 0){
      P->UnitLength_in_pc = std::stof(line.substr(strlen("UnitLength_in_pc=")));
    } else if (line.find("Hubble") == 0){
      P->Hubble = std::stof(line.substr(strlen("Hubble=")));
    } else if (line.find("useComovingCorrdinate") == 0){
      P->useComovingCorrdinate = std::stof(line.substr(strlen("useComovingCorrdinate=")));
    }

  }
  infile.close();
}

void saveConfig() {
    std::ofstream outfile("config.txt");
    if (!outfile.is_open()) return;
    outfile << "FileFormat=" << gFileInfo->fileFormat << "\n";
    outfile << "FolderPath=" << gFileInfo->folderPath << "\n";  // フォルダパスを保存
    outfile << "TokenCount=" << gFileInfo->formatTokens.size() << "\n";
    for (size_t i = 0; i < gFileInfo->formatTokens.size(); i++) {
        outfile << gFileInfo->formatTokens[i].label << "," 
                << ((gFileInfo->formatTokens[i].type==DataType::Float)?"float":(gFileInfo->formatTokens[i].type==DataType::Int32)?"int32":(gFileInfo->formatTokens[i].type==DataType::Int64)?"int64":"double") << ","
                << gFileInfo->formatTokens[i].count << "\n";
    }
    // ここから粒子タイプごとの設定を保存
    const int numTypes = 6;
    for (int i = 0; i < numTypes; i++) {
        outfile << "ParticleType" << i << "_Size=" << typePointSizes[i] << "\n";
        outfile << "ParticleType" << i << "_Min=" << typeColorMin[i] << "\n";
        outfile << "ParticleType" << i << "_Max=" << typeColorMax[i] << "\n";
        outfile << "ParticleType" << i << "_Periodic=" << (periodicColorBar[i] ? 1 : 0) << "\n";
        outfile << "ParticleType" << i << "_UseLog=" << (useLogScale[i] ? 1 : 0) << "\n";
    }
    
    // もし normalizationFactor もタイプ非依存で保存するなら
    outfile << "NormalizationFactor=" << P->desiredMax << "\n";
    outfile << "skipStep=" << gFileInfo->skipStep << "\n";
    outfile << "currentStep=" << gFileInfo->currentStep << "\n";
    outfile << "UnitMass_in_msolar=" << P->UnitMass_in_msolar << "\n";
    outfile << "UnitLength_in_pc=" << P->UnitLength_in_pc << "\n";
    outfile << "Hubble=" << P->Hubble << "\n";
    outfile << "useComovingCorrdinate=" << P->useComovingCorrdinate << "\n";
    
    outfile.close();
}


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

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

// ------------------------------
// シェーダー関連ヘルパー関数
// ------------------------------
#ifdef SAVE_GPU_MEMORY
static const char* shaderHeader = 
  "#version 330 core\n"
  "#define SAVE_GPU_MEMORY\n";
#else
static const char* shaderHeader = 
  "#version 330 core\n";
#endif

unsigned int compileShader(unsigned int type, const char* source)
{
    unsigned int shader = glCreateShader(type);

    const char* sources[] = {
      shaderHeader,
      source
    };
	
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);
    
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[511];
        glGetShaderInfoLog(shader, 511, nullptr, infoLog);
        std::cerr << "ERROR::SHADER_COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    return shader;
}

unsigned int createShaderProgram(const char* vertexSource, const char* fragmentSource)
{
    unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    
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
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return program;
}


// ------------------------------
// シェーダーソースコード
// ------------------------------
const char* particleVertexShaderSource = R"(
layout (location = 0) in vec3 aPos;
#ifdef SAVE_GPU_MEMORY
layout (location = 1) in float aValShow;
layout (location = 2) in int aType;
layout (location = 3) in int inNodeFlag;
#else
layout (location = 1) in float aDensity;      // 追加：密度
layout (location = 2) in float aTemperature;  // 追加：温度
layout (location = 3) in float aVal;
layout (location = 4) in float aVal2;
layout (location = 5) in int aType;
layout (location = 6) in int inNodeFlag;
#endif

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float pointSizes[6];

#ifdef SAVE_GPU_MEMORY
out float val_show;
#else
out float temperature;
out float density;
out float val;
out float val2;
#endif

flat out int nodeFlag;
flat out int vType;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    gl_PointSize = pointSizes[aType];

#ifdef SAVE_GPU_MEMORY
    val_show = aValShow;
#else
    val = aVal;
    val2 = aVal2;
    density = aDensity;
    temperature = aTemperature;
#endif

    vType = aType;
    nodeFlag = inNodeFlag;
}
)";

const char* particleFragmentShaderSource = R"(
#ifdef SAVE_GPU_MEMORY
in float val_show;
#else
in float density;
in float temperature;
in float val;
in float val2;
#endif

flat in int vType;
flat in int nodeFlag;
out vec4 FragColor;

#ifndef SAVE_GPU_MEMORY
uniform int colorMode[6];
#endif

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

#ifdef SAVE_GPU_MEMORY
  varValue = val_show;
#else
  int colormode = colorMode[vType];
  if(colormode == 0)
    varValue = density;
  else if(colormode == 1)
    varValue = temperature;
  else if(colormode == 2)
    varValue = val;
  else if(colormode == 3)
    varValue = val2;
  else
    varValue = density; // デフォルトは val
#endif

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
  
  if (nodeFlag == 1) 
    color = mix(color, vec3(1.0), 0.5);

  FragColor = vec4(color, 1.);
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
}

void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImPlot::CreateContext();
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

GLuint cubicProgram;

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
  quadShader = createShaderProgram(colormap2DShaderSource, colormap2DFragmentShaderSource);
  // Quad 準備
  SetupQuad();
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
#ifdef STREAM_LINE
GLuint streamlineVAO, streamlineVBO;
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
  glBufferData(GL_ARRAY_BUFFER, P->particles.size() * sizeof(ParticleData), P->particles.data(), GL_DYNAMIC_DRAW);
    
  // attribute 0: pos → offset 0
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleData),  (void*)offsetof(ParticleData, pos));
  glEnableVertexAttribArray(0);

#ifdef SAVE_GPU_MEMORY
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleData), (void*)offsetof(ParticleData, val_show));
  glEnableVertexAttribArray(1);
  // attribute 5: type → offset 4 * sizeof(float)
  glVertexAttribIPointer(2, 1, GL_INT, sizeof(ParticleData), (void*)offsetof(ParticleData, type));
  glEnableVertexAttribArray(2);
  // attribute 6: type → offset 4 * sizeof(float)
  glVertexAttribIPointer(3, 1, GL_INT, sizeof(ParticleData), (void*)offsetof(ParticleData, flag));
  glEnableVertexAttribArray(3);
#else
  // attribute 1: density → offset 3 * sizeof(float)
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleData), (void*)offsetof(ParticleData, density));
  glEnableVertexAttribArray(1);
  // attribute 2: temperature → offsetsizeof(float)
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleData), (void*)offsetof(ParticleData, temperature));
  glEnableVertexAttribArray(2);
  // attribute 3: value → offsetsizeof(float)
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleData), (void*)offsetof(ParticleData, val));
  glEnableVertexAttribArray(3);
  // attribute 4: value2 → offset sizeof(float)
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleData), (void*)offsetof(ParticleData, val2));
  glEnableVertexAttribArray(4);
  // attribute 5: type → offset 4 * sizeof(float)
  glVertexAttribIPointer(5, 1, GL_INT, sizeof(ParticleData), (void*)offsetof(ParticleData, type));
  glEnableVertexAttribArray(5);
  // attribute 6: type → offset 4 * sizeof(float)
  glVertexAttribIPointer(6, 1, GL_INT, sizeof(ParticleData), (void*)offsetof(ParticleData, flag));
  glEnableVertexAttribArray(6);
#endif
    
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

  // VAO 設定完了
  glBindVertexArray(0);
    
  InitVelocityArrowGeometry();
}


// Jet カラーマップ（9色例）
// ここでは RGB の各成分が 0.0～1.0 の値になっているものを定義しています
const float jetMap[] = {
    0.0f,  0.0f,  0.5f,  // dark blue
    0.0f,  0.0f,  1.0f,  // blue
    0.0f,  0.5f,  1.0f,  // cyan
    0.0f,  1.0f,  1.0f,  // light cyan
    0.5f,  1.0f,  0.5f,  // greenish
    1.0f,  1.0f,  0.0f,  // yellow
    1.0f,  0.5f,  0.0f,  // orange
    1.0f,  0.0f,  0.0f,  // red
    0.5f,  0.0f,  0.0f   // dark red
};

// Viridis カラーマップ（11色例）  
const float viridisMap[] = {
    0.267f, 0.004f, 0.329f,
    0.283f, 0.141f, 0.458f,
    0.254f, 0.265f, 0.530f,
    0.207f, 0.372f, 0.553f,
    0.164f, 0.471f, 0.558f,
    0.128f, 0.566f, 0.551f,
    0.135f, 0.659f, 0.517f,
    0.267f, 0.749f, 0.441f,
    0.478f, 0.821f, 0.318f,
    0.741f, 0.873f, 0.150f,
    0.993f, 0.906f, 0.144f
};

// Plasma カラーマップ（11色例）
const float plasmaMap[] = {
    0.050f, 0.029f, 0.527f,
    0.127f, 0.108f, 0.533f,
    0.212f, 0.192f, 0.540f,
    0.307f, 0.274f, 0.545f,
    0.411f, 0.354f, 0.550f,
    0.525f, 0.431f, 0.555f,
    0.647f, 0.506f, 0.557f,
    0.778f, 0.582f, 0.557f,
    0.915f, 0.658f, 0.555f,
    0.993f, 0.778f, 0.512f,
    0.990f, 0.901f, 0.396f
};


// InitColorMaps() を呼び出すと各カラーマップ用の 1D テクスチャが生成される
void InitColorMaps() {
    // --- Jet カラーマップ ---
    glGenTextures(1, &jetTex);
    glBindTexture(GL_TEXTURE_1D, jetTex);
    // jetMap の要素数は 9 (各要素は RGB の3値)
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 9, 0, GL_RGB, GL_FLOAT, jetMap);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // --- Viridis カラーマップ ---
    glGenTextures(1, &viridisTex);
    glBindTexture(GL_TEXTURE_1D, viridisTex);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 11, 0, GL_RGB, GL_FLOAT, viridisMap);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // --- Plasma カラーマップ ---
    glGenTextures(1, &plasmaTex);
    glBindTexture(GL_TEXTURE_1D, plasmaTex);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 11, 0, GL_RGB, GL_FLOAT, plasmaMap);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 終了後、必ずテクスチャをバインド解除
    glBindTexture(GL_TEXTURE_1D, 0);

    // 初期状態として currentColorMapTex を jetTex に設定
    currentColorMapTex = jetTex;
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
    ImGui::Text("Time: %.4f", P->Header.time);
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


bool flagCullingSphere = false;
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
	  // すでにある Point Size, Color などの UI に加えてカラーマップ選択コンボを追加
	  std::string comboLabel = "Colormap##" + std::to_string(i);
	  if (ImGui::Combo(comboLabel.c_str(), &colormapIndex[i], availableColormapNames, IM_ARRAYSIZE(availableColormapNames))) {
	    // ユーザーの選択に応じて colormapForType を更新
	    switch (colormapIndex[i]) {
	    case 0: colormapForType[i] = jetTex; break;
	    case 1: colormapForType[i] = viridisTex; break;
	    case 2: colormapForType[i] = plasmaTex; break;
	    default: colormapForType[i] = jetTex; break;
	    }

	    if(i == 0)
	      currentColorMapTex = colormapForType[i];
	  }
	  ImGui::Checkbox("Periodic Color Bar", &periodicColorBar[i]);
	  
	  std::string sliderLabel = "Point Size##" + std::to_string(i);
	  ImGui::SliderFloat(sliderLabel.c_str(), &typePointSizes[i], 1.0f, 100.0f);
	  std::string minLabel = "Value Min##" + std::to_string(i);
	  ImGui::InputFloat(minLabel.c_str(), &typeColorMin[i], 0.01f, 0.1f, "%.3f");
	  std::string maxLabel = "Value Max##" + std::to_string(i);
	  ImGui::InputFloat(maxLabel.c_str(), &typeColorMax[i], 0.01f, 0.1f, "%.3f");
	  std::string logLabel = "Use Log Scale##" + std::to_string(i);
	  ImGui::Checkbox(logLabel.c_str(), &useLogScale[i]);

	  bool flagHideParticles_prev =  static_cast<bool>(flagHideParticles[i]);
	  std::string hideLabel = "Hide particle##" + std::to_string(i);
	  ImGui::Checkbox(hideLabel.c_str(), &flagHideParticles[i]);
	  if(flagHideParticles_prev != flagHideParticles[i])
	    P->particlesDirty = true;
	    
#ifdef SAVE_GPU_MEMORY
	  int icolor_prev = selectedColorMode[i];
#endif

	  ImGui::Combo("Quantity", selectedColorMode + i, quantities_to_show, IM_ARRAYSIZE(quantities_to_show));  
	  std::string var = quantities_to_show[selectedColorMode[i]];	  
	  ImGui::Text("Current particle %s range: [%g, %g]"
		      , var.c_str(), P->particleValueMin[selectedColorMode[i]][i], P->particleValueMax[selectedColorMode[i]][i]);

#ifdef SAVE_GPU_MEMORY
	  if(icolor_prev != selectedColorMode[i]){
	    P->particlesDirty = true;  // グローバルなフラグをtrueに設定
	  }
#endif
	  
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

#ifdef HAVE_HDF5
  if (ImGui::CollapsingHeader("read Halo catalogue")){
    // Loadボタンを押すと hdf5 から読み込み
    if(ImGui::Button("Load Halo")){
      P->Haloes = gFileInfo->readHaloFromHDF5(gFileInfo->folderPath, gFileInfo->currentFileIndex);
      P->showWindowHaloList();
    }
  }
#endif
  
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
    
    bool flag = flagCullingSphere;
    ImGui::Checkbox("Culling sphere region", &flag);
    if(flag != flagCullingSphere){
      flagCullingSphere = flag;
      P->particlesDirty = true;  // グローバルなフラグをtrueに設定
    }
    
    ImGui::InputFloat("Culling radius", &radiusCullingSphere);
  }

  if (ImGui::CollapsingHeader("Show velocity field")) {
    ImGui::InputInt("show velocity field out of n particles", &velocitySubtraction);
    ImGui::InputFloat("Arrow Scale", &arrowScale, 0.1f, 1.0f, "%.2f");
    ImGui::Checkbox("Use Log Scale", &useVelocityArrowLogScale);
    
    if(ImGui::Checkbox("render velocity field", &showVelocityVectors)){
      if(showVelocityVectors)
	P->velocityDirty = true;
    }
  }
  
  if (ImGui::CollapsingHeader("Radial Profile Compute")) {
    if (ImGui::Button("Compute radial profile")) 
      gRadialProfile->showWindow();    
  }

  if (ImGui::CollapsingHeader("2D histogram Compute")) {    
    //what quantities you want to calculate
    if (ImGui::Button("Compute 2D histogram")) 
      gHistogram2D->showWindow();    
  }

  if(ImGui::CollapsingHeader("Clump Finder Controls")){
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

	if(P->particles.size() == 0)
	  continue;
	
	gClumpFind->do_FOF_and_output_clump_data(method, P->particles, P->Header, filename, newFileIndex);
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
  }

  if (ImGui::CollapsingHeader("Stellar density")) {    
    //what quantities you want to calculate
    static int type=3;
    ImGui::InputInt("particle type for density computation##render", &type);    
    
    if (ImGui::Button("Compute stellar density")){
      P->computeStellarDensity(type);
      P->particlesDirty = true;  // グローバルなフラグをtrueに設定
    }
  }

  if (ImGui::CollapsingHeader("Render projection map")) {
    if (ImGui::Button("make projection map"))
      gProjectionMap2D->showWindow();    

    ImGui::Text("create projection maps for continuous snapshots");

    static int nsnapshots = 10;
    static char outputFileFormat[255]="image_%04d.png";
    static char outputFolderPath[255]="./output/";
    static char outputFileName[255]="output.mp4";
    ImGui::InputInt("number of snapshots##render", &nsnapshots);
    ImGui::InputText("Output File Format##render", outputFileFormat, IM_ARRAYSIZE(outputFileFormat));
    ImGui::InputText("Output Folder##render", outputFolderPath, IM_ARRAYSIZE(outputFolderPath));
    ImGui::InputText("Output Name of Movie##render", outputFileName, IM_ARRAYSIZE(outputFolderPath));

    static bool flagFaceOn = false;
    ImGui::Checkbox("show face-on view", &flagFaceOn);
    
    if(ImGui::Button("generate maps")){
      int savedStep = gFileInfo->currentStep;
      
      namespace fs = std::filesystem;    
      fs::create_directory("ffmpeg_frames");
      
      for(int i=0;i<nsnapshots;i++){
	gFileInfo->currentStep = savedStep;
	if(i > 0) gFileInfo->currentStep += i;

	int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
	gFileInfo->loadNewSnapshot(newFileIndex, P);            
	
	if(P->particles.size() == 0)
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

	gProjectionMap2D->set_projection_parameters(P->particles, flag_use_amvector, flag_center ? pos_center : nullptr, -1.0f,
						    std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(), -1, -1, "");
       	
	gProjectionMap2D->make_density_map(P, filename);

        char linkname[512];
        snprintf(linkname, sizeof(linkname), "ffmpeg_frames/frame_%04d.png", i);

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
    }
  }

#ifdef GEOMETRICAL_ANALYSIS
  if (ImGui::CollapsingHeader("Extract disk")) {
    static int queryID_disk=0;
    ImGui::InputInt("Particle ID1##disk", &queryID_disk);
    ImGui::SliderFloat("Opacity##disk", &diskOpacity, 0.0f, 1.0f); 

    DiskRadiusFinder::Params param_disk;
    
    if (ImGui::Button("Find a disk around the paritlce")) {
      bool flag_found = false;
      for(auto &p : P->particles){
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
	
	gDiskFinder->compute(P->particles, param_disk);
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
	  if(P->particles.size() == 0)
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
	    for(auto &p : P->particles){
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
	      
	      gDiskFinder->compute(P->particles, param_disk0);
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
	    time_not_disk = P->Header.time;
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
		       , snap, P->Header.time, sqrt(dist2)*scale_fac, r1*scale_fac, r2*scale_fac, static_cast<int>(flag_disk));
	  std::fclose(fp_evo);

	  if(flag_disk){
	    time_disk = P->Header.time;
	    dist_disk = sqrt(dist2) * scale_fac;
	    snap_disk = snap;
	    r_disk1 = r1 * scale_fac;
	    r_disk2 = r2 * scale_fac;	    
	    break;
	  }else{
	    time_not_disk = P->Header.time;
	    snap_not_disk = snap;
	  }
	}
	
	std::fprintf(fp_out, "%d %d %d %g %d %g %g %g %g %d\n", r.idx, r.idA, r.idB, time_disk, snap_disk, dist_disk, r_disk1, r_disk2, time_not_disk, snap_not_disk);
	std::fclose(fp_out);
      }
    }
  }

  

  if (ImGui::CollapsingHeader("Extract ios-density contour")) {
    static int queryID1=0, queryID2=0;
    ImGui::InputInt("Particle ID1", &queryID1);
    ImGui::InputInt("Particle ID2", &queryID2); 
    ImGui::SliderFloat("Opacity##contour_ellipse", &isoOpacityEllipsoid, 0.0f, 1.0f); 
    
    if (ImGui::Button("Fit Iso-density ellipsoid")) {
      showEllipsoid = true;
      gEllipsoid->computeEllipse(P->particles, queryID1, queryID2);
    }
    
    if (ImGui::Button("disable Ellipsoid")) {
      showEllipsoid = false;
    }

    char fname_input[255]="binary_fragmentation.txt";
    char fname_output[255]="binary_fragmentation_output.txt";
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
	if(P->particles.size() == 0)
	  continue;        
	
	gEllipsoid->computeEllipse(P->particles, r.idA, r.idB);

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
    
  }
#endif
  
#ifdef ISO_CONTOUR
  if (ImGui::CollapsingHeader("Render Iso-contour")) {
    static float isoLevel = 0.;
    
    ImGui::InputFloat("Threshold value for iso-contour", &isoLevel);
    ImGui::SliderFloat("Opacity", &isoOpacity, 0.0f, 1.0f);

    static int max_treelevel = 15;
    ImGui::SliderInt("Maximum level of OctTree", &max_treelevel, 5, 20);

    const char* quantities_iso[] = { "Density", "Temperature", "val", "val2", "Mass" };
    // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
    static int selectedVar_iso = 0;
    ImGui::Combo("Quantity for Iso-Contour", &selectedVar_iso, quantities_iso, IM_ARRAYSIZE(quantities_iso));
    std::string var_iso = quantities_iso[selectedVar_iso];
    
    if (ImGui::Button("Build OctTree & Mesh")) {
      showIsocontour = true;

      TrackingVector<ParticleDataForTree> particles;
      particles.reserve(P->particles.size());
      for (const auto& pd : P->particles) {
	float val = pd.getValue(var_iso);
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
  }
#endif

#ifdef STREAM_LINE
  if (ImGui::CollapsingHeader("Render stream line")) {
    static int n_seeds=0;
    ImGui::Text("Seed setup");
    ImGui::InputInt("number of seed points", &n_seeds);

    static float seed_center[3] = {0.,0.,0.}, seed_len[3] = {0.,0.,0.}, seed_opacity = 0.5;
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
      gStreamLine->setRegionFromParticleData(P->particles);
      gStreamLine->setStreamRegionFromParticleData(P->particles);

      gStreamLine->setSeeds(P->particles, n_seeds);
      float degree = 10.;
      gStreamLine->build(P->particles, degree);
      
      showStreamLine = true;
      flagStreamDirty = true;
    }
    
    if (ImGui::Button("disable Grid & Mesh")) {
      showStreamLine = false;
    }    
  }
#endif
  
  if(ImGui::CollapsingHeader("Other settings")){
    bool unitChanged = false;
    if(ImGui::CollapsingHeader("Units")){
      unitChanged |= ImGui::InputDouble("UnitLength_in_cm"   , &P->UnitLength_in_cm   , 0.,0., "%g");
      unitChanged |= ImGui::InputDouble("UnitMass_in_msun"   , &P->UnitMass_in_g      , 0.,0., "%g");
      unitChanged |= ImGui::InputDouble("UnitVelocity_in_cgs", &P->UnitVelocity_in_cgs, 0.,0., "%g");
      unitChanged |= ImGui::InputDouble("Hubble"             , &P->Hubble, 0.,0., "%g");
      unitChanged |= ImGui::Checkbox("ComovingCorrdinate", &P->useComovingCorrdinate);

      ImGui::SeparatorText("Presets");      
      if (ImGui::Button("AU"))   { P->UnitLength_in_cm = P->au_in_cm;      unitChanged = true; }
      ImGui::SameLine();
      if (ImGui::Button("pc"))   { P->UnitLength_in_cm = P->pc_in_cm;      unitChanged = true; }
      ImGui::SameLine();
      if (ImGui::Button("kpc"))  { P->UnitLength_in_cm = P->kpc_in_cm;     unitChanged = true; }

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

  for (size_t i = 0; i < P->particles.size(); ++i) {
    auto &p = P->particles[i];

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
  if(P->particles.size() == 0){
    //to avoid an error when the particle is not loaded.
    return;
  }
  
  //ImDrawList* draw = ImGui::GetForegroundDrawList();
  ImDrawList* draw = ImGui::GetBackgroundDrawList(); 
  
  for (size_t idx : g_labelIndices) {
    const auto& p = P->particles[idx];

    glm::vec4 clip = proj * view *
      glm::vec4(p.pos[0], p.pos[1], p.pos[2], 1.0f);

    if (clip.w <= 0.f) continue;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (abs(ndc.x) > 1.f || abs(ndc.y) > 1.f) continue;

    float sx = g_viewportX + (ndc.x * 0.5f + 0.5f) * g_viewportWidth;
    float sy = g_viewportY + (1.f   - (ndc.y * 0.5f + 0.5f)) * g_viewportHeight;

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

/*

GLuint g_CuboidVAO = 0;
GLuint g_CuboidVBO = 0;

// 直方体のワイヤーフレーム用の頂点データをアップデートする関数
void RenderCuboid(const glm::mat4 &view, const glm::mat4 &projection)
{
  static bool g_CuboidInitialized = false;

  if (!g_CuboidInitialized) {
    glGenVertexArrays(1, &g_CuboidVAO);
    glGenBuffers(1, &g_CuboidVBO);
    g_CuboidInitialized = true;
  }

  TrackingVector<glm::vec3> edgeVertices; 
  
  edgeVertices.push_back(g_cubicPoints[0]); edgeVertices.push_back(g_cubicPoints[1]);
  edgeVertices.push_back(g_cubicPoints[1]); edgeVertices.push_back(g_cubicPoints[2]);
  edgeVertices.push_back(g_cubicPoints[2]); edgeVertices.push_back(g_cubicPoints[3]);
  edgeVertices.push_back(g_cubicPoints[3]); edgeVertices.push_back(g_cubicPoints[0]);
  
  // Top face edges: v4-v5, v5-v6, v6-v7, v7-v4
  edgeVertices.push_back(g_cubicPoints[4]); edgeVertices.push_back(g_cubicPoints[5]);
  edgeVertices.push_back(g_cubicPoints[5]); edgeVertices.push_back(g_cubicPoints[6]);
  edgeVertices.push_back(g_cubicPoints[6]); edgeVertices.push_back(g_cubicPoints[7]);
  edgeVertices.push_back(g_cubicPoints[7]); edgeVertices.push_back(g_cubicPoints[4]);
  
  // Vertical edges: v0-v4, v1-v5, v2-v6, v3-v7
  edgeVertices.push_back(g_cubicPoints[0]); edgeVertices.push_back(g_cubicPoints[4]);
  edgeVertices.push_back(g_cubicPoints[1]); edgeVertices.push_back(g_cubicPoints[5]);
  edgeVertices.push_back(g_cubicPoints[2]); edgeVertices.push_back(g_cubicPoints[6]);
  edgeVertices.push_back(g_cubicPoints[3]); edgeVertices.push_back(g_cubicPoints[7]);

  glBindVertexArray(g_CuboidVAO);
  glBindBuffer(GL_ARRAY_BUFFER, g_CuboidVBO);
  glBufferData(GL_ARRAY_BUFFER, edgeVertices.size() * sizeof(glm::vec3), edgeVertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);  
  glBindVertexArray(0);
  
  glUseProgram(lineProgram);
  glUniformMatrix4fv(glGetUniformLocation(lineProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(lineProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

  glBindVertexArray(g_CuboidVAO);
  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(edgeVertices.size()));  
  glBindVertexArray(0);
}
*/

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
const float colorBarWidth = 200.0f;   // 色バーの横幅
const float colorBarHeight = 20.0f;   // 色バーの高さ
const float margin = 20.0f;           // 画面右下からの余白

// ImGui のディスプレイサイズを利用して色バーの四隅のピクセル座標を計算する関数
void ComputeColorBarPixelCoords(float &left, float &right, float &top, float &bottom) {
#ifdef USE_LETTERBOX
    // 実際のレンダリング領域（レターボックス）のサイズを使う
    float effectiveWidth = static_cast<float>(g_viewportWidth);
    float effectiveHeight = static_cast<float>(g_viewportHeight);
#else
    ImGuiIO& io = ImGui::GetIO();
    float effectiveWidth = io.DisplaySize.x;
    float effectiveHeight = io.DisplaySize.y;
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
    float effectiveWidth = io.DisplaySize.x;
    float effectiveHeight = io.DisplaySize.y;
#endif

    float x_ndc = (x / effectiveWidth) * 2.0f - 1.0f;
    float y_ndc = -((y / effectiveHeight) * 2.0f - 1.0f);

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

void ShowTopParticlesUI() {
  static int queryID = -1;
  static bool hasFound = false;
  static ParticleData foundParticle;
  
  // 粒子の種類と表示件数 m をユーザーが入力できるようにする
  static int particleType = 3;   // デフォルトは Type 3
  static int m = 10;             // デフォルトは上位 10 件

  static std::deque<ParticleData> historyData;  // 検索結果を最大10件まで保存
  static int                  historySel = -1;  // ラジオボタンで選択中のインデックス

  const int histSizeMax = 10;
  
  ImGui::Begin("Particles Info");
  
  ImGui::InputInt("Particle ID", &queryID);
  ImGui::SameLine();
  
  if (ImGui::Button("Show Info")) {
    hasFound = false;
    for (const auto &p : P->particles) {
      if (p.ID == queryID) {
        foundParticle = p;
        hasFound = true;
        break;
      }
    }

    if (hasFound == true) {
      historyData.push_front(foundParticle);
      if (historyData.size() > histSizeMax)
        historyData.pop_back();
      historySel = 0;  // the newest entry
    }else
      ImGui::TextColored(ImVec4(1,0,0,1), "Particle ID %d not found", queryID);    
  }

  // ── Refresh ボタン ──
  if (ImGui::Button("Refresh History")) {
    int prevID = (historySel >= 0 && historySel < (int)historyData.size())
                   ? historyData[historySel].ID
                   : -1;

    std::deque<ParticleData> newHistory;
    newHistory.clear();

    std::unordered_set<int> seen;
    
    for (const auto &oldP : historyData) {
      if(seen.find(oldP.ID) != seen.end())
	continue;
      
      auto it = std::find_if(
          P->particles.begin(), P->particles.end(),
          [&](const ParticleData &p){ return p.ID == oldP.ID; });
      if (it != P->particles.end()) {
        newHistory.push_back(*it);
	seen.insert(oldP.ID);
      }
    }
    historyData.swap(newHistory);

    // リフレッシュ後に元の選択IDが残っていれば選択を復元
    if (prevID >= 0) {
      historySel = -1;
      for (int i = 0; i < (int)historyData.size(); ++i) {
        if (historyData[i].ID == prevID) {
          historySel = i;
          break;
        }
      }
    }

    if(historySel == -1 && historyData.size() > 0)
      historySel = 0;
  }
  ImGui::SameLine();
  
  if (ImGui::Button("Clear History")) {
    historyData.clear();
    historySel = -1;
  }
  
  for(size_t i=0;i<historyData.size();i++){
    auto &p = historyData[i];
    
    char label[256];
    std::snprintf(label, sizeof(label),
		  "ID %d: mass = %.3g, pos = (%.2g, %.2g, %.2g), vel = (%.2g, %.2g, %.2g), r=%g rho=%g T=%g H=%g",
		  p.ID, p.mass * (P->UnitMass_in_msolar/P->Hubble),
		  p.pos[0], p.pos[1], p.pos[2],
		  p.vel[0], p.vel[1], p.vel[2]
		  , p.originalHsml, p.density, p.temperature, P->Hubble);

    if (ImGui::Selectable(label)) {
      // 選択された粒子の位置をカメラの注視点に設定
      float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
      glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
      camCtx.cameraTarget = glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
      camCtx.cameraPos = camCtx.cameraTarget - direction * distance;

      historySel = i;
    }
  }
  
  if (ImGui::Button("Center this paritcle")) {
    if (hasFound == true && historySel >= 0){
      P->flag_follow_particle_ID = true;
      P->TargetParticleID = historyData[historySel].ID;

      P->flag_follow_clump_center = false;
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Disable center paritcle")) {
    P->flag_follow_particle_ID = false;  
  }
  
  // GUI内での粒子タイプ選択例
  static bool selectType0 = false;
  static bool selectType1 = false;
  static bool selectType2 = false;
  static bool selectType3 = false;
  static bool selectType4 = false;
  static bool selectType5 = false;

  bool flag_pushed = false;
  
  ImGui::Text("Select Particle Types:");
  // 1つの行に並べるためにSameLine()を使用
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 0", &selectType0)) {
    flag_pushed = true;
    // 必要ならここで状態変化に応じた処理を行う
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 1", &selectType1)) {
    flag_pushed = true;
    // 必要ならここで状態変化に応じた処理を行う
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 2", &selectType2)) {
    flag_pushed = true;
    // 必要ならここで状態変化に応じた処理を行う
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 3", &selectType3)) {
    flag_pushed = true;
    // 必要ならここで状態変化に応じた処理を行う
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 4", &selectType4)) {
    flag_pushed = true;
    // 同上
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Type 5", &selectType5)) {
    flag_pushed = true;
    // 同上
  }

  // 例：選択されている粒子タイプを vector にまとめる
  bool selectedTypes[6];
  selectedTypes[0] = selectType0;
  selectedTypes[1] = selectType1;
  selectedTypes[2] = selectType2;  
  selectedTypes[3] = selectType3;
  selectedTypes[4] = selectType4;
  selectedTypes[5] = selectType5;  
  
  // ユーザーが入力できるウィジェット
  ImGui::InputInt("Number of Particles", &m);

  // 入力値の検証（例：粒子タイプは 0～5 の間）
  if (m < 1) m = 1;

  // 指定したタイプの粒子を抽出
  static TrackingVector<ParticleData> filtered;

  if(flag_pushed){
    filtered = {};
  
    for (size_t i=0;i<P->particles.size();i++){
      const ParticleData p = P->particles[i];
      if (selectedTypes[p.type])
	filtered.push_back(p);
    }

    // 質量の大きい順にソート
    std::sort(filtered.begin(), filtered.end(), [](const ParticleData &a, const ParticleData &b) {
      return a.mass > b.mass;
    });
  }

  // 表示件数は m 件（件数が足りなければ全件表示）
  int count = std::min(m, static_cast<int>(filtered.size()));

  ImGui::Text("Type %d : Showing top %d particles sorted by mass", particleType, count);
  for (int i = 0; i < count; i++) {
    char label[256];
    std::snprintf(label, sizeof(label),
		  "ID %d: mass = %.3g, pos = (%.2g, %.2g, %.2g) vel = (%.2g, %.2g, %.2g), radius = %g rho=%g t=%g Hubble=%g",
		  filtered[i].ID, filtered[i].mass * (P->UnitMass_in_msolar/P->Hubble),
		  filtered[i].pos[0], filtered[i].pos[1], filtered[i].pos[2],
		  filtered[i].vel[0], filtered[i].vel[1], filtered[i].vel[2],
		  filtered[i].originalHsml,
		  filtered[i].density, filtered[i].temperature, P->Hubble);
    if (ImGui::Selectable(label)) {
      // 選択された粒子の位置をカメラの注視点に設定
      float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
      glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
      camCtx.cameraTarget = glm::vec3(filtered[i].pos[0], filtered[i].pos[1], filtered[i].pos[2]);
      camCtx.cameraPos = camCtx.cameraTarget - direction * distance;
    }
  }

  ImGui::Text("Plot 1d histogram");
  
  const char* quantities[] = { "x", "y", "z", "r", "Density", "Temperature", "val", "val2", "Hsml", "Mass" };
  // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
  static int selectedVar = 4;
  ImGui::Combo("Quantity", &selectedVar, quantities, IM_ARRAYSIZE(quantities));
  std::string var = quantities[selectedVar];
  
  // ビン数の入力
  static int bins = 50;
  ImGui::InputInt("Number of bins", &bins);

  static bool histogramLogScaleX = true;
  static bool histogramLogScaleY = true;  
  ImGui::Checkbox("Use Log scale X", &histogramLogScaleX);
  ImGui::Checkbox("Use Log scale Y", &histogramLogScaleY);

  // 自動レンジを使うかどうかのチェックボックス
  static bool autoRange = true;
  ImGui::Checkbox("Auto Range", &autoRange);
  
  // 手動レンジ入力用（autoRange==false の場合）
  static float range1_min = 0.0f, range1_max = 1.0f;
  static float range2_min = 0.0f, range2_max = 1.0f;
  if (!autoRange)
    {
      ImGui::InputFloat("X Axis Min", &range1_min, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("X Axis Max", &range1_max, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Y Axis Min", &range2_min, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Y Axis Max", &range2_max, 0.0f, 0.0f, "%g");
    }

  // GUI側の変数
  static bool useCameraCenter = false;
  static float cameraRadius = 10.0f; // 半径の初期値（ユーザーが入力可能）
  
  // 例：チェックボックスと半径入力ウィジェット
  ImGui::Checkbox("Filter: Use Camera Center", &useCameraCenter);
  if(useCameraCenter) 
    ImGui::InputFloat("Camera Radius", &cameraRadius, 0.1f, 1.0f, "%.2f");  
  
  static bool histogramComputed = false;
  static TrackingVector<float> histBins(bins);
  static TrackingVector<float> binCenters(bins);
  static float vmin, vmax, binSize;
  
  // ④ ヒストグラム作成（対象全粒子、ここでは質量を例とする）
  if (ImGui::Button("Compute 1D Histogram")) {
    // カメラ中心条件がオンなら、条件関数を合成する
    std::function<bool(const ParticleData&)> func = [](const ParticleData&) { return true; };

    if (useCameraCenter) {
      glm::vec3 camCenter = camCtx.cameraTarget; // ここではカメラの注視点を中心とする例
      auto isWithinRadius = [camCenter](const ParticleData &p) -> bool {
	glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
	return glm::length(pos - camCenter) <= cameraRadius;
      };
      
      auto prevFunc = func;
      func = [prevFunc, isWithinRadius](const ParticleData &p) -> bool {
	return prevFunc(p) && isWithinRadius(p);
      };
    }
    
    float massMin = std::numeric_limits<float>::max();
    float massMax = std::numeric_limits<float>::lowest();
    for (const auto &p : filtered) {
      float mass = p.getValue(var);
      mass *= (P->UnitMass_in_msolar/P->Hubble);
      if(mass == 0.)
	continue;

      if(!func(p))
	continue;
      
      if(histogramLogScaleX)
	mass = log10(mass);
      
      massMin = std::min(massMin, mass);
      massMax = std::max(massMax, mass);
    }
    
    if (massMin == massMax)
      massMax = massMin + 1.0f;

    if (autoRange){
      range1_min = massMin;
      range1_max = massMax;
    }
    
    // ヒストグラムの各ビンのカウント
    TrackingVector<int> binCounts(bins, 0);
    binSize = (range1_max - range1_min) / bins;
    for (const auto &p : filtered) {
      float mass = p.getValue(var);
      mass *= (P->UnitMass_in_msolar/P->Hubble);
      
      if(mass == 0.)
	continue;

      if(!func(p))
	continue;
      
      if(histogramLogScaleX)
	mass = log10(mass);
      
      int bin = static_cast<int>((mass - range1_min) / binSize);

      printf("mass=%g min=%g bin=%d\n", mass, range1_min, bin);
      
      if (bin < 0) bin = 0;
      if (bin >= bins) bin = bins - 1;
      binCounts[bin]++;
    }

    vmin = std::numeric_limits<float>::max();
    vmax = std::numeric_limits<float>::lowest();    

    // ImPlot用にfloat配列に変換
    for (int i = 0; i < bins; i++) {      
      histBins[i] = static_cast<float>(binCounts[i]);
      
      float value = histBins[i];
      if(histogramLogScaleY){
	if(value == 0.)
	  continue;
	
	value = log10(value);
      }
      
      vmin = std::min(vmin, value);
      vmax = std::max(vmax, value);	      
    }

    if(histogramLogScaleY){
      vmin = std::floor(vmin);
      vmax = std::ceil(vmax);

      vmin = 0.8*std::pow(10., vmin);
      vmax = std::pow(10., vmax);
    }else{
      vmin = 0.;

      int digits = static_cast<int>(log10(vmax));
      double scale = std::pow(10., digits);
      vmax = std::ceil(vmax / scale) * scale;
    }

    if (autoRange){
      range2_min = vmin;
      range2_max = vmax;
    }
    
    // X軸（ビン中心）の値
    for (int i = 0; i < bins; i++) 
      binCenters[i] = range1_min + (i + 0.5f) * binSize;    

    histogramComputed = true;
  }

  if(histogramComputed){
    // ヒストグラム描画（ImPlotを利用）
    if (ImPlot::BeginPlot("Mass Histogram", ImVec2(-1,300))) {
      // PlotHistogram expects an array of counts and optionally bin centers;
      // ここではカウントのみプロットします。

      if (histogramLogScaleY)
	ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
      else
	ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
      
      ImPlot::SetupAxisLimits(ImAxis_X1, range1_min, range1_max, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, range2_min, range2_max, ImGuiCond_Always);
            
      //ImPlot::PlotHistogram("Mass", histBins.data(), bins, bins, 1., ImPlotRange(range1_min, range1_max));
      ImPlot::PlotBars("Mass", binCenters.data(), histBins.data(), bins, binSize);
      ImPlot::EndPlot();
    }
  }

    
  ImGui::End();
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

  gRadialProfile->ShowRadialProfileUI(P->particles);     // Radial Profile の UI
  gHistogram2D->Show2DHistogramUI(P->particles);
  gClumpFind->ShowFindClumpsUI(P->particles, P->Header);

#ifdef CLUMP_DATA_READ
  gClumpFind->ReadAndShowClumpsUI(P, gFileInfo->currentFileIndex);
  gClumpFind->showClumpChainList(P, gProjectionMap2D);
#endif
  
  gProjectionMap2D->RenderProjectionUI(P, camCtx, gFileInfo->currentFileIndex);

  P->ShowHaloesUI();
  gFileInfo->DrawFormatDialog();
#ifdef HAVE_HDF5
  gFileInfo->ShowHDF5FieldMappingDialog();
#endif
  
  // 新たなウィンドウとしてトップ粒子リストを表示
  ShowTopParticlesUI();
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


void RenderColorBar() {
  // シーンとは別にオーバーレイ表示するため、深度テストを一時無効化
  glDisable(GL_DEPTH_TEST);

  glUseProgram(colorbarProgram);

  // テクスチャユニット 0 に現在のカラーマップテクスチャをバインド
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_1D, currentColorMapTex);
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
        float value = 0.0f + t * (typeColorMax[0] - typeColorMin[0]);

        // 色バーの下端の y 座標（ピクセル単位）を基準に、ラベルの y 座標を決定
        // たとえば、下端より 5 ピクセル下に表示する
        float label_y = offsetY + bottom_pixel + 5.0f;

        // x 座標は、色バーの左端と右端の間を線形補間して求める
        float label_x = offsetX + left_pixel + t * (right_pixel - left_pixel);

        // ラベルを描画（ImGui::GetForegroundDrawList() はスクリーン座標での描画を行うので、ピクセル座標をそのまま使用）
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", value);
        draw_list->AddText(ImVec2(label_x, label_y), IM_COL32(255,255,255,255), buf);
    }
}

void RenderScene() {
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (P->particlesDirty) {
    // パーティクルデータの更新
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);

#ifdef SAVE_GPU_MEMORY
    for(int i=0;i<6;i++){
      const auto& var = quantities_to_show[selectedColorMode[i]];
      for (auto& p : P->particles) {
	int itype = p.type;
	if(itype != i)
	  continue;
	
	p.val_show = p.getValue(var);
      }
    }

    /*for (auto& p : P->particles) {
      int itype = p.type;
      const auto& var = quantities_to_show[selectedColorMode[itype]];
      
      p.val_show = p.getValue(var);
      }*/
#endif
    
    if(flagCullingSphere){
      TrackingVector<ParticleData> filtered;
      filtered.reserve(P->particles.size());
      for (const auto& p : P->particles) {
        if (glm::distance(glm::vec3(p.pos[0], p.pos[1], p.pos[2]), camCtx.cameraTarget) <= radiusCullingSphere
	    && flagHideParticles[p.type] == false)
	  filtered.push_back(p);
      }
      
      g_filteredParticleCount = filtered.size();
      
      glBufferData(GL_ARRAY_BUFFER, filtered.size() * sizeof(ParticleData), filtered.data(), GL_DYNAMIC_DRAW);      
    }else{
      TrackingVector<ParticleData> filtered;
      filtered.reserve(P->particles.size());
      for (const auto& p : P->particles) {
        if (flagHideParticles[p.type] == false)
	  filtered.push_back(p);
      }

      g_filteredParticleCount = filtered.size();
      
      glBufferData(GL_ARRAY_BUFFER, filtered.size() * sizeof(ParticleData), filtered.data(), GL_DYNAMIC_DRAW);
      //glBufferData(GL_ARRAY_BUFFER, P->particles.size() * sizeof(ParticleData), P->particles.data(), GL_DYNAMIC_DRAW);
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    P->particlesDirty = false; // 転送後、フラグをクリア

    P->velocityDirty = true;
  }

  glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
#ifdef USE_LETTERBOX
  //glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)SCR_WIDTH / SCR_HEIGHT, 0.1f, 100.0f);
  const float targetAspect = 1280.0f / 720.0f;  // 固定アスペクト比
  glm::mat4 projection = glm::perspective(glm::radians(45.0f), targetAspect, 0.1f, 1000.0f);  
#else
  // 方法A: 現在のウィンドウサイズからアスペクト比を計算（ウィンドウ全体を使用）
  int width, height;
  glfwGetFramebufferSize(window, &width, &height);
  float aspect = static_cast<float>(width) / static_cast<float>(height);
  glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
#endif

#ifdef USE_CONVEX_HULL
  // 例：各クランプの凸包描画（チェックボックスがONの場合）
  gConvexHullRenderer->Render(view, projection, gClumpFind, P, gHistogram2D);  
#endif
  
  if (g_flagShowCuboid)
    RenderCuboid(view, projection);
  
  glm::mat4 model = glm::mat4(1.0f);

  // --- パーティクル描画 ---
  glUseProgram(particleProgram);
  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(particleProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
  glUniform1fv(glGetUniformLocation(particleProgram, "pointSizes"), 6, typePointSizes);
  glUniform1fv(glGetUniformLocation(particleProgram, "valueMin"), 6, typeColorMin);
  glUniform1fv(glGetUniformLocation(particleProgram, "valueMax"), 6, typeColorMax); 
  
  int intUseLog[6];
  for (int i = 0; i < 6; i++) 
    intUseLog[i] = useLogScale[i] ? 1 : 0;	
  glUniform1iv(glGetUniformLocation(particleProgram, "useLog"), 6, intUseLog);

#ifndef SAVE_GPU_MEMORY
  glUniform1iv(glGetUniformLocation(particleProgram, "colorMode"), 6, selectedColorMode);
#endif
  
  // パーティクルシェーダーを使う前に各カラーマップをバインド
  for (int i = 0; i < 6; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_1D, colormapForType[i]);
  }
  // uniform "colormaps" にテクスチャユニット番号をセットする
  int samplers[6] = {0, 1, 2, 3, 4, 5};
  glUniform1iv(glGetUniformLocation(particleProgram, "colormaps"), 6, samplers);

  int periodicMapping[6];
  for (int i = 0; i < 6; i++) {
    periodicMapping[i] = periodicColorBar[i] ? 1 : 0;
  }
  glUniform1iv(glGetUniformLocation(particleProgram, "periodicMapping"), 6, periodicMapping);
  
  glBindVertexArray(particleVAO);
  glDrawArrays(GL_POINTS, 0, g_filteredParticleCount);
  glBindVertexArray(0);

  // ※速度ベクトルの描画（インスタンシング方式）
  if (showVelocityVectors) {
    // 速度データが更新されている場合のみインスタンスバッファを更新
    if (P->velocityDirty) {
      UpdateVelocityInstanceBuffer(P->particles);
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
  
  RenderColorBar();
  RenderColorBarLabels();
}

void Cleanup() {
  // OpenGL バッファ解放
  glDeleteVertexArrays(1, &particleVAO);
  glDeleteBuffers(1, &particleVBO);
  glDeleteVertexArrays(1, &crossVAO);
  glDeleteBuffers(1, &crossVBO);

  // OpenGL シェーダープログラム解放
  glDeleteProgram(particleProgram);
  glDeleteProgram(lineProgram);

  saveConfig();
  
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
  gRadialProfile = new radialProfile(camCtx.cameraTarget);
  gHistogram2D = new histogram2D(camCtx.cameraTarget);
  gProjectionMap2D = new ProjectionMapGenerator();
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
  loadConfig();

  gFileInfo->setUnit(P);
  
  int newFileIndex = gFileInfo->initialIndex + gFileInfo->currentStep * gFileInfo->skipStep;
  gFileInfo->loadBatch(newFileIndex, gFileInfo->batchSize, gFileInfo->skipStep, P);      
  
  // InitColorMaps() 内、または InitParticles() の後などで
  colormapForType[0] = jetTex;
  colormapForType[1] = viridisTex;
  colormapForType[2] = plasmaTex;
  colormapForType[3] = jetTex;
  colormapForType[4] = viridisTex;
  colormapForType[5] = plasmaTex;

#ifdef USE_CONVEX_HULL
  gConvexHullRenderer = new ConvexHullRenderer();
  gConvexHullRenderer->Init(lineProgram);
#endif

#ifdef STREAM_LINE
  gStreamLine = new StreamlineComputer();
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
    
    // 5. 3D シーンの描画
    RenderScene();         // OpenGL 描画
        
    // 6. ImGui のレンダリング
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    glfwSwapBuffers(window);
  }
  
  Cleanup(); // メモリ解放 & ImGui 終了処理

#ifdef STREAM_LINE
  delete gStreamLine;
#endif
#ifdef USE_CONVEX_HULL
  delete gConvexHullRenderer;
#endif
#ifdef GEOMETRICAL_ANALYSIS
  delete gDiskFinder;
  delete gEllipsoid;
#endif
#ifdef ISO_CONTOUR
  delete gIsoContour;
#endif
  delete P;
  delete gProjectionMap2D;
  delete gClumpFind;
  delete gHistogram2D;
  delete gRadialProfile;
  delete gFileInfo;

  return 0;
}
 
