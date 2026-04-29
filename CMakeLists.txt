
cmake_minimum_required(VERSION 3.20)
project(particle_vis LANGUAGES C CXX)

# ===== Basic settings =====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT MSVC)
  # Release: -O3 -march=native -flto
  string(APPEND CMAKE_C_FLAGS_RELEASE " -O3 -march=native -flto")
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -march=native -flto")

  # Use the same optimization for RelWithDebInfo if desired.
  string(APPEND CMAKE_C_FLAGS_RELWITHDEBINFO " -O3 -march=native -flto")
  string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " -O3 -march=native -flto")

  # Enable similar native/LTO options for MinSizeRel if desired.
  # string(APPEND CMAKE_C_FLAGS_MINSIZEREL " -march=native -flto")
  # string(APPEND CMAKE_CXX_FLAGS_MINSIZEREL " -march=native -flto")
endif()

# ===== Options, corresponding to the old Makefile options =====
option(DEBUG_MODE          "Enable debug mode"                   ON)
option(USE_LUA             "Enable Lua"                          ON)
option(USE_CONVEX_HULL     "Enable Convex Hull"                  ON)
option(USE_LETTERBOX       "Use Letter box"                      ON)
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
option(AUTO_DISABLE_PYTHON_BRIDGE_ON_MISSING_DEPS
  "Automatically disable PYTHON_BRIDGE when ZeroMQ/cppzmq dependencies are unavailable"
  ON)
option(AUTO_DISABLE_GEOMETRICAL_ANALYSIS_ON_MISSING_EIGEN
  "Automatically disable GEOMETRICAL_ANALYSIS when Eigen headers are unavailable"
  ON)
option(AUTO_DISABLE_STREAM_LINE_ON_MISSING_EIGEN
  "Automatically disable STREAM_LINE when Eigen headers are unavailable"
  ON)
option(AUTO_DISABLE_HDF5_ON_MISSING_CXX
  "Automatically disable HAVE_HDF5 when HDF5 C++ bindings are unavailable"
  ON)
option(AUTO_DISABLE_LUA_ON_MISSING_DEPS
  "Automatically disable USE_LUA when Lua development files are unavailable"
  ON)

set(USE_MMAP_DEFAULT ON)
if(WIN32)
  set(USE_MMAP_DEFAULT OFF)
endif()
option(USE_MMAP "Use POSIX mmap for file IO" ${USE_MMAP_DEFAULT})

# Graphics selection: desktop GL by default, or OpenGL ES + EGL for WSL-like environments.
option(USE_GLES            "Use OpenGL ES + EGL instead of desktop OpenGL" OFF)
option(USE_EGL_HEADLESS    "Enable headless EGL OpenGL context for remote rendering" ON)
option(BUILD_EGL_PROBE_ONLY "Build only the standalone EGL probe tool" OFF)
option(PARTICLE_VIS_HEADLESS_ONLY "Build particle_vis without GLFW/window support for EGL remote rendering" OFF)
option(PARTICLE_VIS_BUILD_BATCH "Build GUI-less batch executable" OFF)
option(AUTO_HEADLESS_ON_MISSING_GLFW "Automatically switch to headless mode when GLFW is unavailable on Linux" ON)
option(PARTICLE_VIS_PREFER_SUBMODULE_DEPS
  "Prefer optional dependencies from external/submodules before system packages"
  ON)
option(PARTICLE_VIS_ALLOW_SUBMODULE_BUILDS
  "Build supported dependency submodules from source when present"
  ON)

if(AUTO_HEADLESS_ON_MISSING_GLFW AND NOT PARTICLE_VIS_HEADLESS_ONLY AND UNIX AND NOT APPLE)
  set(_AUTO_HAVE_GLFW FALSE)

  find_package(glfw QUIET CONFIG)
  if(NOT glfw_FOUND)
    find_package(glfw3 QUIET CONFIG)
  endif()

  if(TARGET glfw OR TARGET glfw::glfw OR TARGET glfw3::glfw3 OR TARGET glfw3)
    set(_AUTO_HAVE_GLFW TRUE)
  else()
    find_library(_AUTO_GLFW_LIBRARY NAMES glfw glfw3 PATH_SUFFIXES lib lib64)
    find_path(_AUTO_GLFW_INCLUDE_DIR NAMES GLFW/glfw3.h PATH_SUFFIXES include)
    if(_AUTO_GLFW_LIBRARY AND _AUTO_GLFW_INCLUDE_DIR)
      set(_AUTO_HAVE_GLFW TRUE)
    endif()
  endif()

  if(NOT _AUTO_HAVE_GLFW)
    message(WARNING
      "GLFW not found on this Linux environment; enabling PARTICLE_VIS_HEADLESS_ONLY=ON.")
    set(PARTICLE_VIS_HEADLESS_ONLY ON CACHE BOOL
      "Build particle_vis without GLFW/window support for EGL remote rendering" FORCE)
  endif()
endif()

if(PARTICLE_VIS_HEADLESS_ONLY)
  set(NONATIVEFILEDIALOG ON CACHE BOOL "Disable native file dialog in headless builds" FORCE)
endif()

if(AUTO_DISABLE_PYTHON_BRIDGE_ON_MISSING_DEPS AND PYTHON_BRIDGE)
  set(_AUTO_HAVE_ZMQ FALSE)

  find_package(ZeroMQ QUIET CONFIG)
  if(TARGET libzmq OR TARGET ZeroMQ::libzmq)
    set(_AUTO_HAVE_ZMQ TRUE)
  else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(_AUTO_ZMQ_PC QUIET libzmq)
      if(_AUTO_ZMQ_PC_FOUND)
        set(_AUTO_HAVE_ZMQ TRUE)
      endif()
    endif()

    if(NOT _AUTO_HAVE_ZMQ)
      find_library(_AUTO_ZMQ_LIBRARY NAMES zmq libzmq PATH_SUFFIXES lib lib64)
      find_path(_AUTO_ZMQ_INCLUDE_DIR NAMES zmq.h PATH_SUFFIXES include)
      if(_AUTO_ZMQ_LIBRARY AND _AUTO_ZMQ_INCLUDE_DIR)
        set(_AUTO_HAVE_ZMQ TRUE)
      endif()
    endif()
  endif()

  if(NOT _AUTO_HAVE_ZMQ)
    message(WARNING
      "PYTHON_BRIDGE was enabled, but ZeroMQ dependencies were not found. "
      "Disabling PYTHON_BRIDGE for this build.")
    set(PYTHON_BRIDGE OFF CACHE BOOL "Bridge the main code to python" FORCE)
  endif()
endif()

if(AUTO_DISABLE_GEOMETRICAL_ANALYSIS_ON_MISSING_EIGEN AND GEOMETRICAL_ANALYSIS)
  set(_AUTO_HAVE_EIGEN FALSE)

  find_package(Eigen3 3.3 QUIET CONFIG)
  if(TARGET Eigen3::Eigen)
    set(_AUTO_HAVE_EIGEN TRUE)
  else()
    find_path(_AUTO_EIGEN3_INCLUDE_DIR
      NAMES Eigen/Core
      HINTS
        $ENV{EIGEN3_INCLUDE_DIR}
      PATH_SUFFIXES eigen3 include/eigen3 include)
    if(_AUTO_EIGEN3_INCLUDE_DIR)
      set(_AUTO_HAVE_EIGEN TRUE)
    endif()
  endif()

  if(NOT _AUTO_HAVE_EIGEN)
    message(WARNING
      "GEOMETRICAL_ANALYSIS was enabled, but Eigen3 headers were not found. "
      "Disabling GEOMETRICAL_ANALYSIS for this build.")
    set(GEOMETRICAL_ANALYSIS OFF CACHE BOOL "Enable geometrical analysis" FORCE)
  endif()
