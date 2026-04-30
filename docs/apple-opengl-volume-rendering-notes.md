# Apple OpenGL Volume Rendering Notes

## Summary

This project hit an apparent Apple OpenGL driver/compiler issue in the adaptive
volume renderer. Treat this as a platform-specific limitation when changing the
volume shader.

## Observed behavior

- Raw-value volume trees with procedural transfer-function evaluation work.
- Fixed and procedural heat volume colors work.
- Volume color mapping with a 1D colormap texture can work in simple cases.
- Adding extra samplerBuffer resources or post-transfer-function logic to the
  same fragment shader caused Apple OpenGL to report:

```text
UNSUPPORTED (log once): POSSIBLE ISSUE: unit 0 GLD_TEXTURE_INDEX_1D is unloadable and bound to sampler type (Float) - using zero texture because texture unloadable
```

- The symptom was a missing/transparent volume, even though the tree and draw
  call were present.

## Current workaround

The volume shader does not sample texture colormaps. The adaptive volume tree
stores raw scalar values, and the shader evaluates transfer-function components
from uniforms. Volume color is limited to fixed color or procedural heat.

OpenGL colormaps used elsewhere are uploaded as height-1 `GL_TEXTURE_2D`
textures. The 1D texture upload path was removed from `ColorbarRenderer`.

## Development guidance

- Do not add `sampler1D` back into OpenGL shaders.
- Avoid texture colormaps in the volume shader on Apple OpenGL.
- Keep new post-transfer-function experiments small and verify them on Apple
  OpenGL before merging into the stable rendering path.
- If the `GLD_TEXTURE_INDEX_1D` warning returns, first check whether
  `sampler1D`, `GL_TEXTURE_1D`, or an old 1D colormap path was reintroduced.

## Status

Preliminary. The active volume path now avoids texture colormap sampling and
supports immediate transfer-function opacity updates after applying the editor:
the tree stores raw values, and the shader evaluates the applied components.

A historical post-TF test that added a height-1 `sampler2D` colormap to a
separate post-TF program failed on Apple OpenGL with:

```text
GLD_TEXTURE_INDEX_2D is unloadable and bound to sampler type (Float)
```

Therefore texture colormaps should remain outside the volume shader unless a
future backend proves that path safe.
