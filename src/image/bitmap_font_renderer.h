#pragma once

#include <memory>
#include <string>
#include <vector>

class ImageCanvas;

struct TextBBox {
  int width = 0;
  int height = 0;
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
};

class BitmapFontRenderer {
public:
  BitmapFontRenderer();
  ~BitmapFontRenderer();

  BitmapFontRenderer(const BitmapFontRenderer&) = delete;
  BitmapFontRenderer& operator=(const BitmapFontRenderer&) = delete;

  BitmapFontRenderer(BitmapFontRenderer&&) noexcept;
  BitmapFontRenderer& operator=(BitmapFontRenderer&&) noexcept;

  int fontCount() const;
  const std::string& fontPath(int index) const;

  void discoverFonts();
  void discoverFonts(const std::vector<std::string>& directories);

  bool loadFont(const std::string& path);
  bool loadFontByIndex(int index);
  bool hasFont() const;

  TextBBox measureTextBBox(const char* text, float pixelSize) const;
  float measureTextAdvance(const char* text, float pixelSize) const;

  void drawTextBaseline(ImageCanvas& canvas,
                        int baselineX,
                        int baselineY,
                        const char* text,
                        float pixelSize,
                        unsigned char r = 255,
                        unsigned char g = 255,
                        unsigned char b = 255) const;

  void drawTextCenteredBaseline(ImageCanvas& canvas,
                                int centerX,
                                int baselineY,
                                const char* text,
                                float pixelSize,
                                unsigned char r = 255,
                                unsigned char g = 255,
                                unsigned char b = 255) const;

  void drawValueCenteredBaseline(ImageCanvas& canvas,
                                 int centerX,
                                 int baselineY,
                                 double value,
                                 float pixelSize,
                                 const char* format,
                                 unsigned char r = 255,
                                 unsigned char g = 255,
                                 unsigned char b = 255) const;

  void drawTextRotated90Centered(ImageCanvas& canvas,
                                 int centerX,
                                 int centerY,
                                 const char* text,
                                 float pixelSize,
                                 unsigned char r = 255,
                                 unsigned char g = 255,
                                 unsigned char b = 255) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
