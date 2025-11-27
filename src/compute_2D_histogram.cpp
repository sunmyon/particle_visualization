
#include "main.h"
#include "compute_2D_histogram.h"

#include <imgui.h>
#include "implot.h"

/// ── 2Dヒストグラム計算関数 ─────────────────────────────────────
///
/// particles   : 粒子データのベクター
/// var1, var2  : ヒストグラムの各軸として用いる変数（"x", "y", "z", "r", "val", "val2" など）
/// bins1, bins2: 各軸のビン数
/// autoRange   : 範囲を自動計算するか、false にすると下記の range_* パラメータを利用する
/// range1_min, range1_max, range2_min, range2_max : 自動計算を使わない場合の各軸の値の範囲
///
/// 戻り値はタプルで、
///   - 第一要素：軸1（var1）の各ビンの中心座標（サイズ bins1 の vector<float>）
///   - 第二要素：軸2（var2）の各ビンの中心座標（サイズ bins2 の vector<float>）
///   - 第三要素：各ビンの値（質量の和が格納された bins1×bins2 の 2D vector）
std::tuple<TrackingVector<float>, TrackingVector<float>, TrackingVector<TrackingVector<float>>>
histogram2D::compute2DHistogram(const TrackingVector<ParticleData>& particles,
				const std::string &var1,
				const std::string &var2,
				int bins1, int bins2,
				bool autoRange,
				float &range1_min, float &range1_max,
				float &range2_min, float &range2_max,
				std::function<bool(const ParticleData&)> condition)
{
  float min1=0., max1=0., min2=0., max2=0.;
  if (autoRange) {
    bool first = true;
    for (const ParticleData &p : particles) {
      if(p.type != 0)
	continue;

      if (!condition(p))
	continue;

      float v1, v2;
      if(var1 == "r"){
	glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
	v1 = glm::length(pos - this->camCenter) ;
      }else{
	v1 = p.getValue(var1);
      }

      if(var2 == "r"){
	glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
	v2 = glm::length(pos - this->camCenter) ;
      }else{	
	v2 = p.getValue(var2);
      }

      if(histogram2DLogScaleX) v1 = log10(v1);
      if(histogram2DLogScaleY) v2 = log10(v2);
      
      if (first) {
	min1 = max1 = v1;
	min2 = max2 = v2;
	first = false;
      } else {
	if (v1 < min1) min1 = v1;
	if (v1 > max1) max1 = v1;
	if (v2 < min2) min2 = v2;
	if (v2 > max2) max2 = v2;
      }
    }

    if (particles.empty()) {
      min1 = range1_min; max1 = range1_max;
      min2 = range2_min; max2 = range2_max;
    }
  } else {
    min1 = range1_min; max1 = range1_max;
    min2 = range2_min; max2 = range2_max;
  }
    
  TrackingVector<TrackingVector<float>> histogram(bins1, TrackingVector<float>(bins2, 0.0f));
    
  float binSize1 = (max1 - min1) / bins1;
  float binSize2 = (max2 - min2) / bins2;
  
  for (const ParticleData &p : particles) {
    if(p.type != 0)
	continue;

    if (!condition(p))
      continue;
    
    float v1 = p.getValue(var1);
    float v2 = p.getValue(var2);

    if(histogram2DLogScaleX) v1 = log10(v1);
    if(histogram2DLogScaleY) v2 = log10(v2);
    
    if (v1 < min1 || v1 >= max1 || v2 < min2 || v2 >= max2)
      continue;
   
    int binIndex1 = std::min(bins1 - 1, static_cast<int>((v1 - min1) / binSize1));
    int binIndex2 = std::min(bins2 - 1, static_cast<int>((v2 - min2) / binSize2));
    histogram[binIndex1][binIndex2] += p.mass;
  }
  
  TrackingVector<float> centers1(bins1, 0.0f);
  for (int i = 0; i < bins1; i++) {
    centers1[i] = min1 + (i + 0.5f) * binSize1;
    if(histogram2DLogScaleX)
      centers1[i] = pow(10., min1 + (i + 0.5f) * binSize1);
  }
  
  TrackingVector<float> centers2(bins2, 0.0f);
  for (int j = 0; j < bins2; j++) {
    centers2[j] = min2 + (j + 0.5f) * binSize2;
    if(histogram2DLogScaleY)
      centers2[j] = pow(10., min2 + (j + 0.5f) * binSize2);
  }

  if(histogram2DLogScaleCB){
    float min_val = 1.e30;
    for (int i = 0; i < bins1; i++){
      for (int j = 0; j < bins2; j++){
	if(histogram[i][j] > 0.)
	  min_val = std::min(min_val, histogram[i][j]);
      }
    }
    
    for (int i = 0; i < bins1; i++){
      for (int j = 0; j < bins2; j++){
	printf("[%d,%d] val=%g min_val=%g\n", i, j, histogram[i][j], min_val);
	
	if(histogram[i][j] == 0.)
	  histogram[i][j] = 0.1 * min_val;
	histogram[i][j] = log10(histogram[i][j]);
      }
    }
    
  }
  
  range1_min = min1;
  range1_max = max1;
  range2_min = min2;
  range2_max = max2;
  
  return std::make_tuple(centers1, centers2, histogram);
}

