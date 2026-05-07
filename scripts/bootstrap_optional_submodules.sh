#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
submodule_root="${repo_root}/external/submodules"
build_root="${submodule_root}/_build"
install_root="${submodule_root}/_install"
with_wayland=0
usage() {
  cat <<'EOF'
Usage: ./scripts/bootstrap_optional_submodules.sh [--with-wayland] [dep ...]

Adds optional dependencies as git submodules under external/submodules and
builds/installs supported dependencies into external/submodules/_install.

Flags:
  --with-wayland  Build Wayland + xkbcommon stack locally and enable GLFW
                  Wayland backend. Requires Python 3 (pip) for meson/ninja.

Examples:
  ./scripts/bootstrap_optional_submodules.sh
  ./scripts/bootstrap_optional_submodules.sh --with-wayland glfw
  ./scripts/bootstrap_optional_submodules.sh glfw eigen glm
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-wayland)
      with_wayland=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

declare -A repos=(
  [glfw]="https://github.com/glfw/glfw.git"
  [glm]="https://github.com/g-truc/glm.git"
  [eigen]="https://gitlab.com/libeigen/eigen.git"
  [nlohmann_json]="https://github.com/nlohmann/json.git"
  [cppzmq]="https://github.com/zeromq/cppzmq.git"
  [libzmq]="https://github.com/zeromq/libzmq.git"
  [hdf5]="https://github.com/HDFGroup/hdf5.git"
  [wayland]="https://gitlab.freedesktop.org/wayland/wayland.git"
  [wayland-protocols]="https://gitlab.freedesktop.org/wayland/wayland-protocols.git"
  [xkbcommon]="https://github.com/xkbcommon/libxkbcommon.git"
)

declare -a default_deps=(glfw glm eigen nlohmann_json cppzmq libzmq)
declare -a wayland_deps=(wayland wayland-protocols xkbcommon)

append_dep_once() {
  local needle="$1"
  local item
  for item in "${deps[@]}"; do
    if [[ "${item}" == "${needle}" ]]; then
      return 0
    fi
  done
  deps+=("${needle}")
}

if [[ $# -gt 0 ]]; then
  deps=("$@")
else
  deps=("${default_deps[@]}")
fi

# When --with-wayland is set, inject Wayland stack before glfw.
if [[ ${with_wayland} -eq 1 ]]; then
  # Prepend wayland deps so they are cloned/built before glfw.
  rebuilt_deps=()
  wayland_injected=0
  for dep in "${deps[@]}"; do
    if [[ "${dep}" == "glfw" && ${wayland_injected} -eq 0 ]]; then
      for wdep in "${wayland_deps[@]}"; do
        append_dep_once "${wdep}"
      done
      wayland_injected=1
    fi
  done
  # If glfw was not in the list, still add wayland deps.
  if [[ ${wayland_injected} -eq 0 ]]; then
    for wdep in "${wayland_deps[@]}"; do
      append_dep_once "${wdep}"
    done
  fi
fi

mkdir -p "${submodule_root}" "${build_root}" "${install_root}"

add_or_update_submodule() {
  local dep="$1"
  local url="$2"
  local rel_path="external/submodules/${dep}"
  local abs_path="${repo_root}/${rel_path}"
  local configured_path=""
  local submodule_registered=0

  configured_path=$(git -C "${repo_root}" config -f .gitmodules --get "submodule.${rel_path}.path" || true)
  if git -C "${repo_root}" ls-files --stage -- "${rel_path}" | awk '{print $1}' | grep -q '^160000$'; then
    submodule_registered=1
  fi

  if [[ ${submodule_registered} -eq 0 ]]; then
    git -C "${repo_root}" submodule add --force "${url}" "${rel_path}"
  elif [[ ! -e "${abs_path}" && -z "${configured_path}" ]]; then
    git -C "${repo_root}" submodule add "${url}" "${rel_path}"
  elif [[ ! -e "${abs_path}" && -n "${configured_path}" ]]; then
    echo "Submodule ${rel_path} already declared in .gitmodules; initializing checkout."
  fi

  git -C "${repo_root}" submodule update --init --recursive "${rel_path}"
}

ensure_autotools_configure() {
  local src_dir="$1"

  if [[ -x "${src_dir}/configure" ]]; then
    return 0
  fi

  if [[ -x "${src_dir}/autogen.sh" ]]; then
    (cd "${src_dir}" && ./autogen.sh)
  elif [[ -x "${src_dir}/bootstrap" ]]; then
    (cd "${src_dir}" && ./bootstrap)
  elif [[ -x "${src_dir}/.bootstrap" ]]; then
    (cd "${src_dir}" && ./.bootstrap)
  elif command -v autoreconf >/dev/null 2>&1; then
    (cd "${src_dir}" && autoreconf -fi)
  else
    echo "No configure script in ${src_dir} and no autogen/bootstrap toolchain found." >&2
    return 1
  fi
}

build_cmake_dep() {
  local dep="$1"
  local src_dir="${submodule_root}/${dep}"
  local dep_build="${build_root}/${dep}"
  local dep_install="${install_root}/${dep}"

  if [[ ! -f "${src_dir}/CMakeLists.txt" ]]; then
    echo "Skipping ${dep}: no CMakeLists.txt found at ${src_dir}" >&2
    return 0
  fi

  local -a cmake_args=(
    -S "${src_dir}"
    -B "${dep_build}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="${dep_install}"
  )

  case "${dep}" in
    glfw)
      cmake_args+=(
        -DGLFW_BUILD_EXAMPLES=OFF
        -DGLFW_BUILD_TESTS=OFF
        -DGLFW_BUILD_DOCS=OFF
      )
      if [[ ${with_wayland} -eq 1 ]]; then
        local wayland_pkgdir=""
        wayland_pkgdir="${install_root}/wayland/lib/pkgconfig"
        wayland_pkgdir+=":${install_root}/wayland/share/pkgconfig"
        wayland_pkgdir+=":${install_root}/wayland-protocols/share/pkgconfig"
        wayland_pkgdir+=":${install_root}/xkbcommon/lib/pkgconfig"
        export PKG_CONFIG_PATH="${wayland_pkgdir}${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
        cmake_args+=(
          -DGLFW_BUILD_X11=OFF
          -DGLFW_BUILD_WAYLAND=ON
          -DCMAKE_PREFIX_PATH="${install_root}/wayland;${install_root}/xkbcommon;${install_root}/wayland-protocols"
        )
        echo "Building GLFW with Wayland backend (PKG_CONFIG_PATH=${PKG_CONFIG_PATH})"
      else
        # Build with X11 backend using local stub headers for missing -devel packages
        # (Xinerama, XInput2, Xkb). The stubs provide compile-time types only;
        # all symbols are loaded at runtime via dlopen/dlsym by GLFW.
        local stubs_dir="${repo_root}/external/submodules/_x11_stubs"
        cmake_args+=(
          -DGLFW_BUILD_X11=ON
          -DGLFW_BUILD_WAYLAND=OFF
          "-DX11_Xkb_INCLUDE_PATH=${stubs_dir}"
          "-DX11_Xinerama_INCLUDE_PATH=${stubs_dir}"
          "-DX11_Xi_INCLUDE_PATH=${stubs_dir}"
          "-DCMAKE_INCLUDE_PATH=${stubs_dir}"
        )
        echo "Building GLFW with X11 backend (using local stub headers from ${stubs_dir})"
      fi
      ;;
    glm)
      cmake_args+=(-DGLM_BUILD_TESTS=OFF)
      ;;
    eigen)
      cmake_args+=(
        -DBUILD_TESTING=OFF
        -DEIGEN_BUILD_DOC=OFF
        -DEIGEN_BUILD_PKGCONFIG=ON
      )
      ;;
    nlohmann_json)
      cmake_args+=(-DJSON_BuildTests=OFF)
      ;;
    cppzmq)
      cmake_args+=(
        -DCPPZMQ_BUILD_TESTS=OFF
        -DCPPZMQ_BUILD_EXAMPLES=OFF
      )
      ;;
    libzmq)
      cmake_args+=(
        -DBUILD_TESTING=OFF
        -DWITH_PERF_TOOL=OFF
        -DZMQ_BUILD_TESTS=OFF
      )
      ;;
    hdf5)
      cmake_args+=(
        -DBUILD_TESTING=OFF
        -DHDF5_BUILD_CPP_LIB=ON
        -DHDF5_BUILD_TOOLS=OFF
        -DHDF5_BUILD_EXAMPLES=OFF
      )
      ;;
  esac

  cmake "${cmake_args[@]}"
  cmake --build "${dep_build}" -j "$(nproc)"
  cmake --install "${dep_build}"
}

