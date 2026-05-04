#pragma once
#include "core/quantity.h"
#include "imgui.h"
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>

enum class TFShape { Gaussian, Box, Triangle };

struct TFComponent {
  TFShape type = TFShape::Gaussian;
  float center = 1.0f;  // rho.
  float width  = 1.0f;  // Gaussian: sigma; otherwise half width.
  float amp    = 0.5f;  // 0..1
  bool  logDomain = true;
};

class TransferFunctionEditor {
public:
  TransferFunctionEditor()
    : rhoMin_(1.0f),
      rhoMax_(1e16f),
      yPlotMax_(100.0f),
      ampSliderMax_(100.0f),
      logScale_(true),
      showAxes_(true)
  {
  }

  // Draw the UI and optionally return the rho-to-sigma evaluator in outEval.
  bool showUI(std::function<float(float)>* outEval = nullptr) {
    bool changed = false;
    bool applyRequested = false;
    const bool wasOpen = showWindow_;
    if (showWindow_ == false)
      return false;
    
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);	
    if (!ImGui::Begin("Transfer Function", &showWindow_, ImGuiWindowFlags_None)) {
      // Return early when the window is collapsed or minimized.
      ImGui::End();
      if (wasOpen && !showWindow_ && dirty_) {
        dirty_ = false;
        return true;
      }
      return false;
    }

