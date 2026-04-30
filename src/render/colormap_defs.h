#pragma once

struct ColormapDef {
  const char* name;
  const float* data;
  int count;
};

const ColormapDef* AvailableColormaps();
int AvailableColormapCount();

extern const ColormapDef* gColormapDefs;
extern const int gNumColormaps;
