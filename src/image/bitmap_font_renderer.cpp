#include "image/bitmap_font_renderer.h"

#include "image/image_canvas.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace fs = std::filesystem;

namespace {

struct Color {
  unsigned char r = 255;
  unsigned char g = 255;
  unsigned char b = 255;
};

bool ContainsIgnoreCase(const std::string& str, const std::string& substr)
{
  return std::search(
           str.begin(), str.end(),
           substr.begin(), substr.end(),
           [](char a, char b) {
             return std::tolower(static_cast<unsigned char>(a)) ==
                    std::tolower(static_cast<unsigned char>(b));
           }) != str.end();
}

bool LoadFileBytes(const std::string& path, std::vector<unsigned char>& out)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return false;

  const std::streamsize size = file.tellg();
  if (size <= 0) return false;

  file.seekg(0, std::ios::beg);
  out.resize(static_cast<size_t>(size));

  return static_cast<bool>(
    file.read(reinterpret_cast<char*>(out.data()), size));
}

std::vector<std::string> DefaultFontSearchPaths()
{
  std::vector<std::string> paths;

  if (const char* envPath = std::getenv("MYAPP_FONT_PATH")) {
    paths.push_back(envPath);
  }

#ifdef __APPLE__
  paths.push_back("/Library/Fonts");
  paths.push_back("/System/Library/Fonts");
  paths.push_back("/System/Library/Fonts/Supplemental");
#elif defined(_WIN32)
  paths.push_back("C:\\Windows\\Fonts");
#elif defined(__linux__)
  paths.push_back("/usr/share/fonts");
  paths.push_back("/usr/share/fonts/ttf");
  paths.push_back("/usr/local/share/fonts");
#endif

  paths.push_back(fs::current_path().string());
  return paths;
}

} // namespace

struct BitmapFontRenderer::Impl {
  bool loaded = false;
  stbtt_fontinfo font{};
  std::vector<unsigned char> fontBuffer;
  std::vector<std::string> fontPaths;

  struct GlyphPlacement {
    int codepoint = 0;
    float penX = 0.0f;
    float penY = 0.0f;

    int bitmapX0 = 0;
    int bitmapY0 = 0;
    int bitmapX1 = 0;
    int bitmapY1 = 0;
  };

  struct TextLayout {
    std::vector<GlyphPlacement> glyphs;
    TextBBox bbox;
    float advance = 0.0f;
    float scale = 1.0f;
  };

  bool loadFontFile(const std::string& path)
  {
    std::vector<unsigned char> nextBuffer;
    if (!LoadFileBytes(path, nextBuffer)) return false;

    stbtt_fontinfo nextFont{};
    const int offset = stbtt_GetFontOffsetForIndex(nextBuffer.data(), 0);
    if (offset < 0) return false;
    if (!stbtt_InitFont(&nextFont, nextBuffer.data(), offset)) return false;

    fontBuffer = std::move(nextBuffer);
    font = nextFont;
    loaded = true;
    return true;
  }

  TextLayout layoutText(const char* text, float pixelSize) const
  {
    TextLayout layout;
    if (!loaded || !text || !*text || pixelSize <= 0.0f) {
      return layout;
    }

    layout.scale = stbtt_ScaleForPixelHeight(&font, pixelSize);

    float penX = 0.0f;
    float penY = 0.0f;

    float minX = std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();

    for (int i = 0; text[i]; ++i) {
      const int codepoint = static_cast<unsigned char>(text[i]);

      if (codepoint == '\n') {
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
        penX = 0.0f;
        penY += (ascent - descent + lineGap) * layout.scale;
        continue;
      }

      int advanceWidth = 0;
      int leftBearing = 0;
      stbtt_GetCodepointHMetrics(&font, codepoint, &advanceWidth, &leftBearing);

      int x0 = 0;
      int y0 = 0;
      int x1 = 0;
      int y1 = 0;
      stbtt_GetCodepointBitmapBox(&font, codepoint, layout.scale, layout.scale,
                                  &x0, &y0, &x1, &y1);

      GlyphPlacement glyph;
      glyph.codepoint = codepoint;
      glyph.penX = penX;
      glyph.penY = penY;
      glyph.bitmapX0 = x0;
      glyph.bitmapY0 = y0;
      glyph.bitmapX1 = x1;
      glyph.bitmapY1 = y1;
      layout.glyphs.push_back(glyph);

      const float gx0 = penX + x0;
      const float gy0 = penY + y0;
      const float gx1 = penX + x1;
      const float gy1 = penY + y1;

      minX = std::min(minX, gx0);
      minY = std::min(minY, gy0);
      maxX = std::max(maxX, gx1);
      maxY = std::max(maxY, gy1);

      penX += advanceWidth * layout.scale;

      if (text[i + 1]) {
        const int nextCodepoint = static_cast<unsigned char>(text[i + 1]);
        penX += stbtt_GetCodepointKernAdvance(&font, codepoint, nextCodepoint)
                * layout.scale;
      }

      layout.advance = std::max(layout.advance, penX);
    }

    if (!std::isfinite(minX) || !std::isfinite(minY) ||
        !std::isfinite(maxX) || !std::isfinite(maxY)) {
      return layout;
    }

    layout.bbox.minX = minX;
    layout.bbox.minY = minY;
    layout.bbox.maxX = maxX;
    layout.bbox.maxY = maxY;
    layout.bbox.width = static_cast<int>(std::ceil(maxX - minX));
    layout.bbox.height = static_cast<int>(std::ceil(maxY - minY));

    return layout;
  }

