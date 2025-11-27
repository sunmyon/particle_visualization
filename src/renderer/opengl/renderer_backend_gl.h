#pragma once
#include "../render_backend.h"

#include <vector>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>

struct GLFWwindow;

// ========= ユーティリティ =========
struct TBO { GLuint buf=0, tex=0; };     // Texture Buffer Object
struct GpuOctree {
    TBO nodeMin, nodeMax;     // GL_RGBA32F
    TBO texChildA, texChildB; // GL_RGBA32I
    TBO cornerLo, cornerHi;   // GL_RGBA32F
    int root=0, nNodes=0;
};

// ========= ドロー用のパラメータ =========
struct ParticleDrawParams {
    const float* pointSizes6 = nullptr;        // 6要素
    const float* valueMin6   = nullptr;        // 6要素
    const float* valueMax6   = nullptr;        // 6要素
    const int*   useLog6     = nullptr;        // 6要素（0/1）
#ifndef SAVE_GPU_MEMORY
    const int*   colorMode6  = nullptr;        // 6要素
#endif
    const int*   periodic6   = nullptr;        // 6要素（0/1）
    GLuint       colormapTex1D[6] = {0,0,0,0,0,0};
    bool         hideAll = false;
};

struct WboitParams {
    float baseAlpha  = 0.1f;
    float softness   = 0.9f;
    float focalPx    = 1.0f;
    int   kernelMode = 0;
    float gaussNSigma= 2.0f;
    float enlargeHsml= 1.0f;
};

struct RtBVHParams {
    int   root       = 0;
    int   lodMode    = 0;
    float pxThreshold= 1.0f;
    float tauMax     = 2.0f;
    float stepBias   = 1e-4f;
    float focalPx    = 1.0f;
    glm::vec3 camForward{0,0,-1};
    glm::mat4 invProj{1}, invView{1}, view{1};
    int debugMode    = 0;
};

struct RtOctreeParams {
    int   root       = 0;
    float pxThreshold= 1.0f;
    float tauMax     = 2.0f;
    float stepBias   = 1e-4f;
    float focalPx    = 1.0f;
    glm::vec3 camForward{0,0,-1};
    glm::mat4 invProj{1}, invView{1}, view{1};
    int debugMode    = 0;
};

// ========= バックエンド本体 =========
class RenderBackendGL final : public IRenderBackend {
public:
    // ---- ライフサイクル ---------------------------------------------------
    bool init(GLFWwindow* window, bool vsync=true); // GLAD, 状態, 最小シェーダ等の構築
    void shutdown() override;

    void beginFrame(int fbWidth, int fbHeight) override; // glViewport
    void endFrame() override;

    void setClearColor(float r,float g,float b,float a);
    void clear(bool color=true, bool depth=true);

    // ---- シェーダ（必要なら外部のソースに差し替え可能） ----------------------
    // 既存の createShaderProgram() を統合する場合もここに寄せる
    GLuint createProgramFromSources(const char* vs, const char* fs);
    GLuint createProgramWithHeader(const char* vs, const char* fs, const char* header);

    // ---- パーティクル頂点バッファ ------------------------------------------
    void createParticleVAO(GLsizei strideBytes,
                           GLint offPos, GLint offTypeU8, GLint offFlagU8,
                           GLint offHsml, GLint offDensity, GLint offTemp,
                           GLint offVal, GLint offVal2,
                           bool saveGpuMemory);

    void destroyParticleVAO();

    // 更新：フィルタ済み ParticleData 配列全体を転送
    void updateParticles(const void* particleData, std::size_t count, GLsizei strideBytes);

    // ドロー：粒子（通常 or WBOIT の粒子パスで再利用）
    void drawParticles(GLuint program, const glm::mat4& model,
                       const glm::mat4& view, const glm::mat4& proj,
                       const ParticleDrawParams& params,
                       GLsizei drawCount);

    // ---- 交差マーカー（小さなライン群） ------------------------------------
    void createCrossVAO(int numLines);
    void destroyCrossVAO();
    // 6 * N_LINES_FOR_CROSS の float3 を転送（glBufferSubData 相当）
    void updateCrossVertices(const float* xyz, std::size_t countFloats);
    // 単色ライン描画
    void drawLinesSimple(GLuint program, const glm::mat4& view, const glm::mat4& proj,
                         const glm::vec4& color,
                         GLsizei vertexCount /* = 2 * numLines */);