endif()

if(AUTO_DISABLE_STREAM_LINE_ON_MISSING_EIGEN AND STREAM_LINE)
  set(_AUTO_HAVE_EIGEN_FOR_STREAM FALSE)

  find_package(Eigen3 3.3 QUIET CONFIG)
  if(TARGET Eigen3::Eigen)
    set(_AUTO_HAVE_EIGEN_FOR_STREAM TRUE)
  else()
    find_path(_AUTO_EIGEN3_INCLUDE_DIR_FOR_STREAM
      NAMES Eigen/Core
      HINTS
        $ENV{EIGEN3_INCLUDE_DIR}
      PATH_SUFFIXES eigen3 include/eigen3 include)
    if(_AUTO_EIGEN3_INCLUDE_DIR_FOR_STREAM)
      set(_AUTO_HAVE_EIGEN_FOR_STREAM TRUE)
    endif()
  endif()

  if(NOT _AUTO_HAVE_EIGEN_FOR_STREAM)
    message(WARNING
      "STREAM_LINE was enabled, but Eigen3 headers were not found. "
      "Disabling STREAM_LINE for this build.")
    set(STREAM_LINE OFF CACHE BOOL "Enable streamline feature" FORCE)
  endif()
endif()

if(AUTO_DISABLE_HDF5_ON_MISSING_CXX AND HAVE_HDF5)
  find_library(_AUTO_HDF5_CXX_LIBRARY
    NAMES hdf5_cpp
    HINTS
      $ENV{HDF5_ROOT}
      $ENV{HDF5_DIR}
      $ENV{HDF5_HOME}
      $ENV{EBROOTHDF5}
      $ENV{HDF5_ROOT}/lib
      $ENV{HDF5_DIR}/lib
      $ENV{HDF5_HOME}/lib
      $ENV{EBROOTHDF5}/lib
    PATH_SUFFIXES lib lib64)

  if(NOT _AUTO_HDF5_CXX_LIBRARY)
    message(WARNING
      "HAVE_HDF5 was enabled, but libhdf5_cpp was not found. "
      "Disabling HAVE_HDF5 for this build.")
    set(HAVE_HDF5 OFF CACHE BOOL "Enable HDF5 IO" FORCE)
  endif()
endif()

if(AUTO_DISABLE_LUA_ON_MISSING_DEPS AND USE_LUA)
  set(_AUTO_HAVE_LUA FALSE)
  find_package(Lua QUIET)
  if(TARGET Lua::Lua OR LUA_FOUND)
    set(_AUTO_HAVE_LUA TRUE)
  else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(_AUTO_LUA_PC QUIET lua-5.4 lua5.4 lua5.3 lua)
      if(_AUTO_LUA_PC_FOUND)
        set(_AUTO_HAVE_LUA TRUE)
      endif()
    endif()
  endif()

  if(NOT _AUTO_HAVE_LUA)
    message(WARNING
      "USE_LUA was enabled, but Lua development files were not found. "
      "Disabling USE_LUA for this build.")
    set(USE_LUA OFF CACHE BOOL "Enable Lua" FORCE)
  endif()
endif()

if(NOT HAVE_HDF5 AND CLUMP_DATA_READ)
  message(STATUS "CLUMP_DATA_READ requires HDF5; disabling CLUMP_DATA_READ")
  set(CLUMP_DATA_READ OFF CACHE BOOL "Enable Clump data read" FORCE)
endif()

function(add_egl_probe_target)
  find_package(OpenGL COMPONENTS EGL QUIET)
  if(TARGET OpenGL::EGL)
    add_executable(egl_probe tools/egl_probe/main.cpp)
    target_link_libraries(egl_probe PRIVATE OpenGL::EGL)
  else()
    find_path(EGL_INCLUDE_DIR NAMES EGL/egl.h)
    find_library(EGL_LIBRARY NAMES EGL)
    if(NOT EGL_INCLUDE_DIR OR NOT EGL_LIBRARY)
      message(FATAL_ERROR "EGL not found. Install EGL headers/libraries or set CMAKE_PREFIX_PATH.")
    endif()
    add_executable(egl_probe tools/egl_probe/main.cpp)
    target_include_directories(egl_probe PRIVATE ${EGL_INCLUDE_DIR})
    target_link_libraries(egl_probe PRIVATE ${EGL_LIBRARY})
  endif()

  target_compile_options(egl_probe PRIVATE -Wall -Wextra -O0 -g -fno-lto)
  target_link_options(egl_probe PRIVATE -fno-lto)
endfunction()

if(BUILD_EGL_PROBE_ONLY)
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "BUILD_EGL_PROBE_ONLY is only supported on Linux/Unix EGL environments.")
  endif()
  add_egl_probe_target()
  return()
endif()

# ===== Compile definition helper =====
function(add_def cond defname)
  if(${cond})
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${defname})
  endif()
endfunction()

# ===== Dependency paths =====
set(SRC_DIR      ${CMAKE_SOURCE_DIR}/src)
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)
set(PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR
  "${EXTERNAL_DIR}/submodules"
  CACHE PATH "Directory containing optional dependency git submodules")

