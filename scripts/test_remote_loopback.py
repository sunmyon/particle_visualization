#!/usr/bin/env python3
"""Exercise the real renderer and remote transport on the local machine."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import time

try:
    import zmq
except ImportError as error:
    raise SystemExit("pyzmq is required: python3 -m pip install pyzmq") from error


def available_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def receive_frame(subscriber: zmq.Socket, timeout: float,
                  sender: zmq.Socket | None = None) -> tuple[dict, bytes]:
    if not subscriber.poll(round(timeout * 1000), zmq.POLLIN):
        raise RuntimeError("timed out waiting for a remote frame")
    header = json.loads(subscriber.recv().decode("utf-8"))
    if not subscriber.getsockopt(zmq.RCVMORE):
        raise RuntimeError("frame header arrived without an RGBA payload")
    payload = subscriber.recv()
    if sender is not None:
        sender.send_json({"type": "frame_request", "version": 1,
                          "receivedFrameId": header["frameId"]})
    width = header.get("width", 0)
    height = header.get("height", 0)
    expected = width * height * 4
    frame_type = header.get("type")
    if frame_type not in ("rgba_frame", "jpeg_frame", "h264_frame") or expected <= 0:
        raise RuntimeError(f"invalid frame header: {header!r}")
    if header.get("bytes") != len(payload):
        raise RuntimeError(
            f"invalid payload size: header={header.get('bytes')}, "
            f"received={len(payload)}"
        )
    for timing in ("serverInputToFrameStartMs", "serverFrameToRenderMs",
                   "serverRenderMs", "serverEncodeToSendMs",
                   "serverPreviousSendMs"):
        if not isinstance(header.get(timing), (int, float)) or header[timing] < 0:
            raise RuntimeError(f"missing or invalid {timing}: {header!r}")
    if header.get("outstandingFrames", 0) > 3:
        raise RuntimeError(f"transport exceeded bounded frame slots: {header!r}")
    for timestamp in ("serverInputReceivedAtNs", "serverCameraUpdatedAtNs",
                      "serverRenderStartedAtNs", "serverRenderFinishedAtNs",
                      "serverEncodeStartedAtNs", "serverEncodeFinishedAtNs",
                      "serverSendAttemptAtNs"):
        if not isinstance(header.get(timestamp), int) or header[timestamp] < 0:
            raise RuntimeError(f"missing or invalid {timestamp}: {header!r}")
    if frame_type == "rgba_frame" and len(payload) != expected:
        raise RuntimeError(
            f"invalid RGBA payload size: received={len(payload)}, expected={expected}"
        )
    if frame_type == "jpeg_frame":
        if header.get("rawBytes") != expected:
            raise RuntimeError(f"invalid JPEG raw size: {header!r}")
        if not payload.startswith(b"\xff\xd8") or not payload.endswith(b"\xff\xd9"):
            raise RuntimeError("invalid JPEG payload markers")
    if frame_type == "h264_frame":
        if header.get("rawBytes") != expected:
            raise RuntimeError(f"invalid H.264 raw size: {header!r}")
        if not (payload.startswith(b"\x00\x00\x00\x01") or
                payload.startswith(b"\x00\x00\x01")):
            raise RuntimeError("invalid H.264 Annex B payload marker")
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
    bounded = os.environ.get("PARTICLE_VIS_REMOTE_TRANSPORT") == "bounded"
    still_endpoint = f"tcp://127.0.0.1:{available_port()}"

    backend = args.backend or ("metal" if platform.system() == "Darwin" else "opengl")
    temporary_directory = tempfile.TemporaryDirectory(prefix="particle-vis-loopback-")
    temporary_config = Path(temporary_directory.name) / "config.txt"
    if args.config:
        source_config = Path(args.config).expanduser().resolve()
        if not source_config.is_file():
            temporary_directory.cleanup()
            raise RuntimeError(f"config not found: {source_config}")
        shutil.copyfile(source_config, temporary_config)
    elif args.snapshot:
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
            "PARTICLE_VIS_REMOTE_STILL_ENDPOINT": still_endpoint,
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
    subscriber = context.socket(zmq.PULL if bounded else zmq.SUB)
    if not bounded:
        subscriber.setsockopt(zmq.SUBSCRIBE, b"")
    subscriber.setsockopt(zmq.LINGER, 0)
    subscriber.setsockopt(zmq.RCVHWM, 2)
    subscriber.connect(frame_endpoint)
    still_subscriber = context.socket(zmq.PULL) if bounded else None
    if still_subscriber is not None:
        still_subscriber.setsockopt(zmq.LINGER, 0)
        still_subscriber.connect(still_endpoint)
    sender = context.socket(zmq.PUSH)
    sender.setsockopt(zmq.LINGER, 0)
    sender.setsockopt(zmq.SNDTIMEO, round(args.timeout * 1000))
    sender.connect(input_endpoint)

    try:
        sender.send_json({"type": "frame_request", "version": 1})
        first, _ = receive_frame(subscriber, args.timeout, sender if bounded else None)
        sender.send_json({"type": "frame_request", "version": 1})
        if subscriber.poll(300, zmq.POLLIN):
            unexpected, _ = receive_frame(subscriber, args.timeout,
                                          sender if bounded else None)
            raise RuntimeError(
                "server published an unchanged frame while idle: "
                f"frameId={unexpected.get('frameId')}"
            )
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
            resize_event = {
                "type": "framebuffer_resize",
                "width": active_width,
                "height": active_height,
                "displayWidth": active_display_width,
                "displayHeight": active_display_height,
                "framebufferScaleX": args.display_scale,
                "framebufferScaleY": args.display_scale,
                "presentationMode": "interactive",
            }
            deadline = time.monotonic() + args.timeout
            next_send = 0.0
            while time.monotonic() < deadline:
                now = time.monotonic()
                if now >= next_send:
                    sender.send_json(resize_event)
                    sender.send_json({"type": "frame_request", "version": 1})
                    next_send = now + 0.1
                wait_seconds = min(
                    0.25, max(0.001, deadline - time.monotonic())
                )
                if not subscriber.poll(round(wait_seconds * 1000), zmq.POLLIN):
                    continue
                resized, _ = receive_frame(subscriber, args.timeout,
                                           sender if bounded else None)
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
            else:
                raise RuntimeError(
                    "server did not publish a frame at the requested size "
                    f"{active_width}x{active_height}"
                )

        presentation_event = {
            "type": "framebuffer_resize",
            "width": active_width,
            "height": active_height,
            "displayWidth": active_display_width,
            "displayHeight": active_display_height,
            "framebufferScaleX": args.display_scale,
            "framebufferScaleY": args.display_scale,
            "presentationMode": "idle",
        }
        idle = None
        deadline = time.monotonic() + args.timeout
        next_send = 0.0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_send:
                sender.send_json(presentation_event)
                sender.send_json({"type": "frame_request", "version": 1})
                next_send = now + 0.1
            wait_seconds = min(0.25, max(0.001, deadline - time.monotonic()))
            idle_subscriber = still_subscriber or subscriber
            if not idle_subscriber.poll(round(wait_seconds * 1000), zmq.POLLIN):
                continue
            candidate, _ = receive_frame(idle_subscriber, args.timeout,
                                         sender if bounded else None)
            if candidate.get("presentationMode") == "idle":
                idle = candidate
                break
        if idle is None:
            raise RuntimeError("server did not publish an idle presentation frame")
        if idle.get("format") != "JPEG":
            raise RuntimeError(f"idle presentation did not use JPEG: {idle!r}")

        presentation_event["presentationMode"] = "interactive"
        resumed = None
        deadline = time.monotonic() + args.timeout
        next_send = 0.0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_send:
                sender.send_json(presentation_event)
                sender.send_json({"type": "frame_request", "version": 1})
                next_send = now + 0.1
            wait_seconds = min(0.25, max(0.001, deadline - time.monotonic()))
            if not subscriber.poll(round(wait_seconds * 1000), zmq.POLLIN):
                continue
            candidate, _ = receive_frame(subscriber, args.timeout,
                                         sender if bounded else None)
            if candidate.get("presentationMode") == "interactive":
                resumed = candidate
                break
        if resumed is None:
            raise RuntimeError("server did not resume interactive presentation")
        if resized.get("format") == "H264_ANNEX_B":
            if resumed.get("format") != "H264_ANNEX_B":
                raise RuntimeError(
                    f"interactive presentation did not resume H.264: {resumed!r}"
                )
            if resumed.get("keyFrame"):
                raise RuntimeError(
                    "idle JPEG reset the persistent H.264 stream unexpectedly"
                )
        resized = resumed
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
        for sequence, event in enumerate(events, start=1):
            event["clientSequence"] = sequence
            event["viewport"] = viewport
            sender.send_json(event)
        sender.send_json({"type": "frame_request", "version": 1})

        second, second_payload = receive_frame(subscriber, args.timeout,
                                               sender if bounded else None)
        if second["frameId"] <= resized["frameId"]:
            raise RuntimeError("frame IDs did not advance after sending input")
        if second.get("triggerSequence", 0) <= 0:
            raise RuntimeError(f"input trigger sequence was not returned: {second!r}")
        timing_keys = (
            "serverTriggerToReadbackMs",
            "serverReadbackMs",
            "serverReadbackLatencyMs",
            "serverEncoderQueueMs",
            "serverEncodeMs",
        )
        for key in timing_keys:
            value = second.get(key)
            if not isinstance(value, (int, float)) or value < 0:
                raise RuntimeError(f"invalid or missing {key}: {second!r}")

        if bounded:
            # Keep receiving camera updates but deliberately withhold frame
            # receipts. The server may commit two video frames, never a third.
            for sequence in range(100, 130):
                sender.send_json({"type": "pointer_move", "x": 10 + sequence,
                                  "y": 20, "primaryDown": False,
                                  "clientSequence": sequence,
                                  "viewport": viewport})
                time.sleep(0.015)
            held = []
            deadline = time.monotonic() + 2.0
            while time.monotonic() < deadline:
                if subscriber.poll(50, zmq.POLLIN):
                    held.append(receive_frame(subscriber, args.timeout))
                    if len(held) > 2:
                        raise RuntimeError("bounded server sent more than two unacknowledged frames")
            for header, _ in held:
                sender.send_json({"type": "frame_request", "version": 1,
                                  "receivedFrameId": header["frameId"]})

        # A continuous drag must keep producing feedback. A newer input can
        # arrive during readback or encoding, but that must not starve every
        # frame that was already rendered.
        received_during_drag: set[int] = set()
        drag_deadline = time.monotonic() + 2.0
        next_drag_input = time.monotonic()
        drag_sequence = 200
        while time.monotonic() < drag_deadline:
            now = time.monotonic()
            if now >= next_drag_input:
                sender.send_json({"type": "pointer_move",
                                  "x": 50 + drag_sequence % 100,
                                  "y": 70, "primaryDown": True,
                                  "clientSequence": drag_sequence,
                                  "viewport": viewport})
                drag_sequence += 1
                next_drag_input = now + 0.05
            if subscriber.poll(10, zmq.POLLIN):
                candidate, _ = receive_frame(subscriber, args.timeout,
                                             sender if bounded else None)
                received_during_drag.add(candidate["frameId"])
        if len(received_during_drag) < 3:
            raise RuntimeError(
                "continuous drag produced too few frames: "
                f"{len(received_during_drag)} in 2 seconds"
            )

        if args.save_frame:
            output = Path(args.save_frame).expanduser().resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            if second.get("format") == "JPEG":
                output.write_bytes(second_payload)
            elif second.get("format") == "H264_ANNEX_B":
                raise RuntimeError(
                    "--save-frame is not available for H.264; "
                    "set PARTICLE_VIS_REMOTE_CODEC=jpeg for this diagnostic"
                )
            else:
                try:
                    from PIL import Image
                except ImportError as error:
                    raise RuntimeError("Pillow is required to save raw RGBA") from error
                Image.frombytes(
                    "RGBA",
                    (second["width"], second["height"]),
                    second_payload,
                ).save(output)

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
                    "rawBytes": second.get("rawBytes", second["bytes"]),
                    "encoding": second.get("format", "RGBA8"),
                    "idleEncoding": idle.get("format"),
                    "idleEncodeMs": idle.get("serverEncodeMs"),
                    "resumedWithKeyFrame": resumed.get("keyFrame"),
                    "resumedEncoderQueueMs": resumed.get("serverEncoderQueueMs"),
                    "displaySize": [active_display_width, active_display_height],
                    "framebufferScale": args.display_scale,
                    "inputEventsSent": len(events) + 1 + resize_requested,
                    "serverTimingMs": {
                        "readbackLatency": second["serverReadbackLatencyMs"],
                        "triggerToReadback": second["serverTriggerToReadbackMs"],
                        "readbackCopy": second["serverReadbackMs"],
                        "encoderQueue": second["serverEncoderQueueMs"],
                        "encode": second["serverEncodeMs"],
                    },
                    "serverExitCode": exit_code,
                },
                indent=2,
            )
        )
        return 0
    finally:
        if still_subscriber is not None:
            still_subscriber.close()
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
    parser.add_argument("--config", help="copy this complete application config for the run")
    parser.add_argument("--save-frame", help="save the final received frame as JPEG or PNG")
    parser.add_argument("--show-server-output", action="store_true")
    args = parser.parse_args()
    if (args.snapshot and args.config):
        parser.error("--snapshot and --config cannot be used together")
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
