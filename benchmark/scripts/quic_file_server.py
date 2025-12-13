#!/usr/bin/env python3
"""
QUIC File Server for CTaps Benchmark

Serves two files (large and short) over QUIC streams based on client requests.
"""

import asyncio
import logging
import os
import sys

from aioquic.asyncio import QuicConnectionProtocol, serve
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import StreamDataReceived, QuicEvent

ALPN = ["benchmark"]
LARGE_FILE_SIZE = 10 * 1024 * 1024  # 10 MB
SHORT_FILE_SIZE = 70 * 1460          # 70 packets, ~102 KB


class FileServerProtocol(QuicConnectionProtocol):
    """QUIC protocol that serves files based on stream requests."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.large_file_data = None
        self.short_file_data = None
        self._load_files()

    def _load_files(self):
        """Load or generate test files."""
        large_path = "large_file.dat"
        short_path = "short_file.dat"

        # Generate files if they don't exist
        if not os.path.exists(large_path):
            logging.info(f"Generating {large_path} ({LARGE_FILE_SIZE} bytes)")
            with open(large_path, 'wb') as f:
                f.write(os.urandom(LARGE_FILE_SIZE))

        if not os.path.exists(short_path):
            logging.info(f"Generating {short_path} ({SHORT_FILE_SIZE} bytes)")
            with open(short_path, 'wb') as f:
                f.write(os.urandom(SHORT_FILE_SIZE))

        # Load files into memory
        with open(large_path, 'rb') as f:
            self.large_file_data = f.read()
        with open(short_path, 'rb') as f:
            self.short_file_data = f.read()

        logging.info(f"Loaded large file ({len(self.large_file_data)} bytes)")
        logging.info(f"Loaded short file ({len(self.short_file_data)} bytes)")

    def quic_event_received(self, event: QuicEvent) -> None:
        """Handle incoming QUIC events."""

        if isinstance(event, StreamDataReceived):
            stream_id = event.stream_id
            request = event.data.decode('utf-8', errors='ignore').strip()

            logging.info(f"[Stream {stream_id}] Received request: {request}")

            if request == "LARGE":
                logging.info(f"[Stream {stream_id}] Sending LARGE file ({len(self.large_file_data)} bytes)")
                self._send_file(stream_id, self.large_file_data)
            elif request == "SHORT":
                logging.info(f"[Stream {stream_id}] Sending SHORT file ({len(self.short_file_data)} bytes)")
                self._send_file(stream_id, self.short_file_data)
            else:
                logging.warning(f"[Stream {stream_id}] Invalid request: {request}")
                error_msg = b"ERROR: Invalid request. Use 'LARGE' or 'SHORT'\n"
                self._quic.send_stream_data(stream_id, error_msg, end_stream=True)
                self.transmit()

    def _send_file(self, stream_id: int, data: bytes):
        """Send file data on the specified stream."""
        # Send in chunks to avoid overwhelming the connection
        chunk_size = 65536  # 64 KB chunks
        offset = 0

        while offset < len(data):
            chunk = data[offset:offset + chunk_size]
            is_last = (offset + len(chunk)) >= len(data)

            self._quic.send_stream_data(
                stream_id=stream_id,
                data=chunk,
                end_stream=is_last
            )
            offset += len(chunk)

        self.transmit()
        logging.info(f"[Stream {stream_id}] Transfer complete")


async def main_server(host: str, port: int, cert_path: str, key_path: str):
    """Start the QUIC file server."""

    # Configure QUIC/TLS
    configuration = QuicConfiguration(
        alpn_protocols=ALPN,
        is_client=False,
        max_datagram_frame_size=65536,
    )

    # Load TLS certificate
    configuration.load_cert_chain(cert_path, key_path)

    logging.info(f"Starting QUIC File Server on {host}:{port}")
    logging.info(f"ALPN: {ALPN}")
    logging.info(f"Certificate: {cert_path}")

    # Change to the directory containing the files
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    if os.path.exists("../out/benchmark"):
        os.chdir("../out/benchmark")

    await serve(
        host,
        port,
        configuration=configuration,
        create_protocol=FileServerProtocol,
    )

    await asyncio.Future()  # Run forever


if __name__ == "__main__":
    logging.basicConfig(
        format="%(asctime)s [%(levelname)s] %(message)s",
        level=logging.INFO,
    )

    # Default paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    test_quic_dir = os.path.join(script_dir, "../test/quic")

    cert_path = os.path.join(test_quic_dir, "cert.pem")
    key_path = os.path.join(test_quic_dir, "key.pem")

    # Check if certificates exist
    if not os.path.exists(cert_path) or not os.path.exists(key_path):
        print(f"Error: Certificate files not found!")
        print(f"Expected cert: {cert_path}")
        print(f"Expected key: {key_path}")
        sys.exit(1)

    host = "::"  # Listen on all interfaces (IPv6 and IPv4)
    port = 4433

    if len(sys.argv) > 1:
        port = int(sys.argv[1])

    try:
        asyncio.run(main_server(host, port, cert_path, key_path))
    except KeyboardInterrupt:
        logging.info("Server stopped by user")
