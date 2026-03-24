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

#include "app_services.h"
AppServices gAppServices;

#include "ui_state.h"
SettingsRuntimeState gSettingsRuntimeState;
RenderRuntimeState gRenderRuntimeState;

// tinyfiledialogs（フォルダ選択用）

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
ParticleArray *P;

bool g_flagShowCuboid = false;
TrackingVector<glm::vec3> g_cubicPoints;

#include "particle_visual_config.h"
ParticleVisualConfig gParticleVisualConfig;
GLuint colormapTextures[50] = {0};

size_t g_filteredParticleCount = 0;

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
unsigned meshIsocontourVAO=0, meshIsocontourVBO=0, meshIsocontourEBO=0, meshIsocontourNBO=0;
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

  if (camCtx.distance < gSettingsRuntimeState.minZoom) camCtx.distance = gSettingsRuntimeState.minZoom;
  if (camCtx.distance > gSettingsRuntimeState.maxZoom) camCtx.distance = gSettingsRuntimeState.maxZoom;
    
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
    if(i % gSettingsRuntimeState.velocitySubtraction != 0)
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

  uploadMeshWithNormals(gAppServices.isoContour.verts, gAppServices.isoContour.inds,
                        meshIsocontourVAO, meshIsocontourVBO,
                        meshIsocontourNBO, meshIsocontourEBO); /*temporally here*/
  
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

void RenderCross(const glm::mat4 &view, const glm::mat4 &projection){
  // --- 交差点マーカー描画 ---
  glDisable(GL_DEPTH_TEST); // Zバッファ無効化（手前に表示する）
  glLineWidth(3.0f);        // 線を太くする
    
  glm::vec3 forward = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
  glm::vec3 rightVec = glm::normalize(glm::cross(forward, camCtx.cameraUp));
  glm::vec3 upVec = glm::normalize(glm::cross(rightVec, camCtx.cameraUp));
  glm::vec3 upVec_new = glm::normalize(glm::cross(rightVec, forward));
  glm::vec3 v1 = camCtx.cameraTarget - (rightVec + upVec) * gSettingsRuntimeState.crossSize;
  glm::vec3 v2 = camCtx.cameraTarget + (rightVec + upVec) * gSettingsRuntimeState.crossSize;
  glm::vec3 v3 = camCtx.cameraTarget - (rightVec - upVec) * gSettingsRuntimeState.crossSize;
  glm::vec3 v4 = camCtx.cameraTarget + (rightVec - upVec) * gSettingsRuntimeState.crossSize;
  glm::vec3 v5 = camCtx.cameraTarget - upVec_new * gSettingsRuntimeState.crossSize;
  glm::vec3 v6 = camCtx.cameraTarget + upVec_new * gSettingsRuntimeState.crossSize;

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
  if(gSettingsRuntimeState.flagHideAllParticles == false)
    glDrawArrays(GL_POINTS, 0, g_filteredParticleCount);
  glBindVertexArray(0);

  // ※速度ベクトルの描画（インスタンシング方式）
  if (gSettingsRuntimeState.showVelocityVectors) {
    // 速度データが更新されている場合のみインスタンスバッファを更新
    if (P->velocityDirty) {
      UpdateVelocityInstanceBuffer(P->particleBlock.particles);
      P->velocityDirty = false;
    }
    RenderVelocityVectors(view, projection, gSettingsRuntimeState.arrowScale, gSettingsRuntimeState.useVelocityArrowLogScale);
  }

  if(gSettingsRuntimeState.flagShowSinkIDs){
    UpdateLabelIndicesIfNeeded(); //test
    DrawParticleLabels(view, projection); //test
  }

  RenderCross(view, projection);  // 粒子描画後に呼び出す

#ifdef ISO_CONTOUR
  if (gRenderRuntimeState.showIsocontour && meshIsocontourVAO) {
    glUseProgram(isocontourProgram);       // if you have a separate shader
    glUniformMatrix4fv(glGetUniformLocation(isocontourProgram, "model"),      1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(isocontourProgram, "view"),       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(isocontourProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(isocontourProgram, "opacity"), gRenderRuntimeState.isoOpacity);
    glBindVertexArray(meshIsocontourVAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)gAppServices.isoContour.inds.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
  }
#endif

  if (gEllipsoidManager.show() && ellipsoidVAO) {
    glUseProgram(ellipsoidProgram);
    glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram, "view"),
		       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram, "projection"),
		       1, GL_FALSE, glm::value_ptr(projection));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glBindVertexArray(ellipsoidVAO);

    for (const auto& e : gEllipsoidManager.getEllipsoids()) {
      if (e.renderMode != EllipsoidRenderMode::Solid)
	continue;

      glm::mat4 M = e.modelMatrix();

      glUniformMatrix4fv(glGetUniformLocation(ellipsoidProgram, "model"),
			 1, GL_FALSE, glm::value_ptr(M));
      glUniform3fv(glGetUniformLocation(ellipsoidProgram, "color"),
		   1, glm::value_ptr(e.color));
      glUniform1f(glGetUniformLocation(ellipsoidProgram, "opacity"),
		  e.opacity);

      glDrawElements(GL_TRIANGLES,
		     static_cast<GLsizei>(sphereI.size()),
		     GL_UNSIGNED_INT,
		     0);
    }

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

