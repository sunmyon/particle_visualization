#include "image/image_canvas.h"
#include "image/rgb_image.h"

#include <algorithm>
#include <cstdlib>

ImageCanvas::ImageCanvas(TrackingVector<unsigned char>& rgb,
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

TrackingVector<unsigned char>& ImageCanvas::pixels() const{
  return rgb_;
}

void ImageCanvas::resizeKeepContent(int newWidth, int newHeight, unsigned char value)
{
  TrackingVector<unsigned char> newRgb;
  newRgb.resize(static_cast<size_t>(newWidth) * newHeight * 3, value);

  const int copyWidth = std::min(width_, newWidth);
  const int copyHeight = std::min(height_, newHeight);

  for (int y = 0; y < copyHeight; ++y) {
    for (int x = 0; x < copyWidth; ++x) {
      const int oldIdx = 3 * (y * width_ + x);
      const int newIdx = 3 * (y * newWidth + x);

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

  drawLine(centerX - radius, centerY,
           centerX + radius, centerY,
           r, g, b, alpha);

  drawLine(centerX, centerY - radius,
           centerX, centerY + radius,
           r, g, b, alpha);

  const int d = static_cast<int>(radius * 0.70710678f);

  drawLine(centerX - d, centerY - d,
           centerX + d, centerY + d,
           r, g, b, alpha);

  drawLine(centerX - d, centerY + d,
           centerX + d, centerY - d,
           r, g, b, alpha);
}


void ImageCanvas::copyRgbImage(const TrackingVector<unsigned char>& src,
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
  out.rgb = canvas.pixels(); // const ref を返すAPIならコピー
  return out;
}
