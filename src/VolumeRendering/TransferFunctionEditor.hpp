#pragma once
#include "imgui.h"
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>

class RhoSigmaLUT {
public:
    RhoSigmaLUT() = default;
    RhoSigmaLUT(std::vector<float> lut, float rhoMin, float rhoMax, bool logSample)
        : lut_(std::move(lut)), rhoMin_(rhoMin), rhoMax_(rhoMax), logSample_(logSample) {}

    // rho→sigma 補間評価
    float operator()(float rho) const {
        if (lut_.empty()) return 0.0f;
        if (rho <= rhoMin_) return lut_.front();
        if (rho >= rhoMax_) return lut_.back();

        float pos;
        if (logSample_) {
            float lxMin = std::log10(rhoMin_);
            float lxMax = std::log10(rhoMax_);
            pos = (std::log10(rho) - lxMin) / (lxMax - lxMin);
        } else {
            pos = (rho - rhoMin_) / (rhoMax_ - rhoMin_);
        }

        float fidx = pos * (lut_.size() - 1);
        int i0 = std::floor(fidx);
        int i1 = std::min(i0 + 1, (int)lut_.size() - 1);
        float t = fidx - i0;

        return lut_[i0] * (1.0f - t) + lut_[i1] * t;
    }

    const std::vector<float>& data() const { return lut_; }
    float rhoMin() const { return rhoMin_; }
    float rhoMax() const { return rhoMax_; }
    bool logSample() const { return logSample_; }

private:
    std::vector<float> lut_;
    float rhoMin_ = 1.0f;
    float rhoMax_ = 1.0f;
    bool logSample_ = true;
};

enum class TFShape { Gaussian, Box, Triangle };

struct TFComponent {
  TFShape type = TFShape::Gaussian;
  float center = 1.0f;  // ρ
  float width  = 1.0f;  // Gaussian: σ, 他: 半幅
  float amp    = 0.5f;  // 0..1
  bool  logDomain = true;
};

class TransferFunctionEditor {
public:
  TransferFunctionEditor()
    : rhoMin_(1.0f), rhoMax_(1e16f), yMax_(1.0f), logScale_(true), showAxes_(true)  {}

  // UIを描画し、評価関数（ρ→σ）を outEval に返す（必要なら）
  bool showUI(std::function<float(float)>* outEval = nullptr) {
    bool changed = false;
    if (showWindow_ == false)
      return changed;
    
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);	
    if (!ImGui::Begin("Transfer Function", &showWindow_, ImGuiWindowFlags_None)) {
      // 折りたたみ（最小化）時はここで早期終了
      ImGui::End();
      return changed;
    }

