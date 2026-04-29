#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
submodule_root="${repo_root}/external/submodules"
build_root="${submodule_root}/_build"
install_root="${submodule_root}/_install"
with_heavy=0

usage() {
  cat <<'EOF'
Usage: ./scripts/bootstrap_optional_submodules.sh [--with-heavy] [dep ...]

Adds optional dependencies as git submodules under external/submodules and
builds/installs supported dependencies into external/submodules/_install.

Examples:
  ./scripts/bootstrap_optional_submodules.sh
  ./scripts/bootstrap_optional_submodules.sh --with-heavy
  ./scripts/bootstrap_optional_submodules.sh glfw eigen glm
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-heavy)
      with_heavy=1
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
  [vtk]="https://github.com/Kitware/VTK.git"
  [gmp]="https://github.com/alisw/GMP.git"
  [mpfr]="https://github.com/P-p-H-d/mpfr.git"
  [lua]="https://github.com/lua/lua.git"
  [cgal]="https://github.com/CGAL/cgal.git"
)

declare -a default_deps=(glfw glm eigen nlohmann_json cppzmq libzmq gmp mpfr lua)
declare -a heavy_deps=(hdf5 vtk cgal)

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
  if [[ ${with_heavy} -eq 1 ]]; then
    deps+=("${heavy_deps[@]}")
  fi
fi

# Dependency closure for local builds.
for dep in "${deps[@]}"; do
  if [[ "${dep}" == "mpfr" ]]; then
    append_dep_once gmp
  fi
  if [[ "${dep}" == "cgal" ]]; then
    append_dep_once gmp
    append_dep_once mpfr
  fi
done

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
    vtk)
      cmake_args+=(
        -DBUILD_TESTING=OFF
        -DVTK_BUILD_TESTING=OFF
        -DVTK_GROUP_ENABLE_Qt=NO
      )
      ;;
    cgal)
      cmake_args+=(
        -DBUILD_TESTING=OFF
        -DCMAKE_PREFIX_PATH="${install_root}/gmp;${install_root}/mpfr"
      )
      ;;
  esac

  cmake "${cmake_args[@]}"
  cmake --build "${dep_build}" -j "$(nproc)"
  cmake --install "${dep_build}"
}

build_gmp() {
  local src_dir="${submodule_root}/gmp"
  local dep_build="${build_root}/gmp"
  local dep_install="${install_root}/gmp"

  ensure_autotools_configure "${src_dir}"

  mkdir -p "${dep_build}"
  if [[ ! -f "${dep_build}/Makefile" ]]; then
    (
      cd "${dep_build}"
      MAKEINFO=true "${src_dir}/configure" --prefix="${dep_install}" --enable-cxx >"${dep_build}/configure.log" 2>&1
    )
  fi

  make -C "${dep_build}" MAKEINFO=true -j "$(nproc)"
  make -C "${dep_build}" MAKEINFO=true install
}

build_mpfr() {
  local src_dir="${submodule_root}/mpfr"
  local dep_build="${build_root}/mpfr"
  local dep_install="${install_root}/mpfr"
  local gmp_install="${install_root}/gmp"

  if [[ ! -d "${gmp_install}" ]]; then
    echo "MPFR requires GMP. Build GMP first." >&2
    return 1
  fi

  ensure_autotools_configure "${src_dir}"

  mkdir -p "${dep_build}"
  if [[ ! -f "${dep_build}/Makefile" ]]; then
    (
      cd "${dep_build}"
      MAKEINFO=true "${src_dir}/configure" \
        --prefix="${dep_install}" \
        --with-gmp="${gmp_install}" >"${dep_build}/configure.log" 2>&1
    )
  fi

  make -C "${dep_build}" MAKEINFO=true -j "$(nproc)"
  make -C "${dep_build}" MAKEINFO=true install
}

build_lua() {
  local src_dir="${submodule_root}/lua"
  local dep_install="${install_root}/lua"
  local lua_makefile="${src_dir}/makefile"

  if [[ ! -f "${lua_makefile}" ]]; then
    echo "Lua makefile not found at ${lua_makefile}" >&2
    return 1
  fi

  make -C "${src_dir}" -f makefile clean || true
  make -C "${src_dir}" -f makefile -j "$(nproc)"

  mkdir -p "${dep_install}/bin" "${dep_install}/lib" "${dep_install}/include"
  install -m 755 "${src_dir}/lua" "${dep_install}/bin/lua"
  install -m 644 "${src_dir}/liblua.a" "${dep_install}/lib/liblua.a"
  install -m 644 "${src_dir}/lua.h" "${dep_install}/include/lua.h"
  install -m 644 "${src_dir}/luaconf.h" "${dep_install}/include/luaconf.h"
  install -m 644 "${src_dir}/lualib.h" "${dep_install}/include/lualib.h"
  install -m 644 "${src_dir}/lauxlib.h" "${dep_install}/include/lauxlib.h"
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
    gmp)
      build_gmp
      ;;
    mpfr)
      build_mpfr
      ;;
    lua)
      build_lua
      ;;
    hdf5|vtk)
      if [[ ${with_heavy} -eq 1 ]]; then
        build_cmake_dep "${dep}"
      else
        echo "Leaving ${dep} checked out only. Re-run with --with-heavy to build/install it." >&2
      fi
      ;;
    cgal)
      if [[ ${with_heavy} -eq 1 ]]; then
        build_cmake_dep "${dep}"
      else
        echo "Leaving ${dep} checked out only. Re-run with --with-heavy to build/install it." >&2
      fi
      ;;
    *)
      build_cmake_dep "${dep}"
      ;;
  esac
done

echo
echo "Optional dependency submodules are ready under ${submodule_root}."
echo "Installed package prefixes are under ${install_root}."