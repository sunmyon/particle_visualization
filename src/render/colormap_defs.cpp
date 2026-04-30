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

const float infernoMap[] = {
    0.001f, 0.000f, 0.014f,
    0.087f, 0.045f, 0.224f,
    0.204f, 0.039f, 0.404f,
    0.361f, 0.050f, 0.433f,
    0.532f, 0.133f, 0.415f,
    0.708f, 0.247f, 0.333f,
    0.858f, 0.387f, 0.199f,
    0.961f, 0.553f, 0.041f,
    0.988f, 0.741f, 0.143f,
    0.964f, 0.909f, 0.438f,
    0.988f, 0.998f, 0.645f
};

const float magmaMap[] = {
    0.001f, 0.000f, 0.014f,
    0.079f, 0.054f, 0.211f,
    0.189f, 0.071f, 0.361f,
    0.341f, 0.071f, 0.486f,
    0.513f, 0.148f, 0.507f,
    0.678f, 0.258f, 0.482f,
    0.823f, 0.391f, 0.431f,
    0.933f, 0.547f, 0.366f,
    0.986f, 0.710f, 0.431f,
    0.992f, 0.884f, 0.640f,
    0.987f, 0.991f, 0.749f
};

const float cividisMap[] = {
    0.000f, 0.126f, 0.302f,
    0.000f, 0.185f, 0.381f,
    0.000f, 0.246f, 0.449f,
    0.147f, 0.309f, 0.463f,
    0.290f, 0.376f, 0.455f,
    0.426f, 0.446f, 0.435f,
    0.560f, 0.519f, 0.403f,
    0.698f, 0.596f, 0.354f,
    0.836f, 0.681f, 0.270f,
    0.936f, 0.791f, 0.216f,
    0.996f, 0.909f, 0.218f
};

const float turboMap[] = {
    0.190f, 0.072f, 0.232f,
    0.276f, 0.353f, 0.814f,
    0.158f, 0.604f, 0.969f,
    0.114f, 0.793f, 0.863f,
    0.323f, 0.929f, 0.573f,
    0.643f, 0.990f, 0.234f,
    0.879f, 0.865f, 0.146f,
    0.973f, 0.608f, 0.110f,
    0.915f, 0.328f, 0.085f,
    0.698f, 0.104f, 0.171f,
    0.480f, 0.016f, 0.011f
};

const float coolwarmMap[] = {
    0.230f, 0.299f, 0.754f,
    0.348f, 0.466f, 0.888f,
    0.484f, 0.622f, 0.975f,
    0.619f, 0.745f, 0.999f,
    0.754f, 0.830f, 0.961f,
    0.865f, 0.865f, 0.865f,
    0.951f, 0.786f, 0.704f,
    0.969f, 0.626f, 0.510f,
    0.906f, 0.455f, 0.355f,
    0.793f, 0.256f, 0.255f,
    0.706f, 0.016f, 0.150f
};

const float blueOrangeMap[] = {
    0.020f, 0.110f, 0.380f,
    0.035f, 0.230f, 0.560f,
    0.090f, 0.390f, 0.720f,
    0.300f, 0.590f, 0.820f,
    0.650f, 0.790f, 0.860f,
    0.930f, 0.920f, 0.860f,
    0.960f, 0.760f, 0.480f,
    0.900f, 0.560f, 0.210f,
    0.790f, 0.360f, 0.070f,
    0.600f, 0.180f, 0.020f,
    0.330f, 0.070f, 0.010f
};

const float grayMap[] = {
    0.000f, 0.000f, 0.000f,
    0.100f, 0.100f, 0.100f,
    0.200f, 0.200f, 0.200f,
    0.300f, 0.300f, 0.300f,
    0.400f, 0.400f, 0.400f,
    0.500f, 0.500f, 0.500f,
    0.600f, 0.600f, 0.600f,
    0.700f, 0.700f, 0.700f,
    0.800f, 0.800f, 0.800f,
    0.900f, 0.900f, 0.900f,
    1.000f, 1.000f, 1.000f
};

const float hotMap[] = {
    0.000f, 0.000f, 0.000f,
    0.220f, 0.000f, 0.000f,
    0.450f, 0.000f, 0.000f,
    0.700f, 0.060f, 0.000f,
    0.900f, 0.200f, 0.000f,
    1.000f, 0.420f, 0.000f,
    1.000f, 0.650f, 0.060f,
    1.000f, 0.820f, 0.250f,
    1.000f, 0.920f, 0.520f,
    1.000f, 0.980f, 0.780f,
    1.000f, 1.000f, 1.000f
};

namespace {

const ColormapDef kAvailableColormaps[] = {
  { "Jet",         jetMap,         9  },
  { "Viridis",     viridisMap,     11 },
  { "Plasma",      plasmaMap,      11 },
  { "Inferno",     infernoMap,     11 },
  { "Magma",       magmaMap,       11 },
  { "Cividis",     cividisMap,     11 },
  { "Turbo",       turboMap,       11 },
  { "CoolWarm",    coolwarmMap,    11 },
  { "BlueOrange",  blueOrangeMap,  11 },
  { "Gray",        grayMap,        11 },
  { "Hot",         hotMap,         11 }
};

} // namespace

const ColormapDef* gColormapDefs = kAvailableColormaps;
const int gNumColormaps =
  static_cast<int>(sizeof(kAvailableColormaps) / sizeof(kAvailableColormaps[0]));

const ColormapDef* AvailableColormaps()
{
  return kAvailableColormaps;
}

int AvailableColormapCount()
{
  return static_cast<int>(sizeof(kAvailableColormaps) /
                          sizeof(kAvailableColormaps[0]));
}
