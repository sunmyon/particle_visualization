# Repository Architecture Assessment

## Purpose

This note captures the current repository structure, the main dependency problems, and a practical split strategy for refactoring.

The goal is not to redesign everything at once. The goal is to establish a stable map so later changes do not fight each other.

## Scope

This assessment is based on static inspection of the current tree under `src/`.

It focuses on:

- responsibility distribution
- dependency direction
- boundary violations
- refactoring order

It does not try to fully specify a future renderer abstraction yet.

## Current Module Map

### `src/app`

Current role:

- application state aggregation
- frame orchestration
- request execution
- callback integration
- feature service ownership

Problems:

- `AppState` aggregates nearly every subsystem and acts as a global state bag
- `app_frame.cpp` mixes orchestration, UI calls, derived-state rebuilds, render preparation, and draw execution
- `AppServices` is both a service registry and a feature runtime storage area

Key files:

- `src/app/app_state.h`
- `src/app/app_frame.cpp`
- `src/app/app_lifecycle.cpp`
- `src/app/app_analysis_execution.cpp`
- `src/app/app_services.h`

### `src/render`

Current role:

- CPU-side render data building
- OpenGL resource management
- shader program lifecycle
- draw submission
- overlay and gizmo drawing

Problems:

- CPU render-data building and GPU backend state are mixed together
- public headers expose OpenGL types
- renderers are thin wrappers over OpenGL rather than backend-neutral interfaces
- render setup leaks into app orchestration

Key files:

- `src/render/render_system.h`
- `src/render/render_resources.h`
- `src/render/render_resources.cpp`
- `src/render/particle_renderer.*`
- `src/render/object_renderer.*`
- `src/render/gizmo_renderer.*`

### `src/FileIO`

Current role:

- snapshot source configuration
- file loading
- prefetch control
- format selection
- some dialog-related state

Problems:

- file I/O owns UI-facing dialog state
- snapshot configuration depends on UI types
- the module is not headless because state and UI concerns are fused

Key files:

- `src/FileIO/file_io.h`
- `src/FileIO/snapshot_source.h`
- `src/FileIO/snapshot_loader.*`
- `src/FileIO/snapshot_prefetch_controller.*`

### `src/config`

Current role:

- config serialization/deserialization
- applying saved settings into runtime structures

Problems:

- config layer depends on UI state types
- config is coupled to current in-memory representation instead of a stable config model

Key files:

- `src/config/config_io.h`
- `src/config/config_apply.*`
- `src/config/config_extract.*`

### `src/UI.*` and `src/settingUI.*`

Current role:

- tool window rendering
- settings panels
- feature controls
- some preview integration

Problems:

- UI structs store domain parameters and computed results, not only UI state
- UI entry points directly know about service objects and runtime implementation details

Key files:

- `src/UI.h`
- `src/UI.cpp`
- `src/settingUI.h`
- `src/settingUI.cpp`
- `src/app/ui_state.h`

### `src/data`

Current role:

- particle/header/clump/halo data structures

Status:

- relatively coherent compared to upper layers
- still pulled into too many modules because upper-layer boundaries are weak

### `src/interaction`

Current role:

- camera state
- input interaction helpers

Problems:

- interaction state is simple, but access patterns are broad because `AppState` exposes everything everywhere

### Analysis / feature modules

Modules:

- `src/FindClumps`
- `src/GeometricAnalysis`
- `src/IsoSurface`
- `src/StreamLine`
- `src/VolumeRendering`
- `src/PythonBridge`

Current role:

- feature-specific computations and integrations

Problems:

- feature state is spread across `app/runtime_state.h`, `app/analysis_state.h`, `app/app_services.h`, UI code, and sometimes render code
- compile-time flags cut across the whole repository instead of enclosing features cleanly

## Dependency Map

The following map is intentionally high level. It shows meaningful dependency direction, not every include edge.

### Desired direction

Target shape should move toward:

