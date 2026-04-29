#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Preparing sample data for particle_vis ..."
python3 "$SCRIPT_DIR/download_data.py"
echo "Done. Default sample is ready at $SCRIPT_DIR/output_0000.dat"
