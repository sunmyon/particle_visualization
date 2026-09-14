# Remote input foundation (milestone 1)

## Architecture observed

- `PlatformSession` selects a window/headless context, graphics backend, ImGui
  backend and `IFramePresenter`. Linux OpenGL headless rendering uses EGL;
  headless ImGui backends also exist for Vulkan and Metal where enabled.
- `RemoteFramePresenter` already implements the presentation abstraction. It
  requests readback via `PresentLocalFrame`, then sends a JSON header and raw
  RGBA8 pixels as a ZeroMQ PUB multipart message. The OpenGL readback is
  synchronous and flips rows on the CPU. This milestone does not change it.
- `RemoteInputReceiver` owns a ZeroMQ PULL socket and worker thread. It now
  delegates message decoding to `RemoteInputProtocol::Decode` and queues only
  successfully decoded, owning `InputEvent` values. The decoder depends on JSON
  and the common event model, without ZeroMQ, GLFW, ImGui or application state.
- GLFW callbacks enqueue camera events, while the ImGui GLFW backend chains the
  callbacks for local UI input. `RunFrame` now drains one ordered input batch,
  injects only its remote events before `ImGui::NewFrame`, applies server-side
  UI capture afterward, then passes the same batch to camera/close actions.
  Failed frame starts restore the batch ahead of newer queued input.
- Snapshot requests go through `SnapshotIOService`; loaded `SimulationBlock`
  data is validated and installed in `SimulationDataset`. Render synchronization
  builds particle/LOD scene data from that dataset. Input transport is independent
  of snapshot loading, so no data-source refactor is needed for this milestone.

## Wire contract

Each ZeroMQ message contains one UTF-8 JSON object. `type` is required.
`version` is optional and defaults to `1`; other versions are rejected.
The canonical names below retain the existing prototype vocabulary.

| Type | Payload | Current application behavior |
| --- | --- | --- |
| `pointer_move` | `x`, `y`, `primaryDown` | Existing camera drag path |
| `pointer_button` | `button`: `Left`, `Right`, `Middle`; required `action`: `Press` or `Release`; optional `x`, `y` | Injected into ImGui; camera has no direct button action |
| `pointer_scroll` | `wheelX`, `wheelY` | Existing vertical camera zoom path |
| `key` | `key`, `action`: `Press`, `Release`, `Repeat` | Injected into ImGui; Escape closes unless captured |
| `text` | Nonempty `text`, committed UTF-8 without NUL | Injected into ImGui as committed characters |
| `framebuffer_resize` | Physical `width`, `height`; logical `displayWidth`, `displayHeight`; framebuffer scales | Recreates/resizes the headless target before the next frame and preserves UI DPI |

`key` uses the exact `InputKey` enum names in `src/interaction/input_event.h`:
letters `A`–`Z`, `Digit0`–`Digit9`, `F1`–`F24`, navigation/editing keys,
left/right modifiers, punctuation and keypad keys. Empty names and `Unknown`
remain no-op keys for compatibility with the original viewer. Arbitrary unknown
names are rejected. Text entry is separate from key transitions.

The viewer now emits the complete key vocabulary, Unicode character callbacks,
explicit left/right/middle button transitions and framebuffer resize events.
Pointer coordinates are scaled from the viewer window to the latest remote
logical display size rather than its physical framebuffer size. Losing viewer focus
releases tracked keys and mouse buttons to avoid stuck input state.
The frame subscriber uses a small receive high-water mark without ZeroMQ
`CONFLATE`: conflation is unsafe for the two-part header/payload frame message
and can break multipart boundaries. The viewer logs each received resolution
change after uploading the validated RGBA payload to its OpenGL texture.
Rendered frame rows use a top-left origin. The viewer flips only the OpenGL
texture V coordinate when displaying them; remote pointer coordinates remain in
the unchanged top-left UI coordinate system.

On HiDPI displays the viewer sends both physical framebuffer pixels and logical
window size. The server renders particles at physical resolution while ImGui
uses the logical size and framebuffer scale, keeping text at normal size. The
viewer requests its current size at startup and does not display stale,
lower-resolution frames while a resize is pending.

