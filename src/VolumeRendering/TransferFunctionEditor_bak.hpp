#pragma once
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <array>
#include <glad/glad.h>
#include <imgui.h>

class TransferFunctionEditor {
public:
  struct Knot { float t; float v; }; // t∈[0,1], v∈[0,1] （t: 正規化座標, v: σ正規化値）
  struct Evaluator {
    // 固定スナップショット（UIと独立）
    float rhoMin, rhoMax; bool logSpace;
    std::vector<float> lut; // 0..1 の σ 値（LUT_SIZE）
    static constexpr int LUT_SIZE = 1024;

    inline float operator()(float rho) const {
      float u;
      if(logSpace) {
	float r = std::clamp(rho, rhoMin, rhoMax);
	float lrmin = std::log10(std::max(1e-30f, rhoMin));
	float lrmax = std::log10(std::max(1e-30f, rhoMax));
	float lr    = std::log10(std::max(1e-30f, r));
	u = (lr - lrmin) / std::max(1e-12f, (lrmax - lrmin));
      } else {
	u = (rho - rhoMin) / std::max(1e-12f, (rhoMax - rhoMin));
      }
      u = std::clamp(u, 0.0f, 1.0f);
      float x = u * (LUT_SIZE - 1);
      int i = (int)std::floor(x);
      int j = std::min(i+1, LUT_SIZE-1);
      float a = x - i;
      return (1.0f - a) * lut[i] + a * lut[j];
    }
  };

  TransferFunctionEditor(int lutSize = 1024)
    : LUT_SIZE_(lutSize), rhoMin_(1.0f), rhoMax_(1e16f), logSpace_(true)
  {
    // 初期ノード（端点＋真ん中）
    knots_ = { {0.0f, 0.0f}, {0.5f, 0.3f}, {1.0f, 1.0f} };
    // GL テクスチャ（幅×1 の 2D を 1D LUT として使用）
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    lut_.resize(LUT_SIZE_);
    uploadLUT(); // 初期アップロード
  }
  ~TransferFunctionEditor(){
    if(tex_) glDeleteTextures(1, &tex_);
  }

  // ImGui ウィンドウを描く（必要な時だけ呼ぶ）
  void showUI(void){
    if (showWindow_ == false) return;
	
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
	
    if (!ImGui::Begin("Transfer Function", &showWindow_, ImGuiWindowFlags_None)) {
      // 折りたたみ（最小化）時はここで早期終了
      ImGui::End();
      return;
    }
	
    // レンジとスケール
    ImGui::Checkbox("Log scale", &logSpace_);
    ImGui::InputFloat("rho min", &rhoMin_);
    ImGui::InputFloat("rho max", &rhoMax_);
    rhoMin_ = std::max(1e-30f, rhoMin_);
    rhoMax_ = std::max(rhoMin_ * 1.0001f, rhoMax_);

    // ノード編集
    ImGui::SeparatorText("Knots");
    bool changed = false;
    for(size_t i=0;i<knots_.size();++i){
      ImGui::PushID((int)i);
      float t = knots_[i].t, v = knots_[i].v;
      if(ImGui::SliderFloat("t", &t, 0.0f, 1.0f)) {
	knots_[i].t = t; changed = true;
      }
      if(ImGui::SliderFloat("v", &v, 0.0f, 1.0f)) {
	knots_[i].v = v; changed = true;
      }
      ImGui::SameLine();
      if(i>0 && i+1<knots_.size()){
	if(ImGui::Button("Delete")) { knots_.erase(knots_.begin()+i); changed = true; ImGui::PopID(); break; }
      } else {
	ImGui::Dummy({60,0});
      }
      ImGui::PopID();
    }
    if(ImGui::Button("Add knot")){
      knots_.push_back({0.5f, 0.5f});
      changed = true;
    }

    // 2D キャンバス上に簡易グラフ（ImDrawListで）
    ImGui::Separator();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 sz(std::max(240.0f, avail.x), 160.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = { p0.x + sz.x, p0.y + sz.y };
    dl->AddRectFilled(p0, p1, IM_COL32(20,20,20,255));
    dl->AddRect(p0, p1, IM_COL32(80,80,80,255));

    // ノードを線形で結んで描画
    std::vector<Knot> ks = knots_; // ソートして描く/評価
    std::sort(ks.begin(), ks.end(), [](const Knot&a,const Knot&b){return a.t<b.t;});
    auto toScreen = [&](float t, float v){
      return ImVec2{ p0.x + t*sz.x, p1.y - v*sz.y };
    };
    for(size_t i=1;i<ks.size();++i){
      dl->AddLine(toScreen(ks[i-1].t, ks[i-1].v), toScreen(ks[i].t, ks[i].v), IM_COL32(120,220,120,255), 2.0f);
    }
    for(auto& k : ks){
      ImVec2 c = toScreen(k.t, k.v);
      dl->AddCircleFilled(c, 4.0f, IM_COL32(220,220,80,255));
    }
    ImGui::Dummy(sz);

    if(changed){
      // 範囲チェック＋ソート固定化
      for(auto& k : knots_){
	k.t = std::clamp(k.t, 0.0f, 1.0f);
	k.v = std::clamp(k.v, 0.0f, 1.0f);
      }
      std::sort(knots_.begin(), knots_.end(), [](const Knot&a,const Knot&b){return a.t<b.t;});
      uploadLUT();
    }
    ImGui::End();
  }

  // 今の設定をスナップショットして ρ→σ の関数を返す（UIの寿命に依存しない）
  Evaluator freeze() const {
    Evaluator ev;
    ev.rhoMin   = rhoMin_;
    ev.rhoMax   = rhoMax_;
    ev.logSpace = logSpace_;
    ev.lut.resize(LUT_SIZE_);
    for(int i=0;i<LUT_SIZE_;++i) ev.lut[i] = lut_[i];
    return ev;
  }

  // シェーダ側で使う 1D LUT（幅×1の2D）を得る
  GLuint texture() const { return tex_; }
  int lutSize() const { return LUT_SIZE_; }

  // 直接評価（UIの現状態に依存して良い場合）
  float evaluate(float rho) const { return freeze()(rho); }

  
private:
  int LUT_SIZE_;
  std::vector<Knot> knots_;
  std::vector<float> lut_; // 0..1
  GLuint tex_ = 0;
  float rhoMin_, rhoMax_;
  bool  logSpace_;
  bool showWindow_ = false;
  
  float eval01(float u) const {
    // 端補外は端値
    if(knots_.empty()) return u;
    std::vector<Knot> ks = knots_;
    std::sort(ks.begin(), ks.end(), [](const Knot&a,const Knot&b){return a.t<b.t;});
    if(u <= ks.front().t) return ks.front().v;
    if(u >= ks.back().t)  return ks.back().v;
    // 区間線形
    for(size_t i=1;i<ks.size();++i){
      if(u <= ks[i].t){
	float t0=ks[i-1].t, t1=ks[i].t;
	float v0=ks[i-1].v, v1=ks[i].v;
	float a = (u - t0) / std::max(1e-12f, (t1 - t0));
	return (1.0f-a)*v0 + a*v1;
      }
    }
    return ks.back().v;
  }

  void uploadLUT(){
    // LUT を再生成
    for(int i=0;i<LUT_SIZE_;++i){
      float u = float(i) / float(LUT_SIZE_-1);
      lut_[i] = eval01(u); // 0..1
    }
    // GL にアップロード（R32F）
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, LUT_SIZE_, 1, 0, GL_RED, GL_FLOAT, lut_.data());
  }
};
