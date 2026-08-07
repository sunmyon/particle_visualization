#include "projection/projection_pdf_writer.h"

#include "projection/make_2D_projection_map.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct PdfLayout {
  int pageW = 0;
  int pageH = 0;
  int plotX0 = 0;
  int plotY0 = 0;
  int plotW = 0;
  int plotH = 0;
  int barX0 = 0;
  int barY0 = 0;
  int barW = 0;
  int barH = 0;
  int tickLabelX = 0;
  int tickLabelY = 0;
  int titleX = 0;
  int titleY = 0;
  bool horizontal = false;
  bool inset = false;
  bool showLegend = true;
  int tickFontSize = 10;
  int labelFontSize = 12;
};

std::string PdfEscape(const std::string& text)
{
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (c == '(' || c == ')' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

double TextWidth(const std::string& text, double size)
{
  double units = 0.0;
  for (unsigned char c : text) {
    if (c >= '0' && c <= '9') {
      units += 0.56;
    } else if (c == '.' || c == ',' || c == ':' || c == ';') {
      units += 0.28;
    } else if (c == '-' || c == '+') {
      units += 0.34;
    } else if (c == ' ' || c == '\t') {
      units += 0.28;
    } else if (c >= 'A' && c <= 'Z') {
      units += 0.66;
    } else if (c == 'm' || c == 'w' || c == 'M' || c == 'W') {
      units += 0.78;
    } else if (c == 'i' || c == 'l' || c == 'I') {
      units += 0.25;
    } else {
      units += 0.50;
    }
  }
  return units * size;
}

struct PdfTextBBox {
  double minX = 0.0;
  double maxX = 0.0;
  double minY = 0.0;
  double maxY = 0.0;
  double width = 0.0;
  double height = 0.0;
};

PdfTextBBox MeasureTextBBox(const std::string& text, double size)
{
  PdfTextBBox bbox;
  bbox.minX = 0.0;
  bbox.maxX = TextWidth(text, size);
  bbox.minY = -0.75 * size;
  bbox.maxY = 0.25 * size;
  bbox.width = bbox.maxX - bbox.minX;
  bbox.height = bbox.maxY - bbox.minY;
  return bbox;
}

double CenterBaselineForTextY(double centerY, const PdfTextBBox& bbox)
{
  return centerY - 0.5 * (bbox.minY + bbox.maxY);
}

double ClampTextBaselineY(double baseline,
                          const PdfTextBBox& bbox,
                          double topLimit,
                          double bottomLimit)
{
  const double minBaseline = topLimit - bbox.minY;
  const double maxBaseline = bottomLimit - bbox.maxY;
  if (minBaseline > maxBaseline) {
    return 0.5 * (minBaseline + maxBaseline);
  }
  return std::clamp(baseline, minBaseline, maxBaseline);
}

double ClampTextCenterX(double centerX,
                        const PdfTextBBox& bbox,
                        double leftLimit,
                        double rightLimit)
{
  const double halfWidth = 0.5 * bbox.width;
  if (leftLimit + halfWidth > rightLimit - halfWidth) {
    return 0.5 * (leftLimit + rightLimit);
  }
  return std::clamp(centerX, leftLimit + halfWidth, rightLimit - halfWidth);
}

std::vector<double> GenerateTicks(double minVal, double maxVal, int desired)
{
  std::vector<double> ticks;
  if (!(maxVal > minVal) || desired <= 0) return ticks;
  const double rawStep = (maxVal - minVal) / static_cast<double>(desired - 1);
  const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
  const double norm = rawStep / mag;
  double nice = 1.0;
  if (norm <= 1.0) nice = 1.0;
  else if (norm <= 2.0) nice = 2.0;
  else if (norm <= 5.0) nice = 5.0;
  else nice = 10.0;
  const double step = nice * mag;
  const double first = std::ceil(minVal / step) * step;
  for (double v = first; v <= maxVal + 0.5 * step; v += step) {
    if (v >= minVal - 1.0e-12 && v <= maxVal + 1.0e-12) {
      ticks.push_back(v);
    }
  }
  if (ticks.empty()) {
    ticks.push_back(minVal);
    ticks.push_back(maxVal);
  }
  return ticks;
}

std::string FormatTick(double value, bool inset)
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), inset ? "%.3g" : "%.4g", value);
  return buf;
}