#ifdef GEOMETRICAL_ANALYSIS
  if (gRenderRuntimeState.showDisks && diskVAO) {
    glUseProgram(diskProgram);
    glm::mat4 M = gAppServices.diskFinder->getModelMatrix();
    
    glUniformMatrix4fv(glGetUniformLocation(diskProgram,"model"),1,GL_FALSE,glm::value_ptr(M));
    glUniformMatrix4fv(glGetUniformLocation(diskProgram,"view"), 1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(diskProgram,"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(diskProgram,"color"), 1.0f,1.0f,1.0f);
    glUniform1f(glGetUniformLocation(diskProgram,"opacity"), gRenderRuntimeState.diskOpacity);
    
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
  if (gRenderRuntimeState.showStreamLine && streamlineVAO) {
    static std::vector<size_t> m_firsts, m_counts;    
    static std::vector<GLint> firstsGL;
    static std::vector<GLsizei> countsGL;
    
    if(gRenderRuntimeState.flagStreamDirty){
      const StreamlineMeshData stream_mesh = gAppServices.streamLine->meshData();
      
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

      gRenderRuntimeState.flagStreamDirty = false;
    }
    
    glUseProgram(streamlineProgram);

    glUniformMatrix4fv(glGetUniformLocation(streamlineProgram,"model"),1,GL_FALSE,glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(streamlineProgram,"view"), 1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(streamlineProgram,"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(streamlineProgram,"color"), 1.0f,1.0f,1.0f);
    glUniform1f(glGetUniformLocation(streamlineProgram,"opacity"), gRenderRuntimeState.streamlineOpacity);

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
    
    if (gRenderRuntimeState.flagCubesDirty) {
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
      
      gRenderRuntimeState.flagCubesDirty = false;
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
  if(gRenderRuntimeState.showVolumeRendering && gRenderRuntimeState.flagRT == 1){    
    if(gRenderRuntimeState.flagUpdateRendering){
      G_bvh = uploadBVH_TBO(gBVHresult);
      gRenderRuntimeState.flagUpdateRendering = false;
    }
    
    int lowW = std::max(1.0f, g_viewportWidth  / gRenderRuntimeState.rtDownscale);
    int lowH = std::max(1.0f, g_viewportHeight / gRenderRuntimeState.rtDownscale);
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
    glUniform1i (glGetUniformLocation(rtProgram,"uLodMode"), gRenderRuntimeState.lodMode); // 0/1/2
    glUniform1f (glGetUniformLocation(rtProgram,"uPxThreshold"), gRenderRuntimeState.pxThreshold);
    glUniform1f (glGetUniformLocation(rtProgram,"uTauMax"), gRenderRuntimeState.tauMax);
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

  if(gRenderRuntimeState.showVolumeRendering && gRenderRuntimeState.flagRT == 2){    
    if(gRenderRuntimeState.flagUpdateRendering){
      uploadOctTree_TBO(gAppServices.volume.octTree, gGPUOctTree, gAppServices.volume.rho2sigma);
      gRenderRuntimeState.flagUpdateRendering = false;
    }
    
    int lowW = std::max(1.0f, g_viewportWidth  / gRenderRuntimeState.rtDownscale);
    int lowH = std::max(1.0f, g_viewportHeight / gRenderRuntimeState.rtDownscale);
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
    bindTBO(octrayProgram, "nodeMinTB", gGPUOctTree.nodeMin.tex,    0);
    bindTBO(octrayProgram, "nodeMaxTB", gGPUOctTree.nodeMax.tex,    1);
    bindTBO(octrayProgram, "childATB",  gGPUOctTree.texChildA.tex,  2);
    bindTBO(octrayProgram, "childBTB",  gGPUOctTree.texChildB.tex,  3);
    bindTBO(octrayProgram, "cornerLoTB", gGPUOctTree.cornerLo.tex,  4);
    bindTBO(octrayProgram, "cornerHiTB", gGPUOctTree.cornerHi.tex,  5);

    // カメラ・LOD ユニフォーム
    glUniform1i (glGetUniformLocation(octrayProgram,"uRoot"), gGPUOctTree.root);
    glUniform1f (glGetUniformLocation(octrayProgram,"uPxThreshold"), gRenderRuntimeState.pxThreshold);
    glUniform1f (glGetUniformLocation(octrayProgram,"uTauMax"), gRenderRuntimeState.tauMax);
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


  if(gRenderRuntimeState.showVolumeRendering && (gRenderRuntimeState.flagRT == 0)){    
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
    glUniform1i (glGetUniformLocation(wboitParticleProgram, "uKernelMode"), gRenderRuntimeState.kernelMode);
    glUniform1f (glGetUniformLocation(wboitParticleProgram, "uGaussNSigma"), gRenderRuntimeState.gaussNSigma);
    glUniform1f (glGetUniformLocation(wboitParticleProgram, "uEnlargeHsml"), gRenderRuntimeState.enlargeKernel);
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
  if (gAppServices.clumpFind->checkClumpComputation()){
    glUseProgram(lineProgram);
    glUniformMatrix4fv(glGetUniformLocation(lineProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(lineProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    const int nclumps = gAppServices.clumpFind->get_nclumps();

    // 2) clump側が「全クリア」と言ったらGPUキャッシュも削除
    if (gAppServices.clumpFind->checkClearCache()) {
      for (auto& [id, e] : s_hull) {
	if (e.vbo) glDeleteBuffers(1, &e.vbo);
	if (e.vao) glDeleteVertexArrays(1, &e.vao);
      }
      s_hull.clear();
      gAppServices.clumpFind->finishClearCache();
      // CPU側のPolyhedronや頂点配列は**持っていない**ので何もする必要なし
    }

    // 3) 各クランプの描画
    for (int i = 0; i < nclumps; ++i) {
      if (!gAppServices.clumpFind->flagShowHull(i)) continue;

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
	TrackingVector<ParticleData> pts = gAppServices.clumpFind->get_particle_indices(i, P->particleBlock.particles);       
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

  ColorBarLabelLayout layout;
  ComputeColorBarPixelCoords(layout.left_pixel,
			     layout.right_pixel,
			     layout.top_pixel,
			     layout.bottom_pixel);
  
#ifdef USE_LETTERBOX
  layout.offsetX = static_cast<float>(g_viewportX);
  layout.offsetY = static_cast<float>(g_viewportY);
#else
  layout.offsetX = 0.0f;
  layout.offsetY = 0.0f;
#endif

  DrawColorBarLabelsUI(layout,
		       gParticleVisualConfig.types[0].colorMin,
		       gParticleVisualConfig.types[0].colorMax);
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
  InitGLFW();        // GLFW & OpenGL の初期化
  InitImGui();       // ImGui の初期化
  InitShaders();     // シェーダーの初期化
  InitColorMaps();
  InitColorBar();

  InitAppServices(gAppServices, camCtx);
  
  gFileInfo = new FileInfo(camCtx);
  P = new ParticleArray(camCtx);
  
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
        
    // 6. ImGui のレンダリング
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    glfwSwapBuffers(window);
  }
  
  Cleanup(); // メモリ解放 & ImGui 終了処理

#ifdef USE_CONVEX_HULL
  delete gConvexHullGenerator;
#endif
  delete P;
  delete gFileInfo;

  return 0;
} 