    // 範囲とスケール
    ImGui::SeparatorText("Range / Scale");
    float rMin = rhoMin_, rMax = rhoMax_;
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputFloat("rho min", &rMin, 0, 0, "%.3e")) { rhoMin_ = std::max(1e-30f, rMin); changed = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputFloat("rho max", &rMax, 0, 0, "%.3e")) { rhoMax_ = std::max(rMax, rhoMin_ * 1.0001f); changed = true; }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("log", &logScale_);

    // ★ y表示上限（amplitude を 1 超に対応）
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputFloat("y max (display)", &yMax_, 0, 0, "%.3f")) {
      yMax_ = std::max(1e-6f, yMax_);
      changed = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("show axes", &showAxes_);
    
    // 追加ボタン
    if (ImGui::Button("Add Gaussian")) { addComponent(TFShape::Gaussian); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Add Box")) { addComponent(TFShape::Box); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Add Triangle")) { addComponent(TFShape::Triangle); changed = true; }

    // キャンバス
    ImGui::SeparatorText("Curve (drag to edit)");
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + std::max(420.0f, avail.x), p0.y + 220.0f);
    ImGui::InvisibleButton("tf_canvas", ImVec2(p1.x - p0.x, p1.y - p0.y),
			   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(25, 25, 28, 255), 6.0f);

    plot0_ = ImVec2(p0.x + 60, p0.y + 12);
    plot1_ = ImVec2(p1.x - 16, p1.y - 34);
    dl->AddRectFilled(plot0_, plot1_, IM_COL32(40, 40, 45, 255), 4.0f);
    dl->AddRect(plot0_, plot1_, IM_COL32(90, 90, 95, 255), 4.0f);

    // グリッド少し
    drawGrid(dl);
    drawAxes(dl); 

    // 曲線
    drawCurve(dl);

    // ハンドル（描画＆ヒットテスト＆ドラッグ反映）
    changed |= drawAndEditHandles(dl);
    
    // コンポーネントの詳細（数値編集）
    ImGui::SeparatorText("Components");
    for (int i = 0; i < (int)comps_.size(); ++i) {
      ImGui::PushID(i);
      auto& c = comps_[i];
      const char* label = (c.type == TFShape::Gaussian ? "Gaussian" : (c.type == TFShape::Box ? "Box" : "Triangle"));
      ImGui::TextUnformatted(label);
      ImGui::Indent();
      changed |= ImGui::InputFloat("center", &c.center, 0, 0, "%.3e");
      changed |= ImGui::InputFloat(c.type == TFShape::Gaussian ? "sigma" : "half-width", &c.width, 0, 0, "%.3e");
      if(c.type == TFShape::Gaussian)
	changed |= ImGui::Checkbox("log-domain Gaussian", &c.logDomain);
      
      changed |= ImGui::SliderFloat("amplitude", &c.amp, 0.0f, 1.0f);
      if (ImGui::Button("Remove")) { comps_.erase(comps_.begin() + i); changed = true; ImGui::Unindent(); ImGui::PopID(); break; }
      ImGui::Unindent();
      ImGui::Separator();
      ImGui::PopID();
    }

    // Deleteキーで選択コンポーネント削除
    if (selected_ >= 0 && selected_ < (int)comps_.size()) {
      if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
	comps_.erase(comps_.begin() + selected_);
	selected_ = -1;
	changed = true;
      }
    }

    ImGui::End();

    if (outEval) {
      *outEval = [snap = *this](float rho) { return snap.evaluate(rho); };
    }
    return changed;
  }

  float evaluate(float rho) const {
    float s = 0.0f;
    for (auto& c : comps_) s += evalOne(c, rho);
    return std::max(s, 0.f);
  }

  void set_window(){
    showWindow_ = true;
  }

  void set_minmax(const std::string &var, float val_min, float val_max){
    if(flag_show == false || var != var_show_){
      rhoMin_ = val_min;
      rhoMax_ = val_max;

      var_show_ = var;
      flag_show = true;
    }
  }

  RhoSigmaLUT bakeLUT(int size) const {
    std::vector<float> lut(size);
    constexpr float eps = 1e-30f;
    for (int i = 0; i < size; ++i) {
      float rho;
      if (logScale_) {
	float lxMin = std::log10(std::max(rhoMin_, eps));
	float lxMax = std::log10(std::max(rhoMax_, eps));
	float t = (size == 1) ? 0.0f : (float)i / (size - 1);
	rho = std::pow(10.0f, lxMin * (1.0f - t) + lxMax * t);
      } else {
	float t = (size == 1) ? 0.0f : (float)i / (size - 1);
	rho = rhoMin_ * (1.0f - t) + rhoMax_ * t;
      }
      lut[i] = evaluate(rho);
    }
    
    return RhoSigmaLUT(std::move(lut), rhoMin_, rhoMax_, logScale_);
  }
  