// 2Dヒストグラム用 GUI 表示関数
// compute2DHistogram() 関数および originalParticles（粒子データ配列）は既に実装済みである前提です。
void histogram2D::Show2DHistogramUI(TrackingVector<ParticleData>& particles)
{
  if (!showWindow2Dhistogram) return;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("histogram 2D", &showWindow2Dhistogram, ImGuiWindowFlags_None);
  
  const char* quantities[] = { "x", "y", "z", "r", "Density", "Temperature", "val", "val2", "Hsml" };
  // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
  static int selectedVar1 = 4;
  static int selectedVar2 = 5;
  ImGui::Combo("X Axis Quantity", &selectedVar1, quantities, IM_ARRAYSIZE(quantities));
  ImGui::Combo("Y Axis Quantity", &selectedVar2, quantities, IM_ARRAYSIZE(quantities));

  std::string var1 = quantities[selectedVar1];
  std::string var2 = quantities[selectedVar2];
  
  // ビン数の入力
  static int bins1 = 50;
  static int bins2 = 50;
  ImGui::InputInt("Bins X", &bins1);
  ImGui::InputInt("Bins Y", &bins2);

  ImGui::Checkbox("Use Log scale X", &histogram2DLogScaleX);
  ImGui::Checkbox("Use Log scale Y", &histogram2DLogScaleY);
  ImGui::Checkbox("Use Log color scale", &histogram2DLogScaleCB);

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

#ifdef USE_CONVEX_HULL
  static bool useConvexHull = false;
  ImGui::Checkbox("Filter: Use Convex Hull", &useConvexHull);
#endif

  static bool useCameraCenter = false;
  static float cameraRadius = 10.0f; // 半径の初期値（ユーザーが入力可能）
  ImGui::Checkbox("Filter: Use Camera Center", &useCameraCenter);
  if(useCameraCenter) {
    ImGui::InputFloat("Camera Radius", &cameraRadius, 0.1f, 1.0f, "%.2f");
  }

  // 「Compute Histogram」ボタン押下でヒストグラムを計算
  static bool histogramComputed = false;
  static TrackingVector<float> centers1, centers2;
  static TrackingVector<TrackingVector<float>> hist2D;
  if (ImGui::Button("Compute Histogram"))
    {
      std::function<bool(const ParticleData&)> func = [](const ParticleData&) { return true; };

#ifdef USE_CONVEX_HULL
      if (useConvexHull) {
	TrackingVector<std::function<bool(const ParticleData&)>> convexConditions;
	/*for (const auto &conv : convexHullCache) {
	  auto insideTestPtr = std::make_shared<CGAL::Side_of_triangle_mesh<Polyhedron_3, Kernel>>(conv);
	  auto isInsideConvexHull = [insideTestPtr](const ParticleData &p) -> bool {
	    Point_3 pt(p.pos[0], p.pos[1], p.pos[2]);
	    return ((*insideTestPtr)(pt) != CGAL::ON_UNBOUNDED_SIDE);
	  };
	  
	  convexConditions.push_back(std::move(isInsideConvexHull));	  
	  }*/

	for (const auto &testerPtr : convexHullCache) {
	  // testerPtrはstd::unique_ptr<IConvexHull>なのでget()で生ポインタを取り出す
	  auto isInsideConvexHull = [tester = testerPtr.get()](const ParticleData &p) -> bool {
	    // ParticleDataから点を作成。ここではstd::array<double, 3>を利用
	    std::array<double, 3> pt = { p.pos[0], p.pos[1], p.pos[2] };
	    return tester->isInside(pt);
	  };
	  convexConditions.push_back(isInsideConvexHull);
	}
	

	// 全ての convexConditions を OR で合成する
	std::function<bool(const ParticleData&)> convexFunc = [](const ParticleData&) { return false; };
	for (auto &cond : convexConditions) {
	  auto prev = convexFunc;
	  convexFunc = [prev, cond](const ParticleData &p) -> bool {
	    return prev(p) || cond(p);
	  };
	}

	func = convexFunc;
      }
#endif
      
      // カメラ中心条件がオンなら、条件関数を合成する
      if (useCameraCenter) {
	auto isWithinRadius = [this](const ParticleData &p) -> bool {
	  glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
	  return glm::length(pos - this->camCenter) <= cameraRadius;
	};
	
	auto prevFunc = func;
	func = [prevFunc, isWithinRadius](const ParticleData &p) -> bool {
	  return prevFunc(p) && isWithinRadius(p);
	};
      }
      
      if (autoRange)
	{
	  std::tie(centers1, centers2, hist2D) =
	    compute2DHistogram(particles, std::string(var1), std::string(var2),
			       bins1, bins2, true, range1_min, range1_max, range2_min, range2_max, func);
	}
      else
	{
	  std::tie(centers1, centers2, hist2D) =
	    compute2DHistogram(particles, std::string(var1), std::string(var2),
			       bins1, bins2, false, range1_min, range1_max, range2_min, range2_max, func);
	}
      histogramComputed = true;
    }

  // ヒストグラムの計算結果がある場合、ImPlot を使って表示
  if (histogramComputed)
    {
      // ImPlot の PlotHeatmap() は1次元配列を入力するため、2D配列 hist2D を 1次元に変換
      // 例えば、計算済みヒストグラムのサイズを使って heatmapData を構築する
      size_t computedBins1 = hist2D.size(); // 以前に計算したヒストグラムの x 軸ビン数
      size_t computedBins2 = (computedBins1 > 0) ? hist2D[0].size() : 0;

      TrackingVector<float> heatmapData;
      heatmapData.reserve(computedBins1 * computedBins2);
      for (size_t j = 0; j < computedBins2; j++)
	for (size_t i = 0; i < computedBins1; i++)
	  heatmapData.push_back(hist2D[i][j]);

      // ヒートマップ表示用ウィンドウ
      if (ImPlot::BeginPlot("2D Histogram", ImVec2(-1, 300)))
	{
	  // 軸ラベルやスケールの設定は必要に応じて
	  ImPlot::SetupAxes(var1.c_str(), var2.c_str());

	  // 手動レンジの場合、X 軸と Y 軸のリミットを設定
	  ImPlot::SetupAxisLimits(ImAxis_X1, range1_min, range1_max, ImGuiCond_Always);
	  ImPlot::SetupAxisLimits(ImAxis_Y1, range2_min, range2_max, ImGuiCond_Always);	  

	  ImPlot::PushColormap(ImPlotColormap_Viridis);

	  // ヒートマップのプロット
	  ImPlot::PlotHeatmap("Histogram", heatmapData.data(), computedBins2, computedBins1, 0, 0, ""
	  		      , ImPlotPoint(range1_min, range2_min), ImPlotPoint(range1_max, range2_max));
	  
	  ImPlot::EndPlot();
	}
    }    

  ImGui::End();
}


