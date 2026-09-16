#!/usr/bin/env bash
# Rebuild the existing OpenH264 checkout with x86 assembly, without root access.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
install_root="${repo_root}/external/submodules/_install"
source_dir="${repo_root}/external/submodules/openh264"
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
case "$(uname -m)" in
  x86_64|i386|i686) ;;
  *) echo "This helper is for x86; use bootstrap_optional_submodules.sh openh264 on other architectures." >&2; exit 1 ;;
esac
if [[ ! -f "${source_dir}/Makefile" ]]; then
  echo "Initialize external/submodules/openh264 first." >&2
  exit 1
fi
export PATH="${install_root}/nasm/bin:${PATH}"
if ! command -v nasm >/dev/null 2>&1; then
  version=2.16.03
  checksum=1412a1c760bbd05db026b6c0d1657affd6631cd0a63cddb6f73cc6d4aa616148
  mkdir -p "${install_root}/nasm-source"
  archive="${install_root}/nasm-source/nasm-${version}.tar.xz"
  curl --fail --location --max-time 120 \
    "https://www.nasm.us/pub/nasm/releasebuilds/${version}/nasm-${version}.tar.xz" \
    -o "${archive}"
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s  %s\n' "${checksum}" "${archive}" | sha256sum -c -
  else
    printf '%s  %s\n' "${checksum}" "${archive}" | shasum -a 256 -c -
  fi
  tar -xf "${archive}" -C "${install_root}/nasm-source"
  (
    cd "${install_root}/nasm-source/nasm-${version}"
    ./configure --prefix="${install_root}/nasm"
    make -j "${jobs}"
    make install
  )
fi
nasm -v
# OpenH264 make does not track a changed USE_ASM flag; stale scalar objects must
# be removed before enabling assembly. This only removes generated build files.
make -C "${source_dir}" clean
make -C "${source_dir}" -j "${jobs}" USE_ASM=Yes \
  PREFIX="${install_root}/openh264" install-static
# Verify actual assembly definitions, not just availability of the assembler.
nm "${install_root}/openh264/lib/libopenh264.a" > "${install_root}/openh264/symbols.txt"
if ! grep -Eq ' [Tt] .*(_sse[0-9]*|_avx[0-9]*)$' "${install_root}/openh264/symbols.txt"; then
  echo "OpenH264 was built, but expected x86 SIMD symbols were not found." >&2
  exit 1
fi
echo "OpenH264 x86 SIMD verified. Rebuild particle_vis to link the new static library."
