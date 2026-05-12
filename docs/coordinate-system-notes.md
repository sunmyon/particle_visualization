# Coordinate System Notes

This note records the current intended boundary between data-space analysis and
render-space drawing. Keep this boundary explicit; do not hide coordinate bugs
by silently replacing invalid scales with fallback values.

## Spaces

- Data coordinates are the coordinates stored in `SimulationElement::position`
  and exposed as `VectorId::OriginalPos`.
- Render coordinates are data coordinates multiplied by
  `SimulationBlock::worldToRenderScale`.
- `CameraContext` is still render-space for now. Code that uses the camera as an
  analysis input must convert at the boundary.

## Derived State Contract

- `AppDerivedState::analysis` stores analysis results in data coordinates.
- `AppDerivedState::scene` stores renderable objects in render coordinates.
- The primary conversion boundary is `app_derived_rebuild.cpp`, via
  `ApplyRenderScale(...)`.
- Analysis outputs that bypass `AppDerivedState::scene` must convert in render
  sync. Current examples are iso-contours and adaptive volume trees in
  `app_render_sync.cpp`.

## Current Geometry Ownership

- Disk, ellipsoid, streamline seed boxes, streamline lines, snapshot extract
  boxes/spheres, power-spectrum boxes, scale-guide lines, convex hull vertices,
  and projection cuboid annotations are stored in `derived.analysis` or tool
  state as data coordinates and converted when copied to `derived.scene`.
- Iso-contour vertices are stored in data coordinates and scaled only when
  building render data.
- Adaptive volume tree bounds are stored in data coordinates and scaled only
  when copied to render scene data.
- Projection map image generation uses data coordinates internally. Interactive
  cuboid editing temporarily converts to render coordinates for camera arcball
  interaction, then converts back to data coordinates.

## Known Remaining Boundaries

- Radial profile now computes in data coordinates. Its center is converted from
  the render-space camera target to data coordinates at the tool execution
  boundary.
- Histogram2D computes in data coordinates. Its optional camera-center filter
  receives a data-space center and radius from the tool execution boundary.
- Clump UI helpers still contain render/data conversion for camera focusing and
  some display-related operations.
- Camera state itself is still render-space and should be separated later.