function(particle_vis_prepend_prefix_if_exists prefix)
  if(EXISTS "${prefix}")
    list(PREPEND CMAKE_PREFIX_PATH "${prefix}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
  endif()
endfunction()

function(particle_vis_prepend_pkgconfig_if_exists prefix)
  if(EXISTS "${prefix}/lib/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "${prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  endif()
  if(EXISTS "${prefix}/lib64/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "${prefix}/lib64/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  endif()
  if(EXISTS "${prefix}/share/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "${prefix}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  endif()
endfunction()

function(particle_vis_register_dependency_prefixes dep)
  if(NOT PARTICLE_VIS_PREFER_SUBMODULE_DEPS)
    return()
  endif()

  set(_prefix_candidates
    "${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/${dep}"
    "${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/${dep}/install"
    "${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/${dep}/build"
    "${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/${dep}/build/install"
    "${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/_install/${dep}")

  foreach(_prefix IN LISTS _prefix_candidates)
    particle_vis_prepend_prefix_if_exists("${_prefix}")
    particle_vis_prepend_pkgconfig_if_exists("${_prefix}")
  endforeach()
endfunction()

function(particle_vis_get_submodule_source_dir dep out_var)
  set(${out_var} "${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/${dep}" PARENT_SCOPE)
endfunction()

function(particle_vis_add_glfw_submodule_if_present)
  if(NOT PARTICLE_VIS_ALLOW_SUBMODULE_BUILDS)
    return()
  endif()

  if(TARGET glfw OR TARGET glfw::glfw OR TARGET glfw3 OR TARGET glfw3::glfw3)
    return()
  endif()

  particle_vis_get_submodule_source_dir(glfw _glfw_root)
  if(EXISTS "${_glfw_root}/CMakeLists.txt")
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${_glfw_root}" "${CMAKE_BINARY_DIR}/_deps/glfw" EXCLUDE_FROM_ALL)
    message(STATUS "glfw: using source submodule at ${_glfw_root}")
  endif()
endfunction()

foreach(_particle_vis_dep IN ITEMS
    glfw glm eigen vtk hdf5 libzmq cppzmq nlohmann_json lua cgal gmp mpfr)
  particle_vis_register_dependency_prefixes("${_particle_vis_dep}")
endforeach()

# ===== Source list =====
set(SOURCES_CPP
  ${SRC_DIR}/main.cpp
  ${SRC_DIR}/UI/common_ui.cpp
  ${SRC_DIR}/UI/tool_window_ui.cpp
  ${SRC_DIR}/UI/settings_ui.cpp
  ${SRC_DIR}/analysis/histogram2d.cpp
  ${SRC_DIR}/analysis/radial_profile.cpp
  ${SRC_DIR}/render/particle_label_overlay.cpp
  ${SRC_DIR}/platform/shell_utils.cpp
  ${SRC_DIR}/platform/imgui_context.cpp
  ${SRC_DIR}/platform/window_context.cpp
  ${SRC_DIR}/platform/glfw_window.cpp
  ${SRC_DIR}/platform/viewport_context.cpp
  ${SRC_DIR}/platform/opengl_context.cpp
  ${SRC_DIR}/platform/local_present.cpp
  ${SRC_DIR}/platform/platform_session.cpp
  ${SRC_DIR}/app/app_services.cpp  
  ${SRC_DIR}/app/app_callbacks.cpp
  ${SRC_DIR}/app/app_lifecycle.cpp
  ${SRC_DIR}/app/app_frame.cpp
  ${SRC_DIR}/app/execution/input_execution.cpp
  ${SRC_DIR}/app/execution/analysis_dispatch.cpp
  ${SRC_DIR}/app/execution/geometric_analysis_execution.cpp
  ${SRC_DIR}/app/execution/field_geometry_execution.cpp
  ${SRC_DIR}/app/execution/convex_hull_execution.cpp
  ${SRC_DIR}/app/execution/settings_navigation_execution.cpp
  ${SRC_DIR}/app/execution/clump_window_execution.cpp
  ${SRC_DIR}/app/execution/clump_batch_execution.cpp
  ${SRC_DIR}/app/app_derived_rebuild.cpp
  ${SRC_DIR}/app/app_render_sync.cpp
  ${SRC_DIR}/app/execution/projection_execution.cpp
  ${SRC_DIR}/app/settings_analysis_requests.cpp
  ${SRC_DIR}/app/app_snapshot_load.cpp
  ${SRC_DIR}/app/execution/tool_particle_execution.cpp
  ${SRC_DIR}/app/execution/tool_analysis_execution.cpp
  ${SRC_DIR}/app/execution/tool_projection_execution.cpp
  ${SRC_DIR}/app/execution/tool_filter_execution.cpp
  ${SRC_DIR}/app/execution/tool_halo_execution.cpp
  ${SRC_DIR}/app/execution/tool_clump_execution.cpp
  ${SRC_DIR}/app/execution/tool_window_execution.cpp
  ${SRC_DIR}/app/execution/profile_histogram_execution.cpp
  ${SRC_DIR}/app/app_data_actions.cpp
  ${SRC_DIR}/app/app_visibility_actions.cpp
  ${SRC_DIR}/render/frame_matrices.cpp
  ${SRC_DIR}/render/colormap_defs.cpp
  ${SRC_DIR}/render/render_system.cpp
  ${SRC_DIR}/render/render_frame.cpp
  ${SRC_DIR}/render/opengl/opengl_render_backend.cpp
  ${SRC_DIR}/render/opengl/render_programs.cpp
  ${SRC_DIR}/render/opengl/shader_utils.cpp
  ${SRC_DIR}/render/opengl/shader_sources.cpp
  ${SRC_DIR}/render/opengl/field_renderer.cpp
  ${SRC_DIR}/render/opengl/particle_renderer.cpp
  ${SRC_DIR}/render/opengl/object_renderer.cpp
  ${SRC_DIR}/render/opengl/gizmo_renderer.cpp
  ${SRC_DIR}/render/opengl/projection_preview_gl.cpp
  ${SRC_DIR}/render/render_resources.cpp
  ${SRC_DIR}/interaction/interaction_utils.cpp
  ${SRC_DIR}/config/config_io.cpp
  ${SRC_DIR}/config/config_apply.cpp
  ${SRC_DIR}/config/config_extract.cpp
  ${SRC_DIR}/config/config_validation.cpp
  ${SRC_DIR}/data/particle_block.cpp
  ${SRC_DIR}/data/particle_block_validation.cpp
  ${SRC_DIR}/data/particle_array.cpp
  ${SRC_DIR}/data/clump_loader.cpp
  ${SRC_DIR}/data/halo_store.cpp
  ${SRC_DIR}/data/quantity_catalog_builder.cpp
  ${SRC_DIR}/FileIO/hdf5_reader.cpp
  ${SRC_DIR}/FileIO/snapshot_io_service.cpp
  ${SRC_DIR}/UI/file_format_dialog.cpp
  ${SRC_DIR}/FileIO/snapshot_loader.cpp
  ${SRC_DIR}/FileIO/snapshot_prefetch_controller.cpp
  ${SRC_DIR}/FileIO/clump_io.cpp
  ${SRC_DIR}/analysis/clump/clump_chain.cpp
  ${SRC_DIR}/analysis/clump/find_clumps.cpp
  ${SRC_DIR}/app/execution/loaded_clump_tool.cpp
  ${SRC_DIR}/UI/clump_finder_window.cpp
  ${SRC_DIR}/UI/clump_loaded_window.cpp
  ${SRC_DIR}/UI/clump_chain_window.cpp
  ${SRC_DIR}/projection/make_2D_projection_map.cpp
  ${SRC_DIR}/projection/projection_frame_execution.cpp
  ${SRC_DIR}/projection/projection_geometry.cpp
  ${SRC_DIR}/projection/projection_map_context.cpp
  ${SRC_DIR}/image/image_canvas.cpp
  ${SRC_DIR}/image/image_io.cpp
  ${SRC_DIR}/image/bitmap_font_renderer.cpp
  ${SRC_DIR}/data/spatial/particle_octree.cpp
  ${SRC_DIR}/analysis/isosurface/density_evaluate.cpp
)

if(GEOMETRICAL_ANALYSIS)
  list(APPEND SOURCES_CPP ${SRC_DIR}/analysis/ellipse_fitter.cpp)
endif()

if(USE_CONVEX_HULL)
  find_package(CGAL QUIET)
  if(NOT TARGET CGAL::CGAL AND NOT CGAL_FOUND)
    message(WARNING
      "USE_CONVEX_HULL was enabled, but CGAL is not available. "
      "Disabling USE_CONVEX_HULL for this build.")
    set(USE_CONVEX_HULL OFF CACHE BOOL "Enable Convex Hull" FORCE)
  else()
    list(APPEND SOURCES_CPP ${SRC_DIR}/analysis/convex_hull/convex_hull_generator.cpp)
  endif()
endif()

if(STREAM_LINE)
  list(APPEND SOURCES_CPP ${SRC_DIR}/analysis/streamline/streamline.cpp)
endif()

if(VOLUME_RENDERING)
  list(APPEND SOURCES_CPP ${SRC_DIR}/volume/tau_sph.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/volume/adaptive_volume_tree.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/render/opengl/adaptive_volume_renderer.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/app/execution/volume_rendering_execution.cpp)
endif()

if(PYTHON_BRIDGE)
  list(APPEND SOURCES_CPP ${SRC_DIR}/app/execution/python_bridge_execution.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/PythonBridge.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/PythonBridgeRpcServer.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/PythonBridgeSharedMemory.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/BridgeAdapter.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/ShmCommon.cpp)
  list(APPEND SOURCES_CPP ${SRC_DIR}/PythonBridge/JupyterLauncher.cpp)
endif()

list(APPEND SOURCES_CPP ${SRC_DIR}/platform/remote_frame_presenter.cpp)
list(APPEND SOURCES_CPP ${SRC_DIR}/platform/remote_input_receiver.cpp)

if(PARTICLE_VIS_HEADLESS_ONLY)
  list(REMOVE_ITEM SOURCES_CPP ${SRC_DIR}/app/app_callbacks.cpp)
endif()

# ImGui / ImPlot / NFD as needed.
set(IMGUI_SRC
  ${EXTERNAL_DIR}/imgui/imgui.cpp
  ${EXTERNAL_DIR}/imgui/imgui_draw.cpp
  ${EXTERNAL_DIR}/imgui/imgui_widgets.cpp
  ${EXTERNAL_DIR}/imgui/imgui_tables.cpp
	${EXTERNAL_DIR}/imgui/imgui_demo.cpp
	${EXTERNAL_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

if(NOT PARTICLE_VIS_HEADLESS_ONLY)
  list(APPEND IMGUI_SRC ${EXTERNAL_DIR}/imgui/backends/imgui_impl_glfw.cpp)
endif()

set(IMPLOT_SRC
	${EXTERNAL_DIR}/implot/implot.cpp
	${EXTERNAL_DIR}/implot/implot_items.cpp
)

# glad for desktop GL on Windows/macOS/Linux.
# When USE_GLES=ON, prefer <GLES3/gl3.h> instead of glad.
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

# Main target.
add_executable(${PROJECT_NAME}
  ${SOURCES_CPP}
  ${IMGUI_SRC}
  ${IMPLOT_SRC}
)

# ===== Include paths =====
target_include_directories(${PROJECT_NAME} PRIVATE
  ${SRC_DIR}
)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
  ${EXTERNAL_DIR}/imgui
  ${EXTERNAL_DIR}/imgui/backends
  ${EXTERNAL_DIR}/implot
	${EXTERNAL_DIR}/include
	${EXTERNAL_DIR}
)

if(USE_CONVEX_HULL AND TARGET CGAL::CGAL)
  target_link_libraries(${PROJECT_NAME} PRIVATE CGAL::CGAL)
endif()


#debug macro
macro(target_link_libraries_report tgt scope)
  # Forward variadic arguments to target_link_libraries.
  target_link_libraries(${tgt} ${scope} ${ARGN})

  # Print the currently accumulated link items.
  get_target_property(_libs ${tgt} LINK_LIBRARIES)
  message(STATUS "[link-report] ${tgt}: ${_libs}")
endmacro()

function(_print_links tag)
  get_property(_libs TARGET ${PROJECT_NAME} PROPERTY LINK_LIBRARIES)
  message(STATUS "[trace:${tag}] LINK_LIBRARIES = ${_libs}")
  # Count selected link items.
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
    # List the required Homebrew packages.
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

  # Avoid accidentally mixing in Intel libraries.
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
  # Prefer pkg-config on Linux.
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
  # lib_name: CMake package name, e.g. GLFW, GMP, VTK.
  # pkg_name: pkg-config package name, e.g. glfw3, gmp, vtk.

  # 1. CMake find_package.
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

  particle_vis_add_glfw_submodule_if_present()
  if(TARGET glfw)
    target_link_libraries(${target} PRIVATE glfw)
    return()
  elseif(TARGET glfw::glfw)
    target_link_libraries(${target} PRIVATE glfw::glfw)
    return()
  elseif(TARGET glfw3::glfw3)
    target_link_libraries(${target} PRIVATE glfw3::glfw3)
    return()
  elseif(TARGET glfw3)
    target_link_libraries(${target} PRIVATE glfw3)
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
  find_path(${lib_name}_INC
    NAMES GLFW/glfw3.h glfw3.h ${pkg_name}.h
    PATH_SUFFIXES include)
  if(${lib_name}_LIB AND ${lib_name}_INC)
    target_include_directories(${target} PRIVATE ${${lib_name}_INC})
    target_link_libraries(${target} PRIVATE ${${lib_name}_LIB})
    message(STATUS "[detect_and_link] ${lib_name} → LINK_LIBRARIES = ${_libs}")
  else()
    message(FATAL_ERROR "${lib_name} not found. Please install it.")
  endif()
endfunction()


if(NONATIVEFILEDIALOG)
  # Use ImGuiFileDialog instead of the native NFD backend.
  target_sources(${PROJECT_NAME} PRIVATE
    ${EXTERNAL_DIR}/ImGuiFileDialog/ImGuiFileDialog.cpp
  )
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${EXTERNAL_DIR}/ImGuiFileDialog
  )
  target_compile_definitions(${PROJECT_NAME} PRIVATE NONATIVEFILEDIALOG)
else()
  find_path(NFD_INCLUDE_DIR
    NAMES nfd.h
    PATH_SUFFIXES include
  )

  find_library(NFD_LIBRARY
    NAMES nfd
    PATH_SUFFIXES lib
  )

  if(NOT NFD_INCLUDE_DIR OR NOT NFD_LIBRARY)
    message(WARNING
      "nativefiledialog-extended not found; falling back to ImGuiFileDialog backend.")
    target_sources(${PROJECT_NAME} PRIVATE
      ${EXTERNAL_DIR}/ImGuiFileDialog/ImGuiFileDialog.cpp
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
      ${EXTERNAL_DIR}/ImGuiFileDialog
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE NONATIVEFILEDIALOG)
  else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${NFD_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${NFD_LIBRARY})

    if(APPLE)
      find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
      find_library(UTI_FRAMEWORK UniformTypeIdentifiers REQUIRED)
      target_link_libraries(${PROJECT_NAME} PRIVATE
        ${APPKIT_FRAMEWORK}
        ${UTI_FRAMEWORK}
      )
    elseif(WIN32)
      target_link_libraries(${PROJECT_NAME} PRIVATE
        ole32 uuid shell32
      )
    endif()
  endif()
endif()


# ===== ISO_CONTOUR (VTK integration) =====
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
  
  # VTK modules to use, matching the old Makefile list.
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
  
  # Detect VTK. This expects VTK 9.x; adjust the version constraint if needed.
  find_package(VTK 9.0 COMPONENTS ${VTK_COMPONENTS} QUIET)
# find_package(VTK CONFIG REQUIRED BYPASS_PROVIDER COMPONENTS ${VTK_COMPONENTS})
#  find_package(VTK)

  if(VTK_FOUND)
    if(VTK_VERSION VERSION_LESS "9.0" OR VTK_VERSION VERSION_GREATER_EQUAL "10.0")
#    message(FATAL_ERROR "VTK 9.x is required, but found ${VTK_VERSION}")
    endif()

    # Additional sources corresponding to the old Makefile.
    target_sources(${PROJECT_NAME} PRIVATE
      ${SRC_DIR}/analysis/isosurface/iso_contour_build.cpp
      ${SRC_DIR}/analysis/isosurface/iso_surface_field.cpp
      ${SRC_DIR}/analysis/isosurface/marching_cubes.cpp
      ${SRC_DIR}/analysis/isosurface/connectivity_test.cpp
      ${SRC_DIR}/analysis/isosurface/isosurface_generator.cpp
    )

    # Include paths.
    target_include_directories(${PROJECT_NAME} PRIVATE
      ${SRC_DIR}/analysis/isosurface
      ${VTK_INCLUDE_DIRS}      # VTK headers
    )

    # Link imported targets, which is the preferred CMake style.
    # Example: VTK::CommonCore, VTK::FiltersCore, ...
    foreach(c IN LISTS VTK_COMPONENTS)
      target_link_libraries(${PROJECT_NAME} PRIVATE VTK::${c})
    endforeach()
  else()
    message(WARNING
      "ISO_CONTOUR was enabled, but VTK was not found. Disabling ISO_CONTOUR for this build.")
    set(ISO_CONTOUR OFF CACHE BOOL "Enable iso-contour/VTK" FORCE)
  endif()
  
  # Help macOS find VTK dylibs at runtime, like the old Makefile rpath flags.
  if(APPLE)
    # Add VTK_ROOT/lib to rpath; adjust the path if needed.
    set_target_properties(${PROJECT_NAME} PROPERTIES
      BUILD_RPATH "${VTK_ROOT}/lib"
      INSTALL_RPATH "${VTK_ROOT}/lib")
  endif()
  
  if(ISO_CONTOUR)
    # Pass ISO_CONTOUR to C++.
    target_compile_definitions(${PROJECT_NAME} PRIVATE ISO_CONTOUR)
  endif()
endif()

if(NOT PARTICLE_VIS_HEADLESS_ONLY)
  detect_and_link_glfw(${PROJECT_NAME} "glfw" "glfw")
endif()
detect_and_link(${PROJECT_NAME} "GMP" "gmp")
detect_and_link(${PROJECT_NAME} "MPFR" "mpfr")

if(PYTHON_BRIDGE)
  detect_and_link(${PROJECT_NAME} "ZeroMQ" "libzmq" CMAKE_TARGET libzmq)
  detect_and_link(${PROJECT_NAME} "cppzmq" "cppzmq" HEADER_ONLY)
  detect_and_link(${PROJECT_NAME} "nlohmann_json" "nlohmann-json"
    CMAKE_TARGET nlohmann_json::nlohmann_json HEADER_ONLY)  
endif()

# =========================
# GLM (header-only)
# =========================
set(_GLM_FOUND FALSE)
find_package(glm QUIET CONFIG)

if(TARGET glm::glm)
  target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
  set(_GLM_FOUND TRUE)
  message(STATUS "GLM: found via CMake target glm::glm")
endif()

if(NOT _GLM_FOUND AND PKG_CONFIG_FOUND)
  pkg_check_modules(GLM_PC QUIET glm)
  if(GLM_PC_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${GLM_PC_INCLUDE_DIRS})
    set(_GLM_FOUND TRUE)
    message(STATUS "GLM: found via pkg-config include=${GLM_PC_INCLUDE_DIRS}")
  endif()
endif()

if(NOT _GLM_FOUND)
  find_path(GLM_INCLUDE_DIR
    NAMES glm/glm.hpp
    HINTS
      ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/glm
      ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/_install/glm/include
      $ENV{GLM_INCLUDE_DIR}
      $ENV{GLM_ROOT}
      $ENV{GLM_DIR}
      $ENV{GLM_HOME}
      ${GLM_ROOT}
      ${GLM_DIR}
      ${GLM_HOME}
      ${EXTERNAL_DIR}
    PATH_SUFFIXES include)

  if(GLM_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${GLM_INCLUDE_DIR})
    set(_GLM_FOUND TRUE)
    message(STATUS "GLM: include=${GLM_INCLUDE_DIR}")
  endif()
endif()

if(NOT _GLM_FOUND)
  message(FATAL_ERROR
    "GLM headers not found. Load/install GLM, set CMAKE_PREFIX_PATH, "
    "or pass -DGLM_INCLUDE_DIR=/path/to/glm/include.")
endif()

# =========================
# Eigen (header-only)
# =========================
if(GEOMETRICAL_ANALYSIS)
find_package(Eigen3 3.3 QUIET CONFIG)

if(TARGET Eigen3::Eigen)
  target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)
else()
  # 2. Try pkg-config if it is available.
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(EIGEN3_PC QUIET eigen3)
    if(EIGEN3_PC_FOUND)
      add_library(Eigen3::Eigen INTERFACE IMPORTED)
      set_target_properties(Eigen3::Eigen PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_PC_INCLUDE_DIRS}")
      target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)
    endif()
  endif()

  # 3. Fall back to find_path, using standard paths and CMAKE_PREFIX_PATH.
  if(NOT TARGET Eigen3::Eigen)
    find_path(EIGEN3_INCLUDE_DIR
      NAMES Eigen/Core
      PATH_SUFFIXES eigen3
      HINTS
        ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/eigen
        ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/_install/eigen/include
        $ENV{EIGEN3_INCLUDE_DIR}  # Manual override.
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
endif()

# =========================
# OpenMP (optional)
# =========================
# 1. Prefer standard FindOpenMP for MSVC/GCC/Clang.
find_package(OpenMP QUIET)

if(OpenMP_CXX_FOUND)
  target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
else()
  # 2. Fallback if OpenMP was not found.
  if(MSVC)
    # Fallback for older toolchains where FindOpenMP can miss MSVC support.
    target_compile_options(${PROJECT_NAME} PRIVATE /openmp)
  elseif(APPLE)
    # AppleClang does not ship an OpenMP runtime, so look for Homebrew libomp.
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
    # Manual Linux/other-OS fallback, mostly a safety net.
    # Example: unusual distributions where FindOpenMP is unavailable.
    target_compile_options(${PROJECT_NAME} PRIVATE -fopenmp)
    target_link_libraries(${PROJECT_NAME}    PRIVATE gomp)  # Typical GCC libgomp.
  endif()
endif()

# ---- Desktop OpenGL ----
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

if(USE_EGL_HEADLESS AND UNIX AND NOT APPLE)
  find_package(OpenGL COMPONENTS EGL QUIET)
  if(TARGET OpenGL::EGL)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::EGL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PARTICLE_VIS_HAVE_EGL)
    message(STATUS "EGL headless context support: ON")
  else()
    find_path(EGL_INCLUDE_DIR NAMES EGL/egl.h)
    find_library(EGL_LIBRARY NAMES EGL)
    if(EGL_INCLUDE_DIR AND EGL_LIBRARY)
      target_include_directories(${PROJECT_NAME} PRIVATE ${EGL_INCLUDE_DIR})
      target_link_libraries(${PROJECT_NAME} PRIVATE ${EGL_LIBRARY})
      target_compile_definitions(${PROJECT_NAME} PRIVATE PARTICLE_VIS_HAVE_EGL)
      message(STATUS "EGL headless context support: ON (${EGL_LIBRARY})")
    else()
      message(WARNING "EGL not found. Headless remote rendering will fall back to GLFW.")
    endif()
  endif()
endif()

if(PARTICLE_VIS_HEADLESS_ONLY)
  target_compile_definitions(${PROJECT_NAME} PRIVATE PARTICLE_VIS_HEADLESS_ONLY)
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "PARTICLE_VIS_HEADLESS_ONLY currently requires Linux/Unix EGL.")
  endif()
  if(NOT USE_EGL_HEADLESS)
    message(FATAL_ERROR "PARTICLE_VIS_HEADLESS_ONLY requires USE_EGL_HEADLESS=ON.")
  endif()
endif()

# Use glad for desktop GL.
target_sources(${PROJECT_NAME} PRIVATE ${GLAD_SRC})
target_include_directories(${PROJECT_NAME} PRIVATE ${EXTERNAL_DIR}/glad/include)

# ===== HDF5 =====
if(HAVE_HDF5)
  if(NOT DEFINED HDF5_ROOT AND NOT DEFINED ENV{HDF5_ROOT} AND NOT DEFINED HDF5_DIR AND NOT DEFINED ENV{HDF5_DIR})
    find_program(_HDF5_WRAPPER_CXX NAMES h5c++ h5pcc h5cc)
    if(_HDF5_WRAPPER_CXX)
      get_filename_component(_HDF5_BIN_DIR "${_HDF5_WRAPPER_CXX}" DIRECTORY)
      get_filename_component(_HDF5_PREFIX "${_HDF5_BIN_DIR}/.." ABSOLUTE)
      set(HDF5_ROOT "${_HDF5_PREFIX}" CACHE PATH "HDF5 install prefix" FORCE)
      list(PREPEND CMAKE_PREFIX_PATH "${_HDF5_PREFIX}")
      message(STATUS "HDF5: inferred HDF5_ROOT=${HDF5_ROOT} from ${_HDF5_WRAPPER_CXX}")
    endif()
  endif()

  set(HDF5_PREFER_PARALLEL TRUE)
  find_package(HDF5 COMPONENTS CXX QUIET)
  if(HDF5_FOUND)
    if(TARGET HDF5::HDF5)
      target_link_libraries(${PROJECT_NAME} PRIVATE HDF5::HDF5)
    else()
      target_include_directories(${PROJECT_NAME} PRIVATE ${HDF5_INCLUDE_DIRS})
      target_link_libraries(${PROJECT_NAME} PRIVATE ${HDF5_LIBRARIES})
    endif()
    message(STATUS "HDF5: found via CMake")
  elseif(PKG_CONFIG_FOUND)
    pkg_check_modules(HDF5_PC QUIET hdf5 hdf5-openmpi hdf5-mpi hdf5-serial)
    if(HDF5_PC_FOUND)
      target_include_directories(${PROJECT_NAME} PRIVATE ${HDF5_PC_INCLUDE_DIRS})
      target_link_directories(${PROJECT_NAME} PRIVATE ${HDF5_PC_LIBRARY_DIRS})
      target_link_libraries(${PROJECT_NAME} PRIVATE ${HDF5_PC_LIBRARIES})
      message(STATUS "HDF5: found via pkg-config (${HDF5_PC_MODULE_NAME})")
    else()
      find_path(HDF5_MANUAL_INCLUDE_DIR
        NAMES H5Cpp.h
        HINTS
          ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/hdf5
          ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/_install/hdf5
          $ENV{HDF5_ROOT}
          $ENV{HDF5_DIR}
          $ENV{HDF5_HOME}
          $ENV{EBROOTHDF5}
          $ENV{HDF5_ROOT}/include
          $ENV{HDF5_DIR}/include
          $ENV{HDF5_HOME}/include
          $ENV{EBROOTHDF5}/include
        PATH_SUFFIXES include)
      find_library(HDF5_MANUAL_CXX_LIBRARY
        NAMES hdf5_cpp
        HINTS
          ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/hdf5
          ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/_install/hdf5
          $ENV{HDF5_ROOT}
          $ENV{HDF5_DIR}
          $ENV{HDF5_HOME}
          $ENV{EBROOTHDF5}
          $ENV{HDF5_ROOT}/lib
          $ENV{HDF5_DIR}/lib
          $ENV{HDF5_HOME}/lib
          $ENV{EBROOTHDF5}/lib
        PATH_SUFFIXES lib lib64)
      find_library(HDF5_MANUAL_C_LIBRARY
        NAMES hdf5
        HINTS
          ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/hdf5
          ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/_install/hdf5
          $ENV{HDF5_ROOT}
          $ENV{HDF5_DIR}
          $ENV{HDF5_HOME}
          $ENV{EBROOTHDF5}
          $ENV{HDF5_ROOT}/lib
          $ENV{HDF5_DIR}/lib
          $ENV{HDF5_HOME}/lib
          $ENV{EBROOTHDF5}/lib
        PATH_SUFFIXES lib lib64)
      if(HDF5_MANUAL_INCLUDE_DIR AND HDF5_MANUAL_CXX_LIBRARY AND HDF5_MANUAL_C_LIBRARY)
        target_include_directories(${PROJECT_NAME} PRIVATE ${HDF5_MANUAL_INCLUDE_DIR})
        target_link_libraries(${PROJECT_NAME} PRIVATE
          ${HDF5_MANUAL_CXX_LIBRARY}
          ${HDF5_MANUAL_C_LIBRARY})
        message(STATUS "HDF5: found manually include=${HDF5_MANUAL_INCLUDE_DIR}")
      else()
        message(FATAL_ERROR
          "HDF5 not found. Load an HDF5 module, set HDF5_ROOT/HDF5_DIR, "
          "or configure with -DHAVE_HDF5=OFF for a non-HDF5 test build.")
      endif()
    endif()
  endif()
endif()

# ===== Lua =====
if(USE_LUA)
# 1)  CMake  FindLua
  find_package(Lua QUIET)

# CMake module mode  LUA_FOUND / LUA_LIBRARIES / LUA_INCLUDE_DIR
  if(TARGET Lua::Lua)
    target_link_libraries(${PROJECT_NAME} PRIVATE Lua::Lua)
    message(STATUS "Lua: using imported target Lua::Lua")
  elseif(LUA_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LUA_LIBRARIES})
    message(STATUS "Lua: include=${LUA_INCLUDE_DIR}")
    message(STATUS "Lua: libs=${LUA_LIBRARIES}")
# 2) pkg-config fallback macOS/Linux
  elseif(PKG_CONFIG_FOUND)
    pkg_check_modules(LUA QUIET lua-5.4 lua5.4 lua5.3 lua)
    if(LUA_FOUND)
      target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIRS})
      target_link_directories(${PROJECT_NAME} PRIVATE ${LUA_LIBRARY_DIRS})
      target_link_libraries(${PROJECT_NAME} PRIVATE ${LUA_LIBRARIES})
      message(STATUS "Lua: pkg-config include=${LUA_INCLUDE_DIRS}")
      message(STATUS "Lua: pkg-config libs=${LUA_LIBRARIES}")
    else()
      message(WARNING "Lua not found.")
    endif()
  else()
      message(WARNING "Lua not found.")
  endif()
