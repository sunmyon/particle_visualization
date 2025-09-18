#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="$SCRIPT_DIR/data"

REPO="sunmyon/particle_visualization"
TAG="test_data_ver1.0"

mkdir -p "$DEST"

echo "Downloading data files into $DEST ..."
gh release download "$TAG" \
  -R "$REPO" \
  -D "$DEST" \
  -p "cloud_SF_Zsolar.dat" \
  -p "star_cluster_metal_poor.dat" \
  -p "supermassive_stars_extremely_metal_poor.dat"

echo "Done. Files are in $DEST"