- `core/data` <- `analysis/services`
- `core/data` <- `FileIO`
- `core/data` <- `render scene extraction`
- `app orchestration` -> `services`
- `app orchestration` -> `UI`
- `app orchestration` -> `render backend`
- `UI` -> view models only
- `render backend` -> backend implementation only

### Current direction

Current dominant edges are closer to:

- `app` -> everything
- `UI` -> `services`, `runtime`, `FileIO`, `render state`
- `FileIO` -> `app/ui_state`
- `config` -> `app/ui_state`, `FileIO`, `visual config`
- `render` -> OpenGL + `app/runtime_state` + scene/object types
- feature modules -> app state types

### Concrete problematic edges

#### `FileIO -> UI`

`src/FileIO/snapshot_source.h` includes `app/ui_state.h` and stores `MaskUIState`.

Why this is a problem:

- file input configuration should not depend on Dear ImGui-facing state types
- UI changes can ripple into snapshot configuration and config serialization

#### `config -> UI`

`src/config/config_io.h` exposes `MaskUIState` in config load/save APIs.

Why this is a problem:

- config should serialize a stable settings model, not current window-state structures

#### `render -> app/runtime`

`src/render/object_renderer.h` includes `app/runtime_state.h`.

Why this is a problem:

- renderer internals now depend on application runtime state layout
- future backend implementations cannot stay isolated

#### `app -> render implementation details`

`src/app/app_frame.cpp` calls raw OpenGL operations and manages render synchronization details.

Why this is a problem:

- orchestration and implementation are fused
- render execution cannot be replaced cleanly

#### `AppState -> all layers`

`src/app/app_state.h` includes scene state, UI state, runtime state, services, camera, and interaction.

Why this is a problem:

- it weakens ownership boundaries
- callback and frame code can mutate almost anything

## Main Structural Problems

### 1. State ownership is unclear

There are many kinds of state:

- source/data state
- UI state
- requests
- derived analysis state
- render state
- backend resource state

These are not consistently separated by lifetime or owner. As a result, mutation flow is hard to follow.

### 2. UI model and domain model are fused

`app/ui_state.h` stores:

- window open flags
- domain parameters
- cached computation results
- temporary selection data

This makes UI code a de facto owner of analysis/session state.

### 3. Runtime flags propagate in too many directions

`cpuUpdated`, `gpuUpdated`, and multiple dirty flags are spread across app, render, and feature execution paths.

This creates hidden update protocols rather than explicit pipelines.

### 4. Compile-time feature flags are cross-cutting

Macros such as:

- `ISO_CONTOUR`
- `STREAM_LINE`
- `VOLUME_RENDERING`
- `PYTHON_BRIDGE`
- `USE_CONVEX_HULL`

appear in multiple layers. That means features are not isolated modules; they are conditional edits to the whole application.

### 5. File and type naming no longer matches responsibility

Examples:

- `render_resources.*` includes both CPU render-data building and render-system lifecycle
- `FileInfo` is not only file info; it is a UI-aware loading façade
- `AppServices` stores both services and runtime-owned feature state

This makes the code harder to reason about even before any algorithmic complexity is considered.

## Refactoring Boundaries To Establish First

These are the first boundaries worth making explicit.

### Boundary A: UI model vs domain/config model

Create separate structures for:

- persisted settings
- session request parameters
- UI-only state

Example split:

- `MaskSettings`
- `ProjectionSettings`
- `MaskWindowState`
- `ProjectionPanelState`

Outcome:

- `FileIO` and `config` stop depending on `app/ui_state.h`

### Boundary B: application orchestration vs feature execution

`app_frame.cpp` should stop being the place where all feature-specific update logic lives.

Introduce focused update steps, for example:

- `execute_requests(...)`
- `rebuild_derived_scene(...)`
- `prepare_render_scene(...)`
- `draw_frame(...)`

Outcome:

- the frame loop becomes readable
- feature logic can move out without breaking the whole app

### Boundary C: CPU render scene vs GPU backend

Split current render layer into:

