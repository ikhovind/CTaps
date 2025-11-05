# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

CTaps is a C implementation of the Transport Services API as described in [RFC 9622](https://www.rfc-editor.org/info/rfc9622). It provides a protocol-agnostic interface for network connections, abstracting over TCP, UDP, and QUIC protocols.

## Build System

This project uses CMake (minimum version 3.21) with custom modules for dependencies, formatting, and linting.

### Building the Project

```bash
# Standard CMake build
cmake . -B build
cmake --build build --target all -j 6

# Run all tests (uses runtests.py which handles test server setup)
./runtests.py

# Run a specific test
./runtests.py <test_name>
```

### Available Targets

- `all` - Build the shared library and mocking executable
- `format` - Format all C files using clang-format
- Tests are built automatically via `enable_testing()` and run with `ctest`

### Running Tests

The `runtests.py` script automatically:
1. Creates build directory if needed
2. Configures and builds the project
3. Starts required test servers (TCP, UDP, QUIC ping servers)
4. Runs ctest with `--output-on-failure`

Individual tests can be run via `./runtests.py <test_name>` or directly with `ctest -R <test_name>` from the build directory.

## Architecture

### API vs Implementation Split

The codebase is organized into two main layers:

- **`source/api/`** - Public API conforming to RFC 9622
  - User-facing types and functions
  - Header files are the public interface

- **`source/impl/`** - Implementation details
  - Protocol implementations (TCP, UDP, QUIC)
  - Internal utilities and helpers
  - Not exposed to library users

### Core Abstractions

The architecture follows RFC 9622's model:

1. **Preconnection** (`source/api/connections/preconnection/`) - Configuration object created before establishing a connection. Contains transport properties, endpoints, and security parameters.

2. **Connection** (`source/api/connections/connection/`) - Active connection created via `preconnection_initiate()`. Manages protocol state and message queues.

3. **Listener** (`source/api/connections/listener/`) - Created via `preconnection_listen()` for accepting incoming connections.

4. **Endpoints**:
   - `LocalEndpoint` - Specifies local address/interface
   - `RemoteEndpoint` - Specifies remote address

5. **Transport Properties** - Preferences and requirements for protocol selection (e.g., reliability, preserve-order)

6. **Protocol Interface** (`source/api/protocols/protocol_interface.h`) - Defines the interface that protocol implementations (TCP, UDP, QUIC) must implement. Each protocol exposes `init`, `send`, `receive`, `listen`, `stop_listen`, `close`, and `remote_endpoint_from_peer` functions.

### Candidate Gathering

The candidate gathering system (`source/impl/candidate_gathering/`) is responsible for selecting appropriate protocol/endpoint combinations:

- Builds a tree of candidate paths (local endpoint → remote endpoint → protocol)
- Scores candidates based on transport properties
- Returns ordered array of candidates to try

### Socket Manager

Located in `source/impl/connections/listener/socket_manager/`, this component manages listening sockets for the Listener abstraction. It handles multiplexing for connectionless protocols and connection acceptance.

### Event Loop

The library uses libuv for async I/O. There is a global event loop (`ctaps_event_loop`) that must be started via `ctaps_start_event_loop()` after calling `ctaps_initialize()`.

## Dependencies

Dependencies are fetched automatically via CMake FetchContent (see `cmake/dependencies.cmake`):

- **libuv** (v1.48.0) - Event loop and async I/O
- **picoquic** - QUIC protocol implementation (also fetches picotls)
- **glib-2.0** - Data structures (GQueue, GArray, GNode) via pkg-config

Test dependencies (auto-fetched):
- **googletest** - Testing framework
- **fff** - Fake function framework for mocking

## Testing Structure

Tests are located in `test/` and use a custom `add_gtest()` CMake function:

- **Integration tests** - Test full protocol flows (e.g., `tcp_ping_test`, `quic_listener_test`)
- **Unit tests** - Test individual components with mocking
- **AddressSanitizer** - Some tests have `ASAN_ENABLED` for memory safety checking
- **Function wrapping** - Tests can specify `WRAP_FUNCTIONS` to mock specific functions

Tests requiring external servers (TCP/UDP/QUIC ping tests) rely on Python helper scripts in `test/` that are launched by `runtests.py`.

## Code Standards

- **C99** standard (`target_compile_features(... c_std_99)`)
- **clang-tidy** is automatically run during build if available (configured via `.clang-tidy`)
- **clang-format** for code formatting - run `cmake --build build --target format`

## Known Refactoring Needs (from REFACTOR.md)

When working on the codebase, be aware of these architectural issues:

1. **Memory allocation pattern** - Functions that malloc should return pointers, not use output parameters
2. **Global state** - The library currently uses global state (`ctaps_global_config`, `ctaps_event_loop`). The plan is to move to per-instance state with a context struct passed to functions (similar to NEAT or picoquic)
3. **Protocol interface** - Callback structs shouldn't be passed to protocol interface; `preconnection_initiate` should place them in the Connection
4. **Socket manager complexity** - The socket_manager.c needs simplification, particularly around connection-oriented vs connectionless protocol handling

## Common Workflow

For adding a new feature:

1. Check if it affects the public API (`source/api/`) or just implementation (`source/impl/`)
2. If adding protocol support, implement the `ProtocolImplementation` interface
3. Add both unit tests and integration tests following the patterns in `test/CMakeLists.txt`
4. Run `./runtests.py` to verify all tests pass
5. Run the `format` target before committing
