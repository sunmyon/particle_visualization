
cmake_minimum_required(VERSION 3.20)
project(particle_vis LANGUAGES C CXX)

# ===== 基本設定 =====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT MSVC)
  # Release: -O3 -march=native -flto
  string(APPEND CMAKE_C_FLAGS_RELEASE " -O3 -march=native -flto")
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -march=native -flto")

  # RelWithDebInfo も同じ最適化にしたいなら（任意）
  string(APPEND CMAKE_C_FLAGS_RELWITHDEBINFO " -O3 -march=native -flto")
  string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " -O3 -march=native -flto")

  # MinSizeRel までO3にしたいなら（任意）
  # string(APPEND CMAKE_C_FLAGS_MINSIZEREL " -march=native -flto")
  # string(APPEND CMAKE_CXX_FLAGS_MINSIZEREL " -march=native -flto")
endif()

# ===== オプション（MakefileのOPTION相当） =====
option(DEBUG_MODE          "Enable debug mode"                   ON)
option(USE_LUA             "Enable Lua"                          ON)
option(USE_CONVEX_HULL     "Enable Convex Hull"                  ON)
option(CLUMP_DATA_READ     "Enable Clump data read"              ON)
option(ISO_CONTOUR         "Enable iso-contour/VTK"              ON)
option(GEOMETRICAL_ANALYSIS "Enable geometrical analysis"        ON)
option(STREAM_LINE         "Enable streamline feature"           ON)
option(VOLUME_RENDERING    "Enable volume rendering"             ON)
option(HAVE_HDF5           "Enable HDF5 IO"                      ON)
option(SAVE_GPU_MEMORY     "Save GPU memory"                     ON)
option(ROTATE_QUATERNION   "Use quaternion rotation"             ON)
option(ROTATE_ARCBALL      "Use arcball rotation"                ON)
option(NONATIVEFILEDIALOG  "Disable native file dialog"          OFF)
option(PYTHON_BRIDGE       "Bridge the main code to python"      ON)
option(CLOUDY              "Bridge the main code to CLOUDY"      OFF)

set(USE_MMAP_DEFAULT ON)
if(WIN32)
  set(USE_MMAP_DEFAULT OFF)
endif()
option(USE_MMAP "Use POSIX mmap for file IO" {USE_MMAP_DEFAULT})

# グラフィックス選択：デスクトップGL(既定) / OpenGL ES + EGL（WSL向け）
option(USE_GLES            "Use OpenGL ES + EGL instead of desktop OpenGL" OFF)

# ===== コンパイル定義付与 =====
function(add_def cond defname)
  if(${cond})
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${defname})
  endif()
endfunction()

# ===== 依存（パス） =====
set(SRC_DIR      ${CMAKE_SOURCE_DIR}/src)
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)

# ===== ソース一覧 =====
set(SOURCES_CPP
  ${SRC_DIR}/main.cpp
  ${SRC_DIR}/make_2D_projection_map.cpp
  ${SRC_DIR}/compute_2D_histogram.cpp
  ${SRC_DIR}/compute_radial_profile.cpp
	${SRC_DIR}/FileIO/file_io.cpp
	${SRC_DIR}/FileIO/file_io_hdf5.cpp
  ${SRC_DIR}/FindClumps/find_clumps.cpp
  ${SRC_DIR}/FindClumps/find_clumps_IO.cpp
  ${SRC_DIR}/FindClumps/find_clumps_make_chains.cpp
  ${SRC_DIR}/OctTree/ParticleOctree.cpp
  ${SRC_DIR}/IsoSurface/density_evaluate.cpp
)

if(GEOMETRICAL_ANALYSIS)
  list(APPEND SOURCES_CPP ${SRC_DIR}/GeometricAnalysis/ellipse_fitter.cpp)
endif()

if(USE_CONVEX_HULL)
  list(APPEND SOURCES_CPP ${SRC_DIR}/FindClumps/create_convex_hull.cpp)