  void drawGlyph(ImageCanvas& canvas,
                 float originX,
                 float originY,
                 const GlyphPlacement& glyph,
                 float scale,
                 Color color) const
  {
    int width = 0;
    int height = 0;
    int xOffset = 0;
    int yOffset = 0;

    unsigned char* bitmap =
      stbtt_GetCodepointBitmap(&font,
                               0.0f,
                               scale,
                               glyph.codepoint,
                               &width,
                               &height,
                               &xOffset,
                               &yOffset);

    if (!bitmap) return;

    const int x = static_cast<int>(std::floor(originX + glyph.penX + xOffset));
    const int y = static_cast<int>(std::floor(originY + glyph.penY + yOffset));

    canvas.drawAlphaMask(x, y, bitmap, width, height,
                         color.r, color.g, color.b);

    stbtt_FreeBitmap(bitmap, nullptr);
  }

  void drawLayout(ImageCanvas& canvas,
                  const TextLayout& layout,
                  float originX,
                  float originY,
                  Color color) const
  {
    if (!loaded) return;

    for (const auto& glyph : layout.glyphs) {
      drawGlyph(canvas, originX, originY, glyph, layout.scale, color);
    }
  }

  std::vector<unsigned char> renderLayoutToAlphaMask(const TextLayout& layout,
                                                        int& outWidth,
                                                        int& outHeight) const
  {
    outWidth = layout.bbox.width;
    outHeight = layout.bbox.height;

    std::vector<unsigned char> mask;
    if (outWidth <= 0 || outHeight <= 0) return mask;

    mask.resize(static_cast<size_t>(outWidth * outHeight), 0);

    for (const auto& glyph : layout.glyphs) {
      int width = 0;
      int height = 0;
      int xOffset = 0;
      int yOffset = 0;

      unsigned char* bitmap =
        stbtt_GetCodepointBitmap(&font,
                                 0.0f,
                                 layout.scale,
                                 glyph.codepoint,
                                 &width,
                                 &height,
                                 &xOffset,
                                 &yOffset);

      if (!bitmap) continue;

      const int dstX =
        static_cast<int>(std::floor(glyph.penX + xOffset - layout.bbox.minX));
      const int dstY =
        static_cast<int>(std::floor(glyph.penY + yOffset - layout.bbox.minY));

      for (int y = 0; y < height; ++y) {
        const int targetY = dstY + y;
        if (targetY < 0 || targetY >= outHeight) continue;

        for (int x = 0; x < width; ++x) {
          const int targetX = dstX + x;
          if (targetX < 0 || targetX >= outWidth) continue;

          const unsigned char a = bitmap[y * width + x];
          unsigned char& dst = mask[targetY * outWidth + targetX];
          dst = std::max(dst, a);
        }
      }

      stbtt_FreeBitmap(bitmap, nullptr);
    }

    return mask;
  }

  static std::vector<unsigned char> rotateMask90Clockwise(
      const std::vector<unsigned char>& src,
      int srcWidth,
      int srcHeight,
      int& outWidth,
      int& outHeight)
  {
    outWidth = srcHeight;
    outHeight = srcWidth;

    std::vector<unsigned char> dst;
    dst.resize(static_cast<size_t>(outWidth * outHeight), 0);

    for (int y = 0; y < srcHeight; ++y) {
      for (int x = 0; x < srcWidth; ++x) {
        const unsigned char value = src[y * srcWidth + x];
        if (value == 0) continue;

        const int rx = y;
        const int ry = srcWidth - 1 - x;
        dst[ry * outWidth + rx] = value;
      }
    }

    return dst;
  }
};

BitmapFontRenderer::BitmapFontRenderer()
  : impl_(std::make_unique<Impl>())
{
  discoverFonts();
  for (const auto& path : impl_->fontPaths) {
    if (impl_->loadFontFile(path)) {
      break;
    }
  }
}

BitmapFontRenderer::~BitmapFontRenderer() = default;

