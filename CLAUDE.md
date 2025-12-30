# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

CTaps is a C implementation of the Transport Services API as described in [RFC 9622](https://www.rfc-editor.org/info/rfc9622). It provides a protocol-agnostic interface for network connections, abstracting over TCP, UDP, and QUIC protocols.

## Build System

This project uses CMake (minimum version 3.21) with custom modules for dependencies, formatting, and linting.

### Building the Project

```bash
# Standard CMake build
cmake . -B out/Debug
cmake --build out/Debug --target all -j 6

# Run all tests
cd out/Debug/test && ctest


# Run a specific test
ctest -R <test-name> --output-on-failure
```

### Available Targets

- `all` - Build the shared library and mocking executable
- `format` - Format all C files using clang-format
- Tests are built automatically via `enable_testing()` and run with `ctest`

### Running Tests

Build the project like normally and use ``ctest`` in the out/Debug/test forlder

## Architecture

### Core Abstractions

The architecture follows RFC 9622's model:

1. **Preconnection** (`src/connections/preconnection/`) - Configuration object created before establishing a connection. Contains transport properties, endpoints, and security parameters.

2. **Connection** (`src/connections/connection/`) - Active connection created via `preconnection_initiate()`. Manages protocol state and message queues.

3. **Listener** (`src/connections/listener/`) - Created via `preconnection_listen()` for accepting incoming connections.

4. **Endpoints**:
   - `LocalEndpoint` - Specifies local address/interface
   - `RemoteEndpoint` - Specifies remote address

5. **Transport Properties** - Preferences and requirements for protocol selection (e.g., reliability, preserve-order)

6. **Protocol Interface** (`src/protocols/protocol_interface.h`) - Defines the interface that protocol implementations (TCP, UDP, QUIC) must implement. Each protocol exposes `init`, `send`, `receive`, `listen`, `stop_listen`, `close`, and `remote_endpoint_from_peer` functions.

### Candidate Gathering

The candidate gathering system (`src/candidate_gathering/`) is responsible for selecting appropriate protocol/endpoint combinations:

- Builds a tree of candidate paths (local endpoint → remote endpoint → protocol)
- Scores candidates based on transport properties
- Returns ordered array of candidates to try

### Socket Manager

Located in `src/connections/listener/socket_manager/`, this component manages listening sockets for the Listener abstraction. It handles multiplexing for connectionless protocols and connection acceptance.

### Event Loop

The library uses libuv for async I/O. There is a global event loop (`ctaps_event_loop`) that must be started via `ctaps_start_event_loop()` after calling `ctaps_initialize()`.

## Code Organization and Internal Structure

### Public vs. Internal API Separation

The codebase maintains a clear separation between public and internal APIs:

- **`include/ctaps.h`** - Public API header exposed to library users
  - Contains opaque type declarations (e.g., `typedef struct ct_connection_s ct_connection_t;`)
  - Only exposes functions and types that external users need
  - Changing this file is an API/ABI breaking change

- **`src/ctaps_internal.h`** - Internal structure definitions
  - Contains full struct definitions for `ct_connection_t`, `ct_listener_t`, `ct_connection_group_t`
  - Used only within the library implementation
  - Can be changed without breaking external users

- **Module headers** (`src/connection/connection.h`, `src/connection/connection_group.h`, etc.)
  - Contain function declarations for working with internal types
  - Provide accessor functions (getters/setters) for internal use

### Header Include Conventions

To maintain encapsulation and minimize coupling:

1. **In `.h` header files**:
   - Include only `ctaps.h` (public header) when possible
   - Declare accessor functions rather than exposing struct internals
   - Avoid including `ctaps_internal.h` in headers unless absolutely necessary

2. **In `.c` implementation files**:
   - Include `ctaps_internal.h` to get full struct definitions
   - Direct field access is permitted here
   - However, prefer using accessor functions when available to reduce coupling

### Accessor Function Pattern

Even within the library, prefer using accessor functions over direct field access when practical:

```c
// Good - using accessor (works even without ctaps_internal.h)
ct_connection_group_t* group = ct_connection_get_connection_group(conn);

// Acceptable in .c files only - direct access
ct_connection_group_t* group = conn->connection_group;
```

**Benefits**:
- Reduces coupling between modules
- Makes refactoring easier (change internal structure without updating many files)
- Clearer separation of concerns
- Better testability (can mock accessors)

**When to add accessor functions**:
- When a field is accessed from multiple modules
- When you want to add validation or logging to field access
- When the access pattern might change in the future

### Guidelines for New Code

When adding new functionality:

1. **New public APIs** - Add to `include/ctaps.h`, use opaque types
2. **New internal types** - Define full structs in `src/ctaps_internal.h`
3. **New functions** - Declare in module headers, implement in `.c` files
4. **Accessing internal state** - Prefer accessors over direct field access
5. **Creating new modules** - Follow the pattern: `module.h` (declarations) + `module.c` (implementation)

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

Tests requiring external servers (TCP/UDP/QUIC ping tests) rely on Python helper scripts in `test/` claude does not have to worry about these

## Code Standards

- **C99** standard (`target_compile_features(... c_std_99)`)
- **clang-tidy** is automatically run during build if available (configured via `.clang-tidy`)

## Known Refactoring Needs (from REFACTOR.md)

When working on the codebase, be aware of these architectural issues:

1. **Memory allocation pattern** - Functions that malloc should return pointers, not use output parameters
2. **Global state** - The library currently uses global state (`ctaps_global_config`, `ctaps_event_loop`). The plan is to move to per-instance state with a context struct passed to functions (similar to NEAT or picoquic)
3. **Protocol interface** - Callback structs shouldn't be passed to protocol interface; `preconnection_initiate` should place them in the Connection
4. **Socket manager complexity** - The socket_manager.c needs simplification, particularly around connection-oriented vs connectionless protocol handling

## Common Workflow

For adding a new feature:

1. Check if it affects the public API (`include/ctaps.h`) or just implementation (`src/`)
2. If adding protocol support, implement the `ProtocolImplementation` interface
3. Add both unit tests and integration tests following the patterns in `test/CMakeLists.txt`
4. Build the project and use `ctest` to run all tests