endif()

if(STREAM_LINE)
  list(APPEND SOURCES_CPP ${SRC_DIR}/StreamLine/stream_line.cpp)
endif()

if(VOLUME_RENDERING)
  list(APPEND SOURCES_CPP ${SRC_DIR}/VolumeRendering/tau_sph.cpp)
endif()

if(PYTHON_BRIDGE)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/PythonBridge.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/BridgeAdapter.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/ShmCommon.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/ZmqServer.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/JupyterLauncher.cpp)
endif()

# ImGui / ImPlot / NFD（必要に応じて）
set(IMGUI_SRC
  ${EXTERNAL_DIR}/imgui/imgui.cpp
  ${EXTERNAL_DIR}/imgui/imgui_draw.cpp
  ${EXTERNAL_DIR}/imgui/imgui_widgets.cpp
  ${EXTERNAL_DIR}/imgui/imgui_tables.cpp
	${EXTERNAL_DIR}/imgui/imgui_demo.cpp
	${EXTERNAL_DIR}/imgui/backends/imgui_impl_glfw.cpp
	${EXTERNAL_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

set(IMPLOT_SRC
	${EXTERNAL_DIR}/implot/implot.cpp
	${EXTERNAL_DIR}/implot/implot_items.cpp
)

set(NFD_SRC_COMMON ${EXTERNAL_DIR}/nfd/nfd_common.c)

# デスクトップGL用 glad（Windows/macOS/Linux のデスクトップGLで使用）
# ※ USE_GLES=ON の場合は glad を使わず <GLES3/gl3.h> を使う方針
set(GLAD_SRC ${EXTERNAL_DIR}/glad/src/glad.c)


# Compute "$(uname -m)" once for a deterministic default
execute_process(COMMAND uname -m
  OUTPUT_VARIABLE _uname_m
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# Build the default prefix: $HOME/usr/local/cloudy/c25.00-$(uname -m)
set(_CLOUDY_PREFIX_DEFAULT "$ENV{HOME}/usr/local/cloudy/c25.00-${_uname_m}")

# Expose CLOUDY_PREFIX cache var with that default (user can override on cmdline)
set(CLOUDY_PREFIX "${_CLOUDY_PREFIX_DEFAULT}" CACHE PATH
    "Prefix to prebuilt Cloudy (e.g. $ENV{HOME}/usr/local/cloudy/c25.00-$(uname -m))")

if(CLOUDY)
  # Locate headers and library under CLOUDY_PREFIX
  find_path(CLOUDY_INCLUDE_DIR
    NAMES cddefines.h
    HINTS
      "${CLOUDY_PREFIX}/include/cloudy"
      "${CLOUDY_PREFIX}/src/source"
      "${CLOUDY_PREFIX}/source"
    NO_DEFAULT_PATH)

  find_library(CLOUDY_LIBRARY
    NAMES cloudy_core cloudy
    HINTS
      "${CLOUDY_PREFIX}/lib"
      "${CLOUDY_PREFIX}"
    NO_DEFAULT_PATH)

  if(NOT CLOUDY_INCLUDE_DIR OR NOT CLOUDY_LIBRARY)
    message(FATAL_ERROR
      "Cloudy not found under CLOUDY_PREFIX='${CLOUDY_PREFIX}'.\n"
      "Expected: <prefix>/include/cloudy/cddefines.h and <prefix>/lib/libcloudy_core.a")
  endif()

  add_library(Cloudy::core STATIC IMPORTED GLOBAL)
  set_target_properties(Cloudy::core PROPERTIES
    IMPORTED_LOCATION             "${CLOUDY_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CLOUDY_INCLUDE_DIR}"
    INTERFACE_COMPILE_FEATURES    "cxx_std_17"
    INTERFACE_POSITION_INDEPENDENT_CODE ON
  )

  message(STATUS "Cloudy: inc='${CLOUDY_INCLUDE_DIR}', lib='${CLOUDY_LIBRARY}'")
endif()

# 目標ターゲット
add_executable(${PROJECT_NAME}
  ${SOURCES_CPP}
  ${IMGUI_SRC}
  ${IMPLOT_SRC}
  ${NFD_SRC_COMMON}
	${NFD_SRC_OS}
)

# ===== インクルードパス =====
target_include_directories(${PROJECT_NAME} PRIVATE
  ${SRC_DIR}
  ${EXTERNAL_DIR}/imgui
  ${EXTERNAL_DIR}/imgui/backends
  ${EXTERNAL_DIR}/implot
	${EXTERNAL_DIR}/nfd
	${EXTERNAL_DIR}/include
	${EXTERNAL_DIR}
)


#debug macro
macro(target_link_libraries_report tgt scope)
  # 可変長引数で渡されたものを target_link_libraries に流す
  target_link_libraries(${tgt} ${scope} ${ARGN})

  # 現在積まれているリンクアイテムを出力
  get_target_property(_libs ${tgt} LINK_LIBRARIES)
  message(STATUS "[link-report] ${tgt}: ${_libs}")
endmacro()

function(_print_links tag)
  get_property(_libs TARGET ${PROJECT_NAME} PROPERTY LINK_LIBRARIES)
  message(STATUS "[trace:${tag}] LINK_LIBRARIES = ${_libs}")
  # カウント
  set(_targets glfw gmp mpfr Cocoa OpenGL)
  foreach(tok IN LISTS _targets)
    set(cnt 0)
    foreach(x IN LISTS _libs)
      if(x STREQUAL ${tok})
        math(EXPR cnt "${cnt}+1")
      endif()
    endforeach()
    message(STATUS "  - ${tok}: ${cnt}")
  endforeach()
endfunction()

# =========================
# Package manager support
# =========================

find_package(PkgConfig QUIET)

# --- macOS + Homebrew ---
if(APPLE)
  find_program(_BREW brew)
  if(_BREW)
    # 必要な brew パッケージを列挙
    set(_brew_packages libomp vtk hdf5 lua gmp mpfr eigen)
    if(PYTHON_BRIDGE)
      list(APPEND _brew_packages zeromq cppzmq nlohmann-json)
    endif()
    
    foreach(_pkg IN LISTS _brew_packages)
      execute_process(
        COMMAND "${_BREW}" --prefix ${_pkg}
        OUTPUT_VARIABLE _BREW_PKG_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(EXISTS "${_BREW_PKG_PREFIX}")
        list(PREPEND CMAKE_PREFIX_PATH "${_BREW_PKG_PREFIX}")
        set(ENV{PKG_CONFIG_PATH}
            "${_BREW_PKG_PREFIX}/lib/pkgconfig:${_BREW_PKG_PREFIX}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
      endif()
    endforeach()
  endif()

  # Intel ライブラリ混入防止
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" AND EXISTS "/opt/homebrew")
    option(IGNORE_USR_LOCAL_ON_SILICON "Ignore /usr/local on Apple Silicon" ON)
    if(IGNORE_USR_LOCAL_ON_SILICON)
      list(APPEND CMAKE_IGNORE_PATH "/usr/local/lib" "/usr/local/include")
    endif()
  endif()
endif()

# --- Windows + vcpkg ---
if(WIN32)
  if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}" "$ENV{VCPKG_ROOT}/installed")
  endif()
  if(DEFINED ENV{VCPKG_INSTALLED} AND EXISTS "$ENV{VCPKG_INSTALLED}")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_INSTALLED}")
  endif()
endif()

# --- Linux ---
if(UNIX AND NOT APPLE)
  # pkg-config が最優先
  if(PKG_CONFIG_FOUND)
    message(STATUS "pkg-config found: ${PKG_CONFIG_EXECUTABLE}")
  else()
    message(WARNING "pkg-config not found. Install it for better dependency detection.")
  endif()
endif()

function(detect_and_link target lib_name pkg_name)
  set(options HEADER_ONLY)
  set(oneValueArgs CMAKE_TARGET)
  cmake_parse_arguments(DAL "${options}" "${oneValueArgs}" "" ${ARGN})

  find_package(${lib_name} QUIET CONFIG)
  if(${lib_name}_FOUND)
    message(STATUS "[detect_and_link] ${lib_name}: found via find_package")

    if(DAL_CMAKE_TARGET)
      target_link_libraries(${target} PRIVATE ${DAL_CMAKE_TARGET})
      return()
    endif()

    if(TARGET ${lib_name}::${lib_name})
      target_link_libraries(${target} PRIVATE ${lib_name}::${lib_name})
      return()
    elseif(TARGET ${lib_name})
      target_link_libraries(${target} PRIVATE ${lib_name})
      return()
    endif()

    if(DAL_HEADER_ONLY)
      return()
    endif()
  endif()

  # 2) pkg-config
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(${lib_name}_PC QUIET ${pkg_name})
    if(${lib_name}_PC_FOUND)
      message(STATUS "[detect_and_link] ${lib_name}: via pkg-config (${pkg_name})")
      target_include_directories(${target} PRIVATE ${${lib_name}_PC_INCLUDE_DIRS})
      target_link_directories(${target} PRIVATE ${${lib_name}_PC_LIBRARY_DIRS})

      if(NOT DAL_HEADER_ONLY)
        target_link_libraries(${target} PRIVATE ${${lib_name}_PC_LIBRARIES})
      endif()

      if(${lib_name}_PC_CFLAGS)
        target_compile_options(${target} PRIVATE ${${lib_name}_PC_CFLAGS})
      endif()
      return()
    endif()
  endif()

  # 3) manual fallback
  message(WARNING "[detect_and_link] ${lib_name} not found via CMake/pkg-config, trying manual search")
  if(DAL_HEADER_ONLY)
    find_path(${lib_name}_INC NAMES ${pkg_name}.hpp ${pkg_name}.h PATH_SUFFIXES include)
    if(${lib_name}_INC)
      target_include_directories(${target} PRIVATE ${${lib_name}_INC})
      message(STATUS "[detect_and_link] ${lib_name}: header-only include at ${${lib_name}_INC}")
      return()
    endif()
  else()
    find_library(${lib_name}_LIB NAMES ${pkg_name} PATH_SUFFIXES lib lib64)
    find_path(${lib_name}_INC NAMES ${pkg_name}.h PATH_SUFFIXES include)
    if(${lib_name}_LIB AND ${lib_name}_INC)
      target_include_directories(${target} PRIVATE ${${lib_name}_INC})
      target_link_libraries(${target} PRIVATE ${${lib_name}_LIB})
      message(STATUS "[detect_and_link] ${lib_name}: linked manually")
      return()
    endif()
  endif()

  message(FATAL_ERROR "[detect_and_link] ${lib_name} not found. Install it or set CMAKE_PREFIX_PATH.")
endfunction()


function(detect_and_link_glfw target lib_name pkg_name)
  # lib_name: CMake のパッケージ名 (例: GLFW, GMP, VTK)
  # pkg_name: pkg-config 名 (例: glfw3, gmp, vtk)

  # 1. CMake の find_package
  find_package(glfw QUIET CONFIG)
  if(NOT glfw_FOUND)
    find_package(glfw3 QUIET CONFIG)
  endif()

  if(glfw_FOUND OR glfw3_FOUND)
    message(STATUS "${lib_name} found via find_package")
    if(TARGET glfw)
      target_link_libraries(${target} PRIVATE glfw)
      message(STATUS "Target 'glfw' is available")
    elseif(TARGET glfw::glfw)
      target_link_libraries(${target} PRIVATE glfw::glfw)
      message(STATUS "Target 'glfw::glfw' is available")
    elseif(TARGET glfw3::glfw3)
      target_link_libraries(${target} PRIVATE glfw3::glfw3)
      message(STATUS "Target 'glfw3::glfw3' is available")
    elseif(TARGET glfw3)
      target_link_libraries(${target} PRIVATE glfw3)
      message(STATUS "Target 'glfw3' is available")
    else()
      message(STATUS "[detect_and_link] could not find target link libraries")	
    endif()
    return()
  endif()

  # 2. pkg-config
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(${lib_name}_PC QUIET ${pkg_name})
    if(${lib_name}_PC_FOUND)
      message(STATUS "[detect_and_link] ${lib_name}: found via pkg-config (${pkg_name})")
      message(STATUS "[detect_and_link] include: ${${lib_name}_PC_INCLUDE_DIRS}")
      message(STATUS "[detect_and_link] libs:    ${${lib_name}_PC_LIBRARIES}")
      target_include_directories(${target} PRIVATE ${${lib_name}_PC_INCLUDE_DIRS})
      target_link_directories(${target} PRIVATE ${${lib_name}_PC_LIBRARY_DIRS})
      target_link_libraries(${target} PRIVATE ${${lib_name}_PC_LIBRARIES})
      return()
    endif()
  endif()

  # 3. manual fallback
  message(WARNING "${lib_name} not found via CMake or pkg-config, falling back to manual search")
  find_library(${lib_name}_LIB NAMES ${pkg_name} PATH_SUFFIXES lib lib64)
  find_path(${lib_name}_INC NAMES ${pkg_name}.h PATH_SUFFIXES include)
  if(${lib_name}_LIB AND ${lib_name}_INC)
    target_include_directories(${target} PRIVATE ${${lib_name}_INC})
    target_link_libraries(${target} PRIVATE ${${lib_name}_LIB})
    message(STATUS "[detect_and_link] ${lib_name} → LINK_LIBRARIES = ${_libs}")
  else()
    message(FATAL_ERROR "${lib_name} not found. Please install it.")
  endif()
endfunction()


if(NONATIVEFILEDIALOG)
  # === ImGuiFileDialog を使う（ネイティブNFDは使わない） ===
  target_sources(${PROJECT_NAME} PRIVATE
    ${EXTERNAL_DIR}/ImGuiFileDialog/ImGuiFileDialog.cpp
  )
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${EXTERNAL_DIR}/ImGuiFileDialog
  )
  target_compile_definitions(${PROJECT_NAME} PRIVATE NONATIVEFILEDIALOG)
else()
  # === OS ネイティブの Native File Dialog を使う ===
  # 共通
  target_sources(${PROJECT_NAME} PRIVATE
    ${EXTERNAL_DIR}/nfd/nfd_common.c
  )

  if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE
      ${EXTERNAL_DIR}/nfd/nfd_win.cpp
    )
    # Windows で必要なライブラリ
    target_link_libraries(${PROJECT_NAME} PRIVATE
      ole32 uuid comdlg32 shell32
    )
  elseif(APPLE)
    target_sources(${PROJECT_NAME} PRIVATE
      ${EXTERNAL_DIR}/nfd/nfd_cocoa.m
    )
    # Cocoa フレームワーク（既にリンク済みなら重複OK）
    find_library(COCOA_FRAMEWORK Cocoa)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${COCOA_FRAMEWORK})
  else() # Linux/Unix
    # 例1: 依存の少ない Zenity 版
    target_sources(${PROJECT_NAME} PRIVATE
      ${EXTERNAL_DIR}/nfd/nfd_zenity.c
    )
    # 例2: GTK 版を使いたいならこちら（コメントアウト解除）
    # find_package(PkgConfig REQUIRED)
    # pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    # target_sources(${PROJECT_NAME} PRIVATE ${EXTERNAL_DIR}/nfd/nfd_gtk.c)
    # target_include_directories(${PROJECT_NAME} PRIVATE ${GTK3_INCLUDE_DIRS})
    # target_link_directories(${PROJECT_NAME} PRIVATE ${GTK3_LIBRARY_DIRS})
    # target_link_libraries(${PROJECT_NAME} PRIVATE ${GTK3_LIBRARIES})
  endif()
