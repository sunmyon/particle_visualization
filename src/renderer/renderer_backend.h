#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <glm/mat4x4.hpp>

class IRenderBackend {
public:
  using Mesh    = uint32_t;  // 頂点バッファ1本（ライン or ポイント）
  using Texture = uint32_t;  // 2D テクスチャ

  virtual ~IRenderBackend() = default;

  // ライフサイクル（GLならコンテキスト作成後に init()）
  virtual bool init() = 0;
  virtual void shutdown() = 0;

  // フレーム境界（必要に応じて viewport などを更新）
  virtual void beginFrame(int fbWidth, int fbHeight) = 0;
  virtual void endFrame() = 0;

  // --- ラインメッシュ ---
  virtual Mesh  createLineMesh() = 0;
  virtual void  destroyMesh(Mesh h) = 0; // Line/Point 兼用で破棄
  virtual void  updateLineMesh(Mesh h, const float* xyz, std::size_t countFloats) = 0; // float3 配列
  virtual void  drawLines(Mesh h, const glm::mat4& view, const glm::mat4& proj) = 0;

  // --- ポイントメッシュ（将来用だが用意） ---
  virtual Mesh  createPointMesh() = 0;
  virtual void  updatePointMesh(Mesh h, const float* xyz, std::size_t countFloats) = 0;
  virtual void  drawPoints(Mesh h, const glm::mat4& view, const glm::mat4& proj) = 0;

  // --- 2Dテクスチャ（RGB8想定、UI/投影図用） ---
  virtual Texture createTexture2D(int w, int h) = 0;
  virtual void    updateTexture2D(Texture t, int w, int h, const unsigned char* rgb) = 0; // RGB8
  virtual void    drawTexture2D(Texture t, int w, int h, int dstX, int dstY) = 0;         // 簡易：全画面等
  virtual void    destroyTexture(Texture t) = 0;
};