PdfLayout ComputeLayout(const ProjectionMapParams& params,
                        const ProjectionMapRenderInfo& info)
{
  PdfLayout layout;
  layout.plotW = info.plotWidth;
  layout.plotH = info.plotHeight;
  const ProjectionColorBarPlacement placement = params.colorBarPlacement;
  layout.inset =
    placement == ProjectionColorBarPlacement::InsetVertical ||
    placement == ProjectionColorBarPlacement::InsetHorizontal ||
    placement == ProjectionColorBarPlacement::Custom;
  layout.horizontal =
    placement == ProjectionColorBarPlacement::Top ||
    placement == ProjectionColorBarPlacement::Bottom ||
    placement == ProjectionColorBarPlacement::InsetHorizontal ||
    (placement == ProjectionColorBarPlacement::Custom &&
     params.colorBarCustomHorizontal);
  layout.showLegend = !layout.inset;
  layout.tickFontSize =
    std::max(1, static_cast<int>((layout.inset ? 0.055f : 0.08f) *
                                 static_cast<float>(layout.plotH)));
  layout.labelFontSize =
    std::max(1, static_cast<int>(0.10f * static_cast<float>(layout.plotH)));

  const std::vector<double> ticks =
    GenerateTicks(info.colorMinVal, info.colorMaxVal, layout.inset ? 3 : 5);
  double maxTickW = 0.0;
  double minTickTextY = 0.0;
  double maxTickTextY = 0.0;
  bool hasTickTextBBox = false;
  for (double tick : ticks) {
    const PdfTextBBox bbox =
      MeasureTextBBox(FormatTick(tick, layout.inset), layout.tickFontSize);
    maxTickW = std::max(maxTickW, bbox.width);
    if (!hasTickTextBBox) {
      minTickTextY = bbox.minY;
      maxTickTextY = bbox.maxY;
      hasTickTextBBox = true;
    } else {
      minTickTextY = std::min(minTickTextY, bbox.minY);
      maxTickTextY = std::max(maxTickTextY, bbox.maxY);
    }
  }
  const int padding = 4;
  const int ticksWidth = static_cast<int>(std::ceil(maxTickW)) + 2 * padding;
  const int ticksHeight =
    static_cast<int>(std::ceil(hasTickTextBBox
                                 ? maxTickTextY - minTickTextY
                                 : static_cast<double>(layout.tickFontSize))) +
    2 * padding;
  const int labelWidth = layout.showLegend ? layout.labelFontSize + 2 * padding : 0;
  const int labelHeight = layout.showLegend ? layout.labelFontSize + 2 * padding : 0;
  const int barThickness = std::max(4, static_cast<int>(0.07f * layout.plotW));
  const int insetTextGap = std::max(2, padding);

  layout.pageW = layout.plotW;
  layout.pageH = layout.plotH;
  layout.plotX0 = 0;
  layout.plotY0 = 0;
  layout.barW = barThickness;
  layout.barH = layout.plotH;

  if (placement == ProjectionColorBarPlacement::Left) {
    layout.pageW = labelWidth + ticksWidth + barThickness + layout.plotW;
    layout.plotX0 = labelWidth + ticksWidth + barThickness;
    layout.barX0 = labelWidth + ticksWidth;
    layout.barY0 = 0;
    layout.tickLabelX = labelWidth + ticksWidth / 2;
    layout.titleX = labelWidth / 2;
    layout.titleY = layout.plotH / 2;
  } else if (placement == ProjectionColorBarPlacement::Top) {
    layout.pageH = labelHeight + ticksHeight + barThickness + layout.plotH;
    layout.plotY0 = labelHeight + ticksHeight + barThickness;
    layout.barX0 = 0;
    layout.barY0 = labelHeight + ticksHeight;
    layout.barW = layout.plotW;
    layout.barH = barThickness;
    layout.tickLabelY = static_cast<int>(std::lround(layout.barY0 -
                                                     padding -
                                                     maxTickTextY));
    layout.titleX = layout.plotW / 2;
    layout.titleY = labelHeight / 2;
  } else if (placement == ProjectionColorBarPlacement::Bottom) {
    layout.pageH = layout.plotH + barThickness + ticksHeight + labelHeight;
    layout.barX0 = 0;
    layout.barY0 = layout.plotH;
    layout.barW = layout.plotW;
    layout.barH = barThickness;
    layout.tickLabelY = static_cast<int>(std::lround(layout.barY0 +
                                                     layout.barH +
                                                     padding -
                                                     minTickTextY));
    layout.titleX = layout.plotW / 2;
    layout.titleY = layout.barY0 + layout.barH + ticksHeight + labelHeight / 2;
  } else if (layout.inset) {
    const float insetX = placement == ProjectionColorBarPlacement::Custom
      ? params.colorBarInsetX
      : (layout.horizontal ? 0.0f : 1.0f);
    const float insetY = placement == ProjectionColorBarPlacement::Custom
      ? params.colorBarInsetY
      : (layout.horizontal ? 1.0f : 0.0f);
    const float insetLength = placement == ProjectionColorBarPlacement::Custom
      ? params.colorBarInsetLength
      : 0.34f;
    const float insetThickness = placement == ProjectionColorBarPlacement::Custom
      ? params.colorBarInsetThickness
      : 0.035f;
    if (layout.horizontal) {
      layout.barW = std::max(20,
                             static_cast<int>(std::lround(insetLength *
                                                          layout.plotW)));
      layout.barH = std::max(4,
                             static_cast<int>(std::lround(insetThickness *
                                                          layout.plotH)));
      const int totalW = layout.barW + ticksWidth;
      const int totalH = layout.barH + ticksHeight + labelHeight;
      const int maxX = std::max(0, layout.plotW - totalW - padding);
      const int maxY = std::max(0, layout.plotH - totalH - padding);
      const int contentX0 = padding +
        static_cast<int>(std::lround(insetX * maxX));
      layout.barX0 = contentX0 + ticksWidth / 2;
      layout.barY0 = padding +
        static_cast<int>(std::lround(insetY * maxY));
      layout.tickLabelY = static_cast<int>(std::lround(layout.barY0 +
                                                       layout.barH +
                                                       insetTextGap -
                                                       minTickTextY));
      layout.titleX = layout.barX0 + layout.barW / 2;
      layout.titleY = layout.barY0 + layout.barH + ticksHeight + labelHeight / 2;
    } else {
      layout.barH = std::max(20,
                             static_cast<int>(std::lround(insetLength *
                                                          layout.plotH)));
      layout.barW = std::max(4,
                             static_cast<int>(std::lround(insetThickness *
                                                          layout.plotW)));
      const int totalW = layout.barW + ticksWidth + labelWidth;
      const int totalH = layout.barH;
      const int maxX = std::max(0, layout.plotW - totalW - padding);
      const int maxY = std::max(0, layout.plotH - totalH - padding);
      layout.barX0 = padding +
        static_cast<int>(std::lround(insetX * maxX));
      layout.barY0 = padding +
        static_cast<int>(std::lround(insetY * maxY));
      layout.tickLabelX = layout.barX0 + layout.barW + ticksWidth / 2;
      layout.titleX = layout.barX0 + layout.barW + ticksWidth + labelWidth / 2;
      layout.titleY = layout.barY0 + layout.barH / 2;
    }
  } else {
    layout.pageW = layout.plotW + barThickness + ticksWidth + labelWidth;
    layout.barX0 = layout.plotW;
    layout.barY0 = 0;
    layout.barW = barThickness;
    layout.barH = layout.plotH;
    layout.tickLabelX = layout.barX0 + layout.barW + ticksWidth / 2;
    layout.titleX = layout.barX0 + layout.barW + ticksWidth + labelWidth / 2;
    layout.titleY = layout.plotH / 2;
  }

  return layout;
}

