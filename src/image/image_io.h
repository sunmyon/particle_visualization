#pragma once

#include <vector>

bool WritePngRgb(const char* filename,
                 int width,
                 int height,
                 const std::vector<unsigned char>& rgb);
