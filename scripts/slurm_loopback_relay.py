#!/usr/bin/env python3
"""Relay local TCP ports to compute-node loopback through SSH and Slurm."""

import argparse
import os
import re
import select
import socket
import subprocess
import sys
import threading


SAFE_VALUE = re.compile(r"^[A-Za-z0-9_.:/@~+-]+$")


def configure_interactive_socket(connection):
    # SSH/Slurm can split one message into small writes. Avoid waiting for a
    # delayed ACK before forwarding the remainder (about 40 ms on Linux).
    connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)


def copy_socket_to_pipe(source, destination):
    try:
        while True:
            data = source.recv(1024 * 1024)
            if not data:
                break
            destination.write(data)
            destination.flush()
    except (BrokenPipeError, OSError):
        pass
    finally:
        try:
            destination.close()
        except OSError:
            pass


def copy_pipe_to_socket(source, destination):
    try:
        while True:
            data = os.read(source.fileno(), 1024 * 1024)
            if not data:
                break
            destination.sendall(data)
    except (BrokenPipeError, OSError):
        pass
    finally:
        try:
            destination.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def connect_mode(host, port):
    upstream = socket.create_connection((host, port), timeout=15)
    configure_interactive_socket(upstream)
    upstream.settimeout(None)
    stdin_fd = sys.stdin.buffer.fileno()
    stdout_fd = sys.stdout.buffer.fileno()
    stdin_open = True
    try:
        while True:
            readers = [upstream]
            if stdin_open:
                readers.append(stdin_fd)
            ready, _, _ = select.select(readers, [], [])
            if upstream in ready:
                data = upstream.recv(1024 * 1024)
                if not data:
                    return 0
                view = memoryview(data)
                while view:
                    written = os.write(stdout_fd, view)
                    view = view[written:]
            if stdin_open and stdin_fd in ready:
                data = os.read(stdin_fd, 1024 * 1024)
                if data:
                    upstream.sendall(data)
                else:
                    stdin_open = False
                    upstream.shutdown(socket.SHUT_WR)
    finally:
        upstream.close()


class Relay:
    def __init__(self, args):
        self.args = args
        self.children = []
        self.lock = threading.Lock()

    def command(self, remote_port):
        values = (self.args.login, self.args.job_id, self.args.node,
                  self.args.remote_script)
        if any(not SAFE_VALUE.match(value) for value in values):
            raise ValueError("login, job ID, node, and remote script must use safe characters")
        return [
            "ssh", "-T", "-o", "BatchMode=yes", "-o", "LogLevel=ERROR",
            self.args.login,
            "srun", "--jobid=" + self.args.job_id, "--overlap", "--unbuffered",
            "--nodes=1",
            "--ntasks=1", "--nodelist=" + self.args.node,
            "python3", self.args.remote_script, "connect", "127.0.0.1",
            str(remote_port),
        ]

    def serve_connection(self, client, remote_port):
        process = subprocess.Popen(
            self.command(remote_port),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )
        with self.lock:
            self.children.append(process)
        to_remote = threading.Thread(
            target=copy_socket_to_pipe, args=(client, process.stdin))
        from_remote = threading.Thread(
            target=copy_pipe_to_socket, args=(process.stdout, client))
        to_remote.daemon = True
        from_remote.daemon = True
        to_remote.start()
        from_remote.start()
        process.wait()
        client.close()
        with self.lock:
            self.children.remove(process)

    def run(self):
        mappings = ((self.args.frame_local_port, self.args.frame_remote_port),
                    (self.args.input_local_port, self.args.input_remote_port))
        if self.args.still_local_port:
            mappings += ((self.args.still_local_port, self.args.still_remote_port),)
        listeners = []
        for local_port, remote_port in mappings:
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("127.0.0.1", local_port))
            listener.listen(4)
            listeners.append((listener, remote_port))
            print("127.0.0.1:{} -> {} job {} node loopback:{}".format(
                local_port, self.args.login, self.args.job_id, remote_port),
                flush=True)
        try:
            while True:
                readable, _, _ = select.select(
                    [item[0] for item in listeners], [], [])
                for listener, remote_port in listeners:
                    if listener in readable:
                        client, _ = listener.accept()
                        configure_interactive_socket(client)
                        thread = threading.Thread(
                            target=self.serve_connection,
                            args=(client, remote_port))
                        thread.daemon = True
                        thread.start()
        except KeyboardInterrupt:
            return 0
        finally:
            for listener, _ in listeners:
                listener.close()
            with self.lock:
                for process in self.children:
                    process.terminate()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode")

    connect = subparsers.add_parser("connect")
    connect.add_argument("host")
    connect.add_argument("port", type=int)

    relay = subparsers.add_parser("relay")
    relay.add_argument("--login", default="freya")
    relay.add_argument("--job-id", required=True)
    relay.add_argument("--node", required=True)
    relay.add_argument(
        "--remote-script",
        default="~/work/particle_visualization/scripts/slurm_loopback_relay.py")
    relay.add_argument("--frame-local-port", type=int, default=5570)
    relay.add_argument("--input-local-port", type=int, default=5571)
    relay.add_argument("--frame-remote-port", type=int, default=5560)
    relay.add_argument("--input-remote-port", type=int, default=5561)
    relay.add_argument("--still-local-port", type=int, default=0)
    relay.add_argument("--still-remote-port", type=int, default=5562)

    args = parser.parse_args()
    if args.mode == "connect":
        return connect_mode(args.host, args.port)
    if args.mode == "relay":
        return Relay(args).run()
    parser.error("choose connect or relay")


if __name__ == "__main__":
    sys.exit(main())