Escape restores a maximized viewer first; from a normal window it closes the
viewer and sends a global remote Escape to stop the server. In a remote session,
`Browse Files` opens the embedded ImGui file dialog, which browses the server's
filesystem inside the streamed UI. Local sessions retain the native OS dialog.

Optional common fields retain their existing meanings and defaults:

- `modifiers`: boolean `shift`, `ctrl`, `alt`, `super`, all defaulting to false.
- `viewport`: integer `x`, `y` (default 0), positive integer `width`, `height`
  (default 1), positive `framebufferScaleX`, `framebufferScaleY` (default 1).
- `primaryDown` and `capturedByUI`: booleans, default false. Moves still carry
  their own `primaryDown` snapshot. For remote mouse/key/text events,
  `capturedByUI` is overwritten from server-side ImGui state.
- Numeric coordinates and wheel values default to zero; key actions default to
  `Press` for compatibility. Optional unknown JSON fields are ignored.
- Resize messages without logical display metrics remain compatible: logical
  size defaults to physical size and framebuffer scale defaults to one.

Coordinates are carried unchanged by the decoder. The viewer maps its logical
cursor position into the latest remote framebuffer dimensions before encoding
an event. More elaborate letterbox/crop transforms remain future work.

All decoded events have `source = Remote`; locally constructed events default
to `Local`. Client-provided `capturedByUI` remains accepted for wire
compatibility but is not authoritative. The main thread injects remote input
before `ImGui::NewFrame`, then routes camera/keyboard actions using server-side
capture state. Local GLFW input is not injected a second time.

Invalid JSON, wrong field types, unknown event/action/key/button names, invalid
UTF-8, invalid viewport/resize dimensions, out-of-range numbers and messages over
64 KiB produce no event. Decoder JSON exceptions cannot unwind the receiver
thread. Zero-size resize events (for example minimization) are ignored. Resize
requests are also capped at 8192 pixels per axis and 32 MiPixels. This is message
validation, not a bound on the application event queue or total network traffic;
queue/backpressure policy remains future work.

## Validation

Configure and build with the existing CMake workflow and `BUILD_TESTING=ON`.
With `PYTHON_BRIDGE=ON`, CTest includes protocol regression checks and, on Unix,
an IPC receiver integration test. The latter needs permission to bind a local
IPC socket. It sends valid input around malformed messages, verifies ordered
receipt, and exercises stop/restart without a GPU or window. The ImGui input
test verifies mouse, wheel, key/modifier and UTF-8 state, local-input isolation,
server capture replacement and activation of a real ImGui button.

For an end-to-end check using the real application, run:

```bash
python3 scripts/test_remote_loopback.py
```

Pass `--snapshot /path/to/snapshot.hdf5` to exercise the same path with real
particle data. The script creates a temporary configuration and sets
`PARTICLE_VIS_CONFIG_PATH`, so the repository's normal `config.txt` is not
changed. Without that environment variable, the application continues to load
and save `config.txt` as before.

Pass `--resize WIDTH HEIGHT` to verify that a remote framebuffer resize updates
the headless render target, ImGui display size, readback size, and published
frame metadata together. Resize requests are limited to 8192 pixels per axis
and 32 MiPixels to bound server-side allocations.

The script starts a headless server on loopback-only, dynamically selected TCP
ports, validates two complete raw RGBA frames, sends pointer/button/wheel/text
events, then confirms that a remote Escape event stops the server cleanly. It
defaults to Metal on macOS and OpenGL elsewhere; `--backend` and `--executable`
can override those choices. This is a manual smoke test because it needs a
working graphics backend. It requires `pyzmq` and a build with
`PYTHON_BRIDGE=ON`.

## Next milestone

Exercise the standalone viewer against the local server for longer interactive
sessions, then move the same raw-frame setup behind an SSH tunnel and measure
latency and bandwidth. Explicit viewer/server focus state can be added if those
sessions reveal stuck-input cases. Compression, asynchronous readback and live
simulation transport remain outside this change.
