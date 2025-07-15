#pragma once
#include <glm/glm.hpp>
struct ParticleDataForKdTree;  // 含めるか前方宣言

/// 任意の密度推定クラスはこのインターフェイスを実装する
struct IDensityEstimator {
  virtual ~IDensityEstimator() = default;
  /// pos: サンプリング位置
  /// return: その位置でのスカラー値（密度など）
  virtual float sample(const glm::vec3& pos) const = 0;
};
