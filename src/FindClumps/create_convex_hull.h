#pragma once
#include <glm/glm.hpp>  // glm::mat4用

// 前方宣言で依存を隠蔽
class FindClump;
class ParticleArray;
class histogram2D;

class ConvexHullGenerator {
public:
    // コンストラクタ・デストラクタ
    ConvexHullGenerator();
    ~ConvexHullGenerator();

  TrackingVector<float>  
  buildLineVertices(const TrackingVector<ParticleData>& pts);

private:
  // コピー禁止（安全性のため）
  ConvexHullGenerator(const ConvexHullGenerator&) = delete;
  ConvexHullGenerator& operator=(const ConvexHullGenerator&) = delete;
  
  struct Impl;
  Impl* impl;  // PImplイディオムで実装を隠蔽
};

