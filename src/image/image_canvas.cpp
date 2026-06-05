#include "image/image_canvas.h"
#include "image/rgb_image.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <array>
#include <limits>

namespace {
float DistanceToSegment(float px,
                        float py,
                        float x0,
                        float y0,
                        float x1,
                        float y1)
{
  const float vx = x1 - x0;
  const float vy = y1 - y0;
  const float wx = px - x0;
  const float wy = py - y0;
  const float len2 = vx * vx + vy * vy;
  if (len2 <= 0.0f) {
    const float dx = px - x0;
    const float dy = py - y0;
    return std::sqrt(dx * dx + dy * dy);
  }

  const float t = std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f);
  const float cx = x0 + t * vx;
  const float cy = y0 + t * vy;
  const float dx = px - cx;
  const float dy = py - cy;
  return std::sqrt(dx * dx + dy * dy);
}
} // namespace

ImageCanvas::ImageCanvas(std::vector<unsigned char>& rgb,
                         int width,
                         int height)
  : rgb_(rgb),
    width_(width),
    height_(height)
{
}

int ImageCanvas::width() const
{
  return width_;
}

int ImageCanvas::height() const
{
  return height_;
}

std::vector<unsigned char>& ImageCanvas::pixels() const{
  return rgb_;
}

void ImageCanvas::resizeKeepContent(int newWidth, int newHeight, unsigned char value)
{
  resizeKeepContentAt(newWidth, newHeight, 0, 0, value);
}

void ImageCanvas::resizeKeepContentAt(int newWidth,
                                      int newHeight,
                                      int dstX,
                                      int dstY,
                                      unsigned char value)
{
  std::vector<unsigned char> newRgb;
  newRgb.resize(static_cast<size_t>(newWidth) * newHeight * 3, value);

  const int copyWidth = std::min(width_, newWidth - dstX);
  const int copyHeight = std::min(height_, newHeight - dstY);

  for (int y = 0; y < copyHeight; ++y) {
    const int outY = dstY + y;
    if (outY < 0 || outY >= newHeight) continue;
    for (int x = 0; x < copyWidth; ++x) {
      const int outX = dstX + x;
      if (outX < 0 || outX >= newWidth) continue;

      const int oldIdx = 3 * (y * width_ + x);
      const int newIdx = 3 * (outY * newWidth + outX);

      newRgb[newIdx + 0] = rgb_[oldIdx + 0];
      newRgb[newIdx + 1] = rgb_[oldIdx + 1];
      newRgb[newIdx + 2] = rgb_[oldIdx + 2];
    }
  }

  rgb_.swap(newRgb);
  width_ = newWidth;
  height_ = newHeight;
}

void ImageCanvas::setPixel(int x,
                           int y,
                           unsigned char r,
                           unsigned char g,
                           unsigned char b)
{
  if (x < 0 || x >= width_ || y < 0 || y >= height_) return;

  const int idx = 3 * (y * width_ + x);
  rgb_[idx + 0] = r;
  rgb_[idx + 1] = g;
  rgb_[idx + 2] = b;
}

void ImageCanvas::blendPixel(int x,
                             int y,
                             unsigned char r,
                             unsigned char g,
                             unsigned char b,
                             float alpha)
{
  if (x < 0 || x >= width_ || y < 0 || y >= height_) return;

  alpha = std::clamp(alpha, 0.0f, 1.0f);

  const int idx = 3 * (y * width_ + x);

  rgb_[idx + 0] = static_cast<unsigned char>(
    r * alpha + rgb_[idx + 0] * (1.0f - alpha));

  rgb_[idx + 1] = static_cast<unsigned char>(
    g * alpha + rgb_[idx + 1] * (1.0f - alpha));

  rgb_[idx + 2] = static_cast<unsigned char>(
    b * alpha + rgb_[idx + 2] * (1.0f - alpha));
}

