#pragma once

struct ColormapDef {
  const char* name;
  const float* data;
  int count;
};

extern const ColormapDef gColormapDefs[];
extern const int gNumColormaps;