BitmapFontRenderer::BitmapFontRenderer(BitmapFontRenderer&&) noexcept = default;

BitmapFontRenderer&
BitmapFontRenderer::operator=(BitmapFontRenderer&&) noexcept = default;

int BitmapFontRenderer::fontCount() const
{
  return static_cast<int>(impl_->fontPaths.size());
}

const std::string& BitmapFontRenderer::fontPath(int index) const
{
  return impl_->fontPaths.at(static_cast<size_t>(index));
}

void BitmapFontRenderer::discoverFonts()
{
  discoverFonts(DefaultFontSearchPaths());
}

void BitmapFontRenderer::discoverFonts(const std::vector<std::string>& directories)
{
  impl_->fontPaths.clear();

  for (const auto& dir : directories) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) continue;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
      if (!entry.is_regular_file()) continue;

      const std::string path = entry.path().string();
      if (ContainsIgnoreCase(path, ".ttf") ||
          ContainsIgnoreCase(path, ".otf") ||
          ContainsIgnoreCase(path, ".ttc")) {
        impl_->fontPaths.push_back(path);
      }
    }
  }

  std::sort(impl_->fontPaths.begin(), impl_->fontPaths.end());
}

bool BitmapFontRenderer::loadFont(const std::string& path)
{
  return impl_->loadFontFile(path);
}

bool BitmapFontRenderer::loadFontByIndex(int index)
{
  if (index < 0 || index >= fontCount()) return false;
  return loadFont(fontPath(index));
}

bool BitmapFontRenderer::hasFont() const
{
  return impl_->loaded;
}

TextBBox BitmapFontRenderer::measureTextBBox(const char* text,
                                             float pixelSize) const
{
  return impl_->layoutText(text, pixelSize).bbox;
}

float BitmapFontRenderer::measureTextAdvance(const char* text,
                                             float pixelSize) const
{
  return impl_->layoutText(text, pixelSize).advance;
}

void BitmapFontRenderer::drawTextBaseline(ImageCanvas& canvas,
                                          int baselineX,
                                          int baselineY,
                                          const char* text,
                                          float pixelSize,
                                          unsigned char r,
                                          unsigned char g,
                                          unsigned char b) const
{
  const auto layout = impl_->layoutText(text, pixelSize);
  impl_->drawLayout(canvas, layout,
                    static_cast<float>(baselineX),
                    static_cast<float>(baselineY),
                    Color{r, g, b});
}

void BitmapFontRenderer::drawTextCenteredBaseline(ImageCanvas& canvas,
                                                  int centerX,
                                                  int baselineY,
                                                  const char* text,
                                                  float pixelSize,
                                                  unsigned char r,
                                                  unsigned char g,
                                                  unsigned char b) const
{
  const auto layout = impl_->layoutText(text, pixelSize);
  if (layout.bbox.width <= 0) return;

  const float originX =
    static_cast<float>(centerX) - 0.5f * (layout.bbox.minX + layout.bbox.maxX);

  impl_->drawLayout(canvas, layout,
                    originX,
                    static_cast<float>(baselineY),
                    Color{r, g, b});
}

void BitmapFontRenderer::drawValueCenteredBaseline(ImageCanvas& canvas,
                                                   int centerX,
                                                   int baselineY,
                                                   double value,
                                                   float pixelSize,
                                                   const char* format,
                                                   unsigned char r,
                                                   unsigned char g,
                                                   unsigned char b) const
{
  char text[128];
  std::snprintf(text, sizeof(text), format ? format : "%g", value);

  drawTextCenteredBaseline(canvas,
                           centerX,
                           baselineY,
                           text,
                           pixelSize,
                           r,
                           g,
                           b);
}

void BitmapFontRenderer::drawTextRotated90Centered(ImageCanvas& canvas,
                                                   int centerX,
                                                   int centerY,
                                                   const char* text,
                                                   float pixelSize,
                                                   unsigned char r,
                                                   unsigned char g,
                                                   unsigned char b) const
{
  const auto layout = impl_->layoutText(text, pixelSize);
  if (layout.bbox.width <= 0 || layout.bbox.height <= 0) return;

  int maskW = 0;
  int maskH = 0;
  const auto mask = impl_->renderLayoutToAlphaMask(layout, maskW, maskH);
  if (mask.empty()) return;

  int rotatedW = 0;
  int rotatedH = 0;
  const auto rotated =
    Impl::rotateMask90Clockwise(mask, maskW, maskH, rotatedW, rotatedH);

  const int x0 = centerX - rotatedW / 2;
  const int y0 = centerY - rotatedH / 2;

  canvas.drawAlphaMask(x0, y0, rotated.data(), rotatedW, rotatedH, r, g, b);
}