void ImageCanvas::drawAlphaMask(int x0,
                                int y0,
                                const unsigned char* alpha,
                                int maskWidth,
                                int maskHeight,
                                unsigned char r,
                                unsigned char g,
                                unsigned char b)
{
  if (!alpha || maskWidth <= 0 || maskHeight <= 0) return;

  for (int y = 0; y < maskHeight; ++y) {
    const int dstY = y0 + y;
    if (dstY < 0 || dstY >= height_) continue;

    for (int x = 0; x < maskWidth; ++x) {
      const int dstX = x0 + x;
      if (dstX < 0 || dstX >= width_) continue;

      const unsigned char a = alpha[y * maskWidth + x];
      if (a == 0) continue;

      blendPixel(dstX,
                 dstY,
                 r,
                 g,
                 b,
                 static_cast<float>(a) / 255.0f);
    }
  }
}

void ImageCanvas::fillRect(int x0,
                           int y0,
                           int x1,
                           int y1,
                           unsigned char r,
                           unsigned char g,
                           unsigned char b)
{
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);

  x0 = std::clamp(x0, 0, width_);
  x1 = std::clamp(x1, 0, width_);
  y0 = std::clamp(y0, 0, height_);
  y1 = std::clamp(y1, 0, height_);

  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      setPixel(x, y, r, g, b);
    }
  }
}

void ImageCanvas::blendRect(int x0,
                            int y0,
                            int x1,
                            int y1,
                            unsigned char r,
                            unsigned char g,
                            unsigned char b,
                            float alpha)
{
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);

  x0 = std::clamp(x0, 0, width_);
  x1 = std::clamp(x1, 0, width_);
  y0 = std::clamp(y0, 0, height_);
  y1 = std::clamp(y1, 0, height_);

  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      blendPixel(x, y, r, g, b, alpha);
    }
  }
}

void ImageCanvas::drawHorizontalLine(int x0,
                                     int x1,
                                     int y,
                                     unsigned char r,
                                     unsigned char g,
                                     unsigned char b)
{
  if (y < 0 || y >= height_) return;

  if (x0 > x1) std::swap(x0, x1);

  x0 = std::clamp(x0, 0, width_ - 1);
  x1 = std::clamp(x1, 0, width_ - 1);

  for (int x = x0; x <= x1; ++x) {
    setPixel(x, y, r, g, b);
  }
}

void ImageCanvas::drawVerticalLine(int x,
                                   int y0,
                                   int y1,
                                   unsigned char r,
                                   unsigned char g,
                                   unsigned char b)
{
  if (x < 0 || x >= width_) return;

  if (y0 > y1) std::swap(y0, y1);

  y0 = std::clamp(y0, 0, height_ - 1);
  y1 = std::clamp(y1, 0, height_ - 1);

  for (int y = y0; y <= y1; ++y) {
    setPixel(x, y, r, g, b);
  }
}


void ImageCanvas::drawLine(int x0,
                           int y0,
                           int x1,
                           int y1,
                           unsigned char r,
                           unsigned char g,
                           unsigned char b,
                           float alpha)
{
  alpha = std::clamp(alpha, 0.0f, 1.0f);

  const int dx = std::abs(x1 - x0);
  const int sx = (x0 < x1) ? 1 : -1;

  const int dy = -std::abs(y1 - y0);
  const int sy = (y0 < y1) ? 1 : -1;

  int err = dx + dy;

  while (true) {
    blendPixel(x0, y0, r, g, b, alpha);

    if (x0 == x1 && y0 == y1) break;

    const int e2 = 2 * err;

    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }

    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}


void ImageCanvas::drawAsterisk(int centerX,
                               int centerY,
                               int radius,
                               unsigned char r,
                               unsigned char g,
                               unsigned char b,
                               float alpha)
{
  if (radius <= 0) return;

  const float fr = static_cast<float>(radius);
  const float diag = fr * 0.70710678f;
  const float halfThickness = std::clamp(fr * 0.12f, 0.55f, 2.5f);
  const int extent =
    std::max(1, static_cast<int>(std::ceil(fr + halfThickness + 1.0f)));
  const float cx = static_cast<float>(centerX);
  const float cy = static_cast<float>(centerY);
  const std::array<std::array<float, 4>, 4> segments = {{
    {{cx - fr, cy, cx + fr, cy}},
    {{cx, cy - fr, cx, cy + fr}},
    {{cx - diag, cy - diag, cx + diag, cy + diag}},
    {{cx - diag, cy + diag, cx + diag, cy - diag}}
  }};

  for (int y = centerY - extent; y <= centerY + extent; ++y) {
    for (int x = centerX - extent; x <= centerX + extent; ++x) {
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      float minDist = std::numeric_limits<float>::max();
      for (const auto& segment : segments) {
        minDist = std::min(minDist,
                           DistanceToSegment(px,
                                             py,
                                             segment[0],
                                             segment[1],
                                             segment[2],
                                             segment[3]));
      }
      const float coverage =
        std::clamp(halfThickness + 0.75f - minDist, 0.0f, 1.0f);
      if (coverage <= 0.0f) continue;
      blendPixel(x, y, r, g, b, alpha * coverage);
    }
  }
}