    // ---- 速度ベクトル（インスタンシング） ----------------------------------
    void createVelocityArrowVAO();
    void destroyVelocityArrowVAO();
    // インスタンス用 float[6]×N（pos3, vel3） を転送
    void updateVelocityInstances(const float* posVel6Array, std::size_t countInstances);
    // 描画（GL_LINES のインスタンシング）
    void drawVelocityArrows(GLuint program, const glm::mat4& view, const glm::mat4& proj,
                            float scaleFactor, bool useLogScale);

    // ---- カラーマップ（1D テクスチャ） -------------------------------------
    GLuint createColorMap1D(const float* rgbTriplets, int count /*色数*/);
    void   destroyTexture1D(GLuint tex1D);

    // ---- カラーバー矩形（スクリーンQUAD、別シェーダ） -------------------------
    void createColorBarQuad();     // VAO/VBO/EBO 作成
    void destroyColorBarQuad();
    // 位置更新（ピクセル→NDCで外側計算した結果の頂点4枚を渡す）
    void updateColorBarVertices(const float* xy_uv_4x4 /* 16 floats */);
    // 描画（オーバーレイ用：深度無効化は呼び出し側で or フラグ引数）
    void drawColorBar(GLuint program, GLuint colormapTex1D);

    // ---- 補助座標軸（画面右下、小さな3軸） ----------------------------------
    void createCoordAxesVAO();
    void destroyCoordAxesVAO();
    void drawCoordAxes(GLuint program, const glm::mat4& modelAxes,
                       const glm::mat4& view /*=I*/, const glm::mat4& projOrtho);

    // ---- Streamline --------------------------------------------------------
    void createStreamlineVAO();
    void destroyStreamlineVAO();
    void uploadStreamlineVertices(const float* verts, std::size_t countFloats);
    // glMultiDrawArrays 相当
    void drawStreamlines(GLuint program,
                         const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj,
                         const GLint* firsts, const GLsizei* counts, GLsizei drawCount,
                         const glm::vec3& color, float opacity);

    // ---- 幾何解析（楕円体・ディスク・等值面） --------------------------------
    // Ellipsoid（ワイヤ or UV-sphere）
    void createEllipsoidWire(const glm::vec3* verts, std::size_t count);
    void createEllipsoidUvSphere(const void* interleavedPosNrm, std::size_t vtxBytes,
                                 const uint32_t* indices, std::size_t idxCount);
    void destroyEllipsoid();
    void drawEllipsoidWire(GLuint program, const glm::mat4& model,
                           const glm::mat4& view, const glm::mat4& proj,
                           float opacity);
    void drawEllipsoidSolid(GLuint program, const glm::mat4& model,
                            const glm::mat4& view, const glm::mat4& proj,
                            const glm::vec3& color, float opacity);

    // Flat Disk
    void createFlatDisk(const glm::vec3* verts, std::size_t vcount,
                        const uint32_t* inds, std::size_t icount);
    void destroyFlatDisk();
    void drawFlatDisk(GLuint program, const glm::mat4& model,
                      const glm::mat4& view, const glm::mat4& proj,
                      const glm::vec3& color, float opacity);

    // Isocontour（頂点＋法線を内部で計算したい場合のアップロード/描画）
    void uploadMeshWithNormals(const float* verts, std::size_t vcount3,
                               const unsigned* inds, std::size_t icount);
    void destroyIsocontour();
    void drawIsocontour(GLuint program, const glm::mat4& model,
                        const glm::mat4& view, const glm::mat4& proj,
                        float opacity, GLsizei indexCount);

    // ---- 立方体群（インスタンシング） -----------------------------------------
    void createUnitCubeVAO(const float* cubeVerts, std::size_t cubeVertsBytes,
                           const unsigned* cubeIdx,  std::size_t cubeIdxBytes);
    void destroyUnitCubeVAO();

