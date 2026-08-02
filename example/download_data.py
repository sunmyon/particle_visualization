#!/usr/bin/env python3
"""Fetch or generate app-readable example snapshots.

The application defaults to reading ./example/output_0000.dat, so this script
always ensures that file exists and follows the expected binary layout. It also
fetches optional example snapshots used for format-specific manual testing.
"""

import math
import os
import shutil
import struct
import subprocess
import urllib.request


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DEST_DIR = os.path.join(BASE_DIR, "data")
TARGET_NAME = "output_0000.dat"
TARGET_IN_DATA = os.path.join(DEST_DIR, TARGET_NAME)
TARGET_AT_EXAMPLE_ROOT = os.path.join(BASE_DIR, TARGET_NAME)
GADGET_SAMPLE_NAME = "ics_gadget.dat"
GADGET_SAMPLE_IN_DATA = os.path.join(DEST_DIR, GADGET_SAMPLE_NAME)
GADGET_SAMPLE_AT_EXAMPLE_ROOT = os.path.join(BASE_DIR, GADGET_SAMPLE_NAME)

# Keep legacy URLs for compatibility. Some installations may still host these.
REMOTE_CANDIDATES = [
    os.environ.get("PARTICLE_VIS_SAMPLE_URL", "").strip(),
    "https://github.com/sunmyon/particle_visualization/releases/download/test_data_ver1.0/output_0000.dat",
    "https://github.com/sunmyon/particle_visualization/releases/download/test_data_ver1.0/cloud_SF_Zsolar.dat",
]

GADGET_SAMPLE_CANDIDATES = [
    os.environ.get("PARTICLE_VIS_GADGET_SAMPLE_URL", "").strip(),
    "https://github.com/sunmyon/particle_visualization/releases/download/test_data_ver1.0/ics_gadget.dat",
]
GITHUB_REPO = "sunmyon/particle_visualization"
EXAMPLE_RELEASE_TAG = "test_data_ver1.0"


def ensure_dirs() -> None:
    os.makedirs(DEST_DIR, exist_ok=True)


def try_download(url: str, out_path: str) -> bool:
    if not url:
        return False
    try:
        print(f"Trying download: {url}")
        with urllib.request.urlopen(url, timeout=30) as resp, open(out_path, "wb") as f:
            shutil.copyfileobj(resp, f)
        print(f"Downloaded: {out_path}")
        return True
    except Exception as exc:
        print(f"Download failed: {url} ({exc})")
        return False


def try_download_release_asset(asset_name: str, out_path: str) -> bool:
    if not shutil.which("gh"):
        return False

    try:
        print(f"Trying GitHub release asset via gh: {asset_name}")
        subprocess.run(
            [
                "gh",
                "release",
                "download",
                EXAMPLE_RELEASE_TAG,
                "--repo",
                GITHUB_REPO,
                "--pattern",
                asset_name,
                "--dir",
                DEST_DIR,
                "--clobber",
            ],
            check=True,
        )
        downloaded_path = os.path.join(DEST_DIR, asset_name)
        if downloaded_path != out_path:
            shutil.move(downloaded_path, out_path)
        print(f"Downloaded: {out_path}")
        return True
    except Exception as exc:
        print(f"GitHub release asset download failed: {asset_name} ({exc})")
        return False


def write_synthetic_snapshot(out_path: str, n_particles: int = 2048) -> None:
    """Write a binary file matching BinaryReader + default format tokens.

    Layout per record:
      pos(3f), vel(3f), type(i32), id(i32), hsml(f), density(f), temperature(f),
      dummy(f), value(f), value2(f), dummy4(4f), mass(f)
    """
    print(f"Generating synthetic sample snapshot: {out_path}")
    with open(out_path, "wb") as f:
        # Header expected by BinaryReader: float time, int32 npart
        f.write(struct.pack("<fi", 0.0, n_particles))

        for i in range(n_particles):
            t = i / max(1, n_particles - 1)
            ang = 2.0 * math.pi * t

            x = math.cos(ang) * 2.0
            y = math.sin(ang) * 2.0
            z = (t - 0.5) * 4.0

            vx = -math.sin(ang)
            vy = math.cos(ang)
            vz = 0.1 * math.sin(2.0 * ang)

            ptype = i % 6
            pid = i + 1
            hsml = 0.05 + 0.01 * (i % 10)
            density = 1.0 + 0.2 * math.sin(3.0 * ang)
            temperature = 1000.0 + 5000.0 * t
            dummy = 0.0
            value = math.sqrt(x * x + y * y + z * z)
            value2 = float(ptype)
            d0 = d1 = d2 = d3 = 0.0
            mass = 1.0

            f.write(
                struct.pack(
                    "<6f2i11f",
                    x,
                    y,
                    z,
                    vx,
                    vy,
                    vz,
                    ptype,
                    pid,
                    hsml,
                    density,
                    temperature,
                    dummy,
                    value,
                    value2,
                    d0,
                    d1,
                    d2,
                    d3,
                    mass,
                )
            )


def mirror_to_example_root(src_path: str, dst_path: str) -> None:
    # Keep app-facing paths at example/*.dat while storing downloads in example/data/.
    shutil.copy2(src_path, dst_path)
    print(f"Prepared example snapshot: {dst_path}")


def fetch_first_available(urls: list[str], out_path: str) -> bool:
    for url in urls:
        if try_download(url, out_path):
            return True
    return False


def main() -> int:
    ensure_dirs()

    if not fetch_first_available(REMOTE_CANDIDATES, TARGET_IN_DATA):
        # Network or URL can be unavailable in cluster environments.
        write_synthetic_snapshot(TARGET_IN_DATA)

    mirror_to_example_root(TARGET_IN_DATA, TARGET_AT_EXAMPLE_ROOT)

    if (fetch_first_available(GADGET_SAMPLE_CANDIDATES, GADGET_SAMPLE_IN_DATA) or
            try_download_release_asset(GADGET_SAMPLE_NAME, GADGET_SAMPLE_IN_DATA)):
        mirror_to_example_root(GADGET_SAMPLE_IN_DATA, GADGET_SAMPLE_AT_EXAMPLE_ROOT)
    else:
        print("Optional Gadget sample unavailable; continuing without it.")

    print("Data setup complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
