
#ifdef VOLUME_RENDERING_TEMP
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


static void DrawVolumePass(const FrameMatrices& fm)
{
  if (!gRenderRuntimeState.volume.show)
    return;

  if (gRenderRuntimeState.volume.flagRT == 1) {
    if (gRenderRuntimeState.volume.cpuUpdated) {
      G_bvh = uploadBVH_TBO(gBVHresult);
      gRenderRuntimeState.volume.cpuUpdated = false;
    }

    int lowW = std::max(1.0f,
                        gWindowContext.viewportWidth() /
                        gRenderRuntimeState.volume.rtDownscale);
    int lowH = std::max(1.0f,
                        gWindowContext.viewportHeight() /
                        gRenderRuntimeState.volume.rtDownscale);
    if (lowW != gRtW || lowH != gRtH) {
      CreateOrResizeRTFBO(lowW, lowH);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, gRtFbo);
    glViewport(0, 0, gRtW, gRtH);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(gRenderPrograms.rt);
    glBindVertexArray(gRenderResources.fullscreenVAO);

    int debug_mode = 0;
    glUniform1i(glGetUniformLocation(gRenderPrograms.rt, "uDebugMode"), debug_mode);

    bindTBO(gRenderPrograms.rt, "nodeMinTB",   G_bvh.nodeMin.tex,   0);
    bindTBO(gRenderPrograms.rt, "nodeMaxTB",   G_bvh.nodeMax.tex,   1);
    bindTBO(gRenderPrograms.rt, "nodeChildTB", G_bvh.nodeChild.tex, 2);
    bindTBO(gRenderPrograms.rt, "particlesTB", G_bvh.particles.tex, 3);
    bindTBO(gRenderPrograms.rt, "partSigmaTB", G_bvh.partSigma.tex, 4);

    glUniform1i(glGetUniformLocation(gRenderPrograms.rt, "uRoot"), G_bvh.root);
    glUniform1i(glGetUniformLocation(gRenderPrograms.rt, "uLodMode"),
                gRenderRuntimeState.volume.lodMode);
    glUniform1f(glGetUniformLocation(gRenderPrograms.rt, "uPxThreshold"),
                gRenderRuntimeState.volume.pxThreshold);
    glUniform1f(glGetUniformLocation(gRenderPrograms.rt, "uTauMax"),
                gRenderRuntimeState.volume.tauMax);
    glUniform1f(glGetUniformLocation(gRenderPrograms.rt, "uStepBias"), 1e-4f);

    glUniform1f(glGetUniformLocation(gRenderPrograms.rt, "uFocalPx"), fm.focalPx);
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.rt, "invProj"),
                       1, GL_FALSE, glm::value_ptr(fm.invProj));
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.rt, "invView"),
                       1, GL_FALSE, glm::value_ptr(fm.invView));
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.rt, "view"),
                       1, GL_FALSE, glm::value_ptr(fm.view));
    glUniform3fv(glGetUniformLocation(gRenderPrograms.rt, "uCamForward"),
                 1, glm::value_ptr(fm.camForward));
    glUniform2f(glGetUniformLocation(gRenderPrograms.rt, "uResolution"),
                (float)gRtW, (float)gRtH);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

#ifdef USE_LETTERBOX
    glViewport(gWindowContext.viewportX(),
               gWindowContext.viewportY(),
               gWindowContext.viewportWidth(),
               gWindowContext.viewportHeight());
#else
    glViewport(0, 0, fm.viewportW, fm.viewportH);
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(gRenderPrograms.upscale);
    glBindVertexArray(gRenderResources.fullscreenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRtTex);
    glUniform1i(glGetUniformLocation(gRenderPrograms.upscale, "uLow"), 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glUseProgram(0);
    return;
  }

  if (gRenderRuntimeState.volume.flagRT == 2) {
    if (gRenderRuntimeState.volume.cpuUpdated) {
      uploadOctTree_TBO(gAppServices.volume.octTree,
                        gGPUOctTree,
                        gAppServices.volume.rho2sigma);
      gRenderRuntimeState.volume.cpuUpdated = false;
    }

    int lowW = std::max(1.0f,
                        gWindowContext.viewportWidth() /
                        gRenderRuntimeState.volume.rtDownscale);
    int lowH = std::max(1.0f,
                        gWindowContext.viewportHeight() /
                        gRenderRuntimeState.volume.rtDownscale);
    if (lowW != gRtW || lowH != gRtH) {
      CreateOrResizeRTFBO(lowW, lowH);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, gRtFbo);
    glViewport(0, 0, gRtW, gRtH);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(gRenderPrograms.octray);
    glBindVertexArray(gRenderResources.fullscreenVAO);

    int debug_mode = 0;
    glUniform1i(glGetUniformLocation(gRenderPrograms.octray, "uDebugMode"), debug_mode);

    bindTBO(gRenderPrograms.octray, "nodeMinTB",  gGPUOctTree.nodeMin.tex,   0);
    bindTBO(gRenderPrograms.octray, "nodeMaxTB",  gGPUOctTree.nodeMax.tex,   1);
    bindTBO(gRenderPrograms.octray, "childATB",   gGPUOctTree.texChildA.tex, 2);
    bindTBO(gRenderPrograms.octray, "childBTB",   gGPUOctTree.texChildB.tex, 3);
    bindTBO(gRenderPrograms.octray, "cornerLoTB", gGPUOctTree.cornerLo.tex,  4);
    bindTBO(gRenderPrograms.octray, "cornerHiTB", gGPUOctTree.cornerHi.tex,  5);

    glUniform1i(glGetUniformLocation(gRenderPrograms.octray, "uRoot"), gGPUOctTree.root);
    glUniform1f(glGetUniformLocation(gRenderPrograms.octray, "uPxThreshold"),
                gRenderRuntimeState.volume.pxThreshold);
    glUniform1f(glGetUniformLocation(gRenderPrograms.octray, "uTauMax"),
                gRenderRuntimeState.volume.tauMax);
    glUniform1f(glGetUniformLocation(gRenderPrograms.octray, "uStepBias"), 1e-4f);

    glUniform1f(glGetUniformLocation(gRenderPrograms.octray, "uFocalPx"), fm.focalPx);
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.octray, "invProj"),
                       1, GL_FALSE, glm::value_ptr(fm.invProj));
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.octray, "invView"),
                       1, GL_FALSE, glm::value_ptr(fm.invView));
    glUniformMatrix4fv(glGetUniformLocation(gRenderPrograms.octray, "view"),
                       1, GL_FALSE, glm::value_ptr(fm.view));
    glUniform3fv(glGetUniformLocation(gRenderPrograms.octray, "uCamForward"),
                 1, glm::value_ptr(fm.camForward));
    glUniform2f(glGetUniformLocation(gRenderPrograms.octray, "uResolution"),
                (float)gRtW, (float)gRtH);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

#ifdef USE_LETTERBOX
    glViewport(gWindowContext.viewportX(),
               gWindowContext.viewportY(),
               gWindowContext.viewportWidth(),
               gWindowContext.viewportHeight());
#else
    glViewport(0, 0, fm.viewportW, fm.viewportH);
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(gRenderPrograms.upscale);
    glBindVertexArray(gRenderResources.fullscreenVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRtTex);
    glUniform1i(glGetUniformLocation(gRenderPrograms.upscale, "uLow"), 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glUseProgram(0);
    return;
  }
}
#endif