PdfLayout ShiftPanelLayout(PdfLayout layout,
                           const ProjectionMapRenderInfo& panel,
                           int pageW,
                           int pageH)
{
  layout.pageW = pageW;
  layout.pageH = pageH;
  layout.plotX0 += panel.tileOffsetX;
  layout.plotY0 += panel.tileOffsetY;
  layout.barX0 += panel.tileOffsetX;
  layout.barY0 += panel.tileOffsetY;
  layout.tickLabelX += panel.tileOffsetX;
  layout.tickLabelY += panel.tileOffsetY;
  layout.titleX += panel.tileOffsetX;
  layout.titleY += panel.tileOffsetY;
  return layout;
}

double PdfY(const PdfLayout& layout, double topY)
{
  return static_cast<double>(layout.pageH) - topY;
}

void SetRgb(std::ostringstream& out, double v)
{
  out << v << " " << v << " " << v << " rg\n";
}

void SetRgb(std::ostringstream& out, double r, double g, double b)
{
  out << r << " " << g << " " << b << " rg\n"
      << r << " " << g << " " << b << " RG\n";
}

void FillRectTop(std::ostringstream& out,
                 const PdfLayout& layout,
                 double x,
                 double y,
                 double w,
                 double h)
{
  out << x << " " << (PdfY(layout, y) - h) << " "
      << w << " " << h << " re f\n";
}

void DrawLineTop(std::ostringstream& out,
                 const PdfLayout& layout,
                 double x0,
                 double y0,
                 double x1,
                 double y1,
                 double width)
{
  out << width << " w\n"
      << x0 << " " << PdfY(layout, y0) << " m "
      << x1 << " " << PdfY(layout, y1) << " l S\n";
}

