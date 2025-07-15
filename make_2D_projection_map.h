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

// GLM 関連
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

/***** needed for GLuint texID *****/
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#ifdef MACOS
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

// もし外部で定義されるカラーマップがあれば、宣言しておく（例: jetMap, viridisMap, plasmaMap）
extern const float jetMap[];
extern const float viridisMap[];
extern const float plasmaMap[];

class ProjectionMapGenerator {
public:
  int npixel = 200;
  float xlen[3] = {2.,2.,1.};
  float xoffset[3] = {0.,0.,0.};
  float tilt[3] = {0.,0.,0.};

  HeaderInfo Header;  
  
  bool flagDensityWeight = true;
  bool flagVoronoi = true;
  int step_z = 200;
  bool flagLogScale = true;
  bool autoRange = true;
  float range_min = 0.0f;
  float range_max = 1.0f;
  bool flagShowStarParticles = true;  
  bool flagShowCuboid = false;
  bool flagFontLoaded = false;
  bool showWindowProjection = false;
  bool showWindowSelectFont = false;

  bool flagSpecifyZoomRegionByMass = false;
  bool flagScaleOriginalCoordinateZoomRegion = true;
  float criticalGasMassForZoomRegion;
  float lenZoomRegion;
  
  bool flagPlaceScale = false;
  bool flagScaleOriginalCoordinate = true;
  float arrowLenX=100.;
  char arrowLabelStr[255]="100 au";

  bool flagTimeLabel = true;
  char timeFormatBuf[255] = "t=%.3f";
  
  double originalMax;
  double desiredMax;

  // ファイル名のフォーマットとフォルダパス
  char fileFormat[255] = "image_%04d.png";  // ファイル名のフォーマット（例: image_0001.png）
  char folderPath[255] = "./output";        // 出力フォルダ
  
  std::string var;
  
  int selectedAxis = 2;
  glm::quat cuboidTransform;
  glm::vec3 center;
  glm::vec3 planeNormal;

  int colormapindex = 0;  
  int countColorMap = 9;
  const float *colorMap;

  TrackingVector<unsigned char> outImage;
  int outW, outH;     
  bool flag2DprojectionComputed = false;

#ifdef USE_LUA
  lua_State* gLua = nullptr;
  bool flag_init_lua = false;
#endif
  
  // ---------------------------
  // ImGui 入力用のバッファ
  char filterExpr[256]      = "return m > 10.0";   // 例: 質量が 10 より大きい粒子のみ描画
  char pointSizeExpr[256]   = "return m / 10.0";     // 例: 点サイズは質量の 1/10
  char pointColorExpr[256]  = "return { r = m/100.0, g = 0.5, b = 0.2, a = 1.0 }"; // 例: 点の色（テーブルを返す）
  char minValueExpr[32]     = "return 0.0";
  char maxValueExpr[32]     = "return 1.0";

  // 2D projection map の定義
  struct ProjectionMap {    
    int npixel, npixel_x, npixel_y, npixel_z;
    float xlen[3], xmin[3];
    float dx, dy, dz;
    float cell_size;
    float minVal, maxVal;
    bool flagDensityWeight;
    bool flagLogScale;
    glm::vec3 center;
    glm::vec3 uAxis, vAxis, wAxis;
    TrackingVector<double> values; // row-major order, size = width * height
    TrackingVector<double> weights; // row-major order, size = width * height
    TrackingVector<unsigned char> image;
  };

  stbtt_fontinfo fontCharacter;
  std::vector<unsigned char> ttf_buffer;
  std::vector<std::string> availableFonts = {};
  std::vector<ImFont*> loadedFonts = {};
  
  GLuint texID; //to be detached

  ProjectionMapGenerator();

  glm::vec3 calc_angular_momentum_axis(const TrackingVector<ParticleData>& originalParticles, glm::vec3 &center, float *xlen);
  
  void RenderProjectionUI(ParticleArray *P, CameraContext& camCtx, int fileindex);
  void make_density_map(ParticleArray *P, char *filename);
  
  // 直方体の8頂点（ローカル座標）を計算し、g_cuboidTransformを適用してワールド座標に変換する関数
  TrackingVector<glm::vec3> computeCuboidVertices(float *xmin, float *xmax, glm::vec3 center, glm::quat cuboidTransform);  
  glm::quat UpdateTransformFromEuler(float *eulerAngles);
  
  float kernel(float u);  
  void createProjectionMap(ProjectionMap &map, const TrackingVector<ParticleData>& particles);
  void createVoronoiSliceMap(ProjectionMap& map, const TrackingVector<ParticleData>& particles);

#ifdef USE_LUA
  bool EvaluateLuaExpressionNumber(const char* expr, double& outValue);
  bool EvaluateLuaExpressionColor(const char* expr, float& r, float& g, float& b, float& a);
  bool EvaluateLuaExpressionBool(const char* expr, bool& outValue);  
#endif
  
  void overlayStarParticles(ProjectionMap& map, const TrackingVector<ParticleData>& particles);
  void showWindow();
  
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
  
  GLuint CreateTexture2D(const unsigned char* data, int width, int height);

  void set_projection_parameters(const TrackingVector<ParticleData>& originalParticles, const int useAngularMomentumAxis, 
				 const float* pos_center, const float len, const float val_min, const float val_max,
				 const int npixel_input, const int nslices, std::string var);
};


#endif