endif()

# ===== Compile definitions passed to C++ =====
add_def(DEBUG_MODE          DEBUG_MODE)
add_def(USE_LUA             USE_LUA)
add_def(USE_CONVEX_HULL     USE_CONVEX_HULL)
add_def(USE_LETTERBOX       USE_LETTERBOX)
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

# ===== Optimization, ASan, and related build-type settings =====
if(MSVC)
  # /MP: parallel build.
  target_compile_options(${PROJECT_NAME} PRIVATE /MP)
else()
  # Common GCC/Clang warnings.
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "USE_GLES: ${USE_GLES}")

# ===== After build: create a symlink to the executable in the repo root =====
set(_ROOT_LINK_NAME
  $<IF:$<PLATFORM_ID:Windows>,${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.exe,${CMAKE_SOURCE_DIR}/${PROJECT_NAME}>
)

# Option: on Windows, fall back to copying if symlink privileges are unavailable.
if(WIN32)
  option(WINDOWS_LINK_AS_COPY "On Windows, copy the exe to repo root instead of making a symlink" ON)
else()
  option(WINDOWS_LINK_AS_COPY "On Windows, copy the exe to repo root instead of making a symlink" OFF)
endif()


if(WIN32)
  file(GLOB WIN_RUNTIME_DLLS "${CMAKE_BINARY_DIR}/*.dll")

  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${WIN_RUNTIME_DLLS}
            "${CMAKE_SOURCE_DIR}"
    COMMENT "Copying ${PROJECT_NAME}.exe and runtime DLLs to repo root"
    VERBATIM)