    // インスタンス行列（mat4×N）と不透明度（float×N）
    void updateCubeInstances(const glm::mat4* models, std::size_t count,
                             const float* opacities /*nullable -> 未使用なら null */);
    void drawCubesInstanced(GLuint program, const glm::mat4& view, const glm::mat4& proj,
                            GLsizei indexCount /*=36*/, GLsizei instanceCount);

    // ---- 単純ライン描画（任意の頂点列） -----------------------------------------
    GLuint createDynamicLineVAO();                 // 一時ライン用
    void   destroyDynamicLineVAO(GLuint vao, GLuint vbo);
    void   updateDynamicLineVertices(GLuint vbo, const glm::vec3* verts, std::size_t count);
    void   drawDynamicLines(GLuint program, GLuint vao, GLsizei vertexCount,
                            const glm::mat4& view, const glm::mat4& proj,
                            const glm::vec4& color);

    // ---- フルスクリーン図形（三角形 or Quad） ----------------------------------
    void createFullscreenTriangleVAO();
    void destroyFullscreenTriangleVAO();
    void drawFullscreenTriangle(GLuint program);   // 例：アップスケール、WBOIT 合成

    // ---- 2D テクスチャ（ImGui 表示など） ---------------------------------------
    IRenderBackend::Texture createTexture2D(int w, int h) override;                  // RGB8
    void    updateTexture2D(Texture t, int w, int h, const unsigned char* rgb) override;
    void    drawTexture2D(Texture t, int w, int h, int dstX, int dstY) override;     // 省略実装可
    void    destroyTexture(Texture t) override;

    // ImGui 用：投影画像テクスチャを確実にアップロードしてIDを返す
    GLuint ensureProjectionTexture(const unsigned char* rgb, int w, int h, uint64_t version,
                                   uint64_t& ioUploadedVersion, int& ioW, int& ioH);

    // ---- WBOIT（Weighted Blended OIT） -----------------------------------------
    void createOrResizeWboitFbo(int W, int H);
    void destroyWboitFbo();
    void beginWboitPass(GLuint accumLocation=0, GLuint revealLocation=1);
    void endWboitPass();
    void compositeWboit(GLuint program /*resolve*/, GLuint fsQuadVao);

    // ---- レイトレーシング：低解像度 FBO + アップスケール --------------------------
    void createOrResizeRtFbo(int w, int h);     // RGBA16F color
    void destroyRtFbo();
    // BVH/TBO のアップロード（配列を受け取り TBO を生成）
    TBO makeTBO(GLenum internalFormat, GLsizeiptr bytes, const void* data);
    void bindTBO(GLuint program, const char* uniformName, GLuint texture, GLint unit);

    // Raymarch BVH パス
    void drawRaytraceBVH(GLuint program,
                         const RtBVHParams& p,
                         int lowW, int lowH,
                         GLuint fsTriangleVao,
                         const TBO& nodeMin, const TBO& nodeMax, const TBO& nodeChild,
                         const TBO& particles, const TBO& partSigma);

    // Raymarch Octree パス
    void drawRaytraceOctree(GLuint program,
                            const RtOctreeParams& p,
                            int lowW, int lowH,
                            GLuint fsTriangleVao,
                            const GpuOctree& oct);

    // アップスケール（低解像度結果 gRtTex → 画面）
    void upscaleToScreen(GLuint program, GLuint lowTex, GLuint fsTriangleVao,
                         int viewportX, int viewportY, int viewportW, int viewportH);

    // ---- プログラムハンドルの保持（任意：ここで管理／外部管理どちらでも） ----------
    // 必要なら setter を用意して外部で作ったプログラムIDを渡せるようにもできる
    void setProgramParticle(GLuint p)           { progParticle_=p; }
    void setProgramLine(GLuint p)               { progLine_=p; }
    void setProgramVelocityArrow(GLuint p)      { progVelArrow_=p; }
    void setProgramColorBar(GLuint p)           { progColorbar_=p; }
    void setProgramIsocontour(GLuint p)         { progIsocontour_=p; }
    void setProgramEllipse(GLuint p)            { progEllipse_=p; }
    void setProgramEllipsoid(GLuint p)          { progEllipsoid_=p; }
    void setProgramDisk(GLuint p)               { progDisk_=p; }
    void setProgramStreamline(GLuint p)         { progStream_=p; }
    void setProgramCubic(GLuint p)              { progCubic_=p; }
    void setProgramCoord(GLuint p)              { progCoord_=p; }
    void setProgramUpscale(GLuint p)            { progUpscale_=p; }
    void setProgramRT_BVH(GLuint p)             { progRT_BVH_=p; }
    void setProgramRT_Oct(GLuint p)             { progRT_Oct_=p; }
    void setProgramWboitParticle(GLuint p)      { progWboitParticle_=p; }
    void setProgramWboitComposite(GLuint p)     { progWboitComposite_=p; }