endif()


# ===== ISO_CONTOUR (VTK 連携) =====
if(ISO_CONTOUR)
  if(DEFINED ENV{VTK_DIR} AND EXISTS "$ENV{VTK_DIR}")
    set(VTK_DIR "$ENV{VTK_DIR}" CACHE PATH "VTK package directory (from environment)" FORCE)
  endif()
   
  if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}" "$ENV{VCPKG_ROOT}/installed")
  endif()
  if(DEFINED ENV{VCPKG_INSTALLED} AND EXISTS "$ENV{VCPKG_INSTALLED}")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_INSTALLED}")
  endif()
  
  # 使いたい VTK モジュール（Makefile の列挙を踏襲）
  set(VTK_COMPONENTS
    CommonCore
    CommonDataModel
    CommonExecutionModel
    FiltersCore
    FiltersExtraction
    FiltersGeneral
    FiltersAMR
    FiltersHyperTree
    FiltersHybrid
    FiltersSources
    FiltersModeling
    CommonMisc
  )
  
  # VTK を検出（9.x系想定。必要ならバージョン縛りを調整）
  find_package(VTK 9.0 COMPONENTS ${VTK_COMPONENTS} REQUIRED)

  # 追加ソース（Makefile 相当）
  target_sources(${PROJECT_NAME} PRIVATE
    ${SRC_DIR}/IsoSurface/marching_cubes.cpp
    ${SRC_DIR}/IsoSurface/connectivity_test.cpp
    ${SRC_DIR}/IsoSurface/IsoSurfaceGenerator.cpp
  )

  # インクルードパス
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${SRC_DIR}/IsoSurface
    ${SRC_DIR}/OctTree
    ${VTK_INCLUDE_DIRS}      # VTK のヘッダ
  )

  # リンク（インポートターゲットを使うのがCMake流）
  # 例: VTK::CommonCore, VTK::FiltersCore, ...
  if(VTK_FOUND)
    foreach(c IN LISTS VTK_COMPONENTS)
      target_link_libraries(${PROJECT_NAME} PRIVATE VTK::${c})
    endforeach()
  else()
    detect_and_link(${PROJECT_NAME} VTK vtk)
  endif()
  
  # macOS で実行時に VTK の .dylib を見つけやすくする（Makefile の -Wl,-rpath,/Users/j/usr/local/lib 相当）
  if(APPLE)
    # VTK_ROOT/lib を rpath に加える（必要ならパスを調整）
    set_target_properties(${PROJECT_NAME} PROPERTIES
      BUILD_RPATH "${VTK_ROOT}/lib"
      INSTALL_RPATH "${VTK_ROOT}/lib")
  endif()
  
  # C++ 側に ISO_CONTOUR 定義を渡す（必要なら）
  target_compile_definitions(${PROJECT_NAME} PRIVATE ISO_CONTOUR)
