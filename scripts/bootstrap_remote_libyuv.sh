#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
revision=2dd4257364d39c38d79465c4ddc4b93137fe729b
source_dir="${repo_root}/external/submodules/_install/libyuv-source"
build_dir="${repo_root}/external/submodules/_install/libyuv-build"
install_dir="${repo_root}/external/submodules/_install/libyuv"
if [[ ! -d "${source_dir}/.git" ]]; then
  git init "${source_dir}"
  git -C "${source_dir}" remote add origin https://chromium.googlesource.com/libyuv/libyuv
fi
git -C "${source_dir}" fetch --depth 1 origin "${revision}"
git -C "${source_dir}" checkout --detach "${revision}"
cmake -S "${source_dir}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release -DLIBYUV_DISABLE_JPEG=ON
cmake --build "${build_dir}" --config Release --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}" --target yuv
mkdir -p "${install_dir}/include" "${install_dir}/lib"
cp -R "${source_dir}/include/." "${install_dir}/include/"
cp "${build_dir}/libyuv.a" "${install_dir}/lib/"
cp "${source_dir}/LICENSE" "${install_dir}/LICENSE"
echo "Installed libyuv ${revision} into ${install_dir}"
