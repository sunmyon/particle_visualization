#!/usr/bin/env python3
"""Exercise the real renderer and remote transport on the local machine."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import socket
import subprocess
import sys
import tempfile

try:
    import zmq
except ImportError as error:
    raise SystemExit("pyzmq is required: python3 -m pip install pyzmq") from error


def available_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def receive_frame(subscriber: zmq.Socket, timeout: float) -> tuple[dict, bytes]:
    if not subscriber.poll(round(timeout * 1000), zmq.POLLIN):
        raise RuntimeError("timed out waiting for a remote frame")
    header = json.loads(subscriber.recv().decode("utf-8"))
    if not subscriber.getsockopt(zmq.RCVMORE):
        raise RuntimeError("frame header arrived without an RGBA payload")
    payload = subscriber.recv()
    width = header.get("width", 0)
    height = header.get("height", 0)
    expected = width * height * 4
    if header.get("type") != "rgba_frame" or expected <= 0:
        raise RuntimeError(f"invalid frame header: {header!r}")
    if header.get("bytes") != expected or len(payload) != expected:
        raise RuntimeError(
            f"invalid payload size: header={header.get('bytes')}, "
            f"received={len(payload)}, expected={expected}"
        )
    return header, payload


def base_config_path(repository: Path) -> Path:
    local_config = repository / "config.txt"
    if local_config.is_file():
        return local_config
    sample_config = repository / "config.txt.sample"
    if sample_config.is_file():
        return sample_config
    raise RuntimeError("neither config.txt nor config.txt.sample was found")


def write_snapshot_config(repository: Path, snapshot: Path, destination: Path) -> None:
    lines = base_config_path(repository).read_text(encoding="utf-8").splitlines()
    replacements = {
        "FileFormat": snapshot.name,
        "FolderPath": f"{snapshot.parent}{os.sep}",
        "ReadFormat": "1",
        "initialIndex": "0",
        "currentFileIndex": "0",
        "currentStep": "0",
    }
    seen: set[str] = set()
    output: list[str] = []
    for line in lines:
        key, separator, _ = line.partition("=")
        if separator and key in replacements:
            output.append(f"{key}={replacements[key]}")
            seen.add(key)
        else:
            output.append(line)
    for key, value in replacements.items():
        if key not in seen:
            output.append(f"{key}={value}")
    destination.write_text("\n".join(output) + "\n", encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    repository = Path(__file__).resolve().parent.parent
    executable = (repository / args.executable).resolve()
    if not executable.is_file():
        raise RuntimeError(f"particle_vis executable not found: {executable}")

    frame_port = available_port()
    input_port = available_port()
    while input_port == frame_port:
        input_port = available_port()
    frame_endpoint = f"tcp://127.0.0.1:{frame_port}"
    input_endpoint = f"tcp://127.0.0.1:{input_port}"

    backend = args.backend or ("metal" if platform.system() == "Darwin" else "opengl")
    temporary_directory = tempfile.TemporaryDirectory(prefix="particle-vis-loopback-")
    temporary_config = Path(temporary_directory.name) / "config.txt"
    if args.snapshot:
        snapshot = Path(args.snapshot).expanduser().resolve()
        if not snapshot.is_file():
            temporary_directory.cleanup()
            raise RuntimeError(f"snapshot not found: {snapshot}")
        write_snapshot_config(repository, snapshot, temporary_config)
    else:
        temporary_config.write_text(
            base_config_path(repository).read_text(encoding="utf-8"),
            encoding="utf-8",
        )

    environment = os.environ.copy()
    environment.update(
        {
            "PARTICLE_VIS_HEADLESS": "1",
            "PARTICLE_VIS_RENDER_BACKEND": backend,
            "PARTICLE_VIS_WINDOW_WIDTH": str(args.width),
            "PARTICLE_VIS_WINDOW_HEIGHT": str(args.height),
            "PARTICLE_VIS_REMOTE_FRAME_ENDPOINT": frame_endpoint,
            "PARTICLE_VIS_REMOTE_INPUT_ENDPOINT": input_endpoint,
            "PARTICLE_VIS_CONFIG_PATH": str(temporary_config),
        }
    )

    process = subprocess.Popen(
        [str(executable)],
        cwd=repository,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    context = zmq.Context()
    subscriber = context.socket(zmq.SUB)
    subscriber.setsockopt(zmq.SUBSCRIBE, b"")
    subscriber.setsockopt(zmq.LINGER, 0)
    subscriber.setsockopt(zmq.RCVHWM, 2)
    subscriber.connect(frame_endpoint)
    sender = context.socket(zmq.PUSH)
    sender.setsockopt(zmq.LINGER, 0)
    sender.setsockopt(zmq.SNDTIMEO, round(args.timeout * 1000))
    sender.connect(input_endpoint)

    try:
        first, _ = receive_frame(subscriber, args.timeout)
        active_width = args.width
        active_height = args.height
        active_display_width = round(active_width / args.display_scale)
        active_display_height = round(active_height / args.display_scale)
        resized = first
        resize_requested = bool(args.resize) or args.display_scale != 1.0
        if resize_requested:
            if args.resize:
                active_width, active_height = args.resize
                active_display_width = round(active_width / args.display_scale)
                active_display_height = round(active_height / args.display_scale)
            sender.send_json(
                {
                    "type": "framebuffer_resize",
                    "width": active_width,
                    "height": active_height,
                    "displayWidth": active_display_width,
                    "displayHeight": active_display_height,
                    "framebufferScaleX": args.display_scale,
                    "framebufferScaleY": args.display_scale,
                }
            )
            for attempt in range(60):
                resized, _ = receive_frame(subscriber, args.timeout)
                if (resized["width"], resized["height"]) == (
                    active_width,
                    active_height,
                ):
                    if (resized.get("displayWidth"), resized.get("displayHeight")) != (
                        active_display_width,
                        active_display_height,
                    ) or resized.get("framebufferScaleX") != args.display_scale:
                        raise RuntimeError(
                            f"incorrect display metrics in resized frame: {resized!r}"
                        )
                    break
                if attempt == 4:
                    sender.send_json(
                        {
                            "type": "framebuffer_resize",
                            "width": active_width,
                            "height": active_height,
                            "displayWidth": active_display_width,
                            "displayHeight": active_display_height,
                            "framebufferScaleX": args.display_scale,
                            "framebufferScaleY": args.display_scale,
                        }
                    )
            else:
                raise RuntimeError(
                    "server did not publish a frame at the requested size "
                    f"{active_width}x{active_height}"
                )
        viewport = {
            "x": 0,
            "y": 0,
            "width": active_display_width,
            "height": active_display_height,
            "framebufferScaleX": args.display_scale,
            "framebufferScaleY": args.display_scale,
        }
        pointer_x = max(1, active_display_width - 20)
        pointer_y = max(1, active_display_height - 20)
        events = [
            {"type": "pointer_move", "x": pointer_x, "y": pointer_y, "primaryDown": False},
            {"type": "pointer_button", "button": "Left", "action": "Press", "x": pointer_x, "y": pointer_y},
            {"type": "pointer_move", "x": pointer_x - 20, "y": pointer_y - 20, "primaryDown": True},
            {"type": "pointer_button", "button": "Left", "action": "Release", "x": pointer_x - 20, "y": pointer_y - 20},
            {"type": "pointer_scroll", "x": pointer_x - 20, "y": pointer_y - 20, "wheelX": 0, "wheelY": 1},
            {"type": "text", "text": "remote"},
        ]
        for event in events:
            event["viewport"] = viewport
            sender.send_json(event)

        second, _ = receive_frame(subscriber, args.timeout)
        if second["frameId"] <= resized["frameId"]:
            raise RuntimeError("frame IDs did not advance after sending input")

        sender.send_json({"type": "key", "key": "Escape", "action": "Press"})
        try:
            exit_code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError("remote Escape did not stop the server") from error
        if exit_code != 0:
            raise RuntimeError(f"server exited with status {exit_code}")

        print(
            json.dumps(
                {
                    "backend": backend,
                    "firstFrame": first["frameId"],
                    "lastFrame": second["frameId"],
                    "initialSize": [first["width"], first["height"]],
                    "finalSize": [second["width"], second["height"]],
                    "payloadBytes": second["bytes"],
                    "displaySize": [active_display_width, active_display_height],
                    "framebufferScale": args.display_scale,
                    "inputEventsSent": len(events) + 1 + resize_requested,
                    "serverExitCode": exit_code,
                },
                indent=2,
            )
        )
        return 0
    finally:
        subscriber.close()
        sender.close()
        context.term()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        output = process.stdout.read() if process.stdout else ""
        if output and args.show_server_output:
            print(output, file=sys.stderr)
        temporary_directory.cleanup()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", default="build/particle_vis")
    parser.add_argument("--backend", choices=("opengl", "metal", "vulkan"))
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--resize", type=int, nargs=2, metavar=("WIDTH", "HEIGHT"))
    parser.add_argument("--display-scale", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--snapshot", help="load this HDF5 snapshot via a temporary config")
    parser.add_argument("--show-server-output", action="store_true")
    args = parser.parse_args()
    if (args.width <= 0 or args.height <= 0 or args.timeout <= 0 or
            args.display_scale <= 0 or
            (args.resize and any(value <= 0 for value in args.resize))):
        parser.error("width, height and timeout must be positive")
    try:
        return run(args)
    except (OSError, RuntimeError, ValueError, zmq.ZMQError) as error:
        print(f"remote loopback smoke failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