void ImageCanvas::drawFilledCircle(int centerX,
                                   int centerY,
                                   float radius,
                                   unsigned char r,
                                   unsigned char g,
                                   unsigned char b,
                                   float alpha)
{
  if (radius <= 0.0f) return;

  const int extent = std::max(1, static_cast<int>(std::ceil(radius + 0.5f)));
  for (int dy = -extent; dy <= extent; ++dy) {
    for (int dx = -extent; dx <= extent; ++dx) {
      const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
      const float coverage = std::clamp(radius + 0.5f - dist, 0.0f, 1.0f);
      if (coverage <= 0.0f) continue;
      blendPixel(centerX + dx,
                 centerY + dy,
                 r,
                 g,
                 b,
                 alpha * coverage);
    }
  }
}

void ImageCanvas::drawCircleOutline(int centerX,
                                    int centerY,
                                    float radius,
                                    float thickness,
                                    unsigned char r,
                                    unsigned char g,
                                    unsigned char b,
                                    float alpha)
{
  if (radius <= 0.0f || thickness <= 0.0f) return;

  const float halfThickness = thickness * 0.5f;
  const int extent =
    std::max(1, static_cast<int>(std::ceil(radius + halfThickness + 0.5f)));
  for (int dy = -extent; dy <= extent; ++dy) {
    for (int dx = -extent; dx <= extent; ++dx) {
      const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
      const float coverage =
        std::clamp(halfThickness + 0.5f - std::abs(dist - radius),
                   0.0f,
                   1.0f);
      if (coverage <= 0.0f) continue;
      blendPixel(centerX + dx,
                 centerY + dy,
                 r,
                 g,
                 b,
                 alpha * coverage);
    }
  }
}

void ImageCanvas::drawSoftCircle(int centerX,
                                 int centerY,
                                 float radius,
                                 unsigned char r,
                                 unsigned char g,
                                 unsigned char b,
                                 float alpha)
{
  if (radius <= 0.0f) return;

  const int extent = std::max(1, static_cast<int>(std::ceil(radius)));
  const float invRadius = 1.0f / radius;
  for (int dy = -extent; dy <= extent; ++dy) {
    for (int dx = -extent; dx <= extent; ++dx) {
      const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
      const float x = dist * invRadius;
      if (x > 1.0f) continue;
      const float core = std::exp(-4.0f * x * x);
      const float edge = 1.0f - x * x;
      blendPixel(centerX + dx,
                 centerY + dy,
                 r,
                 g,
                 b,
                 alpha * core * edge);
    }
  }
}

void ImageCanvas::drawFivePointStar(int centerX,
                                    int centerY,
                                    float radius,
                                    unsigned char r,
                                    unsigned char g,
                                    unsigned char b,
                                    float alpha)
{
  if (radius <= 0.0f) return;

  constexpr float pi = 3.14159265358979323846f;
  std::array<float, 10> vx{};
  std::array<float, 10> vy{};
  const float innerRadius = radius * 0.45f;
  for (int i = 0; i < 10; ++i) {
    const float angle = -0.5f * pi + static_cast<float>(i) * pi / 5.0f;
    const float rr = (i % 2 == 0) ? radius : innerRadius;
    vx[static_cast<size_t>(i)] = centerX + std::cos(angle) * rr;
    vy[static_cast<size_t>(i)] = centerY + std::sin(angle) * rr;
  }

  const int extent = std::max(1, static_cast<int>(std::ceil(radius)));
  for (int y = centerY - extent; y <= centerY + extent; ++y) {
    for (int x = centerX - extent; x <= centerX + extent; ++x) {
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      bool inside = false;
      for (int i = 0, j = 9; i < 10; j = i++) {
        const bool crosses =
          ((vy[static_cast<size_t>(i)] > py) !=
           (vy[static_cast<size_t>(j)] > py)) &&
          (px <
           (vx[static_cast<size_t>(j)] - vx[static_cast<size_t>(i)]) *
             (py - vy[static_cast<size_t>(i)]) /
             (vy[static_cast<size_t>(j)] - vy[static_cast<size_t>(i)]) +
             vx[static_cast<size_t>(i)]);
        if (crosses) inside = !inside;
      }
      if (inside) {
        blendPixel(x, y, r, g, b, alpha);
      }
    }
  }
}