ensure_meson() {
  if command -v meson >/dev/null 2>&1; then
    return 0
  fi
  echo "meson not found; installing via pip --user ..." >&2
  python3 -m pip install --user meson ninja
  export PATH="${HOME}/.local/bin:${PATH}"
  if ! command -v meson >/dev/null 2>&1; then
    echo "ERROR: meson install failed. Please install meson manually and re-run." >&2
    return 1
  fi
}

build_meson_dep() {
  local dep="$1"
  local src_dir="${submodule_root}/${dep}"
  local dep_build="${build_root}/${dep}"
  local dep_install="${install_root}/${dep}"

  ensure_meson

  local -a meson_args=(
    --prefix="${dep_install}"
    --buildtype=release
    --wrap-mode=nodownload
  )

  case "${dep}" in
    wayland)
      meson_args+=(
        -Ddocumentation=false
        -Dtests=false
        -Dlibraries=true
        -Dscanner=true
      )
      ;;
    wayland-protocols)
      meson_args+=(-Dtests=false)
      ;;
    xkbcommon)
      # Point pkg-config at local wayland-protocols and wayland installs.
      local wl_pkg="${install_root}/wayland/lib/pkgconfig"
      wl_pkg+=":${install_root}/wayland/share/pkgconfig"
      wl_pkg+=":${install_root}/wayland-protocols/share/pkgconfig"
      export PKG_CONFIG_PATH="${wl_pkg}${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
      meson_args+=(
        -Denable-docs=false
        -Denable-wayland=true
        -Denable-x11=false
        -Dxkb-config-root=/usr/share/X11/xkb
        -Dx-locale-root=/usr/share/X11/locale
      )
      ;;
  esac

  if [[ ! -f "${dep_build}/build.ninja" ]]; then
    meson setup "${dep_build}" "${src_dir}" "${meson_args[@]}"
  fi
  meson compile -C "${dep_build}" -j "$(nproc)"
  meson install -C "${dep_build}"
}

for dep in "${deps[@]}"; do
  if [[ -z "${repos[${dep}]:-}" ]]; then
    echo "Unknown dependency: ${dep}" >&2
    exit 1
  fi
  add_or_update_submodule "${dep}" "${repos[${dep}]}"
done

for dep in "${deps[@]}"; do
  case "${dep}" in
    wayland|wayland-protocols|xkbcommon)
      build_meson_dep "${dep}"
      ;;
    *)
      build_cmake_dep "${dep}"
      ;;
  esac
done

echo
echo "Optional dependency submodules are ready under ${submodule_root}."
echo "Installed package prefixes are under ${install_root}."
