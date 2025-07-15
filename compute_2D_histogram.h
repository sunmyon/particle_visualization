#pragma once

#include <functional>
#ifdef USE_CONVEX_HULL  
#include <memory>
#endif

class histogram2D{
public:
  const glm::vec3 camCenter; // ここではカメラの注視点を中心とする例
  
  bool showWindow2Dhistogram = false;
  bool histogram2DLogScaleX = true;
  bool histogram2DLogScaleY = true;
  bool histogram2DLogScaleCB = true;

  histogram2D(const glm::vec3 &center = glm::vec3(0.0f, 0.0f, 0.0f),
	      bool logScaleX = true, bool logScaleY = true, bool logScaleCB = true)
    : camCenter(center),
      showWindow2Dhistogram(false),
      histogram2DLogScaleX(logScaleX),
      histogram2DLogScaleY(logScaleY),
      histogram2DLogScaleCB(logScaleCB)
  {
  }

#ifdef USE_CONVEX_HULL  
  TrackingVector<std::shared_ptr<IConvexHull>> convexHullCache;
  void setConvexHulls(const TrackingVector<std::shared_ptr<IConvexHull>>& convexHulls) {
    convexHullCache = convexHulls;
  }
#endif
  
  std::tuple<TrackingVector<float>, TrackingVector<float>, TrackingVector<TrackingVector<float>>>
  compute2DHistogram(const TrackingVector<ParticleData>& particles,
		     const std::string &var1,
		     const std::string &var2,
		     int bins1, int bins2,
		     bool autoRange,
		     float &range1_min, float &range1_max,
		     float &range2_min, float &range2_max,
		     std::function<bool(const ParticleData&)> condition);

  void showWindow(void){
    showWindow2Dhistogram = true;
  }  
  
  void Show2DHistogramUI(TrackingVector<ParticleData>& originalParticles);
};