endif()

detect_and_link_glfw(${PROJECT_NAME} "glfw" "glfw")
detect_and_link(${PROJECT_NAME} "GMP" "gmp")
detect_and_link(${PROJECT_NAME} "MPFR" "mpfr")

if(PYTHON_BRIDGE)
  detect_and_link(${PROJECT_NAME} "ZeroMQ" "libzmq" CMAKE_TARGET libzmq)
  detect_and_link(${PROJECT_NAME} "cppzmq" "cppzmq" HEADER_ONLY)
  detect_and_link(${PROJECT_NAME} "nlohmann_json" "nlohmann-json"
    CMAKE_TARGET nlohmann_json::nlohmann_json HEADER_ONLY)  
endif()

# =========================
# Eigen (header-only)
# =========================
find_package(Eigen3 3.3 QUIET CONFIG)

if(TARGET Eigen3::Eigen)
  target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)
else()
  # 2) pkg-config があるなら それ経由も試せる（任意）
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(EIGEN3_PC QUIET eigen3)
    if(EIGEN3_PC_FOUND)
      add_library(Eigen3::Eigen INTERFACE IMPORTED)
      set_target_properties(Eigen3::Eigen PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_PC_INCLUDE_DIRS}")
      target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)
    endif()
  endif()

  # 3) それでも無ければ find_path（標準パス + CMAKE_PREFIX_PATH を自動検索）
  if(NOT TARGET Eigen3::Eigen)
    find_path(EIGEN3_INCLUDE_DIR
      NAMES Eigen/Core
      PATH_SUFFIXES eigen3
      HINTS $ENV{EIGEN3_INCLUDE_DIR}  # 手動上書きしたい人向け
    )
    if(NOT EIGEN3_INCLUDE_DIR)
      message(FATAL_ERROR "Eigen3 headers not found. Set EIGEN3_INCLUDE_DIR or install Eigen3.")
    endif()

    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    set_target_properties(Eigen3::Eigen PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}")
    target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)
  endif()
