import argparse
import asyncio
import logging

import os
from aioquic.asyncio import QuicConnectionProtocol, serve
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import StreamDataReceived, ProtocolNegotiated, QuicEvent

# Define a custom ALPN protocol to distinguish it from HTTP/3
ECHO_ALPN = ["simple-ping"]
SERVER_NAME = "aioquic/echo"

class QuicEchoProtocol(QuicConnectionProtocol):
    """
    A simple QUIC protocol that echos data received on any stream.
    """
    def quic_event_received(self, event: QuicEvent) -> None:
        """Handle incoming QUIC events."""
        
        # Check if the protocol was successfully negotiated (optional for a simple server)
        if isinstance(event, ProtocolNegotiated):
            logging.info("Protocol negotiated: %s", event.alpn_protocol)

        # Handle data received on a stream
        if isinstance(event, StreamDataReceived):
            # 1. Log the incoming data
            logging.info("Received on stream %d: %s", event.stream_id, event.data.decode(errors='ignore'))
            receive_string = event.data.decode(errors='ignore')
            receive_string = "Pong: " + receive_string
            send_bytes = receive_string.encode()
            
            # 2. Echo the data back
            self._quic.send_stream_data(
                stream_id=event.stream_id,
                data=send_bytes,
                end_stream=event.end_stream, # Keep the end_stream flag from the received data
            )

            # 3. Schedule transmission to send the data immediately
            self.transmit()

async def main_server(host: str, port: int, configuration: QuicConfiguration) -> None:
    """Start the QUIC server."""
    logging.info("Starting QUIC Echo Server on %s:%d with ALPN %s", host, port, ECHO_ALPN)
    await serve(
        host,
        port,
        configuration=configuration,
        create_protocol=QuicEchoProtocol,
    )
    await asyncio.Future()

if __name__ == "__main__":
    logging.basicConfig(
        format="%(asctime)s %(levelname)s %(name)s %(message)s",
        level=logging.INFO,
    )

    # Configure QUIC/TLS
    configuration = QuicConfiguration(
        alpn_protocols=ECHO_ALPN,  # Use our custom protocol identifier
        is_client=False,
    )
    cert_abs_path = os.path.join(os.path.dirname(__file__), "cert.pem")
    key_abs_path = os.path.join(os.path.dirname(__file__), "key.pem")
    configuration.load_cert_chain(
        cert_abs_path,
        key_abs_path)

    try:
        asyncio.run(
            main_server(
                host="::",
                port=4433,
                configuration=configuration,
            )
        )
    except KeyboardInterrupt:
        logging.info("Server stopped by user.")
