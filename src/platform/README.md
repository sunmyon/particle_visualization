# Platform Layer

This directory contains the code that connects the application to the host
machine: windows, graphics contexts, ImGui backends, frame presentation, and
remote input/output.

The main goal is to keep platform-specific details such as GLFW, OpenGL, EGL,
and ZeroMQ out of the application and rendering logic.

## Big Picture

```text
main.cpp
  -> PlatformSession
       -> WindowContext
            -> WindowBackend
                 -> GlfwWindow
       -> GraphicsContext
            -> OpenGLContext
       -> ImGuiBackend
            -> GlfwOpenGLImGuiBackend
            -> HeadlessOpenGLImGuiBackend
       -> IFramePresenter
            -> LocalFramePresenter
            -> RemoteFramePresenter
```

## Responsibilities

### PlatformSession

`PlatformSession` owns the platform lifecycle.

It creates and shuts down:

- the window
- the graphics context
- the ImGui backend
- local or remote frame presentation
- remote input
- native file dialog lifecycle

Application code should normally talk to `PlatformSession`, not to GLFW,
OpenGL, or EGL directly.

### WindowContext

`WindowContext` is the application-facing window object.

It provides:

- window close / polling / time
- framebuffer and viewport size
- access to a typed native window handle

It does not create OpenGL contexts and does not call OpenGL.

### WindowBackend

`WindowBackend` is the interface for native window implementations.

The current implementation is:

- `GlfwWindow`

`GlfwWindow` is the only place that should directly own GLFW window lifecycle
operations such as `glfwInit`, `glfwCreateWindow`, and `glfwPollEvents`.

### NativeWindowHandle

`NativeWindowHandle` describes what kind of native handle is being passed.

This avoids blindly casting a raw `void*`.

Current backends:

- `None`
- `GLFW`

Future backends could include Cocoa, X11, Wayland, SDL, or others.

### GraphicsContext

`GraphicsContext` is the interface for graphics context operations.

It owns:

- graphics context initialization
- headless graphics initialization
- presenting / swapping buffers
- framebuffer readback

The current implementation is:

- `OpenGLContext`

OpenGL, EGL, GLAD, and OpenGL readback code should stay inside
`OpenGLContext` or other OpenGL-specific renderer files.

### ImGuiBackend

`ImGuiBackend` separates Dear ImGui core usage from the platform/renderer
backend combination.

Current implementations:

- `GlfwOpenGLImGuiBackend`
- `HeadlessOpenGLImGuiBackend`

For example, `GlfwOpenGLImGuiBackend` owns calls such as:

- `ImGui_ImplGlfw_InitForOpenGL`
- `ImGui_ImplOpenGL3_Init`

If the app later supports Metal, Vulkan, SDL, or another backend, a new
`ImGuiBackend` implementation should be added instead of spreading those calls
through the app.

### Frame Presenters

Frame presenters decide where the rendered frame goes.

Current presenters:

- `LocalFramePresenter`: presents to the local window
- `RemoteFramePresenter`: reads back the frame and publishes it over ZeroMQ

Presenters use `GraphicsContext`; they should not directly call OpenGL
readback functions.

## Design Rules

- Application code should not include GLFW, EGL, GLAD, or platform-specific
  headers directly.
- Window creation belongs in `WindowBackend` implementations.
- Graphics context creation and framebuffer readback belong in
  `GraphicsContext` implementations.
- ImGui platform/renderer backend calls belong in `ImGuiBackend`
  implementations.
- `PlatformSession` is the composition point that chooses which concrete
  backends are used.

## Japanese / 日本語

このディレクトリには、アプリケーションを実行環境につなぐためのコードが入っています。
具体的には、ウィンドウ、OpenGL/EGL context、ImGui backend、画面表示、
remote 入出力などです。

目的は、GLFW や OpenGL や EGL などの環境依存の処理を、アプリ本体や解析処理から
できるだけ切り離すことです。

## 全体像

```text
main.cpp
  -> PlatformSession
       -> WindowContext
            -> WindowBackend
                 -> GlfwWindow
       -> GraphicsContext
            -> OpenGLContext
       -> ImGuiBackend
            -> GlfwOpenGLImGuiBackend
            -> HeadlessOpenGLImGuiBackend
       -> IFramePresenter
            -> LocalFramePresenter
            -> RemoteFramePresenter
```

## それぞれの役割

### PlatformSession

`PlatformSession` は platform まわりの初期化と終了処理をまとめる場所です。

次のものを作成・終了します。

- window
- graphics context
- ImGui backend
- local / remote の frame presenter
- remote input
- native file dialog

アプリ本体は、基本的に GLFW や OpenGL を直接触らず、`PlatformSession` を通して
platform 機能を使います。

### WindowContext

`WindowContext` はアプリから見える window の窓口です。

担当するもの:

- window を閉じるかどうか
- event polling
- 時刻
- framebuffer / viewport のサイズ
- native window handle の取得

ここでは OpenGL context を作りません。OpenGL の関数も呼びません。

### WindowBackend

`WindowBackend` は、実際の window system を差し替えるための interface です。

現在の実装は:

- `GlfwWindow`

`glfwInit`、`glfwCreateWindow`、`glfwPollEvents` など、GLFW 固有の処理は
`GlfwWindow` に閉じ込めます。

### NativeWindowHandle

`NativeWindowHandle` は、native window handle が何由来なのかを表す型です。

単なる `void*` だけを渡すと、それが GLFW なのか、Cocoa なのか、X11 なのかが
分かりません。そのため、backend の種類も一緒に持たせています。

現在の種類:

- `None`
- `GLFW`

将来的には Cocoa、X11、Wayland、SDL などを追加できます。

### GraphicsContext

`GraphicsContext` は graphics context の interface です。

担当するもの:

- graphics context の初期化
- headless graphics context の初期化
- buffer の swap / present
- framebuffer の readback

現在の実装は:

- `OpenGLContext`

OpenGL、EGL、GLAD、`glReadPixels` などは `OpenGLContext` または OpenGL 専用の
renderer 側に閉じ込めます。

### ImGuiBackend

`ImGuiBackend` は Dear ImGui の backend を分けるための interface です。

現在の実装:

- `GlfwOpenGLImGuiBackend`
- `HeadlessOpenGLImGuiBackend`

例えば `GlfwOpenGLImGuiBackend` は次のような処理を担当します。

- `ImGui_ImplGlfw_InitForOpenGL`
- `ImGui_ImplOpenGL3_Init`

将来 Metal、Vulkan、SDL などに対応する場合は、新しい `ImGuiBackend` を追加します。
アプリ本体に backend 固有の処理を散らさないことが大切です。

### Frame Presenter

Frame presenter は、描画された frame をどこへ出すかを決める部分です。

現在の presenter:

- `LocalFramePresenter`: local window に表示する
- `RemoteFramePresenter`: framebuffer を readback して ZeroMQ で送信する

Presenter は `GraphicsContext` を使います。`glReadPixels` などの OpenGL 関数を
直接呼ばないようにします。

## 設計ルール

- アプリ本体では GLFW、EGL、GLAD などの platform 固有 header を直接 include しない。
- window 作成は `WindowBackend` 実装に置く。
- graphics context 作成と framebuffer readback は `GraphicsContext` 実装に置く。
- ImGui の platform/renderer backend 呼び出しは `ImGuiBackend` 実装に置く。
- どの backend を使うかは `PlatformSession` で組み立てる。