endif()

# =========================
# OpenMP (optional)
# =========================
# 1) まずは標準の FindOpenMP に任せる（MSVC/GCC/Clang で最優先）
find_package(OpenMP QUIET)

if(OpenMP_CXX_FOUND)
  target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
else()
  # 2) 見つからない場合のフォールバック
  if(MSVC)
    # 古いツールチェーンなどで FindOpenMP が外れるケース用
    target_compile_options(${PROJECT_NAME} PRIVATE /openmp)
  elseif(APPLE)
    # AppleClang は OpenMP ランタイムを同梱しないので Homebrew の libomp を探す
    find_path(OMP_INCLUDE_DIR NAMES omp.h)
    find_library(OMP_LIBRARY NAMES omp)
    
    if(OMP_INCLUDE_DIR AND OMP_LIBRARY)
      target_include_directories(${PROJECT_NAME} PRIVATE "${OMP_INCLUDE_DIR}")
      target_link_libraries(${PROJECT_NAME} PRIVATE "${OMP_LIBRARY}")
      target_compile_options(${PROJECT_NAME} PRIVATE -Xpreprocessor -fopenmp)
    else()
      message(WARNING "OpenMP not found. Install libomp (brew install libomp) or disable OpenMP features.")
    endif()
  else()
    # Linux/他OS での手動フォールバック（ほぼ不要だが保険）
    # 例: 特殊ディストリで FindOpenMP が使えない時
    target_compile_options(${PROJECT_NAME} PRIVATE -fopenmp)
    target_link_libraries(${PROJECT_NAME}    PRIVATE gomp)  # 典型: GCC の libgomp
  endif()