else()
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${_ROOT_LINK_NAME}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:${PROJECT_NAME}>" "${_ROOT_LINK_NAME}"
    COMMENT "Creating symlink to ${PROJECT_NAME} in repo root"
    VERBATIM)
endif()

function(link_glm_headers_to tgt)
  if(TARGET glm::glm)
    target_link_libraries(${tgt} PRIVATE glm::glm)
  elseif(GLM_PC_FOUND)
    target_include_directories(${tgt} PRIVATE ${GLM_PC_INCLUDE_DIRS})
  elseif(GLM_INCLUDE_DIR)
    target_include_directories(${tgt} PRIVATE ${GLM_INCLUDE_DIR})
  else()
    message(FATAL_ERROR "GLM headers not found for target ${tgt}.")
  endif()
endfunction()

function(link_hdf5_to tgt)
  if(NOT HAVE_HDF5)
    return()
  endif()

  target_compile_definitions(${tgt} PRIVATE HAVE_HDF5)
  if(TARGET HDF5::HDF5)
    target_link_libraries(${tgt} PRIVATE HDF5::HDF5)
  elseif(HDF5_FOUND)
    target_include_directories(${tgt} PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(${tgt} PRIVATE ${HDF5_LIBRARIES})
  elseif(HDF5_PC_FOUND)
    target_include_directories(${tgt} PRIVATE ${HDF5_PC_INCLUDE_DIRS})
    target_link_directories(${tgt} PRIVATE ${HDF5_PC_LIBRARY_DIRS})
    target_link_libraries(${tgt} PRIVATE ${HDF5_PC_LIBRARIES})
  elseif(HDF5_MANUAL_INCLUDE_DIR AND HDF5_MANUAL_CXX_LIBRARY AND HDF5_MANUAL_C_LIBRARY)
    target_include_directories(${tgt} PRIVATE ${HDF5_MANUAL_INCLUDE_DIR})
    target_link_libraries(${tgt} PRIVATE
      ${HDF5_MANUAL_CXX_LIBRARY}
      ${HDF5_MANUAL_C_LIBRARY})
  else()
    message(FATAL_ERROR "HDF5 was enabled but no link information is available for target ${tgt}.")
  endif()
endfunction()

function(link_lua_to tgt)
  if(NOT USE_LUA)
    return()
  endif()

  if(TARGET Lua::Lua)
    target_compile_definitions(${tgt} PRIVATE USE_LUA)
    target_link_libraries(${tgt} PRIVATE Lua::Lua)
  elseif(LUA_FOUND AND LUA_LIBRARIES)
    target_compile_definitions(${tgt} PRIVATE USE_LUA)
    if(LUA_INCLUDE_DIR)
      target_include_directories(${tgt} PRIVATE ${LUA_INCLUDE_DIR})
    endif()
    if(LUA_INCLUDE_DIRS)
      target_include_directories(${tgt} PRIVATE ${LUA_INCLUDE_DIRS})
    endif()
    if(LUA_LIBRARY_DIRS)
      target_link_directories(${tgt} PRIVATE ${LUA_LIBRARY_DIRS})
    endif()
    target_link_libraries(${tgt} PRIVATE ${LUA_LIBRARIES})
  endif()
endfunction()

function(link_nlohmann_json_to tgt)
  find_package(nlohmann_json QUIET CONFIG)
  if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(${tgt} PRIVATE nlohmann_json::nlohmann_json)
    return()
  endif()

  if(PKG_CONFIG_FOUND)
    pkg_check_modules(NLOHMANN_JSON_PC QUIET nlohmann_json nlohmann-json)
    if(NLOHMANN_JSON_PC_FOUND)
      target_include_directories(${tgt} PRIVATE ${NLOHMANN_JSON_PC_INCLUDE_DIRS})
      return()
    endif()
  endif()

  find_path(NLOHMANN_JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
    HINTS
      ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/nlohmann_json
      ${PARTICLE_VIS_OPTIONAL_SUBMODULE_DIR}/_install/nlohmann_json/include
      $ENV{NLOHMANN_JSON_INCLUDE_DIR}
      $ENV{NLOHMANN_JSON_ROOT}
      $ENV{NLOHMANN_JSON_DIR}
      $ENV{NLOHMANN_JSON_HOME}
      ${EXTERNAL_DIR}/include
      ${EXTERNAL_DIR}
    PATH_SUFFIXES include)
  if(NLOHMANN_JSON_INCLUDE_DIR)
    target_include_directories(${tgt} PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
    return()
  endif()

  message(FATAL_ERROR
    "nlohmann_json not found for target ${tgt}. Install/load nlohmann_json, "
    "set CMAKE_PREFIX_PATH, or set NLOHMANN_JSON_INCLUDE_DIR.")
endfunction()

function(link_eigen_to tgt)
  if(TARGET Eigen3::Eigen)
    target_link_libraries(${tgt} PRIVATE Eigen3::Eigen)
  elseif(EIGEN3_INCLUDE_DIR)
    target_include_directories(${tgt} PRIVATE ${EIGEN3_INCLUDE_DIR})
  elseif(EIGEN3_PC_FOUND)
    target_include_directories(${tgt} PRIVATE ${EIGEN3_PC_INCLUDE_DIRS})
  else()
    message(FATAL_ERROR "Eigen3 headers not found for target ${tgt}.")
  endif()
endfunction()

if(PARTICLE_VIS_BUILD_BATCH)
  set(PARTICLE_VIS_BATCH_SOURCES
    ${CMAKE_SOURCE_DIR}/tools/particle_vis_batch/main.cpp
    ${SRC_DIR}/app/app_services.cpp
    ${SRC_DIR}/app/app_snapshot_load.cpp
    ${SRC_DIR}/app/execution/profile_histogram_execution.cpp
    ${SRC_DIR}/app/execution/projection_execution.cpp
    ${SRC_DIR}/analysis/histogram2d.cpp
    ${SRC_DIR}/analysis/radial_profile.cpp
    ${SRC_DIR}/config/config_apply.cpp
    ${SRC_DIR}/config/config_io.cpp
    ${SRC_DIR}/config/config_validation.cpp
    ${SRC_DIR}/data/particle_array.cpp
    ${SRC_DIR}/data/particle_block.cpp
    ${SRC_DIR}/data/particle_block_validation.cpp
    ${SRC_DIR}/data/quantity_catalog_builder.cpp
    ${SRC_DIR}/FileIO/hdf5_reader.cpp
    ${SRC_DIR}/FileIO/snapshot_io_service.cpp
    ${SRC_DIR}/FileIO/snapshot_loader.cpp
    ${SRC_DIR}/FileIO/snapshot_prefetch_controller.cpp
    ${SRC_DIR}/image/bitmap_font_renderer.cpp
    ${SRC_DIR}/image/image_canvas.cpp
    ${SRC_DIR}/image/image_io.cpp
    ${SRC_DIR}/interaction/interaction_utils.cpp
    ${SRC_DIR}/platform/shell_utils.cpp
    ${SRC_DIR}/projection/make_2D_projection_map.cpp
    ${SRC_DIR}/projection/projection_frame_execution.cpp
    ${SRC_DIR}/projection/projection_geometry.cpp
    ${SRC_DIR}/projection/projection_map_context.cpp
    ${SRC_DIR}/render/colormap_defs.cpp
  )

  if(GEOMETRICAL_ANALYSIS)
    list(APPEND PARTICLE_VIS_BATCH_SOURCES
      ${SRC_DIR}/app/execution/geometric_analysis_execution.cpp
      ${SRC_DIR}/app/execution/settings_navigation_execution.cpp
      ${SRC_DIR}/app/app_data_actions.cpp
      ${SRC_DIR}/app/app_visibility_actions.cpp
      ${SRC_DIR}/data/clump_loader.cpp
      ${SRC_DIR}/data/halo_store.cpp
      ${SRC_DIR}/analysis/ellipse_fitter.cpp
    )
  endif()

  if(CLUMP_DATA_READ)
    list(APPEND PARTICLE_VIS_BATCH_SOURCES
      ${SRC_DIR}/app/execution/clump_batch_execution.cpp
      ${SRC_DIR}/analysis/clump/find_clumps.cpp
      ${SRC_DIR}/FileIO/clump_io.cpp
      ${SRC_DIR}/analysis/clump/clump_chain.cpp
      ${SRC_DIR}/data/clump_loader.cpp
    )
  endif()

  list(REMOVE_DUPLICATES PARTICLE_VIS_BATCH_SOURCES)

  add_executable(particle_vis_batch ${PARTICLE_VIS_BATCH_SOURCES})

  target_include_directories(particle_vis_batch PRIVATE
    ${SRC_DIR}
  )

  target_include_directories(particle_vis_batch SYSTEM PRIVATE
    ${EXTERNAL_DIR}/imgui
    ${EXTERNAL_DIR}/implot
    ${EXTERNAL_DIR}/glad/include
    ${EXTERNAL_DIR}/include
    ${EXTERNAL_DIR}
  )

  link_glm_headers_to(particle_vis_batch)
  link_hdf5_to(particle_vis_batch)
  link_lua_to(particle_vis_batch)
  link_nlohmann_json_to(particle_vis_batch)
  if(GEOMETRICAL_ANALYSIS)
    link_eigen_to(particle_vis_batch)
  endif()

  if(USE_MMAP)
    target_compile_definitions(particle_vis_batch PRIVATE USE_MMAP)
  endif()
  if(ROTATE_QUATERNION)
    target_compile_definitions(particle_vis_batch PRIVATE ROTATE_QUATERNION)
  endif()
  if(ROTATE_ARCBALL)
    target_compile_definitions(particle_vis_batch PRIVATE ROTATE_ARCBALL)
  endif()
  if(GEOMETRICAL_ANALYSIS)
    target_compile_definitions(particle_vis_batch PRIVATE GEOMETRICAL_ANALYSIS)
  endif()
  if(CLUMP_DATA_READ)
    target_compile_definitions(particle_vis_batch PRIVATE CLUMP_DATA_READ)
  endif()
  if(OpenMP_CXX_FOUND)
    target_link_libraries(particle_vis_batch PRIVATE OpenMP::OpenMP_CXX)
  elseif(NOT MSVC AND NOT APPLE)
    target_compile_options(particle_vis_batch PRIVATE -fopenmp)
    target_link_libraries(particle_vis_batch PRIVATE gomp)
  endif()

  if(NOT MSVC)
    target_compile_options(particle_vis_batch PRIVATE -Wall -Wextra)
  endif()
endif()

if(PYTHON_BRIDGE AND NOT PARTICLE_VIS_HEADLESS_ONLY)
  add_executable(remote_frame_viewer
    ${CMAKE_SOURCE_DIR}/tools/remote_frame_viewer/main.cpp
    ${GLAD_SRC}
  )

  target_include_directories(remote_frame_viewer PRIVATE
    ${SRC_DIR}
  )

  target_include_directories(remote_frame_viewer SYSTEM PRIVATE
    ${EXTERNAL_DIR}/glad/include
    ${EXTERNAL_DIR}/include
  )

  detect_and_link_glfw(remote_frame_viewer "glfw" "glfw")
  detect_and_link(remote_frame_viewer "ZeroMQ" "libzmq" CMAKE_TARGET libzmq)
  detect_and_link(remote_frame_viewer "cppzmq" "cppzmq" HEADER_ONLY)
  detect_and_link(remote_frame_viewer "nlohmann_json" "nlohmann-json"
    CMAKE_TARGET nlohmann_json::nlohmann_json HEADER_ONLY)

  if(WIN32)
    target_link_libraries(remote_frame_viewer PRIVATE opengl32)
  elseif(APPLE)
    target_link_libraries(remote_frame_viewer PRIVATE ${OPENGL_FRAMEWORK} ${COCOA_FRAMEWORK})
  else()
    target_link_libraries(remote_frame_viewer PRIVATE OpenGL::GL dl pthread)
  endif()

  if(NOT MSVC)
    target_compile_options(remote_frame_viewer PRIVATE -Wall -Wextra)
  endif()
endif()

if(UNIX AND NOT APPLE)
  add_egl_probe_target()
endif()

if(UNIX)
  add_custom_target(check_include_boundaries
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_include_boundaries.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking source include layer boundaries")
endif()