void DrawText(std::ostringstream& out,
              const PdfLayout& layout,
              const std::string& text,
              double centerX,
              double baselineYTop,
              double size,
              bool rotated90 = false)
{
  const std::string escaped = PdfEscape(text);
  out << "BT /F1 " << size << " Tf\n";
  if (rotated90) {
    const double x = centerX + size * 0.35;
    const double y = PdfY(layout, baselineYTop + TextWidth(text, size) * 0.5);
    out << "0 1 -1 0 " << x << " " << y << " Tm\n";
  } else {
    const double x = centerX - TextWidth(text, size) * 0.5;
    const double y = PdfY(layout, baselineYTop);
    out << "1 0 0 1 " << x << " " << y << " Tm\n";
  }
  out << "(" << escaped << ") Tj ET\n";
}

void DrawTextAtOrigin(std::ostringstream& out,
                      const PdfLayout& layout,
                      const std::string& text,
                      double originX,
                      double baselineYTop,
                      double size)
{
  const std::string escaped = PdfEscape(text);
  out << "BT /F1 " << size << " Tf\n"
      << "1 0 0 1 " << originX << " " << PdfY(layout, baselineYTop)
      << " Tm\n"
      << "(" << escaped << ") Tj ET\n";
}

void DrawPdfCircle(std::ostringstream& out,
                   const PdfLayout& layout,
                   double cx,
                   double cyTop,
                   double radius,
                   bool fill)
{
  constexpr double k = 0.5522847498307936;
  const double cy = PdfY(layout, cyTop);
  out << (cx + radius) << " " << cy << " m\n"
      << (cx + radius) << " " << (cy + k * radius) << " "
      << (cx + k * radius) << " " << (cy + radius) << " "
      << cx << " " << (cy + radius) << " c\n"
      << (cx - k * radius) << " " << (cy + radius) << " "
      << (cx - radius) << " " << (cy + k * radius) << " "
      << (cx - radius) << " " << cy << " c\n"
      << (cx - radius) << " " << (cy - k * radius) << " "
      << (cx - k * radius) << " " << (cy - radius) << " "
      << cx << " " << (cy - radius) << " c\n"
      << (cx + k * radius) << " " << (cy - radius) << " "
      << (cx + radius) << " " << (cy - k * radius) << " "
      << (cx + radius) << " " << cy << " c\n"
      << (fill ? "f\n" : "S\n");
}

void DrawPdfPolygon(std::ostringstream& out,
                    const PdfLayout& layout,
                    const std::vector<std::pair<double, double>>& points)
{
  if (points.empty()) return;
  out << points[0].first << " " << PdfY(layout, points[0].second) << " m\n";
  for (std::size_t i = 1; i < points.size(); ++i) {
    out << points[i].first << " " << PdfY(layout, points[i].second) << " l\n";
  }
  out << "h f\n";
}

void DrawStarOverlaySampleVector(
  std::ostringstream& out,
  const PdfLayout& layout,
  const ProjectionMapRenderInfo::StarOverlaySample& sample)
{
  const double x = layout.plotX0 + sample.x;
  const double y = layout.plotY0 + sample.y;
  const double radius = std::max(static_cast<double>(sample.sizePx) * 0.5, 0.5);
  const double thickness = std::max(1.0, radius * 0.35);
  out << (sample.alpha < 0.75f ? "/GS50 gs\n" : "/GS100 gs\n");
  SetRgb(out, sample.r, sample.g, sample.b);

  constexpr double pi = 3.14159265358979323846;
  if (sample.symbol == ProjectionParticleSymbol::FilledCircle ||
      sample.symbol == ProjectionParticleSymbol::SoftCircle) {
    DrawPdfCircle(out, layout, x, y, radius, true);
  } else if (sample.symbol == ProjectionParticleSymbol::Ring) {
    out << thickness << " w\n";
    DrawPdfCircle(out, layout, x, y, radius, false);
  } else if (sample.symbol == ProjectionParticleSymbol::Star) {
    std::vector<std::pair<double, double>> pts;
    pts.reserve(10);
    for (int i = 0; i < 10; ++i) {
      const double angle = -0.5 * pi + static_cast<double>(i) * pi / 5.0;
      const double rr = (i % 2 == 0) ? radius : radius * 0.45;
      pts.emplace_back(x + std::cos(angle) * rr,
                       y + std::sin(angle) * rr);
    }
    DrawPdfPolygon(out, layout, pts);
  } else if (sample.symbol == ProjectionParticleSymbol::Diamond) {
    DrawPdfPolygon(out,
                   layout,
                   {{x, y - radius},
                    {x + radius, y},
                    {x, y + radius},
                    {x - radius, y}});
  } else if (sample.symbol == ProjectionParticleSymbol::Square) {
    FillRectTop(out, layout, x - radius, y - radius, 2.0 * radius, 2.0 * radius);
  } else {
    out << thickness << " w\n";
    const int spokes =
      sample.symbol == ProjectionParticleSymbol::FiveSpokeStar ? 5 :
      sample.symbol == ProjectionParticleSymbol::Asterisk ? 8 :
      sample.symbol == ProjectionParticleSymbol::Plus ? 4 :
      sample.symbol == ProjectionParticleSymbol::Cross ? 4 : 8;
    const double start =
      sample.symbol == ProjectionParticleSymbol::Cross ? 0.25 * pi : -0.5 * pi;
    for (int i = 0; i < spokes; ++i) {
      double angle = start + static_cast<double>(i) * 2.0 * pi / spokes;
      if (sample.symbol == ProjectionParticleSymbol::Plus) {
        angle = static_cast<double>(i) * 0.5 * pi;
      } else if (sample.symbol == ProjectionParticleSymbol::Cross) {
        angle = 0.25 * pi + static_cast<double>(i) * 0.5 * pi;
      }
      DrawLineTop(out,
                  layout,
                  x,
                  y,
                  x + std::cos(angle) * radius,
                  y + std::sin(angle) * radius,
                  thickness);
    }
  }
}

