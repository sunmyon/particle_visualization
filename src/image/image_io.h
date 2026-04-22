#pragma once

#include "core/tracking_vector.h"

bool WritePngRgb(const char* filename,
                 int width,
                 int height,
                 const TrackingVector<unsigned char>& rgb);