    // Range and scale.
    ImGui::SeparatorText("Quantity range / sigma scale");
    float rMin = rhoMin_, rMax = rhoMax_;
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputFloat("value min", &rMin, 0, 0, "%.3e")) { rhoMin_ = std::max(1e-30f, rMin); changed = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputFloat("value max", &rMax, 0, 0, "%.3e")) { rhoMax_ = std::max(rMax, rhoMin_ * 1.0001f); changed = true; }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("log", &logScale_);

    // Display upper bound for y; this does not clamp stored amplitudes.
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputFloat("plot y max [display]", &yPlotMax_, 1.0f, 10.0f, "%.3g")) {
      yPlotMax_ = std::max(1e-6f, yPlotMax_);
    }
    ImGui::SameLine();
    ImGui::Checkbox("show axes", &showAxes_);
    
    // Add buttons.
    if (ImGui::Button("Add Gaussian")) { addComponent(TFShape::Gaussian); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Add Box")) { addComponent(TFShape::Box); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Add Triangle")) { addComponent(TFShape::Triangle); changed = true; }

    // Canvas.
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

    // Light grid.
    drawGrid(dl);
    drawAxes(dl); 

    // Curve.
    drawCurve(dl);

    // Handles: drawing, hit testing, and drag updates.
    changed |= drawAndEditHandles(dl);
    
    // Component details and numeric editing.
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
      
      changed |= ImGui::InputFloat("sigma amplitude [1/norm length]",
                                   &c.amp,
                                   0.0f,
                                   0.0f,
                                   "%.3g");
      c.amp = std::max(c.amp, 0.0f);
      ImGui::SetNextItemWidth(180);
      if (ImGui::InputFloat("amplitude slider max",
                            &ampSliderMax_,
                            1.0f,
                            10.0f,
                            "%.3g")) {
        ampSliderMax_ = std::max(1.0e-6f, ampSliderMax_);
      }
      changed |= ImGui::SliderFloat("amplitude drag",
                                    &c.amp,
                                    0.0f,
                                    ampSliderMax_);
      if (ImGui::Button("Remove")) { comps_.erase(comps_.begin() + i); changed = true; ImGui::Unindent(); ImGui::PopID(); break; }
      ImGui::Unindent();
      ImGui::Separator();
      ImGui::PopID();
    }

    // Delete the selected component with Delete or Backspace.
    if (selected_ >= 0 && selected_ < (int)comps_.size()) {
      if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
	comps_.erase(comps_.begin() + selected_);
	selected_ = -1;
	changed = true;
      }
    }

    dirty_ = dirty_ || changed;

    ImGui::Separator();
    if (ImGui::Button("Apply")) {
      applyRequested = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(dirty_ ? "edited" : "applied");

    ImGui::End();

    if (wasOpen && !showWindow_ && dirty_) {
      applyRequested = true;
    }

    if (applyRequested) {
      dirty_ = false;
    }

    if (applyRequested && outEval) {
      *outEval = [snap = *this](float rho) { return snap.evaluate(rho); };
    }
    return applyRequested;
  }

  float evaluate(float rho) const {
    float s = 0.0f;
    for (auto& c : comps_) s += evalOne(c, rho);
    return std::max(s, 0.f);
  }

  bool hasComponents() const {
    return !comps_.empty();
  }

  const std::vector<TFComponent>& components() const {
    return comps_;
  }

  void setComponents(const std::vector<TFComponent>& components) {
    comps_ = components;
    selected_ = -1;
    dirty_ = false;
  }

  float valueMin() const { return rhoMin_; }
  float valueMax() const { return rhoMax_; }
  bool logScale() const { return logScale_; }

  void set_window(){
    showWindow_ = true;
  }

  void set_minmax(QuantityId& var, float val_min, float val_max){
    if (!std::isfinite(val_min) || !std::isfinite(val_max)) {
      return;
    }

    val_min = std::max(val_min, 1.0e-30f);
    val_max = std::max(val_max, val_min * 1.0001f);

    const bool rangeChanged =
      std::abs(std::log10(std::max(rhoMin_, 1.0e-30f)) -
               std::log10(val_min)) > 1.0e-4f ||
      std::abs(std::log10(std::max(rhoMax_, 1.0e-30f)) -
               std::log10(val_max)) > 1.0e-4f;

    if(flag_show == false || var != var_show_ || rangeChanged){
      const float oldMin = rhoMin_;
      const float oldMax = rhoMax_;
      const bool preserveShape = flag_show && var == var_show_ && rangeChanged;

      if (preserveShape) {
        for (auto& c : comps_) {
          const float x = x01FromRho(c.center);
          const float left = handleX01(c, false);
          const float right = handleX01(c, true);
          rhoMin_ = val_min;
          rhoMax_ = val_max;
          c.center = rhoFromX01(x);
          const float leftRho = rhoFromX01(left);
          const float rightRho = rhoFromX01(right);
          if (c.type == TFShape::Gaussian && c.logDomain) {
            const float lo = std::log10(std::max(leftRho, 1.0e-30f));
            const float hi = std::log10(std::max(rightRho, 1.0e-30f));
            c.width = std::max(0.5f * std::abs(hi - lo), 1.0e-12f);
          } else {
            c.width = std::max(0.5f * std::abs(rightRho - leftRho), 1.0e-12f);
          }
          rhoMin_ = oldMin;
          rhoMax_ = oldMax;
        }
      }

      rhoMin_ = val_min;
      rhoMax_ = val_max;

      var_show_ = var;
      flag_show = true;
    }
  }