void AddVectorAnnotations(std::ostringstream& out,
                          const ProjectionMapParams& params,
                          const ProjectionMapContext& ctx,
                          const ProjectionMapRenderInfo& info,
                          const PdfLayout& layout)
{
  const double bg = params.whiteBackground ? 1.0 : 0.0;
  const double fg = params.whiteBackground ? 0.0 : 1.0;
  const double denom = std::max(static_cast<double>(info.colorMaxVal - info.colorMinVal),
                                1.0e-30);
  const std::vector<double> ticks =
    GenerateTicks(info.colorMinVal, info.colorMaxVal, layout.inset ? 3 : 5);

  if (layout.inset) {
    const double pad = 4.0;
    double bgX0 = layout.barX0;
    double bgY0 = layout.barY0;
    double bgX1 = layout.barX0 + layout.barW;
    double bgY1 = layout.barY0 + layout.barH;
    for (double tick : ticks) {
      if (tick < info.colorMinVal || tick > info.colorMaxVal) continue;
      const double frac = (tick - info.colorMinVal) / denom;
      const std::string label = FormatTick(tick, layout.inset);
      const PdfTextBBox bbox = MeasureTextBBox(label, layout.tickFontSize);
      if (layout.horizontal) {
        const double x = layout.barX0 + frac * std::max(1, layout.barW - 1);
        const double labelX =
          ClampTextCenterX(x, bbox, 0.0, static_cast<double>(layout.pageW));
        const double labelY =
          ClampTextBaselineY(layout.tickLabelY,
                             bbox,
                             0.0,
                             static_cast<double>(layout.pageH));
        const double originX = labelX - 0.5 * (bbox.minX + bbox.maxX);
        bgX0 = std::min(bgX0, originX + bbox.minX);
        bgX1 = std::max(bgX1, originX + bbox.maxX);
        bgY0 = std::min(bgY0, labelY + bbox.minY);
        bgY1 = std::max(bgY1, labelY + bbox.maxY);
      } else {
        const double y =
          layout.barY0 + (1.0 - frac) * std::max(1, layout.barH - 1);
        const double labelY =
          ClampTextBaselineY(CenterBaselineForTextY(y, bbox),
                             bbox,
                             0.0,
                             static_cast<double>(layout.pageH));
        const double originX =
          static_cast<double>(layout.tickLabelX) -
          0.5 * (bbox.minX + bbox.maxX);
        bgX0 = std::min(bgX0, originX + bbox.minX);
        bgX1 = std::max(bgX1, originX + bbox.maxX);
        bgY0 = std::min(bgY0, labelY + bbox.minY);
        bgY1 = std::max(bgY1, labelY + bbox.maxY);
      }
    }
    out << "/GS55 gs\n";
    SetRgb(out, bg);
    FillRectTop(out,
                layout,
                bgX0 - pad,
                bgY0 - pad,
                bgX1 - bgX0 + 2.0 * pad,
                bgY1 - bgY0 + 2.0 * pad);
    out << "/GS100 gs\n";
  }

  const int steps = std::max(192, layout.horizontal ? layout.barW : layout.barH);
  for (int i = 0; i < steps; ++i) {
    const float t0 = steps > 1
      ? static_cast<float>(i) / static_cast<float>(steps - 1)
      : 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    ProjectionMapGenerator::colormapLookup(t0,
                                           r,
                                           g,
                                           b,
                                           ctx.colorMap,
                                           ctx.colorMapSize);
    out << r << " " << g << " " << b << " rg\n";
    if (layout.horizontal) {
      const double x0 = layout.barX0 +
        static_cast<double>(i) * layout.barW / steps;
      const double x1 = layout.barX0 +
        static_cast<double>(i + 1) * layout.barW / steps;
      FillRectTop(out, layout, x0, layout.barY0, x1 - x0 + 1.0, layout.barH);
    } else {
      const double y0 = layout.barY0 +
        static_cast<double>(steps - 1 - i) * layout.barH / steps;
      const double y1 = layout.barY0 +
        static_cast<double>(steps - i) * layout.barH / steps;
      FillRectTop(out, layout, layout.barX0, y0, layout.barW, y1 - y0 + 1.0);
    }
  }

  SetRgb(out, fg);
  for (double tick : ticks) {
    if (tick < info.colorMinVal || tick > info.colorMaxVal) continue;
    const double frac = (tick - info.colorMinVal) / denom;
    const std::string label = FormatTick(tick, layout.inset);
    const PdfTextBBox bbox = MeasureTextBBox(label, layout.tickFontSize);
    if (layout.horizontal) {
      const double x = layout.barX0 + frac * std::max(1, layout.barW - 1);
      if (params.colorBarPlacement == ProjectionColorBarPlacement::Top) {
        DrawLineTop(out, layout, x, layout.barY0, x,
                    layout.barY0 + std::min(10, layout.barH), 1.0);
      } else {
        DrawLineTop(out, layout, x,
                    layout.barY0 + std::max(0, layout.barH - 10),
                    x, layout.barY0 + layout.barH, 1.0);
      }
      DrawText(out,
               layout,
               label,
               ClampTextCenterX(x,
                                bbox,
                                0.0,
                                static_cast<double>(layout.pageW)),
               ClampTextBaselineY(layout.tickLabelY,
                                  bbox,
                                  0.0,
                                  static_cast<double>(layout.pageH)),
               layout.tickFontSize);
    } else {
      const double y = layout.barY0 + (1.0 - frac) * std::max(1, layout.barH - 1);
      if (params.colorBarPlacement == ProjectionColorBarPlacement::Left) {
        DrawLineTop(out, layout, layout.barX0, y,
                    layout.barX0 + std::min(10, layout.barW), y, 1.0);
      } else {
        DrawLineTop(out, layout,
                    layout.barX0 + std::max(0, layout.barW - 10), y,
                    layout.barX0 + layout.barW, y, 1.0);
      }
      DrawText(out,
               layout,
               label,
               layout.tickLabelX,
               ClampTextBaselineY(CenterBaselineForTextY(y, bbox),
                                  bbox,
                                  0.0,
                                  static_cast<double>(layout.pageH)),
               layout.tickFontSize);
    }
  }

  if (layout.showLegend && layout.horizontal) {
    DrawText(out, layout, info.colorBarLabel, layout.titleX, layout.titleY,
             layout.labelFontSize);
  } else if (layout.showLegend) {
    DrawText(out, layout, info.colorBarLabel, layout.titleX, layout.titleY,
             layout.labelFontSize, true);
  }

  if (params.flagTimeLabel) {
    char timeStr[255];
    double t = ctx.time * params.factorShownTimeInUnitTime;
    if (params.flagUseRedshift) {
      t = 1.0 / ctx.time - 1.0;
    }
    std::snprintf(timeStr, sizeof(timeStr), params.timeFormatBuf, t);
    const std::string label = timeStr;
    const double size =
      std::max(1.0, static_cast<double>(layout.labelFontSize) * 0.78);
    const PdfTextBBox bbox = MeasureTextBBox(label, size);
    const double offsetX = params.flagAdjustTimeLabelPosition
      ? params.timeLabelOffsetX
      : 0.0f;
    const double offsetY = params.flagAdjustTimeLabelPosition
      ? params.timeLabelOffsetY
      : 0.0f;
    const double baseX = layout.plotX0 + 10.0 + offsetX;
    const double baseY = layout.plotY0 + 10.0 + offsetY;
    out << "/GS50 gs\n";
    SetRgb(out, bg);
    FillRectTop(out, layout, baseX - 4.0, baseY - 4.0,
                bbox.width + 8.0, bbox.height + 8.0);
    out << "/GS100 gs\n";
    SetRgb(out, fg);
    DrawTextAtOrigin(out,
                     layout,
                     label,
                     baseX - bbox.minX,
                     baseY - bbox.minY,
                     size);
  }

  if (params.flagPlaceScale) {
    const double lengthPx = info.cellSize > 0.0
      ? static_cast<double>(params.scaleBarLength) / info.cellSize
      : 0.0;
    const double offsetX = params.flagAdjustScaleBarPosition
      ? params.scaleBarOffsetX
      : 0.0f;
    const double offsetY = params.flagAdjustScaleBarPosition
      ? params.scaleBarOffsetY
      : 0.0f;
    const double centerX = layout.plotX0 + layout.plotW / 2.0 + offsetX;
    const double x0 = centerX - lengthPx / 2.0;
    const double x1 = x0 + lengthPx;
    const double y = layout.plotY0 + layout.plotH - 30.0 + offsetY;
    const double thickness = std::clamp(params.scaleBarThickness, 1, 64);
    const std::string label = params.arrowLabelStr;
    const double size = layout.labelFontSize;
    const PdfTextBBox bbox = MeasureTextBBox(label, size);
    const double textW = bbox.width;
    const double labelBaselineY = y - thickness * 0.5 - 10.0;
    const double overlayX0 = std::min(x0, centerX - textW * 0.5) - 4.0;
    const double overlayX1 = std::max(x1, centerX + textW * 0.5) + 4.0;
    const double overlayY0 =
      std::min(y - thickness * 0.5, labelBaselineY + bbox.minY) - 4.0;
    const double overlayY1 =
      std::max(y + thickness * 0.5, labelBaselineY + bbox.maxY) + 4.0;
    out << "/GS50 gs\n";
    SetRgb(out, bg);
    FillRectTop(out, layout, overlayX0, overlayY0,
                overlayX1 - overlayX0, overlayY1 - overlayY0);
    out << "/GS100 gs\n";
    SetRgb(out, fg);
    FillRectTop(out, layout, x0, y - thickness * 0.5, lengthPx, thickness);
    DrawText(out, layout, label, centerX, labelBaselineY, size);
  }

  for (const ProjectionMapRenderInfo::StarOverlaySample& sample :
       info.starOverlaySamples) {
    DrawStarOverlaySampleVector(out, layout, sample);
  }
}

