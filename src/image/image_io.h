#pragma once

#include <vector>

bool WritePngRgb(const char* filename,
                 int width,
                 int height,
                 const std::vector<unsigned char>& rgb);

bool WritePngRgba(const char* filename,
                  int width,
                  int height,
                  const std::vector<unsigned char>& rgba);

bool EncodeJpegRgba(int width,
                    int height,
                    const std::vector<unsigned char>& rgba,
                    int quality,
                    std::vector<unsigned char>& jpeg);