endif()

# ---- デスクトップ OpenGL ----
if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
elseif(APPLE)
	find_library(COCOA_FRAMEWORK Cocoa)
	find_library(OPENGL_FRAMEWORK OpenGL)
	target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_FRAMEWORK} ${COCOA_FRAMEWORK})
else()
	# Linux: -lGL -ldl -lpthread
	find_package(OpenGL REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL dl pthread)
endif()

# デスクトップGLでは glad を使う
target_sources(${PROJECT_NAME} PRIVATE ${GLAD_SRC})
target_include_directories(${PROJECT_NAME} PRIVATE ${EXTERNAL_DIR}/glad/include)

# ===== HDF5 =====
if(HAVE_HDF5)
  find_package(HDF5 COMPONENTS CXX QUIET)
  if(HDF5_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE HDF5::HDF5)
  elseif(PKG_CONFIG_FOUND)
    pkg_check_modules(HDF5_PC QUIET hdf5)
    if(HDF5_PC_FOUND)
      target_include_directories(${PROJECT_NAME} PRIVATE ${HDF5_PC_INCLUDE_DIRS})
      target_link_libraries(${PROJECT_NAME} PRIVATE ${HDF5_PC_LIBRARIES})
    else()
      message(FATAL_ERROR "HDF5 not found. Please install it (e.g. brew install hdf5).")
    endif()
  endif()