class PdfBuilder {
public:
  int addObject(std::string data) {
    objects_.push_back(std::move(data));
    return static_cast<int>(objects_.size());
  }

  bool write(const std::string& path, int rootObject) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::streamoff> offsets;
    offsets.reserve(objects_.size() + 1);
    offsets.push_back(0);
    for (std::size_t i = 0; i < objects_.size(); ++i) {
      offsets.push_back(out.tellp());
      out << (i + 1) << " 0 obj\n" << objects_[i] << "\nendobj\n";
    }
    const std::streamoff xref = out.tellp();
    out << "xref\n0 " << (objects_.size() + 1) << "\n";
    out << "0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
      out << std::setw(10) << std::setfill('0') << offsets[i]
          << " 00000 n \n";
    }
    out << std::setfill(' ');
    out << "trailer\n<< /Size " << (objects_.size() + 1)
        << " /Root " << rootObject << " 0 R >>\n";
    out << "startxref\n" << xref << "\n%%EOF\n";
    return true;
  }

private:
  std::vector<std::string> objects_;
};

bool WriteImageOnlyPdf(const std::string& path, const RgbImage& image)
{
  PdfBuilder pdf;
  std::string imageStream;
  imageStream.assign(reinterpret_cast<const char*>(image.rgb.data()),
                     image.rgb.size());
  std::ostringstream imageObj;
  imageObj << "<< /Type /XObject /Subtype /Image /Width " << image.width
           << " /Height " << image.height
           << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length "
           << imageStream.size() << " >>\nstream\n"
           << imageStream << "\nendstream";
  const int imageId = pdf.addObject(imageObj.str());

  std::ostringstream content;
  content << "q\n"
          << image.width << " 0 0 " << image.height
          << " 0 0 cm\n/Im0 Do\nQ\n";
  const std::string contentText = content.str();
  std::ostringstream contentObj;
  contentObj << "<< /Length " << contentText.size() << " >>\nstream\n"
             << contentText << "\nendstream";
  const int contentId = pdf.addObject(contentObj.str());

  const int pagesId = 4;
  std::ostringstream pageObj;
  pageObj << "<< /Type /Page /Parent " << pagesId
          << " 0 R /MediaBox [0 0 " << image.width << " " << image.height
          << "] /Resources << /XObject << /Im0 " << imageId
          << " 0 R >> >> /Contents " << contentId << " 0 R >>";
  const int pageId = pdf.addObject(pageObj.str());

  std::ostringstream pagesObj;
  pagesObj << "<< /Type /Pages /Kids [" << pageId
           << " 0 R] /Count 1 >>";
  const int actualPagesId = pdf.addObject(pagesObj.str());
  (void)actualPagesId;

  std::ostringstream catalogObj;
  catalogObj << "<< /Type /Catalog /Pages " << pagesId << " 0 R >>";
  const int catalogId = pdf.addObject(catalogObj.str());
  return pdf.write(path, catalogId);
}

} // namespace

