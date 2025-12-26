# CTaps

A C implementation of the Transport Services API as described in:
 * [RFC 9621 - Overall Structure](https://www.rfc-editor.org/info/rfc9621).
 * [RFC 9622 - External API](https://www.rfc-editor.org/info/rfc9622).
 * [RFC 9623 - Internal workings](https://www.rfc-editor.org/info/rfc9623).

CTaps provides a asyncchronous, callback-based interface for network connections, abstracting over TCP, UDP, and QUIC protocols.

## Core Structures

The library implements several key abstractions from RFC 9622.
Among these, the most central to communication are:

| RFC 9622 Concept  | CTaps Equivalent | Description  |
|---|---|---|
| Preconnection         | ct_preconnection_t            | Configuration object for setting up connections before establishment. |
| Connection            | ct_connection_t               | Active connection created from a Preconnection. |
| Message               | ct_message_t                  | A single message delivered by one of the underlying protocols.  |

Several other abstractions exists to set up the underlying connection.

An example of a connection can be seen in the following code snippet, adapted from our TCP ping test:

<details>

<summary>Example CTaps client</summary>

```C
#include <ctaps.h>
#include <stdio.h>
#include <string.h>

int close_on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    ct_connection_close(connection);
    return 0;
}

int send_message_and_receive(struct ct_connection_s* connection) {
    ct_message_t message;
    ct_message_build_with_content(&message, "ping", strlen("ping") + 1);
    ct_send_message(connection, &message); // CTaps takes a deep copy of the passed content, so the message can be freed after this returns
    ct_message_free_content(&message);

    ct_receive_callbacks_t receive_message_request = {
      .receive_callback = close_on_message_received,
    };

    ct_receive_message(connection, receive_message_request);
    return 0;
}

int main() {
   ct_initialize(NULL, NULL); // Init (currently) global state

   ct_remote_endpoint_t remote_endpoint; // where we will try to connect to

   ct_remote_endpoint_build(&remote_endpoint);
   ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
   ct_remote_endpoint_with_port(&remote_endpoint, 1234); // example port

   ct_transport_properties_t transport_properties;

   ct_transport_properties_build(&transport_properties);

   // selection properties decide which protocol(s) will be used, if multiple are compatible with
   // our requirements, then we will race the protocols
   // TCP is the only protocol compatible with this requirement
   ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_MSG_BOUNDARIES, PROHIBIT); // force TCP

   ct_preconnection_t preconnection;
   ct_preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1, NULL);

   ct_connection_callbacks_t connection_callbacks = {
       .ready = send_message_and_receive, // Callback to a function which will be invoked when a connection is ready
   };

   int rc = ct_preconnection_initiate(&preconnection, connection_callbacks); // Gather potential endpoints and start racing, when event loop starts

   if (rc < 0) {
      perror("Error in initiating connection\n");
      return rc;
   }

   ct_start_event_loop(); // Start the libuv event loop, block until all connections close (or no connection can be established)
   return 0;
}
```
</details>

<details>
<summary>Example CTaps server</summary>

```C
#include <ctaps.h>
#include <stdio.h>
#include <string.h>

int close_on_message_received(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* message_context) {
    printf("Received message: %s\n", (*received_message)->content);
    ct_connection_close(connection);
    return 0;
}

int on_connection_received_receive_message(ct_listener_t* listener, ct_connection_t* new_connection) {
    printf("Listener received new connection\n");
    ct_receive_callbacks_t receive_message_request = {
      .receive_callback = close_on_message_received,
    };

    ct_receive_message(new_connection, receive_message_request);
    return 0;
}

int main() {
    ct_initialize(NULL, NULL); // Init (currently) global state

    ct_listener_t listener;

    ct_local_endpoint_t listener_endpoint;
    ct_local_endpoint_build(&listener_endpoint);

    ct_local_endpoint_with_interface(&listener_endpoint, "lo");
    ct_local_endpoint_with_port(&listener_endpoint, 1234);

    ct_remote_endpoint_t listener_remote;
    ct_remote_endpoint_build(&listener_remote);
    ct_remote_endpoint_with_hostname(&listener_remote, "127.0.0.1");

    ct_transport_properties_t listener_props;
    ct_transport_properties_build(&listener_props);

    ct_tp_set_sel_prop_preference(&listener_props, PRESERVE_MSG_BOUNDARIES, PROHIBIT); // force TCP

    ct_preconnection_t listener_precon;
    ct_preconnection_build_with_local(&listener_precon, listener_props, &listener_remote, 1, NULL, listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = on_connection_received_receive_message,
    };

    int listen_res = ct_preconnection_listen(&listener_precon, &listener, listener_callbacks);

    if (listen_res < 0) {
        perror("Error in initiating connection\n");
        return listen_res;
    }

    ct_start_event_loop();
}
```
</details>

## Project Structure

```
ctaps/
├── include/        # Public API headers
│   └── ctaps.h    # Public interface
├── src/           # Implementation
│   ├── connection/         # (pre)connection abstractions
│   ├── protocol/          # Protocol implementations (TCP, UDP, QUIC) and setup
│   ├── candidate_gathering/ # Protocol/endpoint selection and racing
│   └── ...
└── test/          # Test suite (googletest)
```

## Building

```bash
cmake . -B out/Debug
cmake --build out/Debug --target all -j 6
```

## Running Tests

First run the three python ping servers:
```bash
python3 test/tcp_ping_server.py
python3 test/udp_ping_server.py
python3 test/quic/quic_ping_server.py # Need to have aioquic installed
```

Then run the actual tests:
```bash
cd out/Debug/test && ctest
```
