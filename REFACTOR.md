# List of things which really should be refactored:

 * If a function mallocs memory, it should return a pointer instead of using an output pointer
 * ~~The protocol interface should not take callback struct as parameter, instead
   just read them from the passed connection~~
 * ~~Decide on a common error code framework, maybe matching that of libuv?~~
 * Simplify the socket_manager.c functionality?
   * Find some way of making the socket manager more unified with connection-oriented vs connectionless protocols
   * Multiplexing etc. does not make sense for TCP for example.
 * Move from global (per-libary) state to per-instance state
   * Instead of having global configuration and a single ctaps_initialize(); call, create a struct which is passed to every function
     Like NEAT or picoquic etc.
 * Write better tests for candidate racing