bool WriteProjectionPdf(const std::string& path,
                        const RgbImage& plotImage,
                        const ProjectionMapParams& params,
                        const ProjectionMapContext& ctx,
                        const ProjectionMapRenderInfo& info)
{
  if (path.empty() || !plotImage.valid()) return false;
  if (!info.valid) {
    return WriteImageOnlyPdf(path, plotImage);
  }

  const ProjectionMapParams& singleParams =
    info.panels.empty() ? info.params : params;
  const ProjectionMapContext& singleCtx =
    info.panels.empty() ? info.ctx : ctx;
  PdfLayout layout = info.panels.empty()
    ? ComputeLayout(singleParams, info)
    : PdfLayout{};
  if (!info.panels.empty()) {
    layout.pageW = info.pageWidth > 0 ? info.pageWidth : plotImage.width;
    layout.pageH = info.pageHeight > 0 ? info.pageHeight : plotImage.height;
    layout.plotW = plotImage.width;
    layout.plotH = plotImage.height;
  }
  PdfBuilder pdf;

  std::string imageStream;
  imageStream.assign(reinterpret_cast<const char*>(plotImage.rgb.data()),
                     plotImage.rgb.size());
  std::ostringstream imageObj;
  imageObj << "<< /Type /XObject /Subtype /Image /Width " << plotImage.width
           << " /Height " << plotImage.height
           << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length "
           << imageStream.size() << " >>\nstream\n"
           << imageStream << "\nendstream";
  const int imageId = pdf.addObject(imageObj.str()); // 1

  const int fontId =
    pdf.addObject("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"); // 2
  const int gs50Id =
    pdf.addObject("<< /Type /ExtGState /ca 0.50 /CA 0.50 >>"); // 3
  const int gs55Id =
    pdf.addObject("<< /Type /ExtGState /ca 0.55 /CA 0.55 >>"); // 4
  const int gs100Id =
    pdf.addObject("<< /Type /ExtGState /ca 1 /CA 1 >>"); // 5

  std::ostringstream content;
  content << std::fixed << std::setprecision(4);
  const double bg = singleParams.whiteBackground ? 1.0 : 0.0;
  SetRgb(content, bg);
  FillRectTop(content, layout, 0, 0, layout.pageW, layout.pageH);
  content << "q\n"
          << plotImage.width << " 0 0 " << plotImage.height << " "
          << (info.panels.empty() ? layout.plotX0 : 0) << " "
          << (info.panels.empty()
                ? (layout.pageH - layout.plotY0 - plotImage.height)
                : 0)
          << " cm\n/Im0 Do\nQ\n";
  if (info.panels.empty()) {
    AddVectorAnnotations(content, singleParams, singleCtx, info, layout);
  } else {
    const int pageW = layout.pageW;
    const int pageH = layout.pageH;
    for (const ProjectionMapRenderInfo& panel : info.panels) {
      if (!panel.valid) continue;
      PdfLayout panelLayout =
        ShiftPanelLayout(ComputeLayout(panel.params, panel),
                         panel,
                         pageW,
                         pageH);
      AddVectorAnnotations(content,
                           panel.params,
                           panel.ctx,
                           panel,
                           panelLayout);
    }
  }
  const std::string contentText = content.str();
  std::ostringstream contentObj;
  contentObj << "<< /Length " << contentText.size() << " >>\nstream\n"
             << contentText << "\nendstream";
  const int contentId = pdf.addObject(contentObj.str()); // 6

  const int pagesId = 8;
  std::ostringstream pageObj;
  pageObj << "<< /Type /Page /Parent " << pagesId
          << " 0 R /MediaBox [0 0 "
          << layout.pageW << " " << layout.pageH
          << "] /Resources << /XObject << /Im0 " << imageId
          << " 0 R >> /Font << /F1 " << fontId
          << " 0 R >> /ExtGState << /GS50 " << gs50Id
          << " 0 R /GS55 " << gs55Id
          << " 0 R /GS100 " << gs100Id
          << " 0 R >> >> /Contents " << contentId << " 0 R >>";
  const int pageId = pdf.addObject(pageObj.str()); // 7

  std::ostringstream pagesObj;
  pagesObj << "<< /Type /Pages /Kids [" << pageId
           << " 0 R] /Count 1 >>";
  const int actualPagesId = pdf.addObject(pagesObj.str()); // 8
  (void)actualPagesId;
  std::ostringstream catalogObj;
  catalogObj << "<< /Type /Catalog /Pages " << pagesId << " 0 R >>";
  const int catalogId = pdf.addObject(catalogObj.str()); // 9
  return pdf.write(path, catalogId);
}