    // 取得も用意しておくと便利
    GLuint progParticle()        const { return progParticle_; }
    GLuint progLine()            const { return progLine_; }
    GLuint progVelArrow()        const { return progVelArrow_; }
    GLuint progColorbar()        const { return progColorbar_; }
    GLuint progIsocontour()      const { return progIsocontour_; }
    GLuint progEllipse()         const { return progEllipse_; }
    GLuint progEllipsoid()       const { return progEllipsoid_; }
    GLuint progDisk()            const { return progDisk_; }
    GLuint progStream()          const { return progStream_; }
    GLuint progCubic()           const { return progCubic_; }
    GLuint progCoord()           const { return progCoord_; }
    GLuint progUpscale()         const { return progUpscale_; }
    GLuint progRT_BVH()          const { return progRT_BVH_; }
    GLuint progRT_Oct()          const { return progRT_Oct_; }
    GLuint progWboitParticle()   const { return progWboitParticle_; }
    GLuint progWboitComposite()  const { return progWboitComposite_; }

private:
    // 内部ハンドル類
    GLFWwindow* window_ = nullptr;

    // VAO/VBO
    GLuint vaoParticle_=0, vboParticle_=0;
    GLuint vaoCross_=0,    vboCross_=0;

    GLuint vaoVelArrow_=0, vboVelArrow_=0, vboVelInstance_=0;
    GLsizei arrowModelVertexCount_=2;   // 直線(0,0,0)-(0,0,1)
    GLsizei arrowInstanceCount_=0;

    GLuint vaoColorbar_=0, vboColorbar_=0, eboColorbar_=0;

    GLuint vaoCoord_=0, vboCoord_=0;

    GLuint vaoStream_=0, vboStream_=0;

    GLuint vaoEllipsoid_=0, vboEllipsoid_=0, nboEllipsoid_=0, iboEllipsoid_=0;
    GLsizei ellipsoidIndexCount_=0;
    bool    ellipsoidWire_=false;

    GLuint vaoDisk_=0, vboDisk_=0, eboDisk_=0;
    GLsizei diskIndexCount_=0;

    GLuint vaoCube_=0, vboCube_=0, eboCube_=0, vboCubeInst_=0, vboCubeOpacity_=0;
    GLsizei cubeIndexCount_=36; // 12 * 3
    GLsizei cubeInstanceCount_=0;

    GLuint vaoFsTri_=0; // fullscreen triangle

    // FBO/テクスチャ（RT/WBOIT）
    GLuint fboRT_=0, texRT_=0; int rtW_=0, rtH_=0;
    GLuint fboWboit_=0, texAccum_=0, texReveal_=0; int wboitW_=0, wboitH_=0;

    // プログラムID（外部から set 可）
    GLuint progParticle_=0, progLine_=0, progVelArrow_=0, progColorbar_=0;
    GLuint progIsocontour_=0, progEllipse_=0, progEllipsoid_=0, progDisk_=0;
    GLuint progStream_=0, progCubic_=0, progCoord_=0;
    GLuint progUpscale_=0, progRT_BVH_=0, progRT_Oct_=0;
    GLuint progWboitParticle_=0, progWboitComposite_=0;

    // 投影画像（ImGui 表示用）
    GLuint projTex_=0;

    // IRenderBackend のテクスチャ管理（2D）
    std::unordered_map<Texture, GLuint> tex2D_;
    Texture nextTex_ = 1;

private:
    // GL コンパイル/リンク（内部利用）
    static GLuint compile_(GLenum type, const char* src);
    static GLuint link_(GLuint vs, GLuint fs);
};
