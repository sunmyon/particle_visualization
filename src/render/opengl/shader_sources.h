#pragma once

extern const char* particleVertexShaderSource;
extern const char* particleFragmentShaderSource;

extern const char* lineVertexShaderSource;
extern const char* lineFragmentShaderSource;

extern const char* colorbarVertexShaderSource;
extern const char* colorbarFragmentShaderSource;

extern const char* velocityArrowVertexShaderSource;
extern const char* velocityArrowFragmentShaderSource;

extern const char* colormap2DShaderSource;
extern const char* colormap2DFragmentShaderSource;

extern const char* instancedSolidVertexShaderSource;
extern const char* instancedSolidFragmentShaderSource;

extern const char* coordShaderSource;
extern const char* coordFragmentShaderSource;

#ifdef ISO_CONTOUR
extern const char* isocontourVertexShaderSource;
extern const char* isocontourFragmentShaderSource;
#endif

#ifdef VOLUME_RENDERING
extern const char* fullscreenShaderSource;
extern const char* upscaleVS;
extern const char* upscaleFS;
extern const char* octrayFragmentShaderSource;
extern const char* wboitParticleShaderSource;
extern const char* wboitParticleFragmentShaderSource;
extern const char* wboitResolveShaderSource;
extern const char* wboitResolveFragmentShaderSource;
#endif
