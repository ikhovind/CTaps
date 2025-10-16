import socket

# Server configuration
TCP_IP = "127.0.0.1"  # Listen on localhost
TCP_PORT = 5006      # Port to listen on
BUFFER_SIZE = 1024   # Buffer size for received data

def run_tcp_server():
    # Create a TCP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    # Bind the socket to the IP address and port
    sock.bind((TCP_IP, TCP_PORT))

    # Listen for incoming connections
    sock.listen(1)

    print(f"TCP server listening on {TCP_IP}:{TCP_PORT}")

    while True:
        # Wait for a connection
        conn, addr = sock.accept()
        print(f"Connection from {addr}")

        try:
            while True:
                # Receive data from the client
                data = conn.recv(BUFFER_SIZE)
                if not data:
                    break

                # Decode the received message
                message = data.decode('utf-8')
                print(f"Received message: '{message}' from {addr}")

                # Send a response back to the client
                response_message = f"Pong: {message}"
                conn.send(response_message.encode('utf-8'))
                print(f"Sent response: '{response_message}' to {addr}")
        finally:
            # Clean up the connection
            conn.close()

if __name__ == "__main__":
    run_tcp_server()