endif()

# ===== Lua =====
if(USE_LUA)
  # まずはモジュール検索
  find_package(Lua QUIET)
  if(Lua_FOUND AND TARGET Lua::Lua)
    target_link_libraries(${PROJECT_NAME} PRIVATE Lua::Lua)
  elseif(Lua_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LUA_LIBRARIES})
  elseif(PKG_CONFIG_FOUND)
    pkg_check_modules(LUA QUIET lua-5.4 lua5.4 lua5.3 lua)
    if(LUA_FOUND)
      target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIRS})
      target_link_directories(${PROJECT_NAME} PRIVATE ${LUA_LIBRARY_DIRS})
      target_link_libraries(${PROJECT_NAME} PRIVATE ${LUA_LIBRARIES})
    else()
      message(WARNING "Lua not found. Install Lua (e.g. brew install lua).")
    endif()
  endif()
endif()

# ===== コンパイル定義（各種ON/OFFを C++ 側に渡す） =====
add_def(DEBUG_MODE          DEBUG_MODE)
add_def(USE_LUA             USE_LUA)
add_def(USE_CONVEX_HULL     USE_CONVEX_HULL)
add_def(CLUMP_DATA_READ     CLUMP_DATA_READ)
add_def(ISO_CONTOUR         ISO_CONTOUR)
add_def(GEOMETRICAL_ANALYSIS GEOMETRICAL_ANALYSIS)
add_def(STREAM_LINE         STREAM_LINE)
add_def(VOLUME_RENDERING    VOLUME_RENDERING)
add_def(HAVE_HDF5           HAVE_HDF5)
add_def(USE_MMAP            USE_MMAP)
add_def(SAVE_GPU_MEMORY     SAVE_GPU_MEMORY)
add_def(ROTATE_QUATERNION   ROTATE_QUATERNION)
add_def(ROTATE_ARCBALL      ROTATE_ARCBALL)
add_def(NONATIVEFILEDIALOG  NONATIVEFILEDIALOG)
add_def(PYTHON_BRIDGE       PYTHON_BRIDGE)

# ===== 最適化やASanなど（Debug/Releaseで自動） =====
if(MSVC)
  # /MP: 並列ビルド
  target_compile_options(${PROJECT_NAME} PRIVATE /MP)
else()
  # GCC/Clang共通
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "USE_GLES: ${USE_GLES}")

# ===== After build: create a symlink to the executable in the repo root =====
set(_ROOT_LINK_NAME
  $<IF:$<PLATFORM_ID:Windows>,${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.exe,${CMAKE_SOURCE_DIR}/${PROJECT_NAME}>
)

# オプション: Windows で権限がない場合に "コピー" に切り替える
option(WINDOWS_LINK_AS_COPY "On Windows, copy the exe to repo root instead of making a symlink" OFF)

if(WIN32 AND WINDOWS_LINK_AS_COPY)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${_ROOT_LINK_NAME}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${_ROOT_LINK_NAME}"
    COMMENT "Copying ${PROJECT_NAME} to repo root (Windows fallback)"
    VERBATIM)
else()
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${_ROOT_LINK_NAME}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:${PROJECT_NAME}>" "${_ROOT_LINK_NAME}"
    COMMENT "Creating symlink to ${PROJECT_NAME} in repo root"
    VERBATIM)
endif()

