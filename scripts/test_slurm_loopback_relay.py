#!/usr/bin/env python3
"""Check relay byte integrity and optionally compare fragmented TCP latency."""

import argparse
import os
from pathlib import Path
import socket
import statistics
import subprocess
import sys
import threading
import time

from slurm_loopback_relay import configure_interactive_socket, copy_pipe_to_socket


def receive_exact(connection, size):
    chunks = bytearray()
    while len(chunks) < size:
        data = connection.recv(size - len(chunks))
        if not data:
            raise RuntimeError('unexpected EOF')
        chunks.extend(data)
    return bytes(chunks)


def socket_pair():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen()
        client = socket.create_connection(listener.getsockname(), timeout=5)
        server, _ = listener.accept()
    server.settimeout(5)
    return client, server


def fragmented_transfer(nodelay, size, count):
    client, server = socket_pair()
    reader, writer = os.pipe()
    values = []
    with client, server, os.fdopen(reader, 'rb', buffering=0) as source:
        if nodelay:
            configure_interactive_socket(server)
        assert bool(server.getsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY)) == nodelay
        worker = threading.Thread(target=copy_pipe_to_socket, args=(source, server))
        worker.start()
        try:
            for _ in range(count):
                start = time.perf_counter()
                client.sendall(b'x')
                assert receive_exact(server, 1) == b'x'
                os.write(writer, b'h' * 64)
                assert receive_exact(client, 64) == b'h' * 64
                # Ensure separate writes while the first TCP segment may be unacked.
                time.sleep(.001)
                payload = b'p' * size
                view = memoryview(payload)
                while view:
                    view = view[os.write(writer, view):]
                assert receive_exact(client, size) == payload
                values.append((time.perf_counter() - start) * 1000)
        finally:
            os.close(writer)
            worker.join(timeout=5)
            assert not worker.is_alive(), 'relay did not finish after pipe EOF'
        assert client.recv(1) == b'', 'relay did not forward EOF'
    return values


def check_connect_mode():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen()
        listener.settimeout(5)
        process = subprocess.Popen(
            [sys.executable, str(Path(__file__).with_name('slurm_loopback_relay.py')),
             'connect', '127.0.0.1', str(listener.getsockname()[1])],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        try:
            peer, _ = listener.accept()
            with peer:
                peer.settimeout(5)
                payload = bytes(range(256)) * 128
                peer.sendall(payload)
                received = []
                def drain():
                    received.append(receive_exact(peer, len(payload)))
                    received.append(peer.recv(1))
                    peer.shutdown(socket.SHUT_WR)
                receiver = threading.Thread(target=drain)
                receiver.start()
                output, _ = process.communicate(input=payload, timeout=5)
                receiver.join(timeout=5)
                assert not receiver.is_alive()
                assert output == payload
                assert received == [payload, b'']
                assert process.returncode == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark', action='store_true')
    args = parser.parse_args()
    check_connect_mode()
    for nodelay in ((False, True) if args.benchmark else (True,)):
        for size in (1024, 32768, 163840):
            values = fragmented_transfer(nodelay, size, 20 if args.benchmark else 2)
            print('TCP_NODELAY={} bytes={} median_ms={:.3f} max_ms={:.3f}'.format(
                int(nodelay), size, statistics.median(values), max(values)), flush=True)
    print('PASS: byte integrity, bidirectional connect, and EOF')


if __name__ == '__main__':
    main()