void ImageCanvas::drawPlus(int centerX,
                           int centerY,
                           int radius,
                           int thickness,
                           unsigned char r,
                           unsigned char g,
                           unsigned char b,
                           float alpha)
{
  if (radius <= 0 || thickness <= 0) return;

  const int half = std::max(0, thickness / 2);
  blendRect(centerX - radius,
            centerY - half,
            centerX + radius + 1,
            centerY + half + 1,
            r,
            g,
            b,
            alpha);
  blendRect(centerX - half,
            centerY - radius,
            centerX + half + 1,
            centerY + radius + 1,
            r,
            g,
            b,
            alpha);
}

void ImageCanvas::drawCross(int centerX,
                            int centerY,
                            int radius,
                            int thickness,
                            unsigned char r,
                            unsigned char g,
                            unsigned char b,
                            float alpha)
{
  if (radius <= 0 || thickness <= 0) return;

  const int half = std::max(0, thickness / 2);
  for (int offset = -half; offset <= half; ++offset) {
    drawLine(centerX - radius,
             centerY - radius + offset,
             centerX + radius,
             centerY + radius + offset,
             r,
             g,
             b,
             alpha);
    drawLine(centerX - radius,
             centerY + radius + offset,
             centerX + radius,
             centerY - radius + offset,
             r,
             g,
             b,
             alpha);
  }
}

void ImageCanvas::drawDiamond(int centerX,
                              int centerY,
                              int radius,
                              unsigned char r,
                              unsigned char g,
                              unsigned char b,
                              float alpha)
{
  if (radius <= 0) return;

  for (int dy = -radius; dy <= radius; ++dy) {
    const int halfWidth = radius - std::abs(dy);
    for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
      blendPixel(centerX + dx, centerY + dy, r, g, b, alpha);
    }
  }
}

void ImageCanvas::drawSquare(int centerX,
                             int centerY,
                             int radius,
                             unsigned char r,
                             unsigned char g,
                             unsigned char b,
                             float alpha)
{
  if (radius <= 0) return;

  blendRect(centerX - radius,
            centerY - radius,
            centerX + radius + 1,
            centerY + radius + 1,
            r,
            g,
            b,
            alpha);
}

void ImageCanvas::copyRgbImage(const std::vector<unsigned char>& src,
                               int srcWidth,
                               int srcHeight,
                               int dstX,
                               int dstY)
{
  if (srcWidth <= 0 || srcHeight <= 0) return;
  if (src.size() < static_cast<size_t>(srcWidth * srcHeight * 3)) return;

  for (int y = 0; y < srcHeight; ++y) {
    const int targetY = dstY + y;
    if (targetY < 0 || targetY >= height_) continue;

    for (int x = 0; x < srcWidth; ++x) {
      const int targetX = dstX + x;
      if (targetX < 0 || targetX >= width_) continue;

      const int srcIdx = 3 * (y * srcWidth + x);
      const int dstIdx = 3 * (targetY * width_ + targetX);

      rgb_[dstIdx + 0] = src[srcIdx + 0];
      rgb_[dstIdx + 1] = src[srcIdx + 1];
      rgb_[dstIdx + 2] = src[srcIdx + 2];
    }
  }
}

RgbImage ToRgbImage(const ImageCanvas& canvas, uint64_t version)
{
  RgbImage out;
  out.width = canvas.width();
  out.height = canvas.height();
  out.version = version;
  out.rgb = canvas.pixels(); // Copies if the API returns a const reference.
  return out;
}
