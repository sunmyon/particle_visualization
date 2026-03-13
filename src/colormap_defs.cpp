#include "colormap_defs.h"

const float jetMap[] = {
    0.0f,  0.0f,  0.5f,  // dark blue
    0.0f,  0.0f,  1.0f,  // blue
    0.0f,  0.5f,  1.0f,  // cyan
    0.0f,  1.0f,  1.0f,  // light cyan
    0.5f,  1.0f,  0.5f,  // greenish
    1.0f,  1.0f,  0.0f,  // yellow
    1.0f,  0.5f,  0.0f,  // orange
    1.0f,  0.0f,  0.0f,  // red
    0.5f,  0.0f,  0.0f   // dark red
};

const float viridisMap[] = {
    0.267f, 0.004f, 0.329f,
    0.283f, 0.141f, 0.458f,
    0.254f, 0.265f, 0.530f,
    0.207f, 0.372f, 0.553f,
    0.164f, 0.471f, 0.558f,
    0.128f, 0.566f, 0.551f,
    0.135f, 0.659f, 0.517f,
    0.267f, 0.749f, 0.441f,
    0.478f, 0.821f, 0.318f,
    0.741f, 0.873f, 0.150f,
    0.993f, 0.906f, 0.144f
};

const float plasmaMap[] = {
    0.050f, 0.029f, 0.527f,
    0.127f, 0.108f, 0.533f,
    0.212f, 0.192f, 0.540f,
    0.307f, 0.274f, 0.545f,
    0.411f, 0.354f, 0.550f,
    0.525f, 0.431f, 0.555f,
    0.647f, 0.506f, 0.557f,
    0.778f, 0.582f, 0.557f,
    0.915f, 0.658f, 0.555f,
    0.993f, 0.778f, 0.512f,
    0.990f, 0.901f, 0.396f
};

const ColormapDef gColormapDefs[] = {
  { "Jet",     jetMap,     9  },
  { "Viridis", viridisMap, 11 },
  { "Plasma",  plasmaMap,  11 }
};

const int gNumColormaps = sizeof(gColormapDefs) / sizeof(gColormapDefs[0]);