- render scene extraction / draw data building
- backend runtime and GPU resources

Example:

- `render/scene/`
- `render/backends/opengl/`

Outcome:

- OpenGL types disappear from shared render data structures
- backend replacement becomes feasible

### Boundary D: headless data loading vs desktop UI dialogs

Separate:

- snapshot source specification
- loading/prefetch
- format dialog state

Outcome:

- loading code can be tested without UI
- platform/UI-specific concerns stop leaking into data input

## Recommended Refactoring Order

This order is intended to reduce rework.

### Phase 1: repository map and state model cleanup

Goals:

- identify state owners
- name boundaries
- stop new coupling from being introduced

Tasks:

- document current owners of each major state structure
- separate UI-only state from persisted/domain state on paper first
- freeze new cross-layer includes unless justified

### Phase 2: remove UI dependencies from `FileIO` and `config`

Goals:

- make file loading and config stable and UI-independent

Tasks:

- replace `MaskUIState` usage in `SnapshotSource` with a non-UI settings struct
- replace `config_io` API dependence on `MaskUIState`
- move format dialog state out of `FileInfo`

### Phase 3: slim down `AppState`

Goals:

- make state access explicit

Tasks:

- replace monolithic `AppState` passing with narrower arguments in key execution paths
- move feature-owned state closer to each feature module
- keep `AppState` only as a composition root if still needed

### Phase 4: split frame orchestration

Goals:

- isolate update stages

Tasks:

- extract request execution from `app_frame.cpp`
- extract derived-state rebuild from render submission
- define a clear per-frame data flow

### Phase 5: split render scene from backend

Goals:

- prepare for renderer replacement

Tasks:

- move OpenGL-specific types and code under an OpenGL backend directory
- keep render scene data CPU-only and backend-neutral
- remove OpenGL calls from app-level code

### Phase 6: feature isolation

Goals:

- reduce cross-cutting macro damage

Tasks:

- localize feature state and APIs
- shrink macro usage to feature registration/build wiring points

## Suggested Deliverables For The Next Step

Before any broad refactoring starts, the next concrete deliverables should be:

1. A state inventory
2. A dependency diagram
3. A boundary decision note
4. A first refactor target list

### 1. State inventory

List each major state type with:

- owner
- lifetime
- mutators
- consumers

Start with:

- `AppState`
- `RenderRuntimeState`
- `AnalysisDerivedState`
- `ToolWindowUIState`
- `SnapshotSource`
- `FileInfo`
- `RenderResources`
- `AppServices`

### 2. Dependency diagram

At minimum, diagram these modules:

- `app`
- `UI`
- `render`
- `FileIO`
- `config`
- `data`
- `analysis/features`
- `PythonBridge`

### 3. Boundary decision note

Decide and write down:

- what counts as UI-only state
- what counts as persisted configuration
- what counts as session/runtime state
- what counts as backend-specific render state

### 4. First refactor target list

Recommended first targets:

- `src/FileIO/snapshot_source.h`
- `src/FileIO/file_io.h`
- `src/config/config_io.h`
- `src/app/ui_state.h`
- `src/app/app_state.h`
- `src/app/app_frame.cpp`
- `src/render/render_resources.*`

## Immediate Guidance

If only one change stream starts first, start here:

1. Separate UI state from config/domain state
2. Remove UI types from `FileIO` and `config`
3. Split frame orchestration from render implementation

Do not start with backend abstraction first.

The backend work will be cleaner only after these boundaries exist.

## Non-Goals For The First Refactor Pass

To avoid thrashing, the first pass should not try to:

- redesign every feature API
- replace all macros immediately
- introduce Vulkan/Metal backend code yet
- optimize performance unless the refactor requires it

## Summary

The repository is not suffering from one oversized subsystem only. The deeper issue is that boundary objects already represent multiple layers at once.

The most important cleanup is not renderer abstraction first. It is state-model separation and dependency direction repair.

Once those are in place, backend replacement becomes a contained engineering task instead of a repository-wide rewrite.
