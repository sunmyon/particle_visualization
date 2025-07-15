#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>  // glm::mat4用

// 前方宣言で依存を隠蔽
class FindClump;
class ParticleArray;
class histogram2D;

class ConvexHullRenderer {
public:
    // コンストラクタ・デストラクタ
    ConvexHullRenderer();
    ~ConvexHullRenderer();
    
    // 初期化時にlineProgramを受け取る
    void Init(GLuint lineProgram);

    // 毎フレーム呼び出す描画関数
    void Render(const glm::mat4& view, const glm::mat4& projection,
                FindClump* clump, ParticleArray* P, histogram2D* hist);
    
 private:
    // コピー禁止（安全性のため）
    ConvexHullRenderer(const ConvexHullRenderer&) = delete;
    ConvexHullRenderer& operator=(const ConvexHullRenderer&) = delete;

    struct Impl;
    Impl* impl;  // PImplイディオムで実装を隠蔽
};
