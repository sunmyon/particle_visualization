#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Preparing sample data for particle_vis ..."
python3 "$SCRIPT_DIR/download_data.py"
echo "Done. Example data is ready under $SCRIPT_DIR/"