private:
  // State.
  float rhoMin_, rhoMax_, yPlotMax_, ampSliderMax_;
  bool logScale_;
  bool showWindow_ = false;
  bool showAxes_ = true;
  bool dirty_ = false;

  bool flag_show = false;
  QuantityId var_show_;

  std::vector<TFComponent> comps_;

  // Canvas region.
  ImVec2 plot0_{0,0}, plot1_{0,0};

  // Interactive editing state.
  enum class DragMode { None, Center, Left, Right };
  int selected_ = -1;
  DragMode drag_ = DragMode::None;

  // Add a component.
  void addComponent(TFShape type) {
    float c = std::sqrt(rhoMin_ * rhoMax_);
    float w = std::max( (rhoMax_ - rhoMin_) * 0.05f, 1e-6f );
    if(type == TFShape::Gaussian)
      w = std::max(log10(std::max(static_cast<double>(rhoMax_/rhoMin_), 1.e-6)) * 0.1f, 1.e-6);    
    
    comps_.push_back({ type, c, w, 0.5f });
    selected_ = (int)comps_.size() - 1;
  }

  // Evaluate one component.
  float evalOne(const TFComponent& c, float rho) const {
    if (c.type == TFShape::Gaussian) {
      float x;
      if (c.logDomain) {
	float lrho   = std::log10(std::max(rho, 1e-30f));
	float lcenter= std::log10(std::max(c.center, 1e-30f));
	float s      = std::max(c.width, 1e-12f); // Interpret width as sigma in log10 space.
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
    return (yPlotMax_ > 0.f) ? (amp / yPlotMax_) : 0.f;
  }
  inline float ampFromY01(float y01) const
  {
    return std::max(0.f, y01 * yPlotMax_);
  }
  
  // Convert normalized 0..1 coordinates to screen coordinates.
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

  // If right is true, use the right handle. Gaussian+logDomain uses log width; others use linear width.
  float handleRho(const TFComponent& c, bool right) const {
    const float dir = right ? +1.f : -1.f;
    const float eps = 1e-30f;

    float rhoH;
    if (c.type == TFShape::Gaussian && c.logDomain) {
      // rhoH = 10^(log10(center) +/- width).
      float lc  = std::log10(std::max(c.center, eps));
      float lch = lc + dir * c.width;
      rhoH = std::pow(10.0f, lch);
    } else {
      // Linear width.
      rhoH = c.center + dir * c.width;
    }
    // Clamp to the full range.
    rhoH = std::clamp(rhoH, rhoMin_, rhoMax_);
    return rhoH;
  }

  float handleX01(const TFComponent& c, bool right) const {
    return x01FromRho(handleRho(c, right));
  }

  ImVec2 handlePosScreen(const TFComponent& c, bool right) const {
    float x01 = handleX01(c, right);
    // Approximate handle height, normalized consistently for drawing and hit testing.
    float yh  = (c.type==TFShape::Gaussian ? c.amp*std::exp(-0.5f)
		 : (c.type==TFShape::Triangle ? c.amp*0.5f : c.amp));
    float y01 = std::clamp(yh / yPlotMax_, 0.0f, 1.0f);
    return toScreen01(x01, y01);
  }

  ImVec2 centerPosScreen(const TFComponent& c) const {
    float x01 = x01FromRho(c.center);
    float y01 = std::clamp(c.amp / yPlotMax_, 0.0f, 1.0f);
    return toScreen01(x01, y01);
  }
  
  // Draw the curve.
  void drawCurve(ImDrawList* dl) const {
    const int S = 512;
    static ImVec2 poly[512];
    for (int i = 0; i < S; i++) {
      float x01 = (float)i / (S - 1);
      float rho = rhoFromX01(x01);
      float y = evaluate(rho);
      float y01 = std::clamp(y / yPlotMax_, 0.f, 1.0f);
      poly[i] = toScreen01(x01, y01);
    }
    dl->AddPolyline(poly, S, IM_COL32(0, 180, 255, 255), false, 2.0f);
  }

  // Light grid and axis ticks.
  void drawGrid(ImDrawList* dl) const {
    const ImU32 col = IM_COL32(70,70,75,255);
    // y: 0, 0.5, 1
    for (int i=0;i<=2;i++){
      float y01 = i*0.5f;
      ImVec2 a = toScreen01(0.0f, y01);
      ImVec2 b = toScreen01(1.0f, y01);
      dl->AddLine(a,b,col, (i==2)?1.6f:1.0f);
    }
    // x: rhoMin, midpoint, rhoMax.
    for (int i=0;i<3;i++){
      float x01 = (float)i*0.5f;
      ImVec2 a = toScreen01(x01, 0.0f);
      ImVec2 b = toScreen01(x01, 1.0f);
      dl->AddLine(a,b,col, (i==0||i==2)?1.6f:1.0f);
    }
  }

  void drawAxes(ImDrawList* dl) const {
    if(!showAxes_) return;
    // Frame.
    dl->AddRect(plot0_, plot1_, IM_COL32(160,160,160,255), 4.0f);

    // X ticks, logarithmic or linear.
    const ImU32 col = IM_COL32(140,140,150,255);
    auto drawXTick = [&](float rho, const char* label){
      float x01 = x01FromRho(rho);
      ImVec2 a = toScreen01(x01, 0.0f);
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
      // Five subdivisions.
      for (int i=0;i<=5;i++){
	float rho = rhoMin_ + (rhoMax_-rhoMin_)*(i/5.0f);
	char buf[64]; snprintf(buf, sizeof(buf), "%.2g", rho);
	drawXTick(rho, buf);
      }
    }

    // Y ticks from 0 to yPlotMax_.
    auto drawYTick = [&](float y, const char* label){
      float y01 = std::clamp(y / yPlotMax_, 0.0f, 1.0f);
      ImVec2 a = toScreen01(0.0f, y01);
      dl->AddLine(ImVec2(plot0_.x-6, a.y), ImVec2(plot0_.x, a.y), col, 1.0f);
      dl->AddText(ImVec2(plot0_.x-56, a.y-7), col, label);
    };
    for (int i=0;i<=4;i++){
      float y = yPlotMax_ * (i/4.0f);
      char buf[32]; snprintf(buf, sizeof(buf), "%.2f", y);
      drawYTick(y, buf);
    }
  }

  // TransferEditor::drawAndEditHandles
  // Return true if any value changed.
  bool drawAndEditHandles(ImDrawList* dl)
  {
    bool changed = false;

    // --------------- Basic state ---------------
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mp   = io.MousePos;                 // Mouse position in absolute screen coordinates.
    const float hitR  = 7.0f;                        // Hit-test radius in pixels.
    const bool  inside =
      (mp.x >= plot0_.x && mp.x <= plot1_.x &&
       mp.y >= plot0_.y && mp.y <= plot1_.y);

    // --------------- Hover hit test when not dragging ---------------
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

    // --------------- Start dragging on click ---------------
    if (drag_ == DragMode::None && ImGui::IsMouseClicked(0) && inside) {
      if (hot >= 0) {
	selected_ = hot;
	drag_     = hotMode;
      } else {
	// If nothing is hit, optionally select the nearest component.
	// Implement this here if needed.
      }
    }

    // --------------- Drag update ---------------
    if (drag_ != DragMode::None && selected_ >= 0 && selected_ < (int)comps_.size()) {
      auto& c = comps_[selected_];

      // Convert screen coordinates to normalized 0..1 coordinates, then to physical values.
      float x01, y01;
      fromScreen(mp, x01, y01);
      x01 = std::clamp(x01, 0.0f, 1.0f);
      y01 = std::clamp(y01, 0.0f, 1.0f);

      if (drag_ == DragMode::Center) {
	// Center point: X controls center, Y controls amplitude.
	c.center = std::clamp(rhoFromX01(x01), rhoMin_, rhoMax_);
	c.amp    = std::max(0.0f, y01 * yPlotMax_);
	changed  = true;
      } else {
	// Left and right handles: derive rhoH from X and update width.
	float rhoH = rhoFromX01(x01);
	setWidthFromHandleRho(c, rhoH);
	changed = true;
      }

      // Stop dragging when the mouse button is released.
      if (ImGui::IsMouseReleased(0))
	drag_ = DragMode::None;
    }

    // --------------- Draw guides and handles ---------------
    for (int i = 0; i < (int)comps_.size(); ++i) {
      const auto& c = comps_[i];
      const bool  sel = (i == selected_);

      ImVec2 Pc = centerPosScreen(c);
      ImVec2 Pl = handlePosScreen(c, false);
      ImVec2 Pr = handlePosScreen(c, true);

      // Guide lines.
      dl->AddLine(Pl, Pr, IM_COL32(130,130,130,160), 1.0f);
      dl->AddLine(ImVec2(Pc.x, plot0_.y), ImVec2(Pc.x, plot1_.y),
		  IM_COL32(100,100,100,120), 1.0f);

      // Handle points.
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
