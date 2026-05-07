#pragma once
#include <vector>

class ImageCanvas {
public:
  ImageCanvas(std::vector<unsigned char>& rgb, int width, int height);

  int width() const;
  int height() const;
  std::vector<unsigned char>& pixels() const;
  
  void resizeKeepContent(int newWidth, int newHeight, unsigned char value);
  
  void setPixel(int x, int y,
                unsigned char r,
                unsigned char g,
                unsigned char b);

  void blendPixel(int x, int y,
                  unsigned char r,
                  unsigned char g,
                  unsigned char b,
                  float alpha);

  void drawAlphaMask(int x0,
                     int y0,
                     const unsigned char* alpha,
                     int maskWidth,
                     int maskHeight,
                     unsigned char r = 255,
                     unsigned char g = 255,
                     unsigned char b = 255);

  void fillRect(int x0, int y0, int x1, int y1,
                unsigned char r,
                unsigned char g,
                unsigned char b);

  void blendRect(int x0, int y0, int x1, int y1,
                 unsigned char r,
                 unsigned char g,
                 unsigned char b,
                 float alpha);

  void drawHorizontalLine(int x0, int x1, int y,
                          unsigned char r,
                          unsigned char g,
                          unsigned char b);

  void drawVerticalLine(int x, int y0, int y1,
                        unsigned char r,
                        unsigned char g,
                        unsigned char b);

  void drawLine(int x0,
		int y0,
		int x1,
		int y1,
		unsigned char r,
		unsigned char g,
		unsigned char b,
		float alpha = 1.0f);

  
  void drawAsterisk(int centerX,
		    int centerY,
		    int radius,
		    unsigned char r,
		    unsigned char g,
		    unsigned char b,
		    float alpha = 1.0f);

  void drawSoftCircle(int centerX,
                      int centerY,
                      float radius,
                      unsigned char r,
                      unsigned char g,
                      unsigned char b,
                      float alpha = 1.0f);
  
  void copyRgbImage(const std::vector<unsigned char>& src,
                    int srcWidth,
                    int srcHeight,
                    int dstX,
                    int dstY);

private:
  std::vector<unsigned char>& rgb_;
  int width_ = 0;
  int height_ = 0;
};
