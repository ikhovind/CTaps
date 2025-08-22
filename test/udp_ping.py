import socket

# Server configuration
UDP_IP = "127.0.0.1"  # Listen on localhost
UDP_PORT = 5005      # Port to listen on
BUFFER_SIZE = 1024   # Buffer size for received data

def run_udp_server():
    # Create a UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Bind the socket to the IP address and port
    sock.bind((UDP_IP, UDP_PORT))

    print(f"UDP server listening on {UDP_IP}:{UDP_PORT}")

    while True:
        # Receive data from the client
        data, addr = sock.recvfrom(BUFFER_SIZE)

        # Decode the received message
        message = data.decode('utf-8')

        print(f"Received message: '{message}' from {addr}")

        # Send a response back to the client
        response_message = f"Pong: {message}"
        sock.sendto(response_message.encode('utf-8'), addr)
        print(f"Sent response: '{response_message}' to {addr}")

if __name__ == "__main__":
    run_udp_server()
