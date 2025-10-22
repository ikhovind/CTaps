import argparse
import asyncio
import logging
import ssl
import time
from typing import cast
from urllib.parse import urlparse

from aioquic.asyncio.client import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import StreamDataReceived, QuicEvent

# Define the custom ALPN protocol (must match the server)
ECHO_ALPN = ["simple-ping"]

class QuicEchoClientProtocol(QuicConnectionProtocol):
    """
    A simple QUIC client protocol that sends data and collects the echo.
    """
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.stream_data: dict[int, bytearray] = {}
        self.stream_waiter: dict[int, asyncio.Future[bytes]] = {}

    def send_ping(self, data: bytes) -> asyncio.Future[bytes]:
        """Send data on a new stream and return a Future for the response."""

        # 1. Open a new stream
        stream_id = self._quic.get_next_available_stream_id()

        # 2. Send data and close the stream for writing (end_stream=True)
        self._quic.send_stream_data(
            stream_id=stream_id,
            data=data,
            end_stream=True,
        )
        self.transmit()

        # 3. Prepare to collect the response
        self.stream_data[stream_id] = bytearray()
        waiter = self._loop.create_future()
        self.stream_waiter[stream_id] = waiter
        
        logging.info("Sent data on stream %d: %s", stream_id, data.decode(errors='ignore'))
        return waiter

    def quic_event_received(self, event: QuicEvent) -> None:
        """Handle incoming QUIC events."""
        
        # Handle data received on a stream
        if isinstance(event, StreamDataReceived):
            stream_id = event.stream_id
            
            # 1. Collect data
            self.stream_data[stream_id].extend(event.data)
            
            # 2. Check if the server has finished sending its echo
            if event.end_stream:
                response = bytes(self.stream_data.pop(stream_id))
                waiter = self.stream_waiter.pop(stream_id)
                waiter.set_result(response)
                
                logging.info("Echo received on stream %d: %s", stream_id, response.decode(errors='ignore'))


async def main_client(configuration: QuicConfiguration, url: str, ping_data: str) -> None:
    """Connect to the server and perform the ping."""
    
    parsed = urlparse(url)
    host = parsed.hostname
    port = parsed.port if parsed.port is not None else 4433 # Default to 4433

    async with connect(
        host,
        port,
        configuration=configuration,
        create_protocol=QuicEchoClientProtocol,
    ) as client:
        client = cast(QuicEchoClientProtocol, client)
        
        # Perform the ping and wait for the echo response
        start = time.time()
        ping_bytes = ping_data.encode()
        
        await client.send_ping(ping_bytes) 
        
        elapsed = time.time() - start
        logging.info("Ping-Pong time: %.3f ms", elapsed * 1000)
        
        # Close the connection cleanly
        client.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="QUIC Echo Client")
    parser.add_argument("-d", "--data", type=str, default="PING", help="the data to send to the server")
    
    args = parser.parse_args()

    logging.basicConfig(
        format="%(asctime)s %(levelname)s %(name)s %(message)s",
        level=logging.INFO,
    )

    # Prepare configuration
    configuration = QuicConfiguration(
        is_client=True,
        alpn_protocols=ECHO_ALPN, # Use our custom protocol identifier
    )
    # Load CA certificates for server verification
    configuration.load_verify_locations("pycacert.pem")
    configuration.verify_mode = ssl.CERT_REQUIRED # Ensure validation is used

    asyncio.run(
        main_client(
            configuration=configuration,
            url="https://localhost:4433",
            ping_data=args.data,
        )
    )