private:
  // 状態
  float rhoMin_, rhoMax_, yMax_;
  bool logScale_;
  bool showWindow_ = false;
  bool showAxes_ = true;

  bool flag_show = false;
  std::string var_show_;

  std::vector<TFComponent> comps_;

  // キャンバス領域
  ImVec2 plot0_{0,0}, plot1_{0,0};

  // インタラクティブ編集用
  enum class DragMode { None, Center, Left, Right };
  int selected_ = -1;
  int hot_ = -1;
  DragMode drag_ = DragMode::None;

  // 追加
  void addComponent(TFShape type) {
    float c = std::sqrt(rhoMin_ * rhoMax_);
    float w = std::max( (rhoMax_ - rhoMin_) * 0.05f, 1e-6f );
    if(type == TFShape::Gaussian)
      w = std::max(log10(std::max(rhoMax_/rhoMin_, 1.e-6f)) * 0.1f, 1.e-6f);    
    
    comps_.push_back({ type, c, w, 0.5f });
    selected_ = (int)comps_.size() - 1;
  }

  // 評価
  float evalOne(const TFComponent& c, float rho) const {
    if (c.type == TFShape::Gaussian) {
      float x;
      if (c.logDomain) {
	float lrho   = std::log10(std::max(rho, 1e-30f));
	float lcenter= std::log10(std::max(c.center, 1e-30f));
	float s      = std::max(c.width, 1e-12f); // ★ 幅を「log10 の σ」と解釈
	x = (lrho - lcenter) / s;
      } else {
	x = (rho - c.center) / std::max(c.width, 1e-30f);
      }
      return c.amp * std::exp(-0.5f * x * x);
    } else if (c.type == TFShape::Box) {
      return (std::abs(rho - c.center) <= c.width) ? c.amp : 0.0f;
    } else { // Triangle
      float dx = std::abs(rho - c.center);
      if (dx >= c.width) return 0.0f;
      return c.amp * (1.0f - dx / std::max(c.width, 1e-30f));
    }
  }
  
  // ρ <-> x01
  float rhoFromX01(float x01) const {
    x01 = std::clamp(x01, 0.0f, 1.0f);
    if (logScale_) {
      float a = std::log10(std::max(rhoMin_, 1e-30f));
      float b = std::log10(std::max(rhoMax_, 1e-30f));
      return std::pow(10.0f, a * (1.0f - x01) + b * x01);
    } else {
      return rhoMin_ * (1.0f - x01) + rhoMax_ * x01;
    }
  }
  float x01FromRho(float rho) const {
    rho = std::clamp(rho, rhoMin_, rhoMax_);
    if (logScale_) {
      float a = std::log10(std::max(rhoMin_, 1e-30f));
      float b = std::log10(std::max(rhoMax_, 1e-30f));
      float v = std::log10(std::max(rho, 1e-30f));
      return (v - a) / std::max(b - a, 1e-30f);
    } else {
      return (rho - rhoMin_) / std::max(rhoMax_ - rhoMin_, 1e-30f);
    }
  }

  inline float y01FromAmp(float amp) const
  {
    return (yMax_ > 0.f) ? (amp / yMax_) : 0.f;
  }
  inline float ampFromY01(float y01) const
  {
    return std::max(0.f, y01 * yMax_);
  }
  
  // スクリーン座標変換（01→スクリーン）
  inline ImVec2 toScreen01(float x01, float y01) const {
    return ImVec2(
		  plot0_.x + x01 * (plot1_.x - plot0_.x),
		  plot1_.y - y01 * (plot1_.y - plot0_.y)
		  );
  }
  inline void fromScreen(const ImVec2& P, float& x01, float& y01) {
    x01 = (P.x - plot0_.x) / (plot1_.x - plot0_.x);
    y01 = (plot1_.y - P.y)  / (plot1_.y - plot0_.y);
  }

  // 右ハンドルなら right=true。Gaussian+logDomain のときだけ対数幅、他は線形幅。
  float handleRho(const TFComponent& c, bool right) const {
    const float dir = right ? +1.f : -1.f;
    const float eps = 1e-30f;

    float rhoH;
    if (c.type == TFShape::Gaussian && c.logDomain) {
      // ρh = 10^( log10(center) ± width )
      float lc  = std::log10(std::max(c.center, eps));
      float lch = lc + dir * c.width;
      rhoH = std::pow(10.0f, lch);
    } else {
      // 線形幅
      rhoH = c.center + dir * c.width;
    }
    // 全体レンジにクランプ
    rhoH = std::clamp(rhoH, rhoMin_, rhoMax_);
    return rhoH;
  }

  float handleX01(const TFComponent& c, bool right) const {
    return x01FromRho(handleRho(c, right));
  }

  ImVec2 handlePosScreen(const TFComponent& c, bool right) const {
    float x01 = handleX01(c, right);
    // ハンドルの目安の高さ（描画・ヒットテストとも統一して正規化）
    float yh  = (c.type==TFShape::Gaussian ? c.amp*std::exp(-0.5f)
		 : (c.type==TFShape::Triangle ? c.amp*0.5f : c.amp));
    float y01 = std::clamp(yh / yMax_, 0.0f, 1.0f);
    return toScreen01(x01, y01);
  }

  ImVec2 centerPosScreen(const TFComponent& c) const {
    float x01 = x01FromRho(c.center);
    float y01 = std::clamp(c.amp / yMax_, 0.0f, 1.0f);
    return toScreen01(x01, y01);
  }
  
  // 曲線描画
  void drawCurve(ImDrawList* dl) const {
    const int S = 512;
    static ImVec2 poly[512];
    for (int i = 0; i < S; i++) {
      float x01 = (float)i / (S - 1);
      float rho = rhoFromX01(x01);
      float y = evaluate(rho);
      float y01 = std::clamp(y / yMax_, 0.f, 1.0f);
      poly[i] = toScreen01(x01, y01);
    }
    dl->AddPolyline(poly, S, IM_COL32(0, 180, 255, 255), false, 2.0f);
  }

  // グリッド & 軸目盛（軽め）
  void drawGrid(ImDrawList* dl) const {
    const ImU32 col = IM_COL32(70,70,75,255);
    // y: 0, 0.5, 1
    for (int i=0;i<=2;i++){
      float y01 = i*0.5f;
      ImVec2 a = toScreen01(0.0f, y01);
      ImVec2 b = toScreen01(1.0f, y01);
      dl->AddLine(a,b,col, (i==2)?1.6f:1.0f);
    }
    // x: ρMin, 中央, ρMax
    for (int i=0;i<3;i++){
      float x01 = (float)i*0.5f;
      ImVec2 a = toScreen01(x01, 0.0f);
      ImVec2 b = toScreen01(x01, 1.0f);
      dl->AddLine(a,b,col, (i==0||i==2)?1.6f:1.0f);
    }
  }

  void drawAxes(ImDrawList* dl) const {
    if(!showAxes_) return;
    // 枠
    dl->AddRect(plot0_, plot1_, IM_COL32(160,160,160,255), 4.0f);

    // X 目盛（ログ or 線形）
    const ImU32 col = IM_COL32(140,140,150,255);
    auto drawXTick = [&](float rho, const char* label){
      float x01 = x01FromRho(rho);
      ImVec2 a = toScreen01(x01, 0.0f);
      ImVec2 b = toScreen01(x01, 0.02f);
      dl->AddLine(ImVec2(a.x, plot1_.y), ImVec2(a.x, plot1_.y+6), col, 1.0f);
      dl->AddText(ImVec2(a.x-20, plot1_.y+8), col, label);
    };
    if (logScale_) {
      // decade ticks
      int a = (int)std::floor(std::log10(rhoMin_));
      int b = (int)std::ceil (std::log10(rhoMax_));
      for (int e=a; e<=b; ++e) {
	float rho = std::pow(10.0f, (float)e);
	char buf[32]; snprintf(buf, sizeof(buf), "1e%d", e);
	drawXTick(rho, buf);
      }
    } else {
      // 5分割
      for (int i=0;i<=5;i++){
	float rho = rhoMin_ + (rhoMax_-rhoMin_)*(i/5.0f);
	char buf[64]; snprintf(buf, sizeof(buf), "%.2g", rho);
	drawXTick(rho, buf);
      }
    }

    // Y 目盛（0..yMax_）
    auto drawYTick = [&](float y, const char* label){
      float y01 = std::clamp(y / yMax_, 0.0f, 1.0f);
      ImVec2 a = toScreen01(0.0f, y01);
      dl->AddLine(ImVec2(plot0_.x-6, a.y), ImVec2(plot0_.x, a.y), col, 1.0f);
      dl->AddText(ImVec2(plot0_.x-56, a.y-7), col, label);
    };
    for (int i=0;i<=4;i++){
      float y = yMax_ * (i/4.0f);
      char buf[32]; snprintf(buf, sizeof(buf), "%.2f", y);
      drawYTick(y, buf);
    }
  }

  // TransferEditor::drawAndEditHandles
  // 返り値: 何か値が変わったら true
  bool drawAndEditHandles(ImDrawList* dl)
  {
    bool changed = false;

    // --------------- 基本状態 ---------------
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mp   = io.MousePos;                 // 画面絶対座標のマウス
    const float hitR  = 7.0f;                        // ヒット判定半径(px)
    const bool  inside =
      (mp.x >= plot0_.x && mp.x <= plot1_.x &&
       mp.y >= plot0_.y && mp.y <= plot1_.y);

    // --------------- ホバー判定（ドラッグ中以外） ---------------
    int      hot = -1;
    DragMode hotMode = DragMode::None;

    if (drag_ == DragMode::None && inside) {
      for (int i = 0; i < (int)comps_.size(); ++i) {
	const auto& c = comps_[i];
	ImVec2 Pc = centerPosScreen(c);
	ImVec2 Pl = handlePosScreen(c, /*right=*/false);
	ImVec2 Pr = handlePosScreen(c, /*right=*/true);

	auto dist2 = [](ImVec2 a, ImVec2 b){
	  float dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy;
	};

	if (dist2(mp, Pl) <= hitR*hitR) { hot = i; hotMode = DragMode::Left;  }
	if (dist2(mp, Pr) <= hitR*hitR) { hot = i; hotMode = DragMode::Right; }
	if (dist2(mp, Pc) <= hitR*hitR) { hot = i; hotMode = DragMode::Center;}
      }
    }

    // --------------- クリックでドラッグ開始 ---------------
    if (drag_ == DragMode::None && ImGui::IsMouseClicked(0) && inside) {
      if (hot >= 0) {
	selected_ = hot;
	drag_     = hotMode;
      } else {
	// 何もヒットしない → 近い成分を選択だけする（任意）
	// ここは必要なら実装
      }
    }

    // --------------- ドラッグ更新 ---------------
    if (drag_ != DragMode::None && selected_ >= 0 && selected_ < (int)comps_.size()) {
      auto& c = comps_[selected_];

      // スクリーン→正規化座標(0..1)→物理量へ変換
      float x01, y01;
      fromScreen(mp, x01, y01);
      x01 = std::clamp(x01, 0.0f, 1.0f);
      y01 = std::clamp(y01, 0.0f, 1.0f);

      if (drag_ == DragMode::Center) {
	// 中心点: Xはcenter、Yはamp
	c.center = std::clamp(rhoFromX01(x01), rhoMin_, rhoMax_);
	c.amp    = std::max(0.0f, y01 * yMax_);
	changed  = true;
      } else {
	// 左右ハンドル: Xから ρh を求め、width を更新
	float rhoH = rhoFromX01(x01);
	setWidthFromHandleRho(c, rhoH);
	changed = true;
      }

      // ボタンを離したらドラッグ終了
      if (ImGui::IsMouseReleased(0))
	drag_ = DragMode::None;
    }

    // --------------- 描画（補助線とハンドル） ---------------
    for (int i = 0; i < (int)comps_.size(); ++i) {
      const auto& c = comps_[i];
      const bool  sel = (i == selected_);

      ImVec2 Pc = centerPosScreen(c);
      ImVec2 Pl = handlePosScreen(c, false);
      ImVec2 Pr = handlePosScreen(c, true);

      // 補助線
      dl->AddLine(Pl, Pr, IM_COL32(130,130,130,160), 1.0f);
      dl->AddLine(ImVec2(Pc.x, plot0_.y), ImVec2(Pc.x, plot1_.y),
		  IM_COL32(100,100,100,120), 1.0f);

      // ハンドル点
      ImU32 colC = sel ? IM_COL32(255,200,0,255) : IM_COL32(200,200,200,255);
      dl->AddCircleFilled(Pc, 5.5f, colC);
      dl->AddCircleFilled(Pl, 4.5f, IM_COL32(160,220,255,255));
      dl->AddCircleFilled(Pr, 4.5f, IM_COL32(160,220,255,255));
    }

    return changed;
  }
  
  static float distanceSq(const ImVec2& a, const ImVec2& b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx*dx + dy*dy;
  }
  
  void setWidthFromHandleRho(TFComponent& c, float rhoH) {
    const float eps = 1e-30f;
    if (c.type == TFShape::Gaussian && c.logDomain) {
        float lw = std::abs(std::log10(std::max(rhoH, eps)) - std::log10(std::max(c.center, eps)));
        c.width = std::max(lw, 1e-12f);
    } else {
        c.width = std::max(std::abs(rhoH - c.center), 1e-12f);
    }
  }
};


