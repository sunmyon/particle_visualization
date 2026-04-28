#!/usr/bin/env python3
"""Small manual test for the particle_vis Python bridge.

Run particle_vis, launch the Python bridge from the GUI, then run for example:

    python3 python_assets/python_bridge_smoke_test.py --summary
    python3 python_assets/python_bridge_smoke_test.py --offset-pos 0.01 0 0
    python3 python_assets/python_bridge_smoke_test.py --hide-first
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python_assets"))

from params_client import ParamsClient  # noqa: E402
from shm_particles import ShmParticles  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Smoke-test particle_vis Python bridge")
    parser.add_argument("--shm-name", default="cppvis_pos",
                        help="Shared memory name, without or with leading slash")
    parser.add_argument("--endpoint", default="tcp://127.0.0.1:5557",
                        help="ZeroMQ RPC endpoint")
    parser.add_argument("--summary", action="store_true",
                        help="Print shared-memory field summary")
    parser.add_argument("--offset-pos", nargs=3, type=float, metavar=("DX", "DY", "DZ"),
                        help="Add an offset to all current positions and notify C++")
    parser.add_argument("--hide-first", action="store_true",
                        help="Set mask[0] = 1 and notify C++")
    parser.add_argument("--ping", action="store_true",
                        help="Only ping the RPC server")
    return parser.parse_args()


def print_summary(particles: ShmParticles) -> None:
    print(f"N = {particles.N}")
    print(f"fields = {sorted(particles.fields)}")
    if particles.N:
        print(f"pos[0] = {particles.pos[0].tolist()}")
        print(f"origpos[0] = {particles.origpos[0].tolist()}")
        print(f"id[0] = {int(particles.id[0])}")
        print(f"type[0] = {int(particles.type[0])}")
        print(f"mask[0] = {int(particles.mask[0])}")


def main() -> int:
    args = parse_args()
    client = ParamsClient(args.endpoint)

    if args.ping:
        print(client.ping())
        return 0

    particles = ShmParticles(args.shm_name)

    if args.summary:
        print_summary(particles)

    dirty_fields: list[str] = []

    if args.offset_pos is not None:
        particles.pos[:, 0] += args.offset_pos[0]
        particles.pos[:, 1] += args.offset_pos[1]
        particles.pos[:, 2] += args.offset_pos[2]
        dirty_fields.append("pos")

    if args.hide_first:
        if particles.N == 0:
            print("No particles are available")
            return 1
        particles.mask[0] = 1
        dirty_fields.append("mask")

    if dirty_fields:
        response = client.edit(dirty_fields)
        print(response)
    elif not args.summary:
        print(client.ping())

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
